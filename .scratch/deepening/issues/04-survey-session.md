# 04 - Pull the survey's state machine out of its view

Status: resolved, 2026-09-09

`view_survey.c` is 2 667 lines. The helpers it calls are deep and checked --
`survey_sweep.h`, `survey_confirm.h`, `survey_carrier.h`, `survey_suspect.h`,
`site_history`, `survey_store`, `freq_window.h` -- but the decisions that
sequence them live in the view, beside the drawing:

- when a sweep is finished and the fold is applied (`:283`);
- when a confirm pass starts and what it asks about (`:328-440`);
- what a watch reports as appeared and lost (`:543-590`);
- which row is hovered and selected, computed during draw (~`:2400-2450`)
  and reused by the click handler;
- 18 inline `y +=` advances (`.scratch/panel-rows/` measured them) that
  `check-layout` cannot see.

`survey_report.c` (591 lines) is the headless twin and re-sequences the same
steps for `--survey`, `--survey-confirm`, `--survey-watch`, `--survey-save`.

ADR-0012: a function that draws or reads input may not also decide. This view
decides.

## The deepened module

A survey session: idle → sweeping → confirming → watching, block in, state
out. It owns the plan, the fold, the candidates and carriers, the confirm
targets and verdicts, the history marks, and the watch summary. The view
draws the state and turns input into intents (start sweep, ask again, watch,
select candidate N); `survey_report.c` prints the state. Both are adapters.

Rows and hit tests go through `panel_rows.h` / `row_list.h` /
`sdrgui_geometry.h` like every other view, so the 18 inline advances become
layout and `check-layout` sees them.

## What moves

- The sweep/confirm/watch sequencing from `view_survey.c` and
  `survey_report.c` into the session.
- Hover/selection out of the draw phase into the input phase, as an index
  computed by a geometry function.
- `struct survey_view` splits: session state vs drawing state (zoom window,
  menu-open flags, text fields).

## Checks

`check-survey-session`: a capture surveyed once yields the same candidates
twice (the headless invariant, now without a process); a confirm pass over a
synthetic sweep produces the three verdicts; a watch of N sweeps reports
appeared/lost; the machine refuses to watch without a site. Existing
`check-survey-sweep`, `check-suspect`, `check-survey` unchanged.

## Order

After 01: the sweep, the confirm pass and "Open waterfall" all borrow the
receiver, and the session should borrow through the lease rather than carry
`return_frequency` fields of its own.


## What was done

`src/survey_session.{c,h}` and `check-survey-session`, 158 checks where the
sweep, the confirmation pass, the watch and the measurement had **none**: every
one of those decisions could only be reached by running the built program
against a dongle and clicking at it.

The machine is idle -> sweeping -> confirming, with watching as a sweep that
goes round again and measuring as a look at one candidate. Two refusals shape
its interface and both were written because they are what let the two copies
diverge in the first place:

- **It does not touch the receiver.** It says where it wants the tuning
  (`event.retune_hz`) and the adapter obeys, then says whether the tuner moved
  (`survey_session_retuned`) or would not (`survey_session_retune_failed`). The
  window borrows through a lease and the headless sweep calls `retune_receiver`
  straight; neither decides anything.
- **It does not read or write files.** It owns a `struct site_history`, marks
  the sweep against it, and says when what it holds has changed
  (`event.history_dirty`) or needs reloading (`event.marks_stale`). Only the
  program knows which installation a baseline belongs to (ADR-0022).

`struct survey_view` is now the window: text fields, menus, the frequency
window, the selection, the drag, the lease tokens. Nothing in it decides.
`struct survey_block` is the seam -- samples, a spectrum, a scratch array, and
the facts about the container -- so no part of the machine sees `struct app`.

### What the two copies disagreed about

Four things, and no check could see any of them: `make check` never runs a
sweep, and the answers a capture pins are the ones a broken sweep does not
change.

1. **The headless sweep folded every block it consumed.** `survey_sweep.h` has
   said since it was written that a block arriving inside the settle holds the
   previous step's samples, and the window obeyed it; this path did not. At a
   0.10 s settle and a 0.10 s dwell that is about a third of everything it
   measured, written into bins at frequencies nothing was transmitting on.
   One machine, one answer: a 13-step sweep of band II reports
   `blocks 26 settling 13` from either side.

2. **The watch reported "carriers 0" for its first sweep.** It folds the sweep
   into the history and immediately clears the array to go round again, so by
   the time an adapter printed the line the count was zero -- two numbers about
   the same sweep, one of them from after it had been thrown away.
   `watch_carriers` is the count that belongs to the sweep that was folded.

3. **The `# confirm` header disagreed with its own rows.** The headless one
   promised five fields where its rows carried seven, and the window printed no
   `kind` line at all. `survey_print_confirm_{header,target,summary}()` are the
   one spelling, and `docs/band-surveys.md` now shows the real record.

4. **The spectrum a candidate may be measured out of.** The window handed over
   `spectrum_average`, which every block rebuilds from scratch, where the
   headless path kept a peak hold over the whole capture -- so the window's
   saved widths came from whatever happened to be transmitting last.
   `survey_session_spectrum()` is the hold, and it refuses across a swept
   range, where it belongs to whichever step was last.

Also gone: the survey view read the site history **off disk on every folded
block**, because the marks were refreshed inside the peak finder and the peak
finder ran during the dwell. The marks are recomputed from the history the
session already holds, and the file is read when the site changes or a sweep
ends.

### What the extraction itself broke

Five, and they are worth naming because they are the shape of what an
extraction gets wrong. The first two were caught by measurement -- a live sweep
against the old binary, alternating -- and the last three by reading the code
that had just been written.

1. **The settle was timed from the request rather than from the tuning.** The
   first live measurement after the extraction reported `settling 0`. A retune
   flushes the receiver's pipeline and costs about a tenth of a second, which
   is the whole of `SURVEY_SETTLE_SECONDS`, so timed from the request every
   block of the step reads as post-settle. `survey_session_retuned()` exists
   because the session cannot know when the tuner moved and the adapter can.

2. **A step could only end when a block arrived.** The old headless loop
   re-read the clock after each block and could leave a step it had already
   heard something on; the extraction returned early on "no block", which cost
   one block a step -- 39 over a 13-step sweep against 26, the sweep taking
   half again as long for signal it had already measured. `update_survey()` is
   called every frame now with `spectrum_updated` as a parameter rather than as
   a guard, because the machine has decisions on both clocks: a *look* is
   counted only when a block arrives, and a *step* is over on time alone.

The other three are one mistake made three times -- reaching for
`survey_session_clear()`, which forgets the sweep, where the thing to forget
was the measurement:

3. **Selecting a candidate that cannot be measured cleared the sweep**, so a
   click would have emptied the list it was a click *in*. Unreachable today --
   a capture's survey finds nothing in the window, so there is nothing to click
   -- and wrong in intent, which is enough:
   `survey_session_forget_measurement()` is the reset that keeps the sweep.

4. **Leaving the screen during a confirmation pass froze the screen.**
   `survey_session_stop()` set the state to idle and left `confirm.running`,
   and the input handler *waits* on a pass rather than fighting it for the
   receiver -- so a pass nothing ticked sat in front of a view nothing reached.
   Stop is authoritative now, and the view asks the state rather than the flag.

5. **Reset zoom restored the kept sweep and then emptied it.** The same wrong
   reset in the one place it was reachable by clicking. That is what took the
   narrowing snapshot into the session as well -- `survey_session_keep()` and
   `survey_session_restore()`, with the order of the forget and the copy
   written down and checked. `.scratch/testability/` ticket 01 had already
   named this path as one of two faults the operator reported, and it was the
   last survey decision still living in the view. Restoring it now also
   restores the **plan**, which it never did: everything downstream of a sweep
   is scaled by `plan.bin_hz` -- the carrier grouping, the history's matching
   tolerance, whether an extent is a measurement or the instrument's floor --
   so a wide sweep's peaks were being read with a narrow sweep's bin width,
   and the carriers left beside them were the narrowed sweep's.

### Measured

- `check-pipelines` unchanged, and both capture surveys are **byte-identical**
  before and after.
- The survey screen renders **byte-identical** from `gsm_arfcn_69.bin`.
- Three alternating live sweeps of 88-108 MHz at 0.15 s: 34/34/31 carriers
  before, 33/34/36 after, with `blocks 26 settling 13` on both.
- A live confirmation pass and a live two-sweep watch, from both adapters.
- `make check`: 55 suites, no failures, `check-survey-session` contributing
  158 of the checks.

## The two bullets that had already been done

The ticket listed two more things to move and both were done by earlier work,
which is worth recording rather than silently not doing:

> which row is hovered and selected, computed during draw (~`:2400-2450`) and
> reused by the click handler

Hover is computed in `handle_survey_input` (`sdrgui_survey_chart_peak_at`), in
the input phase, and the click handler reads that. The menus compute a hovered
row during their draw, but only to highlight it -- their click handlers compute
their own, so nothing is reused across phases.

> 18 inline `y +=` advances that `check-layout` cannot see

The candidate list goes through `row_list.h`. What is left is
`draw_detail()`'s flowing prose, and `detail_line()` already refuses to draw
past the panel's bottom edge -- which is the rule `panel_rows.h` exists to
enforce. Its own comment says why a row *capacity* is the wrong model there:
the panel's content is prose whose length depends on what was measured, at
three point sizes with gaps between groups, so it needs the bound at every
line rather than a count worked out once. Forcing a uniform step onto it would
make the panel worse, so it stays as it is.
