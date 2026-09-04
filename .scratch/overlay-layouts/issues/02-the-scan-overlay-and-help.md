# 02 — The channel scan, and help

Status: resolved
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

## Comments

Resolved. `src/scan_layout.h` and `src/help_layout.h`, both walked by
`check-layout` at four window sizes including 900x600.

**Help had a real bug, and it was the kind only a small window shows.** The
sidebar is fifteen topics at a fixed 28 pixels plus a 3 pixel gap, inside a
panel that shrinks with the window. At 900x600 that runs the last topic
nineteen pixels below the panel, where nobody can click it -- and because it
is the *last* one, nothing above it looks wrong. The rows now shrink to fit,
and the check asserts the last entry stays inside the panel rather than
asserting a particular row height, so the next topic added is caught rather
than the next resize.

**The scan overlay's buttons overlap the chrome's Settings button, and that is
correct.** The overlay is drawn *instead of* the chrome rather than over it,
and owns the input while it is up, so the two never coexist. The check now
asserts the overlap deliberately, with a message saying which way round it
should be read: if the overlay ever starts drawing the chrome too, that check
fails and the reason is written down. Asserting they do *not* overlap would
have been a false alarm forever.

Also fixed while looking: the help text still called the band survey "Scope,
the Survey button". It has been a top-level tab since the tab reorganisation.
