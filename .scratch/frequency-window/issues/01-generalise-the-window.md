# 01 — The window arithmetic stops being the survey's

Status: resolved
Blocked by: (none)

`src/survey_window.h` is already generic: a data range, a view range inside
it, and clamp, zoom, pan and reset over plain doubles. Rename it
`src/freq_window.h` and `survey_window_*` to `freq_window_*`, and move its
check with it.

Then add what a chart needs that a sweep did not: mapping a pixel to a
frequency and back, and what a drag between two pixels means. Both are pure
and belong beside the rest of it -- `sdrgui_geometry.h` has the plot-inside-a-
rect arithmetic already, so this is the frequency half of the same question.

No behaviour changes. The survey must decode and draw exactly as it does now.

## Comments

**2026-09-04** — Done. `survey_window.h` is `freq_window.h`, the check with
it, and nothing about the survey's behaviour changed: 44 checks passed before
the rename and after it.

Added the half a sweep never needed: `freq_window_hz_at`,
`freq_window_x_at` and `freq_window_drag`. The two mappings are asserted to be
each other's inverse across the whole plot, because a drag is measured with
one and drawn with the other and two copies of that arithmetic is how a
selection lands somewhere other than where the box was drawn.

`freq_window_drag` takes a drag in either direction -- somebody who selects
right to left has not made a mistake -- and refuses one narrower than a
minimum, leaving its outputs untouched. That is a click, or a hand that moved
while clicking, and zooming to a few hertz because of one is worse than doing
nothing.
