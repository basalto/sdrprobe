# 01 - Do any of the three actually overflow?

Status: resolved

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

## Answer

Status: resolved. **Yes, all three overflow**, and not marginally.

Measured from the drawing code -- the offset of the first row, the steps
between the rows each panel can draw at most, and the height of the last line
-- against the rectangles the layout headers give, at the sizes `check-layout`
already walks:

```
                     panel     worst case
1600x900  fm signal   324.9        191.0   fits
1100x720  fm signal   238.5        191.0   fits
1000x540  fm signal   152.1        191.0   OVERFLOWS by  39 px  (2 rows)
 640x400  fm signal    90.0        191.0   OVERFLOWS by 101 px  (5 rows)
1000x540  tetra id    132.7        146.0   OVERFLOWS by  13 px
 640x400  tetra id     90.0        146.0   OVERFLOWS by  56 px
```

The FM station panel is the same shape as its signal panel and overflows by
the same amounts.

A first pass at this counted rows by grepping `draw_row|sdrgui_text_fit` over
a line range and got nine and six where the answer is eight and five --
captions and notes counted as rows. The numbers above are from reading the
functions. The overflow is real either way, but a measurement taken by grep is
not a measurement.

`view_survey.c` is **not** in the table. Its detail panel is variable-length
prose whose row count depends on what is selected, not a fixed table of
fields, so the same arithmetic does not describe it and the same fix does not
apply. That is a different ticket if anyone wants it.
