# 01 - Take raylib out of `app.h`

Status: ready-for-agent

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
