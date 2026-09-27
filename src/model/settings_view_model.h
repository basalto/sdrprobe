#ifndef SETTINGS_VIEW_MODEL_H
#define SETTINGS_VIEW_MODEL_H

#include <stdint.h>

/*
 * What the Settings panel says, as plain data -- no raylib type anywhere
 * (`web-visualization/17`).
 *
 * This panel is a **form with a commit button**, which is why it is
 * reachable from a browser at all and why ticket 07's "typed input is
 * unsolved" does not describe it: `handle_settings_input()` only *stages*,
 * and `apply_settings()` is one function that validates the whole staged set
 * and applies it. There is no click here to reproduce.
 *
 * **The staged values and the applied ones both travel, and that is the
 * point of the model.** A reader has to be able to see that they have typed
 * 32 into a field whose applied value is still 0 -- the window shows that by
 * having a text field in front of them, and a second reader with no field of
 * their own would otherwise be told only one of the two numbers and have no
 * way to know which.
 *
 * Takes the structs it reads rather than a `const struct app *`, so
 * `check-settings-view-model` links `-lm` alone.
 */

/*
 * What a gain setting is, in the profile's own unit.
 *
 * `device_gain_format()` spells it, because what a step is worth depends on
 * the device: a tuner's 29 discrete tenths of a decibel and an AD9361's
 * gain-table index look alike and are not (`GAIN_UNIT_INDEX`). The formatted
 * string travels rather than the number so a browser cannot decide to call
 * an index "dB".
 */
#define SETTINGS_GAIN_TEXT 64

/* The transform-size line, which says what the choice *costs*: a longer
   transform buys resolution and spends averaging, and a reader who picks
   16384 should be told they have gone from 64 averages to 8 rather than
   discover it as a trace that suddenly looks worse. */
#define SETTINGS_FFT_TEXT 96

struct settings_view_model {
    /* Whether the panel is up. A Viewer follows the window into it the same
       way it follows it into a decode view (ADR-0027). */
    int open;

    /*
     * The staged set: what Apply would do. `ppm` is the field's text rather
     * than a number, because it is text until it parses -- "3-" is a state a
     * reader can be in and no integer represents it.
     */
    char staged_ppm[16];
    int staged_gain_choice;             /* 0 is automatic */
    char staged_gain[SETTINGS_GAIN_TEXT];
    int staged_fft_size;
    char staged_fft[SETTINGS_FFT_TEXT];
    int staged_remove_dc;
    int staged_auto_drift;

    /* And what is in force now. */
    int applied_ppm;
    char applied_gain[SETTINGS_GAIN_TEXT];
    int applied_fft_size;
    int applied_remove_dc;
    int applied_auto_drift;

    /*
     * Whether the two differ -- decided here rather than by each reader
     * comparing five pairs of fields, one of which is a string that has to
     * parse first. A reader acts on this: it is the difference between "the
     * receiver is corrected by 32" and "somebody has typed 32 and not
     * pressed Apply".
     */
    int dirty;

    /* Whether gain can be set at all. A capture has one baked in, so the
       panel says so rather than offering a stepper that does nothing. */
    int gain_adjustable;
    int gain_option_count;

    /* The panel's own line, verbatim -- `apply_settings()` writes it and
       this carries it, so a command's refusal and the window's notice are
       the same sentence. */
    char error[160];
};

struct settings_panel;
struct device_profile;

/*
 * Fills `out`. Reads plain fields only -- no I/O, no raylib call.
 *
 * The applied state is passed rather than read: what the receiver is doing
 * belongs to the receiver, and this panel only stages against it.
 */
void settings_view_model_build(const struct settings_panel *set,
                               const struct device_profile *device,
                               int receiver_mode, int manual_gain,
                               int applied_gain_tenths, int applied_ppm,
                               int applied_fft_size, int applied_remove_dc,
                               int applied_auto_drift,
                               uint32_t applied_sample_rate_hz,
                               struct settings_view_model *out);

#endif
