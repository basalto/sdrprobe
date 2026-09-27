# 02 - Take the runtime out of the drawing files

Status: **resolved, 2026-09-27** -- superseded by ticket 05 (ADR-0028), which
moved the runtime into `src/runtime/` wholesale rather than function by
function. The table below is kept as a record of what was moved and is **no
longer a description of the tree**: of the sixteen runtime functions it lists
as living in files that draw, **one does** -- `update_fm_audio()`, and only
because it feeds raylib's audio device. `frame_advance()` is in
`src/runtime/frame_advance.c`, every `update_*` it calls is in that layer's
`*_runtime.c`, `check-frame-advance` runs FM's **real** callees rather than
stubs, and `check-no-window-link` holds the boundary at link time.
`recompute_magnitude_bins()` (item 2) is still in `view_scope.c` and is
called from `sdrprobe.c`; it draws nothing and takes a plot width, so it is
the one piece of this ticket left, and it is small enough to fold into
whatever next touches the Scope rather than to keep a ticket open for.
Blocked by: 01 (done)

## The problem

`frame_advance()` is the one per-block step the window, `headless` and
`server` all drive, and it calls 18 functions. **16 of them are defined in
files that draw**:

| defined in | called by `frame_advance()` |
|---|---|
| `sdrprobe.c` | `process_block` |
| `view_scope.c` | `decay_spectrum_peak`, `advance_waterfall_row`, `advance_scatter_history` |
| `view_fm.c` | `update_fm`, `update_fm_scan`, `update_fm_audio` |
| `view_gsm.c`, `view_lte.c`, `view_adsb.c`, `view_tetra.c`, `view_srd.c`, `view_survey.c` | `update_gsm_sch`, `update_lte`, `update_lte_scan`, `update_adsb`, `update_tetra`, `update_srd`, `update_survey` |
| `overlay_calibration.c`, `overlay_scan.c`, `overlay_startup.c` | `update_calibration_measurement`, `update_drift_check`, `update_scan`, `update_startup` |

`viewer_session.c` also needs `set_tab`, `set_decode`, `retune_receiver` and
`stop_requested` from `sdrprobe.c`. By a regex count, 32 runtime functions
sit inside `view_*.c` and `overlay_*.c` beside their `draw_*` and `handle_*`
code -- 11 in `view_fm.c`. So `server` links every view, every overlay,
raygui and raylib, and `check-frame-advance` can only run with all 19 callees
stubbed.

`view.h` compounds it: 146 declarations -- `draw_*`, `update_*`, `handle_*`
-- in one header included by 20 files.

## What to do

Per technology, one commit each, **FM first** (the one most recently worked
on, with the most runtime functions and a fresh view model to lean on):

1. Move its runtime functions -- `update_*`, `enter_*`, `leave_*`, scans,
   tunes -- from `view_<tech>.c` into `src/<tech>_runtime.c`. The view file
   keeps `draw_*` and `handle_*`.
2. Split `view.h` as each technology moves: runtime declarations go to a
   `runtime.h` that includes no raylib; drawing and input stay in `view.h`.
3. Then `view_scope.c`'s three `advance_*`/`decay_*`, then the overlays'
   four, then `sdrprobe.c`'s `process_block`, `set_tab`, `set_decode`,
   `retune_receiver(_at_rate)` and `stop_requested`.

## Four places where this is a design change, not a move

A file move is mechanical. These four are not, because in each the shared
step currently reaches *up* into presentation:

1. **`frame_advance()` pumps the sound card.** `update_fm_audio()` feeds a
   raylib `AudioStream` and is called every frame from the shared step. Only
   the window plays sound. It belongs in the window's own frame loop, after
   `frame_advance()`; it returns at once unless something is playing, so the
   order change is expected to be invisible -- measure it anyway.
2. **`process_block()` asks the presentation which transform size to use**,
   via `input_scope_owns_spectrum(input_state_now(...))`. `CLAUDE.md`
   already says why: the survey, both band scans and calibration chose their
   floors against 977 Hz bins, so the Scope's resolution applies only while
   the Scope owns the spectrum. The *policy* is right; its *location* is the
   problem. The runtime should take the size as an input the frontend
   supplies, which is what `signal_frame`'s design already does -- "the
   transform size is an argument, because `input_scope_owns_spectrum()` is a
   question about presentation".
3. ~~**`retune_receiver()` calls `view_scope_resize_if_needed()`**~~ --
   **this was wrong and is withdrawn (2026-09-26).** It does not. The name
   appears in `retune_receiver()` only inside a comment, explaining that the
   resize function *notices* a retune when the frame loop next calls it; the
   sole caller is `run_gui()`. The claim came from a grep over the function's
   line range that matched the comment text, written into this ticket without
   reading the body -- the `check-claims` shape exactly, in a ticket rather
   than in a check.

   What is actually there is *already correct* and worth preserving on the
   move: `retune_receiver()` calls `signal_frame_invalidate()` and nothing
   else, because "rebuilding it is drawing and this runs on paths with no
   window". The report-rather-than-act pattern this item recommended is the
   pattern the code already follows here.
4. **`set_tab()` closes the Settings overlay** (`app->set.open = 0`) -- GUI
   state changed inside the shared transition the server's `view <name>`
   command goes through. It is harmless under `server`, where Settings is
   never open, but it is a presentation side effect inside the runtime, and
   it moves out with the rest.

**With item 3 withdrawn, the general "mechanism" question mostly dissolves**
-- and that is the point of having checked. Items 1 and 4 are plain moves: the
window's loop pumps its own sound card, and the frontend closes its own
Settings panel when it switches tab. Neither needs a callback or a reported
flag.

Item 2 is the one that needs a decision, and it is narrower than a mechanism:
`process_block()` already *computes* the transform size and passes it to
`signal_frame_process()`. What moves is only where that number comes from once
`process_block()` is runtime code. **Decided 2026-09-26: a parameter the caller passes.**
`process_block(app, now, fft_size)`. The window computes it from
`input_scope_owns_spectrum()` exactly as today; `headless` and `server` pass
`SDR_DSP_FFT_SIZE` (or whatever `--fft` asked for). The runtime never asks
what is on screen, and a check picks the size it wants without arranging a
screen state to imply it. The cost is one argument threaded through
`frame_advance()`, which is the visible kind.

## Acceptance criteria

- [ ] Every function `frame_advance()` and `viewer_session.c` call is defined
      in a file that includes no raylib header.
- [ ] `runtime.h` exists, includes no raylib, and `view.h` no longer
      declares any `update_*`, `enter_*` or `leave_*`.
- [ ] `frame_advance.o`, the runtime objects and `acquisition.o` link
      together with `-lm -pthread` and no view, overlay, `sdrgui` or raygui
      object -- ticket 04 turns this into a target.
- [ ] Per technology commit: `make check`, `make check-pipelines`, warm
      `make screens NAMES="<tech>"`, `make check-web-layout`, and a live
      `server` run on a capture for that technology.

## Take into account

- **This is where the survey machine broke.** Its extraction was green
  across 55 suites and byte-identical on captures while the settle that
  throws away stale blocks was disabled, because a capture never retunes.
  The survey and every scan retune; their commits need a live receiver run,
  and `survey blocks N settling M` is the line that caught it last time.
- **Not `deepening/10`'s work.** Moving `retune_receiver()` changes which
  file it lives in; who *owns* applied state is that ticket's phases 4-6, and
  must not be done here under cover of a move.
- **`check-frame-advance`'s 19 stubs** are a list of exactly these callees.
  Keep them passing through this ticket; ticket 04 replaces them with the
  real functions.

## FM done, 2026-09-26 -- the first technology out

`src/fm_runtime.c` (659 lines) holds FM's twelve runtime functions:
`view_fm_defaults`, `update_fm_flush`, `enter_fm`, `fm_scan_showing`,
`fm_editing`, `fm_tune`, `fm_scan_begin`, `fm_scan_stop`, `fm_scan_choose`,
`update_fm`, `fm_scan_finish`, `update_fm_scan`. `view_fm.c` is down from
1471 lines to 854 and keeps the drawing, the input, and the three functions
that work the sound card -- an `AudioStream` is a window resource.

**It compiles with a `#error` raylib.h ahead of the real one**, which is the
property this ticket is for, not merely "it builds".

`src/runtime.h` is the raylib-free half of `view.h`, started here: FM's
declarations, plus `retune_receiver`, `retune_receiver_at_rate` and the three
borrow/return functions. Those five had to come first -- a scan borrows the
receiver, walks it and gives it back -- so their *declarations* moved while
their definitions stay in `sdrprobe.c` for that later commit. `view.h`
includes `runtime.h`, so every existing caller is unchanged.

### Measured, not assumed

- `make check`: 21994 checks in 79 suites.
- **The headless FM decode is byte-identical across the move**, with and
  without `--fm-scan`. Determinism confirmed first by running the same binary
  twice. This is the instrument that works here.
- **`make screens` could not settle it, and saying so is the honest result.**
  FM's two screens are time-sampled and enormously noisy: three renders of
  the *same* binary gave 6746 and 144483 differing pixels against each other.
  Old-vs-new came out 5436-5816 on the pairs not involving that outlier --
  inside the within-old noise of 7417-10747 -- but with a spread that large
  the comparison cannot prove anything either way. The deterministic decode
  above is what carries the claim.
- A live `server` run: 200 `fm_state` messages, station 0x8343 " TSF ",
  "reading the station".

### Two things found on the way, committed separately

Both are in `1450a1c`, ahead of the move, because neither is a file move:

- **`fm_scan_begin()` stamped its step clock from raylib's `GetTime()`**,
  which is 0.0 before `InitWindow()` -- and a Viewer's `view fm` reaches it
  through `set_decode()` -> `enter_fm()`. With a live receiver outside band
  II that scan would race its whole plan in one pass. `now` is threaded
  through all three now. No capture can reach it, so the gate shows no
  regression rather than the fix.
- **`make add-argument`, the tool for exactly this, was broken two ways**:
  the rule did not quote `$(VALUE)` (so `CLAUDE.md`'s own documented example
  died in the shell), and the script could not *append* an argument --
  `index == len(args)` did nothing and reported success. Appending is the
  commonest way a parameter is threaded here.

### GSM done, 2026-09-26

`src/gsm_runtime.c` -- channel tuning, the synchronization decode, entering
and leaving, starting a recording. `view_gsm.c` 633 -> 526 lines.
Three shared functions moved their declarations ahead of their definitions:
`start_scan()` and `scan_release_receiver()` (`overlay_scan.c`) and
`start_capture_record()` (`sdrprobe.c`). Poison-tested raylib-free. All three
GSM captures decode byte-identically -- 69, 73 and 113, the set that exists
because the BCC picks the training sequence.

### ADS-B done, 2026-09-26

`src/adsb_runtime.c` -- the tuning check, entering and leaving, one block of
Mode S, and the log row formatting. `view_adsb.c` 480 -> 382 lines.
`adsb_analysis_showing()` was `static` and is now exported, because the
drawing asks it; `adsb_format()` stayed `static`, having no caller outside
the runtime. Poison-tested raylib-free. Both captures decode identically with
the wall-clock column stripped -- `adsb_cpr_pair` is the one that exercises
the even/odd CPR pairing cache end to end.

**And the gate was found to be intermittent, which mattered more.** A run of
`make check` failed roughly one time in six, always on
`check-web-layout`'s "the station was named". Not the move: my own harness
slept a flat seven seconds and assumed the FM decode would have named TSF by
then. A programme service name is four segments seen whole twice, over a
three-second capture that loops, and how many loops that takes varies. It
polls for the name with a thirty-second deadline now; eight consecutive runs
pass. A gated check that fails one run in six is worse than the fault it
looks for.

### TETRA done, 2026-09-26

`src/tetra_runtime.c` -- one block of a 25 kHz carrier down to a network
identity, and the remembering of what it read. Only two functions, so
`view_tetra.c` drops 321 -> 253 lines and is almost entirely drawing.
Poison-tested raylib-free. Both captures decode byte-identically: `cc17` and
`cc32`, the pair that exists because the broadcast channel is scrambled with
the network's own colour code, so a decoder that hardcoded one would read
one capture and fail the other.

### SRD done, 2026-09-26

`src/srd_runtime.c` -- the tuning inside 430-440 MHz, entering and leaving,
one block of OOK/Manchester, remembering frames and undecoded detections, and
the header's tuning group (`srd_freq_show`, `srd_freq_commit`,
`srd_tune_arrow`). `view_srd.c` drops 808 -> 614 lines. Those last three were
`static` and are exported now, because the input handler drives them and the
input handler stayed behind. Poison-tested raylib-free.

**Its headless decode is not deterministic, and that is not a fault.** Two
runs of the *same* binary differ -- in the elapsed-time column only
(`0.6s` against `0.4s`), because the column reports when in playback a burst
was heard and that moves with how fast the machine feeds the capture. Every
payload is identical: the same `3F 04 0B 69 BB CC 9F 42 F2 D4` frames in the
same order. With that column stripped, old and new agree exactly on both
captures, and so do two runs of one binary. Worth knowing before the next
person reads a DIFFERS here as a regression.

### LTE done, 2026-09-26 -- and that is all six technologies

`src/lte_runtime.c` -- entering and leaving at 1.92 MS/s, the band scan and
its confirmation pass, and one block of cell search and broadcast decode.
Fifteen functions, the largest of the six. `view_lte.c` drops 1126 -> 721.

Two things moved that were not simply LTE's own:

- `view_lte_bands()` was a `static inline` in `view.h`, so anything wanting
  the reachable band list compiled against raylib to get it. It is in
  `runtime.h` now; it only reads the device profile.
- `selected_band()` was `static` in `view_lte.c` and needed by both halves,
  so it moved into the runtime and is exported, along with `park_in_band()`,
  `scan_start()`, `scan_stop()` and `scan_select()` -- the input handler
  drives those four and it stayed behind.

`lte_b20_pci28` decodes byte-identically, which is the capture that must keep
reading cell 28 under the normal cyclic prefix.

### Where `frame_advance()` stands now

All six technologies answer from a raylib-free file. What is left:

| still in a drawing file | why it is next |
|---|---|
| `process_block` (`sdrprobe.c`) | item 2: takes the transform size as a parameter |
| `advance_waterfall_row`, `advance_scatter_history`, `decay_spectrum_peak` (`view_scope.c`) | the Scope's three |
| `update_scan` (`overlay_scan.c`), `update_startup` (`overlay_startup.c`), `update_calibration_measurement`, `update_drift_check` (`overlay_calibration.c`) | the overlays' four |
| `update_survey` (`view_survey.c`) | the survey, whose machine is already extracted |
| `update_fm_audio` (`view_fm.c`) | item 1: belongs in the window's own loop, not the shared step |

All six `*_runtime.c` files are poison-tested: each compiles with a `#error`
raylib.h ahead of the real one.

### Item 1 done, 2026-09-26 -- the sound leaves the shared step

`update_fm_audio()` fed a raylib `AudioStream` from `frame_advance()`, the
one step `headless` and `server` also drive. It is in `run_gui()`'s own frame
loop now, right after the `frame_advance()` call and beside the two GPU
uploads already left to it. Every frame rather than every block, unchanged --
the card asks on its own schedule and a block is several of its buffers.

`check-frame-advance` asserted the sound *was* pumped from there. The stub
stays and the assertion is inverted, so the check now pins the absence rather
than falling silent about it -- a check that stops mentioning something is
how a moved call comes back.

Verified in a real window, which is the only place sound exists:
`--fm-play` logs `fm-audio playing at 49951 Hz` and the FM decode is
byte-identical.

### Item 2 done, 2026-09-26 -- the transform size is a parameter

`process_block(app, now, fft_size)` and `frame_advance(app, snapshot, now,
fft_size)`. The size was worked out *inside* `process_block()` from
`input_scope_owns_spectrum()`, so the shared per-block step asked what was on
screen -- on `headless` and `server` runs too, where there is none.

The question is still asked, in the layer allowed to ask it:
`scope_requested_fft_size()` in `sdrprobe.c`, which every call site there and
in `survey_report.c` now passes. Behaviour is identical by construction --
the helper is the old expression, moved. `viewer_session.c` passes
`app->sv.fft_size` outright, with a comment saying why it may: it sets
`app->tab` and `app->view` itself, so the Scope does own the spectrum there,
and there is no screen to route the question through.

`check-frame-advance` gained the pass-through assertion, and deliberately
uses a size unlike any default *and* unlike `app.sv.fft_size` (zero in the
fixture) -- so it cannot pass by the value being looked up from the app after
all. A check pinning only `now` would not have noticed the size being looked
up again.

Verified where a non-default size actually travels: `server --fft 2048` and
`--fft 16384` put 2048 and 16384 bins on the wire.

### The Scope's per-block work done, 2026-09-27

`src/scope_runtime.c` -- `decay_spectrum_peak`, `advance_waterfall_row`,
`advance_scatter_history` and `allocate_waterfall_history`. All four move
*data*: the waterfall's dBFS rows and the scatter's I/Q blocks, which are
what a browser is sent and what the Scope draws from. No texture, no plot
rectangle. Poison-tested raylib-free.

**`recompute_magnitude_bins()` was not moved -- its *call* was.** Its output
is read by exactly one function, `draw_magnitude()`, and the reduction is to
the plot's pixel width, so it is drawing preparation that `process_block()`
happened to run. Headless and server were reducing every block to a capacity
of 1 that nothing read. `run_gui()` does it now, and ticket 01's NULL guard
inside it is gone -- that guard's own comment said moving the call was the
fix, and this is it.

Gated on `spectrum_updated` rather than `have_samples`, which differ only
when a block converts and then yields no spectrum (`pair_count` below the
transform size, which no shipping path produces); there the chart shows the
previous block for one frame instead of being recomputed from magnitudes it
already drew.

**The Scope's screenshots cannot discriminate this change, and that is worth
recording rather than working around.** `scatter.png` compared old against
new at 1739 differing pixels, against an apparent noise floor of 133 -- a
regression by that arithmetic. Five renders of the *same* binary then came
out in clusters at 12, 29 and ~1690 apart, and old-vs-new landed at 1716,
1739, 3372 and 3405: quantised in steps of about 1690. The screen draws one
block's I/Q, so the metric is measuring *which block playback reached*, not
whether the code changed. Same class as `srd-charts` in ticket 01 and the FM
screens in the FM commit.

What carries the claim instead: the gate (which includes
`check-scope-view-model` over `scatter_i/q/count`, `check-frame-advance` and
`check-pipelines`), the poison test, an identical FM decode, and a live
`server` still sending 2048-bin spectrum and waterfall rows.

### The overlays' four and the survey done, 2026-09-27

Four more runtime files, all poison-tested raylib-free:

- `src/scan_runtime.c` -- the GSM band scan's per-block step.
- `src/calibration_runtime.c` -- the residual buffer the lock gate reads and
  the drift re-check, with `cal_selected_band()` which both halves use.
  ADR-0004's source-homogeneity rule is untouched.
- `src/startup_runtime.c` -- the startup machine's frame step and
  `startup_release()`.
- `src/survey_runtime.c` -- the sweep's per-block step and the five helpers a
  click shares with it, since selecting a candidate and obeying an event are
  things a click does too.

`monotonic_seconds()`, `receiver_commit()` and `receiver_restore_held()`
moved their declarations to `runtime.h` along the way -- the clock especially,
which is the one every runtime path must use instead of raylib's `GetTime()`.

**All eleven `*_runtime.c` files pass the poison test**, and every function
`frame_advance()` dispatches to now lives in one of them or in
`acquisition.c`. The two exceptions are deliberate: `process_block()` is in
`sdrprobe.c`, the application layer, and `update_fm_audio()` is no longer
called from the shared step at all.

**Two extraction faults worth recording, both mine and both caught by the
compiler rather than by review.** A regex with `re.S` and a lazy `.*?`
reached backwards across `view.h` and moved three hundred lines of drawing
declarations into `runtime.h`; the fix was to remove declarations by exact
text, never by a pattern that can span a file. And the extractor matched
`survey_select`'s *forward declaration* -- a line ending in `;` -- then
scanned to the next `}`, taking an unrelated function's body with it. It
skips declarations now. Both were reverted with `git checkout` and redone;
neither reached a commit.

Verified: `make check` green, the headless survey byte-identical, the
calibration refusal byte-identical, GSM and FM decodes unchanged.

### Still open in this ticket

`process_block()` and the definitions of `set_tab`, `set_decode`,
`retune_receiver*` and `stop_requested`, all in `sdrprobe.c` -- the
application layer, which is arguably their home; their declarations are
already in `runtime.h`. `check-frame-advance` still stubs all nineteen
callees; ticket 04 replaces them with the real thing for at least one
technology.
