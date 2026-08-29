# Per-technology DSP plugin architecture

## Status

accepted

## Context and decision

The `sdrprobe` cellular calibration grew GSM-specific DSP (ARFCN mapping, FCCH
tone detection) mixed into one `sdrprobe_dsp.*` file alongside generic SDR
primitives. To keep each radio technology easy to inspect and unit-test on its
own — and to make room for future 4G/5G support — we split the DSP into a
**generic core** and **per-technology plugins**:

- `src/sdr_dsp.{c,h}` — technology-independent SDR primitives: byte→float I/Q
  conversion, DC removal, magnitude peak binning, signal statistics, the
  Hann-windowed FFT / dBFS spectrum, the power-centroid carrier estimate, the
  evenly-spaced channel-power reducer, and the PPM correction. Prefix
  `sdr_dsp_` / `SDR_DSP_`.
- `src/gsm_dsp.{c,h}` — the first **technology plugin**. A plugin provides exactly
  two things and reuses the generic core for everything else:
  1. a **channel → frequency map** (`gsm_downlink_hz`), and
  2. a **reference-tone / sync detector** (`gsm_fcch_detect`).
  Prefix `gsm_` / `GSM_`. It depends on nothing from `src/sdr_dsp.h`.

Each layer has its own hardware-free test binary and make target
(`check-sdr-dsp`, `check-gsm-dsp`, aggregated by `check-dsp`), so a technology's
checks build and run in isolation.

## Scope

The plugin contract is deliberately limited to **calibration-grade detection**:
identify a reference carrier and measure its frequency. Full demodulation and
message decoding are explicitly out of scope for now; a future plugin may add a
decode stage behind the same per-technology boundary.

> **Revised by ADR-0009.** The decode stage anticipated here now exists as the
> Mode S / ADS-B plugin. Because Mode S decoding reuses almost none of the
> generic FFT/centroid/power primitives, ADR-0009 clarifies that the reuse
> expectation below applies to calibration-grade plugins, not the decode stage;
> the preserved invariant is the plugin *seam*, not literal primitive reuse.

## Consequences

- Only the DSP layer is split. The GSM *application* logic (calibration/scan
  state and rendering) still lives in `src/sdrprobe.c`, tightly coupled to the
  raylib `struct app` and the retune path. Turning that into a runtime plugin
  is a separate, larger effort; this ADR covers the DSP seam only.
- Adding a technology means adding `<tech>_dsp.{c,h}` + `<tech>_dsp_test.c` +
  a `check-<tech>-dsp` target, and reusing the generic primitives rather than
  duplicating FFT/centroid/power code.
