# 01 — The Settings panel's geometry, once

Status: resolved
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

## Comments

Resolved. `src/settings_layout.h`, and 134 checks over it at four window
sizes.

Two things came out of it that the ticket did not anticipate.

**The rows are laid out from a running cursor, not from literals.** The panel
was a column of hardcoded offsets -- `y + 83`, `y + 164`, `y + 218`, `y + 250`
-- with a height of 420 arrived at by hand and a comment saying why it was not
380. Adding a row meant picking a number and hoping. It now accumulates, and
the panel's height falls out of where the rows end, so the failure that made
it 420 (a new row landing under the buttons) is no longer reachable. The panel
came out shorter than 420 as a result, which is the hand-tuned slack it had
been carrying.

**A second overlap was found in the process, of exactly the kind the first
one was.** The rejection message -- what the panel says when a frequency will
not parse -- was drawn at `panel.y + 289`, sixteen pixels below the Scope
resolution caption at 282. Any rejected frequency would have rendered over
that caption. It had never been seen because it needs a rejected input *and*
that row to exist, and the row was three days old. It now has a line of its
own beside the buttons, and a check.

The caption check is the part worth keeping in mind for `02`. Comparing
controls alone cannot catch this class: a caption is not a control, so two
rows can be a comfortable distance apart while the lower one's caption sits on
the upper one. Only four controls here carry a caption above them -- the
checkboxes label themselves to the right, the buttons carry their text inside
-- so the check asks about those four rather than pretending every rectangle
has one.

`settings_panel()` is gone; there is no second way to get the geometry.
