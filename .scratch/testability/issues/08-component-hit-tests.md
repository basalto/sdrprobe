# 08 — Hit tests inside components that need a window

Status: needs-triage
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
