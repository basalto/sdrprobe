# 05 — Wire the analysis mode into the ADS-B view

Status: resolved
Blocked by: 02, 03, 04

In `src/view_adsb.c` and `struct adsb_view` (`src/app.h`):

- Add `int analysis_mode;` and `int hold_last_good;`, plus the latched
  `struct adsb_frame_trace trace;` and the last CRC-valid trace when holding.
- `update_adsb()` passes the trace and stats structs into `adsb_demod()`.
- A `View: Log` / `View: Analysis` button in the top right, mirroring the GSM
  view's toggle, and a small `Hold last good` toggle beside the scatter.
- In analysis mode draw three `sdrgui_burst_chart`s across the upper row:
  - `SDRGUI_BURST_LINE`, "Preamble Score Landscape", y auto-ranged to the
    landscape's own max.
  - `SDRGUI_BURST_BAR`, "Pulse-Position Bit Confidence", y 0..1.
  - `SDRGUI_BURST_LINE`, "Frame Magnitude Envelope", y 0..1.2.
  Each takes `bit_count` entries, not always 112 — a short frame must not draw
  56 bits of stale tail.
- Lower right: `sdrgui_constellation` with x = `margin[i]`, y = `amplitude[i]`
  recentred, `bit` for the colour, caption "Bit decisions (last frame)". Empty
  notice: "waiting for a Mode S frame...".
- Caption the panel with the attempt's outcome: DF, ICAO when the CRC passed,
  and `CRC FAILED` in the warning colour when it did not. Do not print an ICAO
  from a frame that failed its CRC — those bits are not an address, they are
  noise that happened to sit in those positions.
- Empty states: the charts say "waiting for a Mode S frame..."; when the
  receiver is not near 1090 MHz the existing retune affordance still applies and
  should be the thing shown, not three empty charts.

`Esc` still leaves to the Scope tab, `h` still opens help.

## Comments

**Component finding (implementation).** `sdrgui_message_log` did not take what
this ticket needed. Its raw-hex column sits at a fixed `x_detail + 430` with no
reference to the panel width, and neither the detail nor the raw text was
clipped, so in the analysis mode's narrower log the hex ran past the panel's own
right edge and drew over the scatter beside it. Fixed in the component rather
than worked around here: every field now goes through `sdrgui_text_fit`, and the
raw column is given up when there is no room for it, leaving the decoded text
the space. Same class of bug as the caption/gutter overhang that
`sdrgui_burst_chart`'s comment describes — a component drawing outside the
rectangle it was handed, invisible until a caller made the rectangle small.

**Caption length.** The scatter's caption is "Bit decisions", not "Bit decisions
(margin / amplitude)": the longer text ran under the Hold last good button in
the panel's top-right corner. The axes are explained in the help overlay
instead.
