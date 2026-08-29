# Raw-I/Q rendering seam for sdrprobe

## Status

accepted

## Context and decision

`sdrprobe`'s rendering layer — the `sdrgui` visual components and the widgets
it composes — receives **raw centered complex I/Q** (and the spectra/magnitudes
derived from it), not a pre-reduced magnitude-only frame. Keeping the complex
samples available to the view layer is what lets the spectrum (FFT) and I/Q
scatter views exist at all.

## Considered options

- **A narrow magnitude-only rendering interface** (per-bin peak magnitudes only)
  — rejected: it discards the phase and complex information the spectrum and
  I/Q scatter views require.

## Consequences

- The seam is deliberately wide: raw samples cross it, and the view layer owns
  its own FFT and scatter reductions rather than consuming a reduced frame.
- In exchange, the visualizer can offer views a magnitude-only interface could
  not express.
