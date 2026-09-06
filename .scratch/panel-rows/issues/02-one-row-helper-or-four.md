# 02 - Promote the row helper, or leave it in the LTE view

Status: resolved

Blocked on ticket 01, which says whether there is a fault to fix or only a
gap to close.

## The decision

`lte_panel_rows_for()` and `lte_panel_footer_after()` are twenty lines of
arithmetic with nothing LTE-specific in them: a caption drop, a row step, a
proportional label gutter with a cap, a capacity and a footer. Three other
views want the same thing.

Copying it is how five views end up with four subtly different row heights,
which is the fault it was written to prevent wearing different clothes. A
shared `panel_rows.h` is the obvious answer.

The argument against is that the layout headers are deliberately per-view --
`gsm_layout.h`, `adsb_layout.h`, `tetra_layout.h`, `lte_layout.h` each stand
alone so a change to one cannot move another -- and a shared header is a
coupling those files were written to avoid. `sdrgui_geometry.h` already exists
for geometry every view shares, which is the precedent worth reading first.

## What must be checkable

Whatever comes out, `check-layout` walks it for every view that uses it, and
asserts what the LTE version asserts: every row and the footer inside the
panel, the label column clear of the value column, neither collapsed. Raising
a panel's capacity by three must fail it -- that is how the LTE one was shown
not to be vacuous.

## What this must not become

A rewrite of how the views draw. The rows move into a header; the fields, the
order and the wording stay exactly as they are, and the screenshots before and
after should differ in nothing at all.

## Answer

Status: resolved. Promoted, not copied -- `src/panel_rows.h`.

`sdrgui_geometry.h` settled the argument the ticket recorded. It is already a
shared geometry header for the same reason: raylib for `Rectangle` and nothing
else, so a check compiles it and links `-lm` with no window and no receiver.
Row metrics are the same kind of thing, and four copies of a row step is how
five panels end up with four row heights -- the fault this exists to prevent,
wearing a different coat.

The spacing stays per-view, because it should: a panel of eighteen-point
network fields and one of fifteen-point decode statistics do not want the same
step. What they share is the arithmetic, so `panel_rows_for()` takes the
caption drop, the step, the footer height and the gutter, and each layout
header names its own.

Migrated: `lte_layout.h` (which loses its private copy and keeps only the
statistics columns, the one part no other view has), `fm_layout.h` and
`tetra_layout.h`. `check-layout` walks all of them through
`check_panel_rows()`, and adding three to the capacity fails it sixty-three
times -- which is how the checks were shown not to be vacuous.

### What changed on screen, and what did not

Nothing, at the sizes the screenshots render. FM before and after are
identical field for field; TETRA's identity panel shows the same five fields.
Below 1000x540 the difference is the point: rows that used to be drawn off the
bottom edge are now not drawn at all.

Two things did change and both were mine to fix:

- TETRA's identity rows were 26, 26, 24 and 26 apart. The 24 was an
  inconsistency rather than a decision and is now 26, which moves one field by
  two pixels.
- The FM label gutter was a flat 128 px. As a fraction it truncated
  "subcarrier axis" on this panel width, so it is 0.50 with the same 128 cap
  -- the old width where the panel is wide, and proportional where it is not.
  The first version used 0.45 and I only noticed by comparing screenshots.
