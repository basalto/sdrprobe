# 01 — The survey sweep state machine and what it accumulates

Status: resolved
Blocked by: (none)

`src/view_survey.c` decides, inside a file that links raylib: when a sweep step
is finished (`survey_measure_block`), how a dwell longer than one block is
folded (`survey_fold_block`, peak-hold), when the sweep advances or stops
(`update_survey`), what the next step's tuning is (`survey_sweep_target`), and
which measurements survive a re-sweep of a narrower range (`survey_start`'s
`narrowing` snapshot).

`survey_window.h` already covers the window arithmetic; none of the above is in
it.

Two of these have shipped wrong. The first sweep of a freshly opened survey
ignored the region just selected, because the narrowing test also required a
sweep to exist. Reset-zoom restored the fields and not the chart. Both were
reported by the operator.

What it would take: a `survey_sweep.h` holding a plain struct (step index,
range, dwell, blocks seen at this step) with `survey_sweep_begin/advance/fold`
as pure functions over it, and `view_survey.c` calling them the way it already
calls `survey_window_of()`/`survey_window_put()`. Then `tests/survey_sweep_test.c`:
a dwell of one block against several, a step count that covers the range with
no gap and no overlap, a narrowing sweep that keeps the measurements outside
its range, a sweep of a range narrower than one bin, and the first-sweep case
above as a regression.

## Comments

## Answer

Done in `src/survey_sweep.h`, checked by `tests/survey_sweep_test.c`
(`make check-survey-sweep`, 194 checks).

What moved: `survey_plan_make()` (validation, bin count, step count, the cost
estimate), `survey_plan_step_centre()`, `survey_plan_bin_at()`,
`survey_fold_keeps()`, `survey_fold_hold()`, `survey_step_phase_at()`, and
`struct survey_measurement` with observe/duty/spread. The constants that go
with them moved out of `app.h`, which now includes the header;
`struct survey_view` holds a `struct survey_plan` and a
`struct survey_measurement` instead of eight loose fields.

The check that earns its place is `test_every_frequency_is_covered`: it walks
501 frequencies across five ranges and requires each to fall inside some step's
kept middle. Rounding the step count instead of ceiling it fails it on four
ranges at once -- and that bug is invisible on screen, because the chart draws
exactly as it should with the top of the range never tuned to.

Also checked: bins never finer than the FFT that fills them, peak-hold against
the sentinel, the settle/dwell/advance/finish machine including a very late
block and a one-step sweep, and the duty rule -- "up" is half the prominence
the candidate was *first* seen with, so a weak candidate is judged against
itself rather than against a threshold set for a strong one.

Not moved: `survey_find_peaks` and `survey_select` still read `struct app`.
Peak finding is already checked in `check-sdr-dsp`; selection is tuning, not
arithmetic.
