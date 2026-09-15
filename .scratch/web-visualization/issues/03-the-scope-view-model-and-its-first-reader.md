# 03 - The Scope view model, and its first reader

Status: ready-for-agent

## Goal

Prove the seam on one screen: a **view model** -- plain data saying what the
Scope shows -- produced by the advance step from ticket 02, with the existing
raylib Scope view reading it instead of `struct app`.

One view, so the pattern is demonstrated and costed before it is repeated. The
Scope is the right first one because the Viewer needs exactly this subset
anyway, so ticket 05 gets its payload for free rather than inventing a second
description of the same screen.

## Why a view model and not a display list

The obvious abstraction -- views emit rectangles, text and polylines that
either backend rasterizes -- is wrong here, and the measurement says so.
`MeasureText` appears at **57 sites across 13 files, including
`scope_layout.h` and `sdrgui_geometry.h`**: geometry in this program is
computed from raylib's font metrics. A browser's metrics differ, so a
positioned display list is either wrong in the browser or forces it to a
canvas with the identical font -- a remote framebuffer, which discards
responsive layout, real text input, selectable text and accessibility, all of
which are the reasons to have a web frontend at all.

So the view model says **what to show**, not where. Each frontend lays it out.
That is the same object ADR-0027 calls a State update; this ticket is where it
is born, and the Viewer link later serializes it rather than deriving a second
one.

## What it contains

For the Scope: the spectrum average and peak with their bin count and span,
the newest waterfall row, the decimated normalized scatter points, the
magnitude summary, the signal statistics, the receiver's applied tuning, rate
and ppm, the device profile facts the charts need (full scale), and the
tuning generation. Plain C, no `Rectangle`, no `Color`, no `Texture2D`, no
raylib type anywhere.

## The two sides of the seam, and which is which

`sdrgui.h`'s components are **already** on the right side of it -- ADR-0007
has them taking plain data and geometry and never seeing `struct app`. The
views are not: they bypass those components **174 times** with direct raylib
calls, 104 of them bare `DrawText`. This ticket does not fix all of that. It
moves the Scope's *data* behind the view model; the drawing stays raylib and
stays where it is.

## Acceptance criteria

- [ ] A check links `-lm` alone and asserts the view model against known
      inputs -- no raylib, no librtlsdr, no window. In `CHECK_UNITS`, and its
      headers in the Makefile's header list.
- [ ] `view_scope.c` reads the view model for the four Scope views and no
      longer reaches into `app->frame` or `app->sv` for the data it draws.
- [ ] `make screens NAMES="scope"` and the waterfall and scatter screens are
      compared against the same screens built from the previous commit and are
      unchanged.
- [ ] `make check` passes and `tests/pipelines.sh` output is unchanged.
- [ ] The view model contains no raylib type. A translation unit including its
      header compiles and links without `pkg-config --libs raylib`.

## Not in scope

- Any other view. Those are ticket 07 and are taken one at a time.
- Moving the 174 direct raylib draw calls into components.
- Input. The input half of the seam is a separate and harder problem -- 161
  raylib input call sites, with hit-testing done inline against rectangles
  that exist only during drawing -- and it is not attempted here.
- Serialization. The Viewer link is ticket 05.
