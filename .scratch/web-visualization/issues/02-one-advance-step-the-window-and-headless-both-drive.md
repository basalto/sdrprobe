# 02 - One advance step the window and headless both drive

Status: ready-for-agent

## Goal

Make the per-block work reachable without a window, so a later `--headless
--serve` and the window run the same code rather than two copies of it.

**This ticket ships no feature.** Its output is a refactor that changes
nothing observable, and its value does not depend on the Viewer link: ADR-0012
already requires that a function which draws may not also decide, and two of
these functions do both.

## What moves

`update_waterfall()` (`src/view_scope.c:186-205`) is already two things. Lines
187-204 maintain `app->sv.waterfall_dbfs`, a plain float ring of rows, with
`memmove` and `memcpy` and no GL whatever. The last line calls
`render_waterfall()`, which uploads. Split them: the row maintenance stays in
the advance, the upload moves to the draw phase.

`update_scatter()` (`:320`) is the same shape. It fills
`app->sv.scatter_history` with decimated I/Q normalized by
`app->device.full_scale` -- plain data, and already in the units a State
update wants -- and only then reaches `BeginTextureMode`. Same split.

Then extract the sequence between `consume_latest()` and `BeginDrawing()` in
`run_gui()` (`src/sdrprobe.c:1885-1952`) into one function: block consumption,
`process_block()`, the waterfall row, the scan and calibration updates, and
the per-technology `update_*` calls. `run_gui()` calls it and then draws.

## Why this is the dangerous kind of change

A refactor that is supposed to change nothing is the hardest kind to verify,
and this repository has already shipped one: the survey's machine came out of
its view with 55 suites green, both capture surveys byte-identical and the
screen byte-identical, **while the settle that discards stale blocks was
disabled**. No capture could have caught it because a capture never retunes.

So the acceptance criteria below are deliberately not just `make check`.

## Acceptance criteria

- [ ] `make check` passes, all suites.
- [ ] `tests/pipelines.sh` output is **byte-identical** before and after,
      across all captures, apart from the wall clock in ADS-B timestamps.
      Capture both runs to files and `diff` them; do not eyeball.
- [ ] `make screens` renders all twelve screens and each is compared against
      the same screen built from `master`. The waterfall and scatter screens
      are the two that matter, since their upload moved.
- [ ] A **live** run on the receiver, alternated against a binary built from
      `master`, reporting `survey blocks N settling M` over the same sweep.
      This is the check no capture can make, and it is the one that would have
      caught the survey-session fault.
- [ ] No GL or raylib call remains in the extracted advance step. A
      translation unit that calls it must not need `pkg-config --libs raylib`.

## Not in scope

- Any socket, any serialization, any browser.
- A headless loop that calls the new step. That is ticket 05; this ticket only
  makes it possible.
- Changing what any `update_*` computes, or when.
