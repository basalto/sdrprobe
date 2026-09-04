# The three screens check-layout cannot see

Every view in this program derives its geometry from a header of pure
functions -- `gsm_layout.h`, `adsb_layout.h`, `lte_layout.h`, `fm_layout.h`,
`survey_layout.h`, `calibration_layout.h`, `chrome_layout.h` -- and
`tests/layout_test.c` walks all seven at four window sizes.

Three screens do not:

| screen | rectangles | where they are |
| --- | --- | --- |
| Settings | 20 declarations, 10 distinct | inline, **written twice** |
| Channel scan | 4 declarations, 2 distinct | inline, **written twice** |
| Help | one struct | `help_layout_now()`, inside the .c |

The first two are the same failure the rest of this program has already been
bitten by twice and has built two headers to prevent: `row_list.h` exists
because a list's draw and its hit test were two copies of the same arithmetic,
and `chrome_tab_rect()` exists because a tab's lookup and its rectangle were.
In the Settings panel and the scan overlay, **every rectangle is declared once
in the input handler and again in the draw call**, and nothing but reading
both keeps them equal.

Help is a different and much smaller problem: it already has one source of
truth, it is just not in a header, so no check can reach it.

## The evidence, from today

Adding a resolution stepper to the Settings panel put its caption at `y + 262`
where the checkbox above runs to `y + 272`. It shipped in a build, and the
only thing that found it was a screenshot -- `check-layout` had nothing to
say because it has never seen that panel. `check-layout` currently reports
2340 checks and none of them are about the screen this program opens an
overlay onto most often.

## What this is not

Not a request to check text. `check-layout` compares rectangles and the rule
in `CLAUDE.md` is explicit that a screenshot is still required for anything
else. This is about the rectangles, which are exactly what it can check and
currently does not for three screens.
