# Calibration lock: standard-error gate over a source-homogeneous residual buffer

## Status

accepted

## Context and decision

GSM 900 calibration accumulates per-block PPM residuals and decides when the
correction is trustworthy ("Stable lock"). Two non-obvious statistical decisions
govern this (see `src/rtl_raylib.c` `update_calibration_measurement`,
`robust_center_spread`, and `docs/cellular-frequency-correction.md`):

1. **Gate on the standard error of a robust center, not on raw spread.** The
   center is the **median** of the recent residuals and the spread is
   `1.4826 × MAD`; the lock gate tests the **standard error of the mean**
   (`spread / sqrt(count)`, `CALIBRATION_MAX_SEM_PPM = 1.0`), i.e. how well the
   correction is *known*, not how much individual blocks scatter.
2. **Keep the residual buffer homogeneous per source.** The carrier estimate has
   two sources — the FCCH pure tone and the power centroid — whose residuals
   differ by many PPM. They are never mixed: the buffer resets on a source
   switch, FCCH detection runs independently of the centroid, and the source
   reverts to centroid only after `CALIBRATION_FCCH_MISS_LIMIT = 12` consecutive
   FCCH-free blocks.

## Considered options

- **Raw standard-deviation gate (e.g. stddev ≤ 3 PPM)** — rejected: a modulated
  GSM channel scatters 6-9 PPM per block, so it would never settle even though
  the median of many blocks is precise.
- **Smoothing/EMA of the spectrum feeding the estimator** — tried and removed:
  it correlated successive samples and made the standard error report false
  confidence.
- **A single mixed FCCH+centroid buffer** — rejected: the median flips between
  the two residual clusters, so the lock jumps and never holds.

## Consequences

- The lock reflects the uncertainty of the applied correction; median/MAD make
  it robust to a peak that momentarily hops. Independence of the per-block
  samples is required for the standard error to be valid, which is why no
  smoothing is applied.
- This supersedes the earlier "stddev ≤ 3 PPM" gate described in
  `docs/rtl_raylib-implementation.md`.
