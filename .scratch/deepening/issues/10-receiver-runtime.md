# 10 - One receiver runtime owns transitions and applied state

Status: needs-info

Depends on: `.scratch/device-model/issues/07-a-second-backend.md` reaching the
point where the UHD adapter's actual stream and state semantics are known.

The lower modules are now sound: `device_backend.h` drives a source,
`acquisition.h` owns the worker and latest-block slot, `device_profile.h`
describes the source, and `receiver_lease.h` orders temporary tuning owners.
Their orchestration is not a module. It remains in `sdrprobe.c`:

- `start_acquisition()` and `stop_acquisition()` manage worker state, signal
  masks, source attachment and receiver cancellation;
- `retune_receiver()` and `retune_receiver_at_rate()` implement a multi-step
  stop/apply/flush/restart transaction and its rollback;
- five receiver-lease helpers coordinate the lease with those transactions;
- seven fields in `struct app` describe live/capture mode and applied tuning,
  rate, correction, gain and DC filtering;
- receiver errors and spectrum invalidation are side effects callers must
  know accompany those operations.

That is one concept -- the current receiver runtime -- with no interface of
its own. Ticket 09 counted 68 field-file reads across nineteen files but could
not name a writer. Moving those fields into another struct would preserve the
same shallow implementation. The writer has to be the module that performs
the transitions.

## Hypothesis

A receiver runtime can own one open source, its acquisition lifecycle, its
applied settings and its tuning lease behind a smaller interface than the
current `view.h` receiver surface. It is false if UHD needs callers to manage
driver-specific transition state directly, or if moving acquisition inside
forces the latest-block consumer through a wider interface than it has now.

The discriminating check is a fake backend driven through the real runtime:
rate change succeeds, frequency change fails, and the runtime restores the
old rate and restarts exactly one worker while reporting the old applied
state. Today only pieces of that sequence are checked separately.

## Why this waits for UHD

The backend seam has a real capture adapter and a live RTL-SDR adapter, but
only one receiver. UHD's `recv()` may add timestamps, overflow reports and
different stop semantics. Inventing those now from a synthetic adapter would
not make the seam real; it would make a second copy of the first receiver's
assumptions. Use the arriving hardware to decide what belongs in the runtime
interface and what stays inside the UHD adapter.

This ticket absorbs 09. Do not build `struct applied_receiver_state` first and
then place a runtime beside it: that would create two owners before either has
one writer.

## Intended depth

Callers should ask the runtime to open/close, start/stop, tune, borrow/return,
set gain/correction, and read a snapshot of applied settings or the latest
receiver error. They should not sequence worker shutdown, backend calls,
flushes, restarts, rollback, lease mutation or applied-field writes.

`device_backend`, `device_profile`, `acquisition` and `receiver_lease` remain
modules behind internal seams. The deletion test for the runtime must pass:
deleting it would spread the transaction and rollback rules back into the
application and settings/calibration callers, not merely remove forwarding
functions.

## Implementation plan after the dependency clears

### Phase 1 -- establish the second receiver's facts

Exercise the RTL-SDR and UHD adapters through open, stream, stop, flush,
frequency/rate/correction/gain changes, read-back and failure. Record which
facts are common runtime state and which metadata belongs only to one adapter.
Update this ticket before changing the runtime interface. This is the phase
that clears `needs-info`.

Files: `.scratch/device-model/issues/07-a-second-backend.md`,
`src/backend_rtlsdr.c`, `src/backend_uhd.c`, `src/device_backend.h`.

Check: a table of observed semantics for both receivers with no invented UHD
field left in the proposed common state.

### Phase 2 -- prove the transaction in isolation

Add `receiver_runtime.{c,h}` with only enough state and operations to drive a
fake backend and acquisition worker. Write `check-receiver-runtime` first for
the discriminating sequence: rate succeeds, frequency fails, old rate is
restored, exactly one worker is running, applied state is unchanged, and the
error names the failed operation. Also pin restart failure and read-back
failure before moving production callers.

Files: `src/receiver_runtime.{c,h}`, `tests/receiver_runtime_test.c`,
`Makefile`.

Focused validation: `make check-receiver-runtime`.

### Phase 3 -- move lifecycle and retuning unchanged

Move `start_acquisition()`, `stop_acquisition()`, `retune_receiver()` and
`retune_receiver_at_rate()` from `sdrprobe.c` behind the runtime interface.
Preserve call order, rollback, spectrum invalidation and error text in this
slice. Keep temporary compatibility functions in `view.h` so no view changes
at the same time as the transaction moves.

Files: `src/sdrprobe.c`, `src/receiver_runtime.{c,h}`, `src/view.h`,
`src/app.h`.

Focused validation: `make check-receiver-runtime`, `make check-acquisition`,
`make check-device-backend`, then `make check-pipelines`.

### Phase 4 -- give applied state one writer

Move applied frequency, sample rate, correction, gain and mode/capability
state behind the runtime. Transition success is the only writer; callers get
a read-only snapshot. Decide from Phase 1 whether DC filtering is receiver
state or signal-frame policy rather than moving it by proximity. Delete each
old `app` field only after its readers use the snapshot.

Files: `src/app.h`, `src/receiver_runtime.{c,h}`, settings/calibration and
every view currently reading `app->applied_*` or `app->receiver_mode`.

Focused validation: `make check-receiver-runtime`, `make check-input`,
`make check-calibration`, and `make check-touched`.

### Phase 5 -- absorb lease orchestration and callers

Move the five receiver-borrow helpers behind the runtime without changing the
pure `receiver_lease.h` state machine. Migrate settings, calibration, survey,
GSM, LTE and FM one ownership path at a time. After each path, prove that a
nested owner returns to its immediate parent and then to the original tuning.

Files: `src/receiver_runtime.{c,h}`, `src/receiver_lease.h`, `src/view.h`,
`src/view_*.c`, `src/overlay_*.c`, `src/survey_report.c`.

Focused validation: `make check-receiver-lease`, affected session/survey
checks, and `make check-pipelines` after each caller family.

### Phase 6 -- remove the old surface

Delete the compatibility helpers from `view.h`, remove obsolete receiver
fields and lifecycle code from `sdrprobe.c`, update architecture documents,
and resolve ticket 09 as absorbed here. Run the deletion test: removing the
runtime must make transaction, rollback and ownership rules reappear across
callers rather than merely remove forwarding functions.

Focused checks while iterating: the new runtime check,
`check-device-backend`, `check-acquisition`, `check-receiver-lease`, then
`check-pipelines`. Final gate: `make check-touched` and `make check`.

## Tasks

- [ ] Complete the UHD observations required by Phase 1 and change this ticket
  to `ready-for-agent`.
- [ ] Write the shared/adapter-specific transition table.
- [ ] Add `receiver_runtime.{c,h}` and register both files in Makefile
  prerequisites.
- [ ] Add `check-receiver-runtime` to `CHECK_UNITS` and clean bookkeeping.
- [ ] Pin failed frequency-after-rate rollback before moving production code.
- [ ] Pin restart, stop, flush and read-back failures.
- [ ] Move acquisition start/stop behind the runtime without changing callers.
- [ ] Move frequency/rate transactions and preserve their error text.
- [ ] Move applied settings and make the runtime their only writer.
- [ ] Decide `receiver_mode` as capability, source kind or deletion from UHD
  evidence.
- [ ] Decide `remove_dc` ownership with ticket 11 rather than moving it
  automatically.
- [ ] Move lease orchestration behind the runtime.
- [ ] Migrate settings, calibration and views one owner at a time.
- [ ] Remove receiver lifecycle helpers from `view.h` and `sdrprobe.c`.
- [ ] Resolve ticket 09 as absorbed by this module.
- [ ] Update `AGENTS.md`, `CLAUDE.md` and `docs/ARCHITECTURE.md`.
- [ ] Run `make check-touched` and `make check`.

## Acceptance criteria

- Applied frequency, rate, correction and gain have one writer.
- A failed transition leaves the prior applied snapshot and lease intact.
- Worker stop/start and backend rollback are checked as one sequence.
- Views do not inspect receiver mode to decide whether an operation is legal;
  they ask the runtime and render its refusal.
- Capture playback keeps its truthful refusals.
- RTL-SDR and UHD satisfy the same runtime interface without losing metadata
  either receiver actually supplies.
- Pipeline output is unchanged for every committed capture.

## Not in scope

- Changing ADR-0002's single overwriteable latest block.
- Putting device-specific facts into `device_profile` function pointers.
- Designing UHD timestamps or overflow handling before observing the adapter.
- A uniform interface across technology DSP modules.
- Moving view state unrelated to the receiver.

## Comments

**Reviewed 2026-09-10. Phase 2 is startable now; phase 1 is half startable.**

The rewritten phase 2 answers the objection the previous plan could not. It
does not try to check the existing transaction -- which is unreachable, since
`retune_receiver()` and the rest live in `sdrprobe.c` taking `struct app *`.
It builds the checked shape first, against a fake backend, and phase 3 moves
the real code into it. That is pure addition and needs no hardware.

**Two things that were said to block it do not.** `CLAUDE.md` claimed
`acquisition.h` could not be included by a check because it pulls
`<rtl-sdr.h>`. It has not since ticket 07 put that header behind
`backend_rtlsdr.c`: **no header in `src/` includes it at all**, a translation
unit including `acquisition.h` compiles `-Wall -W` clean and links with `-lm`
alone, and `check-acquisition` already drives the worker with `-pthread`. The
claim is corrected in `CLAUDE.md`; it was cited against attempting exactly
this phase, which is what a stale refusal costs.

**One thing does need doing first.** The fake backend is `static` inside
`tests/device_backend_test.c`, so `check-receiver-runtime` would need its own
-- two definitions of what a device does, drifting apart. Promote it to a
shared `tests/fake_backend.{h,c}` in the same edit rather than copying it.

**Phase 1 bundles two receivers into one gate.** "Exercise the RTL-SDR and UHD
adapters" is one phase, and the RTL-SDR half needs only the dongle already on
the desk -- it is the baseline UHD gets compared against, and reconstructing
it later, with a second device confusing the picture, is guessing. Worth
splitting 1a (now) from 1b (on arrival).

**What still waits, and it is not only UHD.** Phase 4 moves applied state, and
the task list already defers `remove_dc` to ticket 11. So the order 12 -> 11
-> 10 is not merely "10 is blocked": 11 owes this ticket a decision, and of
the 13 files that read `app->applied_*`, **12 also read the sample/spectrum
frame** -- all seven views, all three overlays, `survey_report.c` and
`sdrprobe.c`. Running phase 5 before ticket 11 means walking those twelve
files twice, with a live-hardware verification pass each time.

