# 08 — Hit tests inside components that need a window

Status: resolved
Blocked by: (none)

`sdrgui_scan_chart_channel_at()` exists because a hit test written inside a
draw function selected a bar one or two to the right of the pointer: the bars
were drawn inside a label gutter the hit test did not know about. That one is
now a named function — but it is still not checked, because the geometry it
maps depends on `sdrgui_chart_area()`, which calls raylib's `MeasureText()` to
size the gutter, and `MeasureText` needs a font, which needs a window.

Every other pointer-to-index mapping in `sdrgui_*.c` has the same problem, and
it is the exact class of bug that reached the operator once already.

Two ways out, neither obviously right:
- Inject the measured gutter width: `sdrgui_chart_area()` takes a label width
  the caller measured, making the arithmetic pure and the measurement trivial.
  Touches every chart call site.
- Link the checks against raylib and open an off-screen window. Cheap to write,
  but it breaks the rule that checks need no window, and on this desktop that
  rule is what makes them runnable at all.

Needs triage: pick one. The first is more work and keeps ADR-0012 intact; the
second is quick and buys less.

## Comments

## Triage

Neither of the two options as written. The premise was wrong: `MeasureText` is
not *inside* the arithmetic, it only supplies the `gutter` argument to it.
`sdrgui_chart_area()` already took the measured width as a parameter and was
already pure -- it was simply sitting in a `.c` that links raylib for its
drawing, so nothing could reach it.

So the fix is neither the invasive injection (option 1) nor an off-screen
window (option 2): move the geometry into a header that needs raylib for the
`Rectangle` type and nothing else, exactly as the `*_layout.h` files do, and
let the caller go on measuring its own labels. `tests/layout_test.c` already
proved this compiles and links with `-lm` alone.

## Answer

Done in `src/sdrgui_geometry.h`, checked by `tests/sdrgui_geometry_test.c`
(`make check-geometry`, 24 checks). It holds `sdrgui_chart_area()`,
`sdrgui_point_in()`, `sdrgui_bar_width()`, `sdrgui_bar_left()` and
`sdrgui_bar_index_at()`; `sdrgui_scan_chart_channel_at()` is now three lines
over it, and the scan chart's *drawing* uses the same `sdrgui_bar_left()`, so
the two cannot disagree about where a bar is.

The check that matters is the round trip: for each of 124 bars, the point in
the middle of where that bar is drawn must map back to that bar. That is the
property the original bug broke, and there is no way to see it broken except
by clicking. A second check pins the original failure directly -- hit-testing
against the chart's outer rectangle instead of its plot is off by two channels
on this geometry, which is exactly the "one or two in right" that was
reported.

Also checked: the plot never escapes its chart, a chart too small for its own
furniture still has a drawable plot, the four edges, a click in the label
gutter selecting nothing, zero and negative bar counts, and sub-pixel bars in
a narrow window -- where a hit test tends to fall apart, and where all 124
still find themselves.
