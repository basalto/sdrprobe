# 03 - The Scope view model, and its first reader

Status: resolved, 2026-09-16

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

## Comments

### 2026-09-16 -- done

`src/scope_view_model.h`/`.c` (new): `scope_view_model_build(const struct app
*app, struct scope_view_model *out)` fills a plain struct from the frame,
the applied tuning, the device profile and the Scope's own scatter/waterfall
state. It aliases rather than copies -- the pointers it hands out point into
`app`'s own storage, valid for the one frame it is built for -- since a
same-process, same-frame consumer gains nothing from copying a 512 KB
magnitude array or a 128 KB spectrum pair, and ticket 05's serializer reads
out of the same pointers once, later.

`draw_magnitude()`, `draw_spectrum()`, `draw_scatter()` and `draw_waterfall()`
in `view_scope.c` now take `(const struct app *app, const struct
scope_view_model *svm)`: the measurements come from `svm`, and `app` supplies
only what stays view-owned -- the plot rectangle, the zoom/pan/drag window,
and the scatter/waterfall GPU textures, which cannot be plain data by
definition. One thing this surfaced and this ticket deliberately does not
fix: `app->sv.magnitude_peaks`/`magnitude_bin_count`/`magnitude_lower`/
`magnitude_upper` are a reduction to `app->plot.width` (raylib's pixel
geometry), computed by `recompute_magnitude_bins()` at advance time. A
browser Viewer would bin to its own width and discard this regardless, so it
stays view-owned rather than moving into the view model -- named in the
header comment rather than silently carried over.

Two fields the raylib Scope does not read yet -- `waterfall_row` and the
`scatter_i`/`scatter_q`/`scatter_count` triple -- are populated anyway,
because ticket 05's Viewer needs exactly this subset and building it now
means ticket 05 does not re-derive the same description of this screen.

**ADR-0027's tuning generation is real, not a placeholder field.**
`struct receiver_applied` (`receiver_runtime.h`) gained a `generation`
counter, bumped by `retune_receiver()` and `retune_receiver_at_rate()`
(`sdrprobe.c`) on a successful retune and nowhere else. Named limitation:
the Settings panel's own apply path (`overlay_settings.c`) is a separate,
older transaction that does not yet run through either function, so a
retune from Settings does not bump it -- a known gap, not a silent one.

Acceptance criteria against the ticket:

- `check-scope-view-model`: 39 checks against known inputs (measurement
  pass-through, the zero-samples case, the waterfall ring's front slot, the
  empty and both wrap/no-wrap scatter-history cases), `-lm` alone --
  `pkg-config --cflags raylib` for `struct app`'s types, never `--libs`.
  `ldd` on the test binary confirms no `libraylib`. In `CHECK_UNITS`, headers
  in the Makefile's header list.
- `view_scope.c`'s four Scope views read the view model for their
  measurements; `app` supplies only the view-owned state described above.
- `make check`: 21252 checks, 71 suites, no failures.
- `tests/pipelines.sh`: byte-identical apart from the ADS-B recording's
  wall-clock filename.
- Screens: cropping the volatile HUD strip (which reports the block counters
  a paced run varies by, per the finding in ticket 02) out of `magnitude`,
  `spectrum` and `scatter` shows **0** pixel difference for `magnitude` and
  the same pre-existing jitter magnitude for `spectrum`/`scatter` as two runs
  of an *unchanged* binary already show (see
  `spectrum-scatter-screenshots-are-not-byte-reproducible` in memory) --
  confirming no regression rather than asserting a byte-diff that this pair
  of screens cannot pass even doing nothing.
- No raylib type in `scope_view_model.h`, confirmed by grep; no raylib call
  in `scope_view_model.c`, confirmed by `nm -u` showing only `memset`.
