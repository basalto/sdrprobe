# GUI presentation layer: sdrgui components over vendored raygui widgets

## Status

accepted

## Context and decision

`sdrprobe`'s UI was one ~3600-line file mixing three concerns: generic widgets
(buttons, checkboxes, text fields, hit-testing), bespoke SDR visualizations (the
four plots, the waterfall with a frequency/ARFCN axis and carrier markers, the
scan bar chart, the health circle, HUD, cursor readouts), and the application
logic (calibration/scan/drift state machines, retuning). We split the
presentation into two layers so screens compose from named pieces:

- **`vendor/raygui.h`** (vendored, pinned) — the immediate-mode widget toolkit
  for the generic widgets (buttons, checkboxes, the 2G/4G/5G toggle group).
  `RAYGUI_IMPLEMENTATION` lives in a dedicated `src/raygui_impl.c` compiled
  without the strict `-W` flags, so our own translation units stay `-Wall -W`.
- **`src/sdrgui.{c,h}`** — reusable **SDR visual components** (`sdrgui_spectrum`,
  `sdrgui_waterfall` with an axis-mode flag and a markers array, `sdrgui_scan_chart`,
  `sdrgui_health_dot`, `sdrgui_hud`, `sdrgui_cursor_readout`, plot helpers, and a
  small `sdrgui_text_field`). Each takes a plain param struct (buffers +
  geometry + style), never `struct app`. It depends only on raylib, not raygui.

`src/sdrprobe.c` keeps the application state, the calibration/scan/drift logic
and `retune_receiver`, and now only *composes* screens from sdrgui + raygui.

## Considered options

- **raygui only** (assessed in the previous round) — rejected as the whole
  answer: it replaces only ~250 LOC of generic widgets and leaves the bulk (the
  SDR visualizations) hand-rolled, so it does nothing for HUD composition.
- **sdrgui only, keep hand-rolled widgets** — viable and dependency-free, but we
  chose to also take raygui for the widget boilerplate and free toggle groups.
- **raygui `GuiTextBox` for the entry fields** — rejected: it owns its edit
  buffer and cannot do the per-keystroke validation (frequency `K/M/G` suffixes,
  PPM sign, ARFCN digits-only), so a small `sdrgui_text_field` keeps that exact
  behavior instead.

## Consequences

- Screens become a few composition calls; the SDR visual components are reusable
  across future technology screens and are decoupled from `struct app` — the
  same deep-module direction as ADR 0001, applied to the view layer.
- A vendored third-party header is introduced. Unlike the dependency ADR 0003
  declined for DSP, this one is GUI-build-only: the hermetic DSP checks link only
  `sdr_dsp`/`gsm_dsp` and never the GUI, so the `-lm`-only test contract is
  unaffected. raygui is pinned in-tree (no pkg-config portability problem).
- The look is preserved by matching the current palette via `GuiSetStyle`, so the
  refactor is behavior- and appearance-preserving.
