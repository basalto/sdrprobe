#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "model/scope_view_model.h"
#include "runtime/app.h"
#include "core/device_profile.h"

void scope_view_model_build(const struct scope_view_model_input *in,
                            struct scope_view_model *out) {
    memset(out, 0, sizeof(*out));

    /* The tuning, the rate, the generation and the screen are the
       receiver's, built by its own model -- see scope_view_model.h. */
    receiver_view_model_build(in->applied, in->device, in->tab, in->decode,
                              &out->receiver);
    out->have_samples = in->frame->have_samples;

    out->physical_magnitude_max = device_magnitude_max(in->device);

    out->spectrum_ready = in->frame->spectrum_ready;
    out->spectrum_bins = in->frame->spectrum_bins;
    out->spectrum_windows = in->frame->spectrum_windows;
    out->spectrum_average = in->frame->spectrum_average;
    out->spectrum_peak = in->frame->spectrum_peak;

    out->magnitudes = in->frame->magnitudes;
    out->pair_count = in->frame->pair_count;
    out->magnitude_min = in->frame->magnitude_min;
    out->magnitude_mean = in->frame->magnitude_mean;
    out->magnitude_max = in->frame->magnitude_max;
    out->duration_ms = out->have_samples
                           ? (double)out->pair_count * 1000.0 /
                                 out->receiver.sample_rate_hz
                           : 0.0;

    out->signal_stats_ready = in->frame->signal_stats_ready;
    out->signal_stats = in->frame->signal_stats;

    out->waterfall_ready = in->sv->waterfall_ready;
    /* advance_waterfall_row() (view_scope.c) shifts the ring toward higher
       indices and writes the newest row at the front, so index 0 is always
       the block just measured. */
    out->waterfall_row = in->sv->waterfall_dbfs;

    /* advance_scatter_history() (view_scope.c) writes the newest block at
       `scatter_history_head` and then advances it, so the block just
       inserted sits one slot behind, with wraparound. */
    if (in->sv->scatter_history_count > 0) {
        size_t newest = (in->sv->scatter_history_head +
                         SCATTER_HISTORY_BLOCKS - 1) %
                        SCATTER_HISTORY_BLOCKS;
        const struct scatter_block *block = &in->sv->scatter_history[newest];

        out->scatter_i = block->i;
        out->scatter_q = block->q;
        out->scatter_count = block->count;
    }
}
