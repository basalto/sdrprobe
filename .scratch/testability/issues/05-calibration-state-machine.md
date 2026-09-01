# 05 — The calibration state machine around the gate

Status: resolved
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

## Answer

Done in `src/calibration_gate.h`, beside the gate it feeds, and checked by the
existing `tests/calibration_gate_test.c` (`make check-calibration`, now 100
checks).

`struct calibration_tracker` holds the source, the tone-lock miss counter, the
residual ring and the statistics over it. `calibration_track(t, have_fcch,
have_centroid)` advances it and returns what the block may contribute --
`USE_FCCH`, `HOLD_TONE`, `USE_CENTROID` or `NOTHING` -- performing the buffer
resets on the way. `calibration_tracker_observe()` records a residual and
recomputes the centre, spread and standard error.
`update_calibration_measurement()` is a four-case switch over that, and
`struct calibration` embeds the tracker; `overlay_calibration.c` lost 66 lines
net.

The two mutations that matter both fail loudly:

- Keeping the buffer across a source change -- the exact bug ADR-0004 exists
  for -- fails four checks, including one that shows 41 residuals in a buffer
  that should hold 1.
- Recording the centroid during a tone gap, which looks like a free
  measurement, fails 22.

Also checked: a gap records nothing at all and the tone returning before the
limit leaves the buffer intact; losing the signal empties the buffer rather
than leaving it to age; the ring forgets what scrolls out of it while the
measurement count does not; and a clean tone with a realistic FCCH duty cycle
locks, but not before the gate's eight seconds.
