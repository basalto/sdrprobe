# 01 — The survey sweep state machine and what it accumulates

Status: ready-for-agent
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
