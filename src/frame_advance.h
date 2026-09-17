#ifndef FRAME_ADVANCE_H
#define FRAME_ADVANCE_H

#include "acquisition.h"

struct app;

/*
 * One block's worth of application state, advanced.
 *
 * This is the sequence that used to live inline in `run_gui()`, between
 * `consume_latest()` and `BeginDrawing()` -- extracted so the window and a
 * headless Viewer link (ADR-0027) run the same code rather than two copies
 * of it. It ships no feature of its own; it only makes the per-block work
 * reachable without a window.
 *
 * It contains no GL or raylib call. It decides what to compute this block
 * and dispatches to the functions that compute it -- several of which
 * already run headlessly today, from the `headless --decode` paths in
 * `sdrprobe.c`. `check-frame-advance` proves the dispatch itself: which
 * callee runs under which combination of tab, decode kind and whether a
 * block arrived, checked against fakes standing in for those callees. It
 * does not re-prove any technology's DSP -- each already has its own check
 * for that.
 *
 * `now` is the caller's clock, never read from inside: the window passes
 * `GetTime()`, a headless loop passes `monotonic_seconds()` or a played-back
 * timeline, and nothing here can tell the difference. `snapshot` is filled
 * by the block consumption inside and is the caller's to read afterward
 * (the window's HUD reads `snapshot.worker_failed`); the return value is
 * whether a new spectrum came with this block, which decides whether the
 * caller's draw phase has a waterfall row to upload.
 */
int frame_advance(struct app *app, struct slot_snapshot *snapshot, double now);

#endif
