# Two rendering seams: magnitude-only for the TUI, raw I/Q for the visualizer

## Status

accepted

## Context and decision

The two visual probes cross different seams into their rendering layers.
`rtl_tui` renders through a **magnitude-only** interface (`tui_frame(peaks, ...)`
in `src/tui.h`) — no SDR types cross it, only per-bin peak magnitudes.
`rtl_raylib` renders through a separate **raw-I/Q** seam, keeping centered
complex samples available to the drawing layer.

## Considered options

- **Reuse the single magnitude-only TUI seam for the raylib visualizer** —
  rejected: the magnitude interface discards phase and complex information that
  the spectrum (FFT) and I/Q scatter views require.

## Consequences

- The magnitude seam stays deliberately narrow, so the terminal renderer never
  depends on SDR internals and its ramp can scale to each frame's noise floor.
- The raylib layer receives raw I/Q and owns its own FFT/scatter reductions,
  accepting a wider seam in exchange for views the magnitude interface could not
  express.
