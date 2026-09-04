# 01 — The Settings panel's geometry, once

Status: ready-for-agent
Blocked by: (none)

`src/settings_layout.h`, in the shape of the six that already exist: a pure
`settings_layout_for(float width, float height)` returning a struct of
`Rectangle`s, reading nothing from the window and calling no raylib function.

## Why this one first

Ten rectangles, each declared twice -- once in `handle_settings_input` and
again in `draw_settings`. Two copies of the same numbers, and the only thing
keeping them equal is that both were edited at the same time. That is the
exact failure `row_list.h` was extracted to prevent for lists and
`chrome_tab_rect()` for tabs.

It is also the panel that shipped an overlapping caption today, and the panel
that will grow again: the centre frequency is due to leave it
(`.scratch/frequency-window/issues/04`), which moves rows around.

## The check

Add it to `tests/layout_test.c` beside the others: all-against-all overlap at
four window sizes, everything inside the panel, and the panel inside the
window. The calibration overlay's block is the closest model -- it was written
after that overlay shipped with three regions on top of each other, and it
catches the same class.

Two properties specific to this panel are worth stating outright: **every
control lies inside the panel** (they are positioned relative to it, so an
added row runs off the bottom rather than overlapping), and **the buttons
clear the last row**, which is what the caption collided with today.
