# Layer boundaries: a server that does not need the window

Status: needs-triage -- reviewed and measured 2026-09-26; five tickets, the
first ready. Decide ticket 05 (folders) only after 01-04 have made the
boundaries true.

## The question this answers

Can the code be separated into folders and data contracts so that the code a
check has to compile -- and the code `server` has to link -- is only what it
needs?

**Two answers, and they point different ways.**

- **For the unit checks: there is little to gain.** 75 C check rules compile
  116 `src/*.c` files between them -- 1.5 each on average, 6 at most -- and
  all but 8 link `-lm` alone. `CLAUDE.md` has already measured where the gate
  goes: `check-signal-probe`'s *runtime* is 45 of 57 seconds, and
  `.scratch/gate-time/issues/02-*` declined caching compilation as four
  seconds bought for a Makefile rewrite. Folders change what a reader has to
  scroll past, not what a check compiles. **Do not start this expecting a
  faster gate.**
- **For the server and headless paths: a great deal.** `sdrprobe server`
  cannot be built without every view, every overlay, raygui and raylib, and
  the one per-block step all three frontends share can only be checked with
  its callees stubbed out. That is the problem worth fixing, and folders are
  its last step rather than its first.

## What is already separate, and must stay so

| Layer | Lines | What it needs |
|---|---|---|
| DSP and decode chains (`*_dsp`, `rds`, `*_sync`, `lte_*`, `signal_probe`) | 10.3k | `-lm` (ADR-0003) |
| Sessions, machines, records (`*_session`, `survey_*`, `site_history`, `installation`) | 4.7k | plain structs |
| Viewer server (`websocket`, `viewer_link`, `viewer_command`, `viewer_session`) | 2.4k | raylib-free *in its own code*; the command handler is a function pointer |
| `web/` | -- | the wire format and nothing else |
| View models (`*_view_model.c`) | 290 | the data contracts -- but see (4) below |

## Where the coupling actually is

Measured, with the command that measured it, so each can be re-run after the
ticket that should move it.

**(1) `app.h` includes `<raylib.h>` for six fields.** Everything that touches
`struct app` therefore compiles against raylib. Eight checks take
`pkg-config --cflags raylib` for types alone, and they are two different
cases. Three are GUI geometry checks -- `check-layout`, `check-geometry`,
`check-row-list` -- that use raylib's `Rectangle` on purpose, because the
`*_layout.h` headers they test are GUI code; they stay as they are. **Five
are not GUI code and should not need raylib at all**: `check-fm-view-model`,
`check-scope-view-model`, `check-survey-view-model`, `check-frame-advance`
and `check-viewer-link`. The first four get it only through `app.h`; the
last through (6) as well.

```
app.h:253   fm_view                   AudioStream audio_stream;
app.h:836   scope_view                RenderTexture2D scatter;
app.h:837   scope_view                Texture2D waterfall;
app.h:838   scope_view                Color *waterfall_pixels;
app.h:1029  waterfall_signal_context  Vector2 mouse_pos;
app.h:1140  app                       Rectangle plot;
```

Plus `chart_window.h`, the one header `app.h` includes that includes raylib
itself -- and `struct chart_window` is pure; only two *prototypes* take a
`Rectangle`. **Every reader of those six fields is a GUI file** (`view_fm.c`,
`view_scope.c`, `overlay_*.c`, `sdrgui_scope.c`, `sdrprobe.c`), so moving them
touches nothing on the server side.

```sh
grep -nE '^\s+(Rectangle|Texture2D|RenderTexture2D|AudioStream|Color|Vector2)\b' src/app.h
```

**(2) Runtime logic lives in drawing files.** `frame_advance()` is the one
per-block step the window, `headless` and `server` all drive. It calls 18
functions, and **16 are defined in files that draw**: `view_*.c`,
`overlay_*.c` or `sdrprobe.c`. By a regex count, 32 runtime functions --
`update_*`, `enter_*`, `leave_*`, the FM scan and tune -- sit beside `draw_*`
and `handle_*` code, 11 of them in `view_fm.c` alone. `viewer_session.c`
additionally needs `set_tab`, `set_decode`, `retune_receiver` and
`stop_requested`, all in `sdrprobe.c`.

**(3) `view.h` holds three concerns.** 146 declarations -- 23 `draw_*`, 15
`update_*`, 12 `handle_*` input, and the rest -- in one header included by 20
files. A file that wants to advance a decode gets the declarations for
drawing every screen.

**(4) The data contracts take the god-struct.** `fm_view_model_build(const
struct app *, ...)` and its siblings read `struct app` rather than the state
they actually use, so the contract layer inherits (1).

**(5) The seam is already wanted.** `check-frame-advance` defines stubs for
all of `frame_advance()`'s callees. The boundary exists in the test and not
in the code.

**(6) The server calls a GUI header for a decision.** `viewer_link.c`
includes `sdrgui.h` -- and so raylib -- to call
`sdrgui_survey_peak_mark(flags)`, the precedence that turns a candidate's
flag word into one of four marks. That is a *decision*, shared by the
window's chart and the wire, living in the component layer ADR-0007 says
takes plain data and decides nothing. It belongs in the survey view model as
a value the candidate carries, the way `fm_view_model` carries
`reading_tone` rather than leaving each reader to derive an emphasis.

```sh
grep -n '#include "sdrgui.h"' src/viewer_*.c src/*_view_model.c
```

**(7) Enums cross the wire as ordinals, and one is already wrong.** The mark
travels as `enum sdrgui_peak_mark`'s integer, and `web/views/survey.js`
re-declares that enum in a different order -- so **the browser draws
receiver-like and empty candidates swapped**, telling a reader to keep
looking at empty frequencies and to ignore spurs. Green since ticket 07:
the wire carries a bare number and nothing checks how the browser reads it.
`reading_tone`, `seen`, `tab` and `decode` travel the same way; none is
known wrong, all are the same shape. `web-visualization/15` fixes the mark
now; ticket 03 makes "enums by name" a contract rule.

## The target

Folders as **link boundaries**, each allowed to include only what is beneath
it -- which a one-line audit can enforce, the way `MISSING:` and
`NOT GATED` are enforced today:

```
src/core/      sdr_dsp, signal_*, device_*, capture_*              -lm
src/tech/      gsm_*, lte_*, adsb_*, tetra_*, fm_*, rds, srd_*      -lm, core
src/runtime/   acquisition, receiver_*, frame_advance, sessions,
               options, config, <tech>_runtime.c                    -pthread; no raylib
src/model/     *_view_model, survey_record: plain state in,
               plain struct out, never struct app                   -lm
src/server/    websocket, viewer_*                                  no raylib
src/gui/       sdrgui*, view_* (draw and input only), overlay_*,
               *_layout.h, raygui_impl                              raylib
src/app/       sdrprobe.c: main, chrome, the frame loop
web/           unchanged
```

**The order is the design.** Moving files first relocates the entanglement
into subdirectories and stales every path this repository's prose cites,
while fixing nothing. Tickets 01-04 make the boundaries true without moving a
single file; 05 then draws them.

## Tickets

0. **`web-visualization/15`**, outside this spec and ahead of it: the
   swapped survey marks. A reader acts on it wrongly today, and its fix
   needs none of the refactor. **ready-for-agent.**
1. `01-raylib-out-of-app-h` -- the six fields and chart_window's two
   prototypes. Smallest step, most leverage. **ready-for-agent.**
2. `02-runtime-out-of-drawing-files` -- one technology per commit, FM first.
   Four of its moves are design changes, not moves: the shared step pumps
   the sound card, asks the presentation for a transform size, resizes GUI
   textures on a retune, and closes the Settings overlay.
3. `03-view-models-take-plain-state` -- the contracts stop taking
   `struct app`, the peak-mark decision leaves `sdrgui.h`, and enums cross
   the wire by name.
4. `04-a-raylib-free-server` -- a `sdrprobe-server` target, and
   `check-frame-advance` running real callees instead of stubs.
5. `05-folders-as-link-boundaries` -- `git mv`, the include audit, and an ADR.
   Optional: 01-04 deliver all of the decoupling without it.

## What every ticket here must measure

Each is a refactor that is supposed to change nothing, which is the shape
`CLAUDE.md` records going wrong: the survey machine came out of its view with
55 suites green, both capture surveys byte-identical and the screen
byte-identical, while the settle that throws away stale blocks was disabled.
So each ticket, before it closes:

- `make check` and `make check-pipelines`;
- `make screens NAMES="<touched views>"` compared **warm on both sides** --
  a render straight after a full compile processes fewer blocks and reads as
  a regression (ticket 14's Phase 4 comment has the numbers);
- `make check-web-layout`, since the server's page must still draw;
- one live `server` run on a capture, read with `scripts/viewer_client.py`,
  because nothing in the gate drives a sweep or a retune.

## Relationship to other work

- **ADR-0007** keeps `sdrgui` components free of `struct app`; this extends
  the same rule outward to runtime, models and server. **ADR-0012** gains:
  the real per-block step becomes reachable by a check. Neither is
  contradicted; ticket 05 records the extension as its own ADR.
- **`deepening/10`** (the receiver runtime) changes *who owns* applied state
  in its phases 4-6. These tickets change *which file* code lives in, and
  must not do its ownership work under cover of a move. Ticket 02 moving
  `retune_receiver()` out of `sdrprobe.c` makes its phase 6 easier and
  nothing harder.
- **`deepening/05`** (FM receiver interface, wontfix) is about `fm_pilot`'s
  exposed fields, which nothing here touches. Not reopened.
- **`web-visualization/07` and `/14`** add a view model per remaining decode
  view. After ticket 03 those should be written in the plain-state shape from
  the start, not converted afterwards.
