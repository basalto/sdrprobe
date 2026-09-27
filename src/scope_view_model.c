#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "scope_view_model.h"
#include "app.h"
#include "device_profile.h"

/*
 * Tab plus decode kind, as the one name a reader needs. On the Decode tab
 * the screen *is* the technology; elsewhere it is the tab. Kept here rather
 * than in `input_route.h` because it is what crosses the wire, and the two
 * enums it spells are `app.h`'s and `input_route.h`'s respectively -- this
 * is the only place both are already in hand.
 */
void scope_screen_name(char *out, size_t size, int tab, int decode) {
    static const char *const decodes[] = {
        "fm", "adsb", "gsm", "lte", "tetra", "srd"
    };
    const char *name = "scope";

    if (tab == TAB_SURVEY)
        name = "survey";
    else if (tab == TAB_DECODE)
        name = (decode >= 0 &&
                decode < (int)(sizeof(decodes) / sizeof(decodes[0])))
                   ? decodes[decode] : "decode";
    snprintf(out, size, "%s", name);
}


void scope_view_model_build(const struct scope_view_model_input *in,
                            struct scope_view_model *out) {
    memset(out, 0, sizeof(*out));

    out->tab = in->tab;
    out->decode = in->decode;
    scope_screen_name(out->screen, sizeof(out->screen), out->tab,
                      out->decode);
    out->have_samples = in->frame->have_samples;

    out->center_hz = in->applied->frequency_hz;
    out->sample_rate_hz = in->applied->sample_rate_hz;
    out->ppm = in->applied->ppm;
    out->tuning_generation = in->applied->generation;

    out->full_scale = in->device->full_scale;
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
                                 out->sample_rate_hz
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
