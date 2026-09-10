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

1. Add a pure `check-signal-frame` around the current processing behavior
   before moving callers.
2. Move conversion, magnitude summary, signal statistics, DC-filter copies,
   spectrum calculation and peak hold into the module without changing their
   order.
3. Pass the requested FFT size explicitly from the frame loop. Keep
   `input_scope_owns_spectrum()` in the input/application module.
4. Replace loose `app` sample/spectrum fields with one frame container and
   migrate one consumer family at a time: Scope, technology sessions,
   calibration/scans, then survey adapters.
5. Make waterfall invalidation consume a geometry-changed result instead of
   being mutated from the processing implementation.
6. Remove `process_block()` from `view.h`; the frame loop calls the module.

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
