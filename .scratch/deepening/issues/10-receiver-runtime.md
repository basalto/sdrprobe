# 10 - One receiver runtime owns transitions and applied state

Status: needs-info -- **phases 1a, 2 and 3 are done** (2026-09-11); 4 to 6 still want the second receiver. See the comments.

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
- [x] Write the shared/adapter-specific transition table -- **RTL-SDR half
  only**, above; UHD's is what clears `needs-info`.
- [x] Add `receiver_runtime.{c,h}` and register both files in Makefile
  prerequisites.
- [x] Add `check-receiver-runtime` to `CHECK_UNITS` and clean bookkeeping.
- [x] Pin failed frequency-after-rate rollback before moving production code.
- [x] Pin restart, stop, flush and read-back failures.
- [ ] Move acquisition start/stop behind the runtime without changing callers.
- [x] Move frequency/rate transactions and preserve their error text.
- [ ] Move applied settings and make the runtime their only writer.
- [ ] Decide `receiver_mode` as capability, source kind or deletion from UHD
  evidence.
- [x] Decide `remove_dc` ownership with ticket 11 rather than moving it
  automatically -- **it is signal-frame policy**, an argument to
  `signal_frame_process()`, not receiver state. Ticket 11 settled it.
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

## Phase 1a: what the RTL-SDR backend actually does, 2026-09-11

Observed against the attached R820T, one call at a time. This is the baseline
UHD gets compared against; recording it now rather than reconstructing it
later with a second device confusing the picture.

| transition | rc | read back |
| --- | --- | --- |
| `open(0)` | 0 | full scale 127.5, 2 bytes/pair, reference 28 800 000 Hz |
| `set_sample_rate_hz(2000000)` | 0 | 2000000 |
| `set_frequency_hz(948400000)` | 0 | 948400000, exactly what was asked |
| `set_ppm(-31)` | 0 | -31 |
| `set_gain(manual, 297)` | 0 | 297 |
| `flush()` | 0 | -- |
| `stop()` with no stream running | **-1** | -- |
| `set_sample_rate_hz(250000)` | **0** | 250000 |
| `set_frequency_hz(10)` | **0** | 10 |
| `close()` then `device_session_open()` | -- | 0, not open |

**Every setter reads back exactly what it was given**, which is worth knowing:
`retune_receiver()` reads the tuning back and uses 0 to mean "could not", and
on this device the read-back is never a *correction*. A device that rounds to
a PLL step would make that line load-bearing in a way it currently is not.

### Two findings, and both change what a check can test

**`stop()` returns -1 when no stream is running.** `stop_acquisition()` only
reaches it inside `worker_is_reading()`, so the program never sees this -- but
a runtime that stopped unconditionally would read a failure that is not one.
Whatever owns the lifecycle has to keep that guard, or treat "nothing to stop"
as success.

**The backend does not refuse an unreachable setting.** A sample rate of
250 kHz -- inside librtlsdr's documented hole -- returns 0 and reads back
250000. A frequency of **10 Hz**, eight orders of magnitude below the tuner,
also returns 0 and reads back 10. librtlsdr prints `[R82XX] PLL not locked!`
to stderr and reports success.

So `retune_receiver()`'s "Receiver rejected %.6f MHz or %+d ppm" is a message
for a failure this backend does not produce from an out-of-range value: it
accepts the request and tunes somewhere useless. The rollback paths that *are*
reachable are a failing `flush`, a read-back of 0, and a failing restart --
which is what the discriminating check has to drive, and it means the fake
backend must be told to fail rather than merely asked for something absurd.

It is also a real gap rather than a test detail: **nothing in the program
notices a tuning the tuner could not honour.** Whether a runtime should
validate against `device_profile`'s reach before asking is a decision for this
ticket, and the profile already carries the numbers (ticket 05).

## Comments

**Phases 2 and 3 done 2026-09-11, and they did not need the second receiver.**

`src/receiver_runtime.{c,h}` owns the sequence -- stop, apply, flush, read
back, restart, and the rollback at every step -- and `check-receiver-runtime`
is **65 checks** over it, driven by a fake device and a fake worker. The
sequence had been correct by inspection since it was written; nothing had ever
executed a single rollback branch.

The discriminating case is pinned: **the rate takes and the tuning refuses**,
and what the caller is left holding is the old rate, the old tuning, one
running worker and a sentence naming which half refused. So are a refusing
rate, a flush that fails, a read-back of zero, a restart that fails, a stop
that fails, an unchanged rate not stopping the worker at all, and a capture
refusing both with a reason.

**`retune_receiver()` and `retune_receiver_at_rate()` delegate to it now**, so
this is not a module waiting for a caller -- which is the fault
`.scratch/deepening/issues/13-*` was opened about. `runtime_over()` is the one
place the runtime is built over `struct app`, and it **borrows**: the applied
state stays the application's single owner, the worker's lifecycle stays in
`sdrprobe.c` with the thread and the signal mask.

**The three applied fields are one struct now** -- `struct receiver_applied`,
201 references across 12 files -- because a rollback has to put all three back
and a rollback over three separately owned fields is three chances to restore
two of them. That is part of phase 4, and deliberately only the part that a
single device can answer. `receiver_mode` and the gain are still loose: what
"mode" should be is exactly what the second receiver is needed to decide.

**Verified on hardware, because this is the half no check reaches.** A live
88-92 MHz sweep retuned at every step and came back with ten FM carriers,
89.499 MHz among them at 55.9 dB, and `survey blocks 6 settling 3` -- three
steps, three stale blocks discarded, which is the one line that caught the
settle fault when the survey machine was extracted. `make check` is 18936 in
58 suites and the capture pipelines are byte-identical.

## What is still blocked, and it is genuinely UHD

- **Phase 1b**, the UHD half of the transition table. Phase 1a is above.
- **The rest of phase 4**: `receiver_mode` as capability, source kind or
  deletion, and where gain belongs. One device cannot answer either.
- **Phase 5**, the lease orchestration and the caller migration, and
  **phase 6**, removing the old surface from `view.h`.

One thing phase 1a found that belongs to whoever finishes this: **the backend
does not refuse an unreachable setting** -- 10 Hz and a rate inside
librtlsdr's own hole both return success and read back -- so nothing in the
program notices a tuning the tuner could not honour. `device_profile` carries
the reach (ticket 05). Whether the runtime should check it before asking is a
decision this ticket now has the evidence to make.
