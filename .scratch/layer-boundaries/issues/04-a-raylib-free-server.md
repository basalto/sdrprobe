# 04 - A server built without the window, and a real per-block check

Status: ready-for-agent -- **(B) chosen, 2026-09-27**, and the ticket's scope
is now the shipped binary rather than the link check alone. The decision, and
the measurement it was taken on, are at the bottom.
Blocked by: 02, 03 (03 done)

## Why

Tickets 01-03 make the boundary *true*. This one makes it *held*: a build
that fails the moment a server-side file reaches for a view, an overlay,
raygui or raylib, and a check that drives the real per-block step instead of
nineteen stubs.

Without it the boundary decays the way this repository's other hand-kept
lists have: `check-signal-probe` existed, passed and was never run by the
gate; `link_health` named three streams of nine for weeks; the subscribe
parser was two names short. A boundary nothing enforces is a comment.

## What to do

1. **A link target that proves the server stands alone.** Core, tech,
   runtime, model and server objects, linked with `-lm -pthread` and the
   receiver backend -- no `view_*`, `overlay_*`, `sdrgui*`, `raygui_impl`,
   no `pkg-config --libs raylib`. It goes in `CHECK_UNITS`: if it stops
   linking, the gate fails, and says which symbol pulled the window in.
2. **`check-frame-advance` runs real callees for at least one technology.**
   Its 19 stubs are the list of what ticket 02 moved. Replace FM's with the
   real `fm_runtime` over synthetic blocks, and assert what the per-block
   step *did* -- the pilot locked, the funnel advanced -- rather than that a
   stub was called.
3. **An include audit**, one line like `MISSING:` and `NOT GATED`: no file
   outside the GUI set includes `<raylib.h>`, `"raygui.h"` or `sdrgui*.h`.
   Gated, so a stray include fails `make check` rather than a code review.

## The decision to make -- ask, do not pick

**A link check, or a second binary?**

- **(A) A link target only** (recommended). The boundary is held; one
  `sdrprobe` binary still ships, and `server` inside it links raylib as now.
  Nothing a user runs changes.
- **(B) A shipped `sdrprobe-server` binary** without raylib, for a headless
  box with no graphics stack installed. Real value on a Raspberry Pi beside
  an antenna -- and a packaging and install decision (two binaries, two sets
  of command words), which belongs to the operator rather than a refactor.

(A) is the ticket; (B) is a follow-up if it is wanted.

**Decided 2026-09-27: (B).** See "What (B) actually costs" below -- the code
work turned out to be tickets 02-04's own work plus one genuine split, and
the packaging objection shrank to one question, answered here.

## Acceptance criteria

- [ ] A server link target in `CHECK_UNITS` that fails when any server-side
      object needs a GUI symbol, naming the symbol.
- [ ] `check-frame-advance` exercises at least FM's real runtime and asserts
      on its effects, not on stub call counts.
- [ ] The include audit is gated and clean, and fails when a
      `#include <raylib.h>` is added to a runtime file (mutation-tested).
- [ ] `make check` and `make check-pipelines` pass.

## Take into account

- **The receiver backend is not the window.** `librtlsdr` is a legitimate
  server dependency; only raylib and raygui are excluded. `HAVE_UHD` stays
  as it is.
- **A link check reports the first undefined symbol, not all of them.** Make
  its failure message say "the server pulled in the window through X" so a
  reader knows what boundary broke rather than reading linker output.

## What (B) actually costs -- measured, 2026-09-27

Not estimated. Every `src/*.c` that is not a `view_*`, `overlay_*`, `sdrgui*`
or `raygui_impl` was compiled with **no raylib cflags at all** and linked
against a stub `main` with `librtlsdr -lm -pthread`.

**All 64 compiled.** Not one needed a raylib header. 25 symbols were
undefined, in exactly four files:

- **`chart_window.c` is the only file in the set that calls raylib**, and
  only its six input functions: `IsKeyPressed`, `IsKeyPressedRepeat`,
  `IsMouseButtonPressed`, `IsMouseButtonDown`, `GetMousePosition`,
  `CheckCollisionPointRec`. This is the one genuine split the ticket did not
  know about -- the zoom and pan *arithmetic* from the *reading of the
  mouse*, which is this repository's own rule (ADR-0012: a function that
  reads input may not also decide).
- **`sdrprobe.c`, 13 symbols** -- `monotonic_seconds`, `retune_receiver`,
  `retune_receiver_at_rate`, the five `receiver_*` lease calls,
  `process_block`, `stop_requested`, `set_tab`, `set_decode`,
  `scope_requested_fft_size`, `start_capture_record`. This is exactly ticket
  02's stated remainder, which called them "arguably the application layer's
  home". They are, and the link proves it: nothing else in the set reaches a
  GUI symbol to get at them.
- **`view_survey.c`, 7 symbols** -- and **six of them are the headless
  report's own `printf` helpers** (`survey_print_confirm_header`, `_target`,
  `_summary`) plus `survey_bin_hz`, `survey_clamp_view`,
  `survey_peak_visible`, `survey_history_refresh`. A file named for a window
  holding the output of a run that has none.
- **`overlay_scan.c`, 2** -- `start_scan`, `scan_release_receiver`.

So the code is: finish ticket 02, split `chart_window.c`, move nine
misplaced functions. All of it work this spec already wants; (B) adds a
`main` and a link rule on top.

### The packaging question, answered

The objection to (B) was "two sets of command words". There are none:
`sdrprobe-server` takes **the same** subcommands and the same flags, offers
`headless` and `server`, and refuses the window modes with a message naming
the build. `headless` moves in with `server` because the real split is
**window / no window**, not gui / server -- a headless decode on a Pi beside
an antenna is the use this binary exists for, and it needs raylib today for
nothing.

`./sdrprobe` is unchanged: same one binary, same modes, raylib as now.

## Phase A done, 2026-09-27 -- the one file that called raylib

`chart_window.c` read the mouse and the arrow keys in the same function that
decided what they meant, which is this repository's own rule broken
(ADR-0012: a function that reads input may not also decide) and the whole of
why it could not be in a raylib-free build.

`chart_window_input.c` is the six readings -- `GetMousePosition`,
`IsMouseButtonPressed`, `IsMouseButtonDown`, `CheckCollisionPointRec`,
`IsKeyPressed`, `IsKeyPressedRepeat` -- into a `struct chart_gesture_input`,
and nothing else; it is in `GUI_SRC`. `chart_window_gesture()` is the
deciding half and takes plain numbers. `chart_window_drag_of()` went the
same way for a smaller reason: it never called raylib, it only named a
`Rectangle` to read `.x` and `.width`, and it takes them as doubles now.

`chart_window.o` compiles with no raylib cflags and has no raylib symbol in
it, measured with `nm -u`.

**The gesture had no check at all**, which is what the split buys.
`check-freq-window` links `chart_window.c` now and goes from 79 checks to
99: the drag's three frames (a press outside the plot starts nothing, and
`press` and `held` are both needed or a drag never starts or never ends),
zoom anchored on the pointer only while it is over the chart, zoom beating
reset, and a pan reporting the hertz it could not travel.

**One claim in those checks was wrong on its first run**, in the ordinary
shape: `a pan with room asks for no retune` at a tolerance of 1e-9. One zoom
step of a 2 MHz span puts that pan exactly on the delivered edge and the
overflow comes back as **2.4e-8 Hz** of floating-point residue. The
arithmetic was right and the tolerance was absurd -- 1e-9 *hertz* -- so it
is a millihertz now, which is what the number means.

### And three browser bugs the gate turned up on the way

`make check` failed on `check-web-layout` -- about one run in five, which had
been true for a while and read as flakiness. It was three faults, and the
first two are user-visible:

1. **The FM canvases were `flex:1 1 auto`.** A canvas's content size *is*
   its backing store, so fitting the store to the box changed the box, which
   changed the store: 365 against 364, then 330, then 325, converging a pixel
   at a time and never settling. The survey's canvas has always been
   `flex:1 1 0` and was stable throughout. Both FM canvases are `1 1 0` now
   and read 324 == 324 exactly.
2. **Nothing re-fitted a canvas when a *sibling* changed height.** The only
   triggers were a view switch and `window`'s own resize event; the health
   footer grows once a second the first time a stream drops (it gains a
   "(x% lost)" span, which wraps the line), taking a row off `#panels`. A
   `ResizeObserver` on `#panels` now does, and the suite asserts it
   deterministically -- forcing the footer taller and requiring the stores to
   follow, which fails without the observer every time where waiting for a
   real drop failed one time in five.
3. **A tab click flipped back to the previous view.** `selectView()` shows
   the panel at once and sends `view <name>`; a `receiver_state` already in
   flight still named the old screen, and `handleState()` switched straight
   back to it. `pendingScreen` now makes a state's `screen` field -- and only
   that field -- ignored while a request is outstanding, cleared when a state
   names the view, when `command_result` refuses it, or on reconnect.

The server was verified innocent before any of that was changed: over a
capture, `view survey` returns `ok=True` and **every** subsequent
`receiver_state` says `screen=survey`.

And the suite had a fault of its own that the third bug hid behind: it
measured whichever panel was up and labelled it with the tab it had asked
for, so `exactly one view panel is laid out` passed **on the wrong panel**
and the Scope's canvases were asserted under a heading saying "survey". It
polls for the panel it clicked and asserts it, the same fix the TSF wait
already had. 25 checks to 35, and the count is now the same every run --
which it was not before, and that variance was the tell.

`make check`: 80 suites, 22046 checks, green.
