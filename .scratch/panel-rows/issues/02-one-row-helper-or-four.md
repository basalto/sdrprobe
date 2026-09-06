# 02 - Promote the row helper, or leave it in the LTE view

Status: needs-info

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
