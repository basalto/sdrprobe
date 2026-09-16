# 02 - One advance step the window and headless both drive

Status: resolved, 2026-09-16

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

## Comments

### 2026-09-16 -- done

Split `advance_waterfall_row()`/`render_waterfall()` and
`advance_scatter_history()`/`render_scatter()` in `src/view_scope.c`. Fixed
three `GetTime()` calls in `src/overlay_scan.c` (`update_scan()` and
`start_scan()`) to `monotonic_seconds()` -- undiscovered scope until this
ticket: `update_scan` was the one function among everything the sequence
touches that read the window's clock directly rather than taking `now` or
using the POSIX clock every other headless-reachable function already uses.

The sequence between `consume_latest()` and `BeginDrawing()` is now
`frame_advance()` in a new `src/frame_advance.c`/`.h`, called once from
`run_gui()`; the draw phase makes the two GPU calls (`render_waterfall()`
gated on `spectrum_updated`, `render_scatter()` unconditionally, matching
what the inline code did).

**"No raylib" is proven by construction, not by inspection.**
`check-frame-advance` links `frame_advance.c` against a fake for each of its
~19 callees and `-lm` alone -- `pkg-config --cflags raylib` (for `struct
app`'s types) but never `--libs raylib`. `ldd` on the resulting binary confirms
no `libraylib` dependency. The fakes also pin the dispatch itself: which
callee runs under which tab/decode/cal.open/have_new combination, including
one asymmetry preserved exactly rather than corrected -- FM keeps running
while calibration is open, unlike every other decode technology, because
that is what the code being extracted already did.

Acceptance criteria against the ticket:

- `make check`: 21211 checks, 70 suites (was 21168/69), no failures.
- `tests/pipelines.sh`: byte-identical apart from the ADS-B recording's
  wall-clock filename, the named exception.
- Screens: `waterfall` and `magnitude` byte-identical. `spectrum` and
  `scatter` are not, but **this is pre-existing and not a regression**: two
  runs of the identical post-change binary already disagree with each other
  by the same margin (`compare -metric AE`: ~2150 px before-vs-after, ~2153
  px same-binary-vs-itself, both about 0.001% of the image, confined to the
  peak-hold trace's anti-aliased pixels). Paced file playback races real
  wall-clock `--duration` against block arrival, so which block is on screen
  when the shutter falls is not reproducible bit-for-bit -- this was already
  true before this ticket and is worth knowing before trusting a byte-diff
  on these two screens again.
- Live run: alternated the built binary against one compiled from `fc777dd`
  (the commit before this ticket) in a worktree, both surveying
  935-945 MHz at a 0.2 s dwell. `settling 7` on both -- the exact property
  this criterion exists to catch, since the fault it is named for showed up
  as a changed settle count, not a changed candidate list. `blocks` (23 vs
  24) and the candidates found (19 vs 14 carriers) differ because it is real
  air five minutes apart, not a capture; a live sweep repeating its findings
  was never the claim.
- No GL or raylib call in `frame_advance.c`, confirmed above.
