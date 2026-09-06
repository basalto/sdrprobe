# 01 - Do any of the three actually overflow?

Status: needs-triage

Before moving a line of drawing code: at every window size the program
supports, does `view_fm.c`, `view_survey.c` or `view_tetra.c` draw past the
bottom of a panel?

## Where to start

The row counts are static per state -- a panel draws a fixed set of fields --
so this is arithmetic rather than a search. For each panel: its rectangle from
the layout header at the sizes `check-layout` already walks (480x320 through
1600x900), the first row's offset and the step from the drawing code, and the
number of rows the worst case draws.

`make screens NAMES="fm survey tetra"` renders them, but a screenshot is one
window size and the fault is at the small end, which is exactly where nobody
looks.

## What the answer decides

- **Overflow found** -- ticket 02 moves that view's rows into its layout
  header and the check grows to cover it, the way the LTE one did.
- **No overflow anywhere** -- ticket 02 is still worth doing but for a weaker
  reason, and should be sized accordingly: the geometry is unchecked rather
  than wrong, and the next field added to any of those panels is what breaks
  it silently.

Either way the number belongs in `spec.md` so the next person does not have to
measure it again.
