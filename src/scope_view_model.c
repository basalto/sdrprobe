#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "scope_view_model.h"
#include "app.h"
#include "device_profile.h"

void scope_view_model_build(const struct app *app, struct scope_view_model *out) {
    memset(out, 0, sizeof(*out));

    out->tab = (int)app->tab;
    out->have_samples = app->frame.have_samples;

    out->center_hz = app->applied.frequency_hz;
    out->sample_rate_hz = app->applied.sample_rate_hz;
    out->ppm = app->applied.ppm;
    out->tuning_generation = app->applied.generation;

    out->full_scale = app->device.full_scale;
    out->physical_magnitude_max = device_magnitude_max(&app->device);

    out->spectrum_ready = app->frame.spectrum_ready;
    out->spectrum_bins = app->frame.spectrum_bins;
    out->spectrum_windows = app->frame.spectrum_windows;
    out->spectrum_average = app->frame.spectrum_average;
    out->spectrum_peak = app->frame.spectrum_peak;

    out->magnitudes = app->frame.magnitudes;
    out->pair_count = app->frame.pair_count;
    out->magnitude_min = app->frame.magnitude_min;
    out->magnitude_mean = app->frame.magnitude_mean;
    out->magnitude_max = app->frame.magnitude_max;
    out->duration_ms = out->have_samples
                           ? (double)out->pair_count * 1000.0 /
                                 out->sample_rate_hz
                           : 0.0;

    out->signal_stats_ready = app->frame.signal_stats_ready;
    out->signal_stats = app->frame.signal_stats;

    out->waterfall_ready = app->sv.waterfall_ready;
    /* advance_waterfall_row() (view_scope.c) shifts the ring toward higher
       indices and writes the newest row at the front, so index 0 is always
       the block just measured. */
    out->waterfall_row = app->sv.waterfall_dbfs;

    /* advance_scatter_history() (view_scope.c) writes the newest block at
       `scatter_history_head` and then advances it, so the block just
       inserted sits one slot behind, with wraparound. */
    if (app->sv.scatter_history_count > 0) {
        size_t newest = (app->sv.scatter_history_head +
                         SCATTER_HISTORY_BLOCKS - 1) %
                        SCATTER_HISTORY_BLOCKS;
        const struct scatter_block *block = &app->sv.scatter_history[newest];

        out->scatter_i = block->i;
        out->scatter_q = block->q;
        out->scatter_count = block->count;
    }
}
