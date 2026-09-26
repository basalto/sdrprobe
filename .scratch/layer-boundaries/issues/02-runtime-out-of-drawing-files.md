# 02 - Take the runtime out of the drawing files

Status: needs-triage
Blocked by: 01

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
3. **`retune_receiver()` calls `view_scope_resize_if_needed()`**, a GUI
   function, so a retune from the server path reaches into the Scope's
   textures. The repository's own pattern for this is to **report rather
   than act**: `signal_frame` reports a change of geometry and "the Scope
   drops the waterfall's rows, because those rows are not the frame's to
   clear"; `survey_session` "says where it wants the tuning and the adapter
   obeys". Follow it: the retune reports that the rate changed, and the
   frontend resizes.
4. **`set_tab()` closes the Settings overlay** (`app->set.open = 0`) -- GUI
   state changed inside the shared transition the server's `view <name>`
   command goes through. It is harmless under `server`, where Settings is
   never open, but it is a presentation side effect inside the runtime, and
   it moves out with the rest.

**Decide the mechanism once, before FM, and ask**: reported state the caller
reads after the call (recommended -- it is what `signal_frame` and
`survey_session` already do, and a check can read a flag without mocking a
callback), or a small table of frontend hooks that `server` leaves NULL.

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
