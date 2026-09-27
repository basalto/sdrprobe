/*
 * The Scope's per-block work, with nothing that draws.
 *
 * Three things `frame_advance()` calls -- the spectrum peak's decay, the
 * waterfall's row history, and the scatter's block history -- plus the
 * allocation behind the first of those. All of them move *data*: the
 * waterfall's dBFS rows and the scatter's I/Q blocks are what a browser is
 * sent (ADR-0027) and what the Scope draws from, and neither the texture nor
 * the plot rectangle is touched here.
 *
 * Out of `view_scope.c` by `.scratch/layer-boundaries/issues/02-*`. No
 * raylib, and it must not gain any.
 */

#include <stdlib.h>
#include <string.h>

#include "runtime/app.h"
#include "runtime/runtime.h"

/*
 * The waterfall ring's own allocation -- the one part of
 * `recreate_waterfall()` with no GL dependency, so headless serving
 * (`viewer_session.c`, which has no plot rectangle and no texture to go
 * with it) can call this alone rather than duplicating the realloc.
 * Returns 0 and leaves `app->sv.waterfall_dbfs`/`waterfall_capacity` sized
 * for at least `rows`, or -1 on allocation failure.
 */
int allocate_waterfall_history(struct app *app, int rows) {
    if (!app->sv.waterfall_dbfs || rows > app->sv.waterfall_capacity) {
        float *history = realloc(
            app->sv.waterfall_dbfs,
            (size_t)rows * SDR_DSP_FFT_MAX * sizeof(*history));
        if (!history) {
            fprintf(stderr, "Failed to allocate %d waterfall history rows.\n",
                    rows);
            return -1;
        }
        app->sv.waterfall_dbfs = history;
        app->sv.waterfall_capacity = rows;
    }
    return 0;
}

/*
 * The waterfall's data half: shift the row history and insert the newest
 * row. Plain floats, no GL -- this is what an advance step reachable without
 * a window may call. `render_waterfall()` is the other half, the texture
 * upload, and stays a draw-phase call; the frame loop calls both in
 * sequence, same as `update_waterfall()` used to do internally.
 */
void advance_waterfall_row(struct app *app) {
    if (!app->sv.waterfall_ready || !app->frame.spectrum_ready)
        return;

    int retained = app->sv.waterfall_rows < app->sv.waterfall_capacity
                       ? app->sv.waterfall_rows
                       : app->sv.waterfall_capacity - 1;
    if (retained > 0)
        memmove(app->sv.waterfall_dbfs + SDR_DSP_FFT_MAX,
                app->sv.waterfall_dbfs,
                (size_t)retained * SDR_DSP_FFT_MAX *
                    sizeof(*app->sv.waterfall_dbfs));
    memcpy(app->sv.waterfall_dbfs, app->frame.spectrum_average,
           (size_t)app->frame.spectrum_bins *
           sizeof(*app->sv.waterfall_dbfs));
    if (app->sv.waterfall_rows < app->sv.waterfall_height)
        app->sv.waterfall_rows++;
}

/*
 * The scatter's data half: insert the newest block's decimated, normalized
 * points and expire whatever has aged out of the history. Plain floats, no
 * GL -- expiry runs every call regardless of `insert`, because the fade is a
 * function of `now` advancing, not of a block arriving. `render_scatter()`
 * is the other half, the render-to-texture pass, and stays a draw-phase
 * call.
 */
void advance_scatter_history(struct app *app, double now, int insert) {
    if (insert && app->frame.pair_count > 0) {
        struct scatter_block *block =
            &app->sv.scatter_history[app->sv.scatter_history_head];
        block->count = app->frame.pair_count < SCATTER_SAMPLES
                           ? app->frame.pair_count
                           : SCATTER_SAMPLES;
        block->time = now;
        for (size_t n = 0; n < block->count; n++) {
            size_t index = block->count == 1
                               ? 0
                               : n * (app->frame.pair_count - 1) /
                                     (block->count - 1);
            /* The scatter axes are in units of full scale, so a
               constellation looks the same whatever the container. */
            block->i[n] = app->frame.i_samples[index] / app->device.full_scale;
            block->q[n] = app->frame.q_samples[index] / app->device.full_scale;
        }
        app->sv.scatter_inserted = block->count;
        app->sv.scatter_history_head =
            (app->sv.scatter_history_head + 1) % SCATTER_HISTORY_BLOCKS;
        if (app->sv.scatter_history_count < SCATTER_HISTORY_BLOCKS)
            app->sv.scatter_history_count++;
    }

    while (app->sv.scatter_history_count > 0) {
        size_t oldest = (app->sv.scatter_history_head + SCATTER_HISTORY_BLOCKS -
                         app->sv.scatter_history_count) %
                        SCATTER_HISTORY_BLOCKS;
        if (now - app->sv.scatter_history[oldest].time <=
            SCATTER_HISTORY_SECONDS)
            break;
        app->sv.scatter_history_count--;
    }
}

void decay_spectrum_peak(struct app *app, double now) {
    /* The rate is this view's preference; the walk across the bins is the
       frame's, and so is the array. */
    signal_frame_decay_peak(&app->frame, now, PEAK_DECAY_DB_PER_SECOND);
}

/* The Scope's starting numbers, set by `main()` before any window
   exists -- which is why they are here and not beside the drawing
   (`.scratch/layer-boundaries/issues/04-*`). `view_lte_defaults()` in
   lte_runtime.c is the precedent. */
/* The scales each view starts at. */
void view_scope_defaults(struct app *app) {
    app->sv.fft_size = SDR_DSP_FFT_SIZE;
    app->sv.magnitude_lower = 0.0f;
    app->sv.magnitude_upper = 64.0f;
    app->sv.spectrum_lower_dbfs = SDR_DSP_DBFS_FLOOR;
    app->sv.scatter_axis_limit = 0.5f;
    app->waterfall_lower_dbfs = SDR_DSP_DBFS_FLOOR;
}

/*
 * The waterfall's rows, which every run allocates and only some draw.
 *
 * `view_scope_release()` freed the textures and this in one call, and a
 * server has the second without the first -- `app->gui` is NULL there while
 * `app->sv.waterfall_dbfs` is a real allocation that `frame_advance()` fills
 * (`.scratch/layer-boundaries/issues/04-*`). Idempotent, because the window's
 * release calls it too and `main()` then calls it again.
 */
void scope_release_history(struct app *app) {
    free(app->sv.waterfall_dbfs);
    app->sv.waterfall_dbfs = NULL;
}
