# Every decision verifiable without hands, eyes, or a receiver

## Status

accepted

## Context

This program is developed largely by agents, and agents cannot see or click.
That is not a passing inconvenience: on the desktop this is written on,
synthetic key and pointer events do not reach the raylib window at all, so
`wtype q` will not even quit the program. Anything that can only be checked by
pressing a key is, in practice, checked by the operator after it ships.

The record is unambiguous about what that costs. Every one of these reached a
person before it reached a check:

- `+` and `-` did nothing, because raylib names keys after physical positions
  on a US keyboard and the layout in use is Portuguese;
- the first sweep of a freshly opened survey ignored the region just selected,
  because the test for "is this narrower" also required a sweep to exist;
- zoom, pan and drag did nothing before the first sweep, because the window had
  no extent until one gave it one;
- the message log drew its hex column past its own panel and onto the chart
  beside it;
- the scan chart selected a bar one or two to the right of the pointer;
- `--view` started a second acquisition worker and deadlocked the shutdown.

Not one of those was a hard problem. Every one was arithmetic or ordering, and
every one would have been caught in a millisecond by a check that did not need
a window. What they had in common was living inside a function that also draws
or reads input, where nothing could reach them.

Meanwhile the parts of this codebase that *are* checked — the DSP, the band
plan, the layouts, the survey window — have produced almost no operator-visible
bugs, and the two they did produce (the peak-finder reporting shoulders, the
occupied bandwidth measuring the noise floor) were caught by their own checks
before anyone ran the program.

## Decision

**Every decision the program makes must be reachable by a check that needs no
window, no receiver, and no person.** Drawing is exempt; deciding is not.

Concretely, three layers, in the order they should be reached for:

1. **Pure units.** Anything that decides — arithmetic, state transitions,
   parsing, selection, validation — lives in a module or header that depends on
   neither raylib nor librtlsdr, and is checked directly. `sdr_dsp.c`,
   `band_plan.c`, `*_layout.h` and `survey_window.h` are the existing shape:
   plain data in, plain data out, `make check-*` links `-lm` and runs in
   milliseconds. This is where the great majority of logic belongs, and where
   new logic goes by default.

2. **Headless pipelines.** Whole paths that genuinely need acquisition,
   decoding or the receiver are driven through the command line and assert on
   machine-readable output: `--headless --decode --once` over a capture,
   `--record-seconds` with its sidecar, `--survey-range`. These prove that the
   units are wired together correctly, which unit checks by construction
   cannot. They are slower and fewer.

3. **Eyes.** Whether a label is legible, a shaded band reads as a band, or a
   chart is beautiful. Screenshots and a person. This layer is deliberately
   *small*, and shrinks as decisions move down to layer 1.

The rule that keeps the layers honest: **a function that draws or reads input
may not also decide.** If it computes a threshold, chooses a range, maps a
pointer to an index, or advances a state machine, that part comes out into a
unit with a name. `survey_window.h` was extracted under this rule after two of
its decisions shipped wrong; `sdrgui_scan_chart_channel_at()` exists because a
hit test inside a draw function was off by two bars.

## What this decision is not

It is **not a goal of 100% test coverage**, and it is not "expose every
function through the CLI". Both were considered and rejected, for reasons worth
recording because they will be proposed again:

- **100% is the wrong target.** Drawing cannot be asserted, only looked at. A
  check that a chart called `DrawLine` 148 times tests the test, not the chart.
  Pursuing the last few per cent produces exactly that kind of check, which
  then has to be maintained while catching nothing. The target is *every
  decision*, and the honest measure is whether a bug could have been caught by
  a check, not what fraction of lines were executed.

- **The CLI is a user interface, not a test harness.** Exposing internals
  through it to make them testable would create a second API to keep in sync
  with the first, make every check pay for a process launch and a text format,
  and test the wrapper rather than the function. Direct unit checks are faster,
  sharper, and closer to the code. The CLI earns its place at layer 2, where
  the thing being tested genuinely *is* the assembled program — and there it
  should keep growing, because an agent driving `--headless --decode --once`
  and reading stdout is the cheapest end-to-end verification available.

## Consequences

- New logic starts in a unit with a check. Adding a decision to a draw or input
  handler is a review finding, not a style preference.
- `make check` runs every check; agents and humans have one command that says
  whether the tree is sound. Anything that cannot be reached by it should be
  named in an audit, not left implicit.
- Existing untestable logic is debt with a register: `.scratch/testability/`
  lists what is not yet reachable and what it would take, so the gap is
  measurable rather than felt.
- Some verification stays manual, and the program should be honest about which:
  appearance, and the feel of an interaction. Reports about those say they were
  looked at, not that they were tested.
- The layers have a cost order — a unit check is milliseconds, a headless run
  is seconds, a screenshot is a minute and a person. Reaching for a lower layer
  when a higher one would do is the main way this decision gets eroded.
