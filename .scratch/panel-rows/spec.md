# Panel rows are geometry, and three views still compute them by hand

`lte_layout.h` gained `lte_panel_rows_for()` on 2026-09-06 because the LTE
view drew ten rows and a footer into a rectangle that holds six on a 900x540
window, and `check-layout` could not see it: the rows were `y += 21` between
draw calls, and the check only had the rectangle.

That is a general fault and it was fixed in one place. Measured across the
views:

| file             | inline `y +=` | rows in a layout header |
| ---------------- | ------------- | ----------------------- |
| `view_fm.c`      | 22            | no                      |
| `view_survey.c`  | 18            | no                      |
| `view_tetra.c`   | 4             | no                      |
| `view_lte.c`     | 0             | yes                     |
| `view_gsm.c`     | 0             | --                      |
| `view_adsb.c`    | 0             | --                      |
| `view_scope.c`   | 0             | --                      |

Only `lte_layout.h` models rows at all. Whether the other three actually
overflow at any window size this program supports is **unmeasured** -- the
count of inline advances says the check cannot see them, not that they are
wrong.

## Why this is worth doing and worth doing carefully

CLAUDE.md: "If a change adds geometry it goes in the view's layout header --
**all of it, not some of it**: a header holding half a screen puts a green
tick over the half it does not model, which is worse than having none." Three
headers currently hold half a screen each.

Against that, this is a refactor of drawing code with no behaviour to gain if
the panels happen to fit today. It earns its place only if it finds a real
overflow or prevents the next one, so the first ticket measures before
anything is moved.

## What the LTE version looks like

`lte_panel_rows_for(Rectangle)` returns the first row's baseline, the step,
the label and value columns, where the footer goes, and **how many rows the
panel holds**. A row past that capacity is not drawn: off the bottom edge is
worse than absent, so the caller orders its rows and the ones that fit are the
ones that matter. `lte_panel_footer_after(rows, used)` puts the footer under
what was actually drawn rather than at the panel's floor.

The arithmetic is not LTE-specific. Whether it should be promoted to a shared
header or copied is a decision for ticket 02, and copying it four times is how
five views end up with four subtly different row heights.
