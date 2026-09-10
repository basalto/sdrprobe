# 11 - Make an acquired signal frame a deep module

Status: ready-for-agent

`process_block()` in `sdrprobe.c` turns one raw sample block into everything
the Probe and Decoder contexts consume:

- centred I/Q and magnitudes;
- minimum, maximum and mean magnitude;
- signal statistics;
- optionally DC-filtered spectrum input;
- a screen-dependent transform size;
- average spectrum, peak hold, window count and readiness;
- invalidation of peak hold and waterfall history when the size changes.

The primitives are checked in `check-sdr-dsp`, and the rule selecting whether
Scope owns the transform size is checked in `check-input`. Their composition
is not checked. It is expressed as more than twenty loose arrays, counters and
ready flags in `struct app`, read directly by views, overlays, survey adapters
and headless paths. A primitive can be correct and the assembled frame still
wrong -- for example, retaining a peak hold across a transform-size change or
filtering the raw I/Q that a Decoder session expects.

## Hypothesis

One acquired signal-frame module can own the converted block and all generic
derived measurements behind a processing interface and read-only result
access, without knowing which tab or view is active. It is false if a caller
must mutate one of its derived arrays or if choosing the transform requires
the module to know presentation state.

The cheapest disproof is to replace `input_state_now()` inside
`process_block()` with an explicit requested FFT size supplied by the frame
loop, then drive the same raw block through the new module in a check. If any
consumer cannot use the resulting frame without reaching into mutable
internals, the proposed interface is too small.

## The deepened module

Add `signal_frame.{c,h}` in the Probe context. It owns the working and result
buffers currently scattered in `struct app`, plus the `sdr_dsp` workspace.
Its caller supplies:

- raw bytes and their `device_profile`;
- whether the spectrum input removes DC;
- the requested FFT size;
- the current time needed by peak hold.

It returns whether a usable frame was produced. Consumers read the raw centred
I/Q, magnitudes, statistics and spectra from the frame. Presentation remains
responsible for choosing the requested FFT size and for GPU waterfall history;
the frame only says that its spectrum geometry changed.

This is depth rather than a struct move: conversion, filtering policy, FFT
workspace, readiness and invalidation become implementation. Deleting the
module would put those decisions and buffers back into `sdrprobe.c` and every
consumer.

## Implementation plan

### Phase 1 -- pin the assembled behavior

Create `signal_frame.{c,h}` and `check-signal-frame` around a copy of the
current `process_block()` sequence before moving any caller. The first check
must distinguish raw Decoder I/Q from DC-filtered spectrum input and must fail
if a transform-size change retains the old peak hold. Add the module and check
to all Makefile prerequisite/gate lists in the same edit.

Files: `src/signal_frame.{c,h}`, `tests/signal_frame_test.c`, `Makefile`.

Focused validation: `make check-signal-frame`.

### Phase 2 -- move processing without redesign

Move conversion, magnitude summary, signal statistics, DC-filter copies,
spectrum calculation and peak hold in their existing order. The module owns
its `sdr_dsp` workspace, working buffers, result buffers and ready state. The
caller supplies the `device_profile`, DC-filter choice, requested FFT size and
time. Do not change arithmetic or normalize samples in this phase.

Files: `src/sdrprobe.c`, `src/signal_frame.{c,h}`, `src/app.h`.

Focused validation: `make check-signal-frame`, `make check-sdr-dsp`, and
`make check-sample-format`.

### Phase 3 -- make presentation policy explicit

Compute the requested FFT size in the frame loop from
`input_scope_owns_spectrum()` and pass it into the module. Return a
geometry-changed fact when the bin count changes. `view_scope` consumes that
fact to clear GPU waterfall history; the frame module must not include raylib
or mutate presentation state.

Files: `src/sdrprobe.c`, `src/input_route.h`, `src/view_scope.c`,
`src/signal_frame.{c,h}`.

Focused validation: `make check-input`, `make check-signal-frame`, and
`make check-layout`.

### Phase 4 -- migrate consumers by family

Replace loose `app` sample/spectrum fields with one frame container. Migrate
Scope first, then technology sessions, calibration and channel scans, and
finally survey window/headless adapters. After each family, remove only the
fields with no remaining reader and run that family's focused check.

Files: `src/app.h`, `src/view_scope.c`, `src/view_*.c`,
`src/overlay_*.c`, `src/survey_report.c`, `src/view_survey.c`.

Focused validation: the affected `check-*-session` or survey suite after each
family, followed by `make check-pipelines`.

### Phase 5 -- close the old seam

Remove `process_block()` from `view.h` and `sdrprobe.c`, audit that no consumer
mutates frame outputs, update architecture documentation, and run screenshots
for Scope views because geometry checks cannot show stale or blank plots.

## Checks

`check-signal-frame` must cover:

- one U8 block and its rescaled S16 equivalent produce bit-identical centred
  I/Q and identical derived answers;
- DC removal changes spectrum input but never the raw I/Q handed to Decoder;
- changing FFT size resets peak hold and reports changed geometry;
- repeated equal-size frames retain maxima in the peak hold;
- returning to the default size rebuilds rather than reusing stale bins;
- short/malformed input produces no ready frame;
- signal statistics and spectrum readiness describe the same block.

Focused validation: `make check-signal-frame`, `make check-sdr-dsp`,
`make check-input`, and `make check-sample-format`. After consumers move, run
their technology session checks and `make check-pipelines`. Final gate:
`make check-touched` and `make check`.

## Tasks

- [ ] Add `signal_frame.{c,h}` with no GUI, driver or technology dependency.
- [ ] Add and gate `check-signal-frame` with complete Makefile prerequisites.
- [ ] Pin raw I/Q versus DC-filtered spectrum input.
- [ ] Pin U8/S16-equivalent frame results.
- [ ] Pin FFT-size geometry change and peak-hold reset.
- [ ] Pin equal-size peak-hold accumulation and malformed input refusal.
- [ ] Move the current processing sequence unchanged into the module.
- [ ] Pass FFT size explicitly from the frame loop.
- [ ] Return geometry change instead of clearing waterfall rows internally.
- [ ] Migrate Scope consumers and inspect Scope screenshots.
- [ ] Migrate GSM, LTE, ADS-B, TETRA and FM sessions.
- [ ] Migrate calibration and both channel scans.
- [ ] Migrate Survey window and headless adapters.
- [ ] Remove obsolete sample/spectrum fields from `struct app`.
- [ ] Remove `process_block()` from `view.h` and `sdrprobe.c`.
- [ ] Update `AGENTS.md`, `CLAUDE.md` and `docs/ARCHITECTURE.md`.
- [ ] Run `make check-touched`, `make screens NAMES="magnitude spectrum scatter waterfall"`, and `make check`.

## Acceptance criteria

- Generic block-derived data has one owner outside `struct app`.
- No consumer mutates the frame's derived arrays or readiness state.
- The module includes no raylib, driver or technology DSP header.
- Presentation state is reduced to an explicit FFT request, not read inside
  the module.
- Every decision currently made by `process_block()` is directly checked.
- Existing capture decode and survey output remain byte-identical where they
  are deterministic.

## Not in scope

- Changing FFT, DC-filter or peak-hold algorithms.
- Moving technology-specific decoding into the frame.
- Moving GPU textures or waterfall row history out of `view_scope`.
- Changing the latest-block acquisition seam or its lossless mode.
- Normalizing samples instead of retaining device counts.

## Comments

**Reviewed 2026-09-10, before starting ticket 12.** The plan is startable as
written -- phase 1 is pure addition, and building the check around a copy of
the sequence before moving any caller is the safe foothold ticket 10 does not
have. Three notes.

**Phase 1's first assertion needs `build/testfiles16/`.** "One U8 block and
its rescaled S16 equivalent produce bit-identical centred I/Q" is the
`$(FORMAT16)` prerequisite that `check-sample-format` and
`check-device-backend` both carry. The Makefile task says "register complete
prerequisites" without naming it, and on a clean tree the check fails for a
missing corpus rather than for anything it measures.

**A copy is a second implementation until phase 2 deletes the original.**
Phase 1's check pins the copy; `process_block()` in `sdrprobe.c` is still the
one that runs. That is the right order, but it means phase 1 alone is not a
shippable stopping point -- a green `check-signal-frame` beside an unchanged
`sdrprobe.c` proves nothing about the program. `probe-two-cell` is the
standing lesson: a harness that copies a fixture is running a second fixture.

**Ticket 10 is waiting on this ticket for one decision.** Its task list says
"Decide `remove_dc` ownership with ticket 11 rather than moving it
automatically" -- receiver state or signal-frame policy. Phase 2 takes the
DC-filter choice as a caller-supplied argument, which is the right shape for
either answer, but the answer itself should be written down here when phase 3
settles what presentation supplies.

