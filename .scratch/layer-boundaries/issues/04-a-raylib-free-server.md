# 04 - A server built without the window, and a real per-block check

Status: **resolved, 2026-09-27**. All three items done, and two names in the
line this replaces were already stale: the binary is **`sdrprobe`**, not
`sdrprobe-server` (`cli-subcommands/04` renamed it the same day -- the
windowless build took the plain name because that is where the usage is),
and the gate is **`check-no-window-link`**, not `check-server-link`. Item 2
is done: `check-frame-advance` runs FM's real callees rather than stubs.
Item 3 is done: `check-no-raylib-headers` compiles all 65 no-window sources
with a `#error` raylib.h **earlier on the include path** than the real one,
which is what catches an include that is never used -- a linker cannot see
one, and two `CORE_SRC` files were reaching for `view.h` with every symbol
resolving.
Blocked by: 02, 03 (both done)

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

- [x] A server link target in `CHECK_UNITS` that fails when any server-side
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

## Phase B done, 2026-09-27 -- the nine that were in a GUI file by accident

None of them drew anything, and every one was already called from the
raylib-free set, which is how the link found them.

**`view_survey.c`, seven.** Three are a *headless* confirmation pass's own
`printf`s -- `survey_print_confirm_header`, `_target`, `_summary` -- living
in the file named for the screen that pass does not have; they are in
`survey_runtime.c` now, beside the step that calls them. The other four --
`survey_bin_hz`, `survey_clamp_view`, `survey_peak_visible`,
`survey_history_refresh` -- are `freq_window` arithmetic over
`struct survey_view`, and they were where they were only because the two
adapters they stand on (`freq_window_of`, `freq_window_put`) were `static`
in that file. Those are `survey_window.h` now, `static inline`, named
`survey_freq_window_of/_put`: a header rather than a second copy, because
two spellings of "which hertz is this bin" is what `chart_window.h`'s own
comment says this program has already paid for once.

**`overlay_scan.c`, two.** `start_scan` and `scan_release_receiver` into
`scan_runtime.c`. The two `scan_strongest_*` selectors stay where they are
deliberately -- nothing outside the window asks either, so moving them would
be tidying rather than a boundary, and this ticket is about the boundary.

### Where the link stands

Re-measured the same way. Every `src/*.c` outside the GUI set compiles with
no raylib cflags and links against a stub `main` with librtlsdr, libm and
pthread, leaving **14 undefined symbols, all of them in `sdrprobe.c`**:

    monotonic_seconds  process_block  receiver_borrow  receiver_borrow_at
    receiver_commit  receiver_restore_held  receiver_return  retune_receiver
    retune_receiver_at_rate  scope_requested_fft_size  set_decode  set_tab
    start_capture_record  stop_requested

That is the application layer and nothing else -- ticket 02's stated
remainder, now the only thing between here and the binary. Phase C is
splitting `sdrprobe.c`.

`make check`: 80 suites, 22046 checks, green.

## Phase C, step 1 done, 2026-09-27 -- `sdrprobe.c`'s application layer

`src/app_runtime.c`: the clock, the receiver's lifecycle, the retune
transaction, the lease, the tabs, the per-block step and the input-state
read. It was the first eight hundred lines of `sdrprobe.c`, sitting beside
`run_gui()` and `InitWindow()`, and that is the whole of why a server could
not be built without a window. `-Wall -W` clean with no raylib cflags.

Three things came with it that the 14-symbol list did not predict, and each
is the same shape -- something that *decides* was living where something
*draws*:

- **`view_survey_enter()` and `view_survey_leave()`.** `set_tab()` calls
  both, so they cannot be beside the drawing, and neither draws: one loads
  the installation, borrows the receiver and starts a sweep the command line
  asked for; the other stops the sweep and returns the lease inside out.
  They are in `survey_runtime.c` with the four statics they stand on
  (`survey_start`, `survey_clear`, `survey_reset_view`,
  `survey_load_installation`), which are named in `runtime.h` now because the
  view's own Sweep button still calls them -- clicking Sweep and arriving
  with `--survey-range` are the same act.
- **`input_state_now()` and `view_input_now()`**, which
  `scope_requested_fft_size()` asks every block on every path, window or not.
- **The waterfall menu's two flags.** `input_state_now()` read
  `app->gui->wf_menu.menu_open` and `.popup_open`, and `gui_state.h` is
  behind `<raylib.h>`. They are `app->sv.waterfall_menu_open` and
  `.waterfall_report_open` now, because **they route input** --
  `input_route.h` reads them to decide who gets a key -- and routing must be
  decidable with no window (ADR-0012). The rest of
  `struct waterfall_signal_context` is the pointer position, the report's
  numbers and the notice: that is drawing, and it stays.

And five `signal_stop_requested` reads in `sdrprobe.c` became
`stop_requested()`, which is the accessor that variable's own comment says
exists so there is one global rather than several.

**The link, re-measured**: every `src/*.c` outside `sdrprobe.c` and the GUI
files compiles with no raylib cflags and links against a stub `main` with
librtlsdr, libm and pthread -- **zero undefined symbols**. What is left is
`run_headless()` and `main()`, which is step 2.

**Verified against the previous binary, not just against the gate.** This
moved `set_tab`, the survey's enter/leave and the input routing, and
`make check` passing is not the same as being right -- the survey machine
came out of its view with 55 suites green and the settle disabled. So the
binary before this commit and the binary after it were both run over the
same three: a capture survey (`--survey --once`), a GSM decode and an FM
decode. **All three byte-identical.** Plus `make check`: 80 suites, 22046
checks.

## Phase C2 and D done, 2026-09-27 -- `sdrprobe-server` ships

Two ELF files, from one source list split in two.

`src/headless_run.c` is every run with no window -- `headless` *and*
`server`. `src/app_main.c` is everything both binaries do before and after
the run: the flags, the environment, the installation, the receiver, the
signal handlers, the shutdown. `src/sdrprobe.c` is the window and
`src/server_main.c` is twenty lines, and **the whole difference between the
two programs is one NULL**:

    int main(...) { return sdrprobe_main(argc, argv, &window); }   /* sdrprobe */
    int main(...) { return sdrprobe_main(argc, argv, NULL); }      /* server   */

`struct app_window` is the pair of hooks the window fills in -- the frame
loop, and the teardown that unloads textures, closes the audio device and
calls `CloseWindow()`. It is a *pair* because the cleanup needed splitting
too: `view_scope_release()` freed the textures and the waterfall's rows in
one call, and a server has the second without the first, so
`scope_release_history()` is the plain half and lives in `scope_runtime.c`.

The Makefile has one list, split: `CORE_SRC` (no drawing) and `VIEW_SRC`,
with `APP_SRC = CORE_SRC + VIEW_SRC`. Two lists would drift; this cannot.

**Measured:**

| | `./sdrprobe` | `./sdrprobe-server` |
|---|---|---|
| size | 3.94 MB | 2.56 MB |
| shared libraries | 13, two of them raylib and libGL | **8, none of them graphical** |
| modes | window, `headless`, `server` | `headless`, `server` |

The server's eight are librtlsdr, libusb, libudev, libm, libc, libgcc, the
vdso and the loader.

### What holds it

`check-server-link` is in `CHECK_UNITS` and builds the shipped binary rather
than a contrivance -- a check that built something nobody runs would rot the
way `check-signal-probe` did while it was green and ungated. It then asserts
`ldd` names nothing graphical, because linking is not the same claim as not
depending.

**Mutation-tested**, which the ticket asked for: an `#include <raylib.h>`
and an `IsKeyPressed()` added to `survey_runtime.c` fails it, and the
failure *explains itself* rather than dumping ld:

    The server pulled in the window. What reached for it:
        src/survey_runtime.c line 392 calls IsKeyPressed()

    Each of those is raylib's or a view's, and CORE_SRC may not
    reach either. Take the decision out of the drawing into a
    *_runtime.c, or move the file out of CORE_SRC (Makefile).

And `check-pipelines` runs a GSM decode, an FM decode and a capture survey
through **both** binaries and requires the output **byte-identical**. It is
byte-identical, on all three. Linking is the boundary; answering the same is
the claim a person on a box with no graphics stack actually relies on, and
no weaker comparison would find a decode the two disagree about.

One thing removed rather than moved: `run_headless()` opened with
`SetTraceLogLevel(LOG_NONE)`, beside a comment saying "nothing should reach
raylib on this path". That is now **enforced** -- the file compiles with no
raylib header -- so the guard is a defence against a call that cannot exist,
and its failure would be invisible. Gone, with the reasoning in its place.

`make check`: **81 suites, 22056 checks**, green. Both Makefile audits clean,
`check-make-help` clean.

## Items 2 and 3 done, 2026-09-27

### Item 2 -- FM's real runtime in `check-frame-advance`

`update_fm()` and `update_fm_scan()` are no longer fakes there: the real
`fm_runtime.c` is linked in with `fm_session`, `fm_dsp`, `rds`, `sdr_dsp` and
`debug_log`, all of which link `-lm`, and three receiver entry points stay
faked because they belong to `app_runtime.c`, which would pull in the whole
program to check a dispatcher.

The suite feeds a **synthetic 19 kHz pilot** -- an FM carrier phase-modulated
at 0.355 radians, which is a real pilot's 6.75 kHz deviation over 19 kHz --
and asserts what the step *did*: the pilot locked, baseband came out of it
(which `fm_rds_front_feed()` emits only after the lock, so it is a second,
independent statement of the same thing), and the loop settled within 5 Hz of
19 kHz. A fake can record that `update_fm()` was called; only the real one
can show it was called **with a frame it could use**.

**Writing it found three wrong claims of mine and no bug**, which is the
shape `CLAUDE.md` warns about:

1. `blocks_seen` is not a count of blocks fed. `fm_session_feed()` increments
   it only once baseband comes out, which needs the pilot locked -- so the
   first assertion read zero on a perfectly good run. The check uses
   `spectrum_bins` where it means "the block arrived" and the pilot itself
   where it means "the chain worked".
2. "A running scan means no block reaches the chain" is the opposite of the
   truth. `frame_advance()`'s own comment says the scan *owns the receiver
   and feeds the chain itself*; the check caught the reader, not the code. It
   is kept, asserting that the pilot still locks -- a scan that stopped
   feeding would fail it.
3. The phase was keyed off `blocks_seen`, which does not advance until lock,
   so every block regenerated the same samples from t = 0 and the loop never
   converged. Measured directly against `fm_rds_front_feed()`: fed
   continuously it locks on **block 8** of 65536 samples.

### Item 3 -- the include audit, and it was not redundant

It was written down here as probably unnecessary, on the reasoning that
`check-no-window-link` covers the same ground by linking and linking is
stronger than grepping. **That was wrong, and the audit found it on its first
run.**

`viewer_session.c` and `survey_report.c` -- both in `CORE_SRC` -- included
`view.h`, which includes `<raylib.h>`. They *called* nothing from it, so
every symbol resolved and the link check passed, while **`make sdrprobe`
would have failed outright on a machine with no raylib dev headers
installed** -- which is the entire reason the plain name was given to that
build (`.scratch/cli-subcommands/issues/04-*`). A linker cannot see an
include that is never used.

`check-no-raylib-headers` compiles all **65** no-window sources with a
`#error` raylib.h placed *earlier on the include path* than the real one.
Dropping `pkg-config --cflags raylib` would prove nothing: the system header
is in `/usr/include` and is found anyway.

Mutation-tested -- adding `#include "view.h"` back to `survey_report.c` fails
it, naming the file, the include chain and what to do about it. Fixing it
took two declarations: `set_tab()` and `set_decode()` moved from `view.h` to
`runtime.h`, where they belong -- both retune, so they are application layer,
and `web` reaches them from a Viewer command with no window anywhere.

`make check`: **82 suites, 22127 checks**, green.
