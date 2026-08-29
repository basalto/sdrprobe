# FCCH Tone Detection — Implementation Plan

Status: **Phase 1 (DSP seam + tests) and Phase 2 (calibration integration) are
implemented.** Phase 3 (waterfall marker) and a UI on/off toggle remain
optional follow-ups. This is the robust refinement of the power-centroid carrier
estimate used by the cellular calibration screen (see
[cellular-frequency-correction.md](./cellular-frequency-correction.md)).

## Motivation

The current GSM calibration estimates the carrier by taking the power centroid
of a ~200 kHz-wide, GMSK-modulated channel. That center wanders block to block
because the channel is a wide modulated signal whose strongest bin hops between
spectral features. The per-block scatter (typically 6-9 PPM) is intrinsic to
the method, so a strict per-block stability gate never settles even though the
*average* of many blocks is well determined.

The **FCCH** (Frequency Correction Channel) removes that wander at the source.
It is an all-zeros GMSK burst, which produces a **pure sinusoid**. A CW tone can
be located far more precisely than a modulated channel's centroid and is
inherently stable, so it is the reference used by production GSM receivers
(osmocom, gr-gsm) to discipline frequency.

## FCCH facts

- FCCH exists **only on the BCCH carrier**.
- The all-zeros burst produces a pure tone at exactly
  **+1625/24 kHz = +67 708.33 Hz** above the carrier center.
- It occupies timeslot 0 of frames 0, 10, 20, 30, 40 of the 51-frame BCCH
  multiframe (~235.4 ms), i.e. **~5 bursts per 235 ms**, each **~577 µs**
  (~1 200 samples at 2 MS/s). Duty cycle ~1.2%.
- A 65 ms sample block therefore contains ~1-2 FCCH bursts on average, so most
  blocks yield a detection.

## Why it fixes locking

A tone-frequency estimate over ~1 200 high-SNR samples reaches **< 0.1 PPM**
precision (the Cramér-Rao bound for tone frequency scales as `1/(SNR·N³)`), and
it is **unbiased** — there is no centroid clipping and no peak hopping. Fed into
the existing recent-window median/MAD → standard-error-of-the-mean gate, the
uncertainty collapses well below 1 PPM almost immediately, and the applied
correction is far more accurate than the centroid method.

This is a **time-domain** method operating on raw I/Q. It does **not** require a
larger FFT, so the 2048-point display FFT is unaffected.

## Baseband geometry

The receiver is tuned to `tune_hz = expected − 400 kHz`, so a signal at RF `f`
appears at baseband `f − tune_hz`:

- carrier center → baseband `≈ +400 kHz (+ error)`
- FCCH tone → baseband `≈ +467.708 kHz (+ error)`

Detection therefore searches for a tone near **+467.708 kHz** baseband,
estimates its exact baseband frequency `f_tone`, and derives:

```text
carrier_RF   = tune_hz + f_tone − 67708.33
observed_ppm = (carrier_RF − expected) / expected × 1e6
```

## Implementation phases

### Phase 1 — DSP seam (`src/gsm_dsp.{c,h}` (+ generic `sdr_dsp`)), hardware-free and unit-tested

**Implemented.**

1. `gsm_fcch_detect(i, q, pair_count, sample_rate, target_offset_hz,
   search_window_hz, &result)`:
   - Slides a `~400 µs` window (`GSM_FCCH_WINDOW_SECONDS · sample_rate`,
     ~800 samples at 2 MS/s) across the block, hopping a quarter window.
   - Per window it accumulates the lag-1 autocorrelation
     `sum_n z[n] · conj(z[n−1])`. The angle of that sum is a robust,
     amplitude-weighted tone-frequency estimate; its magnitude divided by
     `sum_n |z[n]||z[n−1]|` is the **coherence** (toneness) in `[0, 1]`. A pure
     tone approaches 1; modulated data and noise cancel toward 0.
   - Among windows whose estimated frequency lands within `search_window_hz` of
     `target_offset_hz`, it keeps the one with the highest coherence, and marks
     `detected` when coherence ≥ `GSM_FCCH_MIN_COHERENCE` (0.9).
2. Result type (actual):

   ```c
   struct gsm_fcch_result {
       int detected;
       double tone_frequency_hz;   /* baseband; app maps to RF carrier */
       float confidence;           /* coherence in [0, 1] */
       float amplitude;
   };
   ```

   The RF-carrier mapping (`tune_hz + tone − 67708.33`) stays in the
   application, keeping the DSP seam free of tuning semantics.
3. Tests (`tests/gsm_dsp_test.c`, `test_fcch_detection`):
   - A known-frequency tone embedded in noise for ~800 µs is detected, and its
     frequency is recovered within 200 Hz.
   - Noise-only blocks are rejected.
   - A tone outside the search window is rejected.

### Phase 2 — Application integration (`src/sdrprobe.c`)

**Implemented as centroid-primary with FCCH refinement** (a small deviation from
the originally proposed FCCH-primary/centroid-fallback ordering).

- Each accepted block first runs the centroid estimator, which supplies the
  peak/guard-floor/prominence metrics and a strong-carrier fallback frequency.
- `update_calibration_measurement` then runs `gsm_fcch_detect` on
  `app->i_samples` / `app->q_samples`, searching near
  `expected − applied_frequency + 67708.33` (≈ +467.708 kHz baseband) within
  ±50 kHz. On a confident detection the carrier is taken from the FCCH tone
  (`applied_frequency + tone − 67708.33`); otherwise the centroid is used.
- **Homogeneous residual buffer.** FCCH and centroid residuals differ by many
  PPM, so they must never share the recent-residual buffer — mixing makes the
  median flip between clusters and the lock jump. The measurement tracks a
  source (`centroid`/`FCCH`): switching source resets the buffer; while locked
  to the tone a burst-free block is skipped rather than recorded; only after
  `CALIBRATION_FCCH_MISS_LIMIT` (12) consecutive burst-free blocks does it
  revert to centroid.
- The resulting residual feeds the existing recent-window → median/MAD →
  standard-error-of-the-mean gate. Because FCCH estimates are precise and
  consistent, the standard error collapses quickly and the lock holds.
- The status line reports the active source, `FCCH tone` or `centroid`.

Remaining: an explicit UI on/off toggle (currently the refinement is always on
for GSM).

### Phase 3 — Visualization (optional)

- Draw an FCCH tone marker at `+67.708 kHz` above the expected carrier on the
  zoomed calibration waterfall so the operator can see the lock source.

## Risks and effort

- Toneness thresholds need tuning to avoid false positives from other CW-like
  signals; the tight search window around `+467.708 kHz` mitigates this.
- Non-BCCH carriers have no FCCH, so the centroid fallback is essential.
- Estimated effort: ~150-250 LOC of DSP plus tests, and ~40 LOC of integration.
  Moderate, and cleanly testable behind the existing DSP seam.

## Relationship to the current estimator

The centroid estimator, the time-independent per-block measurement, and the
median/MAD → SEM stability gate remain in place. FCCH detection replaces only
the per-block frequency source: a precise, unbiased tone estimate instead of a
wandering channel centroid. The stability gate, recent-window statistics, PPM
sign convention, and Apply-PPM flow are unchanged.
