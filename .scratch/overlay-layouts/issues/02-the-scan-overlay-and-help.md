# 02 — The channel scan, and help

Status: needs-triage
Blocked by: 01

The two smaller ones. Worth doing together and after 01, because the shape
will be settled by then.

## The channel scan

Two rectangles -- Back and Rescan -- declared in `draw_scan` and again in
`handle_scan_input`, with the same literals in both. Small enough that nothing
has gone wrong yet, and the same arrangement as the Settings panel, so it will
go wrong the same way.

They are positioned from `GetScreenWidth()` at the top right, where the
chrome's own buttons also live. Whether they collide with anything at a narrow
window is not currently knowable, which is the point.

## Help

Already has one source of truth: `help_layout_now()` builds a `struct
help_layout` and both the input and the draw use it. The only thing wrong is
that it lives in the .c, so `check-layout` cannot reach it.

Moving the struct and the function to `src/help_layout.h` is most of the work.
Worth checking once there: the topic column, the body and the close button do
not overlap, and the body's scroll region stays inside the panel -- it is the
one screen here that scrolls a variable amount of text, so its geometry has a
case the others do not.
