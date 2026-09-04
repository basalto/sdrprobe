# 01 — Inspect should reach every decoder this program has

Status: resolved
Blocked by: (none)

`handle_survey_input`'s inspect branch in `src/view_survey.c`, and the button's
label beside it in `draw_detail`.

## The work

- `BAND_PLAN_FM` in `band_plan.h`, and band II's allocations in
  `band_plan.c` pointing at it. Check it in `check-band-plan`: an allocation
  naming a decoder this program does not have is worse than naming none.
- The two missing branches. LTE needs the EARFCN for the frequency, which
  `lte_earfcn_for_hz` or the band table gives; FM needs the frequency and
  nothing else.
- The label, which today can only say GSM or ADS-B and says nothing at all for
  an LTE carrier -- so a reader looking at 806 MHz sees no button and concludes
  there is nothing to see.

## One decision worth making deliberately

Entering the FM view starts a band scan, which is right when somebody opens
the tab to find out what is on air and wrong when they arrived from the survey
having already chosen a frequency. Inspect should tune and not scan.

The same question does not arise for LTE: entering that view does not scan.

## What must be checkable

Which decoder a frequency maps to is a lookup and belongs in
`check-band-plan`. What Inspect then *does* with it is a frame-loop action and
is not reachable without a window; keep the decision (frequency -> decoder ->
which view) separate from the doing, so the first can be checked even though
the second cannot.

## Comments

**2026-09-04** — Done. `BAND_PLAN_FM` exists, band II points at it, and the
inspect branch handles all four. The label is a table in
`src/band_plan_view.h` rather than a ternary in the draw call, and
`check-band-plan` asserts the property that was missing: every decoder the
band plan can name has somewhere for Inspect to send a reader. A missing case
is not a wrong value, which is why nothing could see the old one.

LTE snaps to the channel raster rather than tuning where the energy was -- a
Zadoff-Chu correlation wants the carrier's centre, and the survey reports the
middle of a maximum, which on a lopsided carrier is not the same place.

FM tunes and does not scan, as the ticket asked. The rule turned out to belong
in `enter_fm` rather than at the call site: it scans when it has no results
*and* the receiver is outside band II. Arriving from Inspect the receiver is
already on the station, so it stays there; opening the tab cold from 1090 MHz
it still walks the band.

**Two things fell out.**

An existing check asserted FM broadcast offered *no* decoder, which was true
when written and went on being true after there was one. It caught the change,
which is the same guard working in the other direction.

And `--view survey` had stopped starting a sweep. `set_tab` returns at once
when the tab is already current, and since the survey became the default tab
it always is -- so `view_survey_enter` was never called and `--survey-range`
did nothing. Nothing failed: the window opened on the survey, which is where
it opens anyway, and the sweep simply never began. Found in seconds by the
debug log showing no tunes at all.

`--survey-select n` picks the nth strongest candidate once a scripted sweep
ends, because the detail panel and its Inspect button exist only behind a
mouse click and there was otherwise no way to photograph that screen.
