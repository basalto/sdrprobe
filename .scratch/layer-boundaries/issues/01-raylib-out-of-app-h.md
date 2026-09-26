# 01 - Take raylib out of `app.h`

Status: resolved 2026-09-26 -- option (A), the opaque `struct gui_state`. The
NULL found two real couplings on its first run, and only two of the four
checks came out raylib-free: the other two reach it through `view.h`, which
is ticket 02. See "Done" at the end.

## Why this first

It is the smallest change in this spec and the one everything else stands on.
`app.h` includes `<raylib.h>` for six fields, and `chart_window.h` -- the only
other raylib path into `app.h`, checked transitively -- includes it for two
function prototypes. Every file that touches `struct app` therefore compiles
against raylib, including four checks that are not GUI code at all.

```
app.h:253   fm_view                   AudioStream audio_stream;
app.h:836   scope_view                RenderTexture2D scatter;
app.h:837   scope_view                Texture2D waterfall;
app.h:838   scope_view                Color *waterfall_pixels;
app.h:1029  waterfall_signal_context  Vector2 mouse_pos;
app.h:1140  app                       Rectangle plot;
```

**Every reader of those six fields is a GUI file** -- `view_fm.c`,
`view_scope.c`, `overlay_scan.c`, `overlay_calibration.c`,
`overlay_settings.c`, `overlay_signal_report.c`, `view_survey.c`,
`sdrgui_scope.c`, `sdrprobe.c`. Nothing on the server side reads any of them,
so this ticket should leave `viewer_*.c`, `frame_advance.c` and every
`*_view_model.c` untouched except for what they include.

## What to do

1. **Move the five window resources out of `struct app`** into a
   GUI-owned struct in a header only GUI files include.
2. **`Rectangle plot`** goes the same way: it is the window's plot area, and
   `headless`/`server` have no plot.
3. **`chart_window.h`'s two `Rectangle` prototypes**
   (`chart_window_input`, `chart_window_drag_of`) move to a GUI header.
   `struct chart_window` itself is pure and stays where it is.
4. `app.h` and `chart_window.h` stop including `<raylib.h>`.
5. Drop `$(shell pkg-config --cflags raylib)` from the rules that no longer
   need it -- expected: `check-fm-view-model`, `check-scope-view-model`,
   `check-survey-view-model`, `check-frame-advance`. **Not**
   `check-viewer-link`, which also includes `sdrgui.h` for a decision (spec
   finding 6); that is ticket 03's. **Not** `check-layout`, `check-geometry`
   or `check-row-list`, which test GUI geometry and use raylib's `Rectangle`
   on purpose.

## The decision to make first -- ask, do not pick

Where the GUI resources live. Two defensible answers:

- **(A) An opaque pointer.** `struct app` carries `struct gui_state *gui;`,
  forward-declared; `gui_state.h` defines it and includes raylib; `run_gui()`
  allocates it and `headless`/`server` leave it NULL. One owner, and "the
  server touched a texture" becomes an immediate crash rather than a
  silently unused field. *Recommended*: NULL is the truth about a session
  with no window.
- **(B) Split each struct.** `struct scope_view` keeps its float data in
  `app.h` and its textures move to a sibling `struct scope_textures` in a GUI
  header, embedded by value in a GUI-side struct; likewise `fm_view`'s audio
  stream. More local, more structs, and each view grows a second home.

Also decide `mouse_pos`: it belongs to `struct waterfall_signal_context`, the
right-click menu's context, which is GUI state as a whole -- likely the whole
struct moves, not the field.

## Acceptance criteria

- [ ] `grep -n '<raylib.h>' src/app.h src/chart_window.h` returns nothing,
      and the transitive walk in the spec finds no raylib path into `app.h`.
- [ ] The four rules above build without raylib's cflags; a
      `raylib.h`-shaped stub on the include path must not be needed.
- [ ] No file outside the GUI list above changes except for `#include`
      lines.
- [ ] `make check` and `make check-pipelines` pass.
- [ ] `make screens` (all twelve -- the scope, FM, survey and every overlay
      are touched) is compared **warm on both sides**, with a same-binary
      noise floor measured first; the Scope's peak-hold trace is known not to
      be byte-reproducible run to run.
- [ ] `make check-web-layout` passes and a live `server` run on a capture
      still delivers `spectrum`, `waterfall_row` and `fm_state`.

## Take into account

- **`allocate_waterfall_history()`** is how `server` gets a waterfall
  without a texture (`viewer_session.c`). Confirm it touches none of the
  moved fields before trusting "nothing on the server side reads them".
- **Headless must not dereference the GUI struct.** Under (A) that is a
  NULL; grep every moved field's reader for a path reachable from
  `frame_advance()`. `advance_waterfall_row()` writes the float history, not
  the texture -- confirm, do not assume.
- **`APP_HDR`** gets any new header, and both audits in `CLAUDE.md` run
  after.

## Done, 2026-09-26

**Option (A), the opaque struct.** `src/gui_state.h` holds `struct gui_state`
-- the FM audio stream, the Scope's two textures and its pixel buffer, the
plot rectangle, and `struct waterfall_signal_context` moved whole. `app.h`
carries `struct gui_state *gui`, forward-declared, and includes no raylib;
`chart_window.h`'s two `Rectangle` prototypes moved to `view.h`, which
already includes raylib for its own 25 uses. `run_gui()` is the one
allocator. Every reader was a GUI file, as measured, so nothing on the
server side changed except includes.

**The transitive walk is clean**: nothing reaches raylib from `app.h`.

### The NULL earned itself on the first run

The ticket argued for a pointer because "a runtime path that reaches for a
texture crashes at once instead of reading a zeroed handle". It found two
couplings immediately, both segfaults, neither of which any check had been
able to see:

- **`recompute_magnitude_bins()` reads the plot width, and `process_block()`
  calls it** -- so the shared per-block step asks the window how wide it is,
  on every headless and server run. Behaviour is unchanged by the guard:
  `app->plot` was a zeroed `Rectangle` on those paths, so the capacity was
  *already* 1 and a windowless run has always reduced every block to a single
  bin nothing reads. The guard is not the fix; ticket 02 item 2 is this exact
  case.
- **`view_scope_release()` mixes owners.** `main()` calls it for every mode,
  and it freed `app->gui->waterfall_pixels` alongside `app->sv.waterfall_dbfs`
  -- the first the window's, the second allocated by `server` itself. Split by
  owner now. Worth noting: `app->sv.waterfall_ready` is **not** a test for a
  texture existing, because `viewer_session.c` sets that flag with no texture
  behind it.

### Two of the four checks, not four

`check-fm-view-model` and `check-scope-view-model` are raylib-free, **proven
with a `#error` raylib.h ahead of the real one on the include path** -- not by
the build succeeding, since the system header is in `/usr/include` and
dropping the flag alone proves nothing.

`check-survey-view-model` and `check-frame-advance` are **not**: both compile a
`.c` that includes `view.h`, which is a GUI header. Their cflags are restored
with a comment saying so, rather than left dropped and building by accident.
That is ticket 02's `view.h` split, which now has a measurement behind it.

### `make screens` is unchanged, and proving it needed a second attempt

Nine of twenty-one screens are byte-identical across the change. The rest
carry real run-to-run noise, and `srd-charts` read 74907 differing pixels
against an apparent noise floor of 8316 -- a regression by that arithmetic.

It is not one. **The old binary compared against itself reads 73817-80629**
once the renders come from different batches. The screen's noise is bimodal:
about 8000 within one `make screens` run, about 78000 between a full run and
a single-screen one, because the SRD frame list is cumulative over playback
and the two land at different points in the capture. The first estimate was
too optimistic because both renders were in the same batch -- the same
"comparison at two machine loads" trap this spec warns about, one level in.
The decode itself is pinned by `check-pipelines`, which passes unchanged.

### And a bug in the checking tool

`check-web-layout` failed, and it was `scripts/web_layout.mjs`'s own fault,
twice over: it killed chromium at the *bottom* of `run()`, which a throw
skips, so a failing run leaked its browser -- and the leaked browser then held
the debug port, so the *next* run attached to a stale page and failed for an
entirely different reason. Self-perpetuating, and it could as easily have
reported a false pass against an old page.

Both halves fixed: the children are killed in a `finally` and on signals, and
the run refuses a debug port that already answers rather than attaching to
whatever is on it -- plus the page target must be at *this* run's server URL.
All three behaviours verified, including that an interrupted run leaks
nothing.

`make check`: 21988 checks in 79 suites. Headless decodes and a live `server`
run verified by hand.
