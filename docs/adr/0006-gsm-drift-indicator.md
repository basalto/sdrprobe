# GSM calibration-health indicator with interrupting drift re-check

## Status

accepted

## Context and decision

A top-right circle on the base views shows whether the applied PPM correction is
backed by a GSM calibration: **grey** (never GSM-calibrated, or the PPM was
changed manually), **green** (the applied PPM came from a stable FCCH-tone lock),
**red** (a periodic re-check found drift), **amber** (a re-check is running).

Crystal error is thermal and drifts over time, but in normal use the receiver is
tuned wherever the operator set it (e.g. 1090 MHz), *not* on a GSM channel — so
drift cannot be observed passively. We decided the periodic check will
**briefly retune the receiver to the calibrated ARFCN, measure the FCCH
residual, then retune back** (`update_drift_check` in `src/sdrprobe.c`).

## Considered options

- **Passive / opportunistic** (only re-check when the receiver already happens
  to be on the GSM channel) — rejected: it would never detect drift while the
  operator is parked on another frequency, which is the normal case.
- **A background/second receiver stream** — not possible: librtlsdr gives one
  process a single tuner.

## Consequences

- The re-check **interrupts the live view for a few seconds** each cycle (retune
  + settle + measure + retune back). To keep that acceptable it is **opt-in**
  (Settings → "Auto GSM drift check", default off) and **infrequent**
  (`DRIFT_CHECK_INTERVAL_SECONDS = 300`), and the amber state plus a banner make
  the interruption explicit.
- Drift is only **reported**, never auto-applied: exceeding
  `DRIFT_MAX_PPM = 2.0` turns the circle red and raises a HUD banner and a
  stderr line; the operator recalibrates.
- Green requires an **FCCH-backed** lock specifically (a centroid-only apply
  stays grey), and any manual PPM change in Settings invalidates it, so the
  green light always means "this correction was verified against a GSM tone".
- The check reuses the calibration primitives (`retune_receiver`,
  `gsm_fcch_detect`, `robust_center_spread`) and runs only in the base views
  while no calibration/scan/settings overlay owns the tuning.
