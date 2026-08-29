# Self-contained DSP; no external DSP library

## Status

accepted

## Context and decision

The DSP in `src/sdr_dsp.c` — a 2048-point radix-2 float FFT with locally
generated Hann coefficients, coherent-gain dBFS normalization, and the
calibration estimators — is **hand-written and self-contained**. We deliberately
do **not** link an external DSP library (liquid-dsp or FFTW).

## Considered options

- **liquid-dsp** (low-level FFT plan or `spgramcf`) or **FFTW**, assessed in
  detail in [../liquid-dsp-sdrprobe-assessment.md](../liquid-dsp-sdrprobe-assessment.md).
  Rejected: the realistic net saving was only ~50-100 LOC; `spgramcf`'s PSD
  normalization differs from this project's unit-tone dBFS convention by
  ~31.35 dB (a wrapper would be needed and could mislabel output); pkg-config
  metadata is not portable across distributions; and it would break the
  `-lm`-only, hermetic contract of the deterministic DSP checks.

## Consequences

- The DSP checks (`make check-dsp`) build with only `-lm` — no optional package,
  no version-dependent numerical differences, fully reproducible.
- The project owns its normalization convention (a bin-centered unit complex
  tone reads 0 dBFS), which the tests lock down.
- The cost is maintaining a small FFT kernel; the FFT twiddle factors use a
  per-stage recurrence, adequate at N=2048 (bounded by the spectrum test) but a
  candidate for a precomputed table if the FFT size is ever raised.
