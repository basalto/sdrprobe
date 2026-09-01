# 05 — The calibration state machine around the gate

Status: ready-for-agent
Blocked by: (none)

`check-calibration` now covers the gate arithmetic — the clauses, the robust
statistics, the standard error, the mixed-source hazard. What it does not cover
is the machine around it in `src/overlay_calibration.c`: when the residual
buffer is reset, when the source switches from centroid to FCCH and back
(`update_calibration_measurement`), how a tone lock rides out missing bursts,
and what `update_drift_check` does across its phases.

The gate is only as good as the buffer it reads. ADR-0004 exists because a
correction was accepted from a buffer holding two sources; the arithmetic that
would expose that is now checked, but the code path that empties the buffer on
a source change is not, and it is the part that actually prevents the bug.

What it would take: a `calibration_state.h` holding the buffer, its source, the
miss counter and the settle clock, with `calibration_observe(state, residual,
source, seconds)` returning whether the buffer was reset and whether the lock
holds. `overlay_calibration.c` keeps the tuning and the drawing. Check: a
source flip mid-buffer empties it and restarts the settle clock; a tone lock
survives `CALIBRATION_FCCH_MISS_LIMIT - 1` misses and not the limit; a lock
already achieved is not silently re-armed by a single bad block.

## Comments
