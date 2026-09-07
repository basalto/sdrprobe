# 04 - Pull the survey's state machine out of its view

Status: ready-for-agent
Blocked by: 01

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
