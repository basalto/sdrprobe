# 03 — Right-click waterfall context menu and signal report

Status: done

Closed 2026-09-15 as already built, in `src/overlay_signal_report.{c,h}` with
the right-click hit test at `src/sdrprobe.c:1253` and the menu opened at
`:1325`.

- `check_waterfall_right_click()` maps the pointer to a frequency and an age
  and calls `waterfall_context_menu_open()`.
- Both entries are there: Save (2.0 s through `iq_ring_save_slice()`) and Run
  Signal Report, with a notice banner reporting where the capture went.
- The report popup is modal, dismissed by its Close button, Escape, or a click
  outside it.

The analysis behind the popup is not in the overlay: it is
`.scratch/deepening/issues/16-retrospective-signal-analysis.md`, which made it
a presentation-free composition of `signal_probe` measurements with the popup
as an adapter over the result -- checked by `check-signal-analysis`.

**One thing this ticket left behind, now fixed under
`.scratch/testability/issues/09-view-state-to-input-state.md`**:
`struct waterfall_signal_context` reached `struct input_state` nowhere, so
`sdrprobe.c` did not know either surface was up. `q` is tested before
`handle_waterfall_context_input()`, so pressing it while the report was open
quit the program.

## What

Allow the user to right-click on any signal in a waterfall chart (Scope, SRD,
LTE, GSM, FM). From the cursor position $(x, y)$, calculate $(f, \text{age})$,
and open a context menu at the mouse coordinates:
1. **Save Signal (2.0s)**: Extract a 2.0 second slice ($\pm 1.0\text{ s}$) from the
   IQ ring buffer and save it to `captures/` with a complete sidecar.
2. **Run Signal Report**: Extract the slice, run `signal_probe` analysis, and
   display a modal popup with signal statistics, modulation verdict, carrier
   prominence, and spectrum.

## Specification

- Track `IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)` on waterfall plots.
- Context menu widget rendered over the window chrome.
- Signal report popup overlay with dismiss / close.
- Seamless interaction without interrupting acquisition or chart rendering.
