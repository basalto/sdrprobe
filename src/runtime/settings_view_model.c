#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "model/settings_view_model.h"
#include "core/device_profile.h"
#include "core/sdr_dsp.h"
#include "runtime/acquisition.h"
#include "runtime/app.h"

/*
 * The Settings panel's staged set, what is applied, and whether they differ.
 *
 * In `runtime/` and not beside the model it fills: a builder reads the
 * application's state and a contract may not (ADR-0028).
 */

/* One gain, in the profile's own unit. `choice` is the panel's, where 0 is
   automatic and the rest index the device's options. */
static void settings_gain_text(const struct device_profile *device,
                               int receiver_mode, int choice, char *out,
                               size_t cap) {
    if (!receiver_mode) {
        /* A capture has a gain baked in and no way to change it, which is a
           different answer from "automatic". */
        snprintf(out, cap, "capture (not adjustable)");
        return;
    }
    if (choice <= 0) {
        snprintf(out, cap, "automatic");
        return;
    }
    device_gain_format(device, device_gain_option_value(device, choice - 1),
                       out, cap);
}

/*
 * The transform size and what choosing it costs.
 *
 * Both numbers, because it is a trade rather than an improvement: the window
 * count is `pair_count / size`, so a longer transform buys resolution and
 * spends averaging.
 */
static void settings_fft_text(int size, uint32_t rate_hz, char *out,
                              size_t cap) {
    double rate = (double)rate_hz;
    int windows = size > 0 ? (int)(SAMPLE_BLOCK_PAIRS / (size_t)size) : 0;

    if (size > 0 && rate > 0.0)
        snprintf(out, cap, "%d points   %.0f Hz bins   %d averaged", size,
                 rate / size, windows);
    else
        snprintf(out, cap, "%d points", size);
}

void settings_view_model_build(const struct settings_panel *set,
                               const struct device_profile *device,
                               int receiver_mode, int manual_gain,
                               int applied_gain_tenths, int applied_ppm,
                               int applied_fft_size, int applied_remove_dc,
                               int applied_auto_drift,
                               uint32_t applied_sample_rate_hz,
                               struct settings_view_model *out) {
    int applied_choice = 0;
    int staged_ppm = 0;
    int staged_ppm_parsed;

    memset(out, 0, sizeof(*out));

    out->open = set->open;
    out->gain_adjustable = receiver_mode;
    out->gain_option_count = receiver_mode
                                 ? device_gain_option_count(device) : 0;

    /* --- the staged set, which is what Apply would do ------------------ */
    snprintf(out->staged_ppm, sizeof(out->staged_ppm), "%s", set->ppm);
    out->staged_gain_choice = set->gain_choice;
    settings_gain_text(device, receiver_mode, set->gain_choice,
                       out->staged_gain, sizeof(out->staged_gain));
    out->staged_fft_size = sdr_dsp_fft_choice(set->fft_choice);
    settings_fft_text(out->staged_fft_size, applied_sample_rate_hz,
                      out->staged_fft, sizeof(out->staged_fft));
    out->staged_remove_dc = set->remove_dc;
    out->staged_auto_drift = set->auto_drift;

    /* --- and what is in force ------------------------------------------ */
    out->applied_ppm = applied_ppm;
    /*
     * The applied gain as a *choice*, so the same spelling serves both
     * columns: `open_settings()` does this search to seed the stepper, and
     * doing it differently here would let the two disagree about what the
     * receiver is on.
     */
    if (receiver_mode && manual_gain) {
        int count = device_gain_option_count(device);
        int i;

        for (i = 0; i < count; i++)
            if (device_gain_option_value(device, i) == applied_gain_tenths) {
                applied_choice = i + 1;
                break;
            }
    }
    settings_gain_text(device, receiver_mode, applied_choice,
                       out->applied_gain, sizeof(out->applied_gain));
    out->applied_fft_size = applied_fft_size;
    out->applied_remove_dc = applied_remove_dc;
    out->applied_auto_drift = applied_auto_drift;

    snprintf(out->error, sizeof(out->error), "%s", set->error);

    /*
     * Whether Apply would change anything.
     *
     * Decided here rather than by each reader comparing five pairs, one of
     * which is a string that has to parse first -- and a field that does
     * *not* parse counts as dirty, because it is certainly not what is
     * applied and a reader who typed it should be told Apply has work to do
     * rather than shown a panel that looks settled.
     */
    staged_ppm_parsed = sscanf(set->ppm, "%d", &staged_ppm) == 1;
    out->dirty = !staged_ppm_parsed ||
                 staged_ppm != applied_ppm ||
                 out->staged_gain_choice != applied_choice ||
                 out->staged_fft_size != applied_fft_size ||
                 out->staged_remove_dc != applied_remove_dc ||
                 out->staged_auto_drift != applied_auto_drift;
}
