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

## Implementation order after the dependency clears

1. Record the observed RTL-SDR and UHD transition semantics, including stop,
   flush, read-back, failure and stream metadata.
2. Add a hardware-free runtime check using the existing fake backend and the
   acquisition worker seam. Prove rollback before moving production callers.
3. Move the start/stop and retune transaction unchanged behind the runtime.
4. Move the applied settings into it and make transition success the only
   writer. Replace direct fields with read-only snapshots/accessors.
5. Move lease orchestration behind it without changing
   `receiver_lease.h`'s pure state machine.
6. Migrate settings, calibration and views one path at a time.
7. Remove the old receiver helpers from `view.h` and close ticket 09.

Focused checks while iterating: the new runtime check,
`check-device-backend`, `check-acquisition`, `check-receiver-lease`, then
`check-pipelines`. Final gate: `make check-touched` and `make check`.

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
