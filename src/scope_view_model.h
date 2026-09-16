#ifndef SCOPE_VIEW_MODEL_H
#define SCOPE_VIEW_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "sdr_dsp.h"

struct app;

/*
 * What the Scope's four charts show, as plain data -- no `Rectangle`, no
 * `Color`, no `Texture2D`, no raylib type anywhere. This is the seam
 * ADR-0027 calls a State update: `scope_view_model_build()` fills one from
 * `struct app` once a block, the raylib Scope reads it to draw, and a future
 * Viewer link (ticket 05) will serialize the same object rather than
 * deriving a second description of the same screen.
 *
 * It says **what** the block measured, not where any of it goes on screen.
 * Geometry stays with whichever frontend is drawing: `MeasureText` alone
 * sits at 57 sites across 13 files including `scope_layout.h` and
 * `sdrgui_geometry.h`, so a positioned display list would be computed from
 * raylib's font metrics and wrong under a browser's (see ADR-0027 and its
 * amendment).
 *
 * Two kinds of state deliberately stay out of it, on the same principle a
 * display list is refused for: state that only means something against one
 * frontend's own geometry. The Scope's zoom, pan, drag and the magnitude
 * view's plot-width peak reduction (`app->sv.window`, `app->sv.magnitude_*`)
 * stay view-owned, exactly where the survey's zoom and the calibration
 * overlay's own state already live -- a browser Viewer bins the magnitude
 * series to its own pixel width and would discard a raylib-width reduction
 * regardless. And GPU resources -- the scatter and waterfall textures --
 * cannot be plain data by definition and stay `app->sv`'s.
 *
 * The waterfall row and the scatter points below are read by nobody in this
 * ticket: `draw_waterfall()` still draws from the Scope's own accumulated
 * ring (`app->sv.waterfall_dbfs`), the same way `draw_scatter()` still blits
 * the texture `render_scatter()` already built. They are here because the
 * Viewer needs exactly this subset (ADR-0027: a waterfall history is
 * reconstructed in the browser from rows, never re-sent whole; scatter
 * arrives as this block's own decimated points) -- so ticket 05 gets its
 * payload for free rather than a second description of this screen.
 */
struct scope_view_model {
    int have_samples;

    /* The receiver's applied tuning, rate and ppm -- Probe language. */
    uint32_t center_hz;
    uint32_t sample_rate_hz;
    int ppm;
    /* ADR-0027's tuning generation: bumped by retune_receiver() on a
       successful retune, and by nothing else -- the Settings panel's own
       apply path is a separate, older transaction that does not yet run
       through it, a known and not a silent gap. */
    uint32_t tuning_generation;

    /* The two device-profile facts the charts read, not the whole profile. */
    float full_scale;
    float physical_magnitude_max;

    /* The spectrum: average and peak hold in dBFS, capacity
       SDR_DSP_FFT_MAX, `spectrum_bins` of it filled. */
    int spectrum_ready;
    int spectrum_bins;
    int spectrum_windows;
    const float *spectrum_average;
    const float *spectrum_peak;

    /* This block's own magnitude series (`pair_count` entries) and its
       scalar summary -- not the plot-width reduction the raylib magnitude
       view draws, which stays view-owned; see the file comment. */
    const float *magnitudes;
    size_t pair_count;
    float magnitude_min;
    float magnitude_mean;
    float magnitude_max;
    double duration_ms;

    /* The signal statistics the HUD and a Viewer both read. */
    int signal_stats_ready;
    struct sdr_signal_stats signal_stats;

    /* The newest waterfall row: capacity SDR_DSP_FFT_MAX, `spectrum_bins`
       of it valid. See the file comment for why draw_waterfall() does not
       read this yet. */
    int waterfall_ready;
    const float *waterfall_row;

    /* This block's decimated, normalized I/Q -- in units of full scale, the
       same units `draw_scatter()`'s chart already uses. See the file
       comment for why draw_scatter() does not read this yet. */
    const float *scatter_i;
    const float *scatter_q;
    size_t scatter_count;
};

/*
 * Fills `out` from the current state of `app`. Reads only plain fields --
 * no GL or raylib call, no I/O -- so `check-scope-view-model` links `-lm`
 * alone against it.
 *
 * The pointers `out` receives point into `app`'s own storage (the frame,
 * the Scope's waterfall ring, the scatter history): valid for as long as
 * `app` is not advanced again, which is exactly the one frame this is built
 * for. It is not a snapshot to keep past that frame.
 */
void scope_view_model_build(const struct app *app, struct scope_view_model *out);

#endif
