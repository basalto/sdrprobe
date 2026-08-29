# Mode S decode extends the technology-plugin seam with low core reuse

## Status

accepted

## Context and decision

ADR-0001 split the DSP into a generic core plus per-technology plugins, and
deferred demodulation/decoding, expecting "a future plugin may add a decode
stage behind the same per-technology boundary." The ADS-B (Mode S) plugin is
that decode stage.

Mode S decoding is magnitude-domain **preamble correlation + pulse-position
bit-slicing + CRC-24 validation + field parsing**. It reuses essentially none of
the generic core's FFT / power-centroid / channel-power primitives; it consumes
only the per-pair magnitude the core already produces from raw I/Q. We
nonetheless keep the plugin **seam** intact — `src/adsb_dsp.{c,h}`,
`tests/adsb_dsp_test.c`, and a hardware-free, `-lm`-only `check-adsb-dsp` target
— because the seam (a self-contained, independently testable per-technology
module), not literal primitive reuse, is the invariant worth preserving.

## Consequences

- ADR-0001's phrasing "reusing the generic primitives rather than duplicating
  FFT/centroid/power code" is **revised**: it applies to calibration-grade
  plugins, not to the decode stage, where there is little shared primitive.
- The plugin owns a small amount of decode state (a per-ICAO even/odd
  **position pairing cache**) needed for CPR global position decode. This is the
  minimal seed from which a full aircraft-tracking table could later grow, and
  is deliberately not that table.
- CRC failures are dropped; no single/two-bit error correction is attempted in
  this first cut.
