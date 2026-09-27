#include "check.h"

#include "model/settings_view_model.h"
#include "core/device_profile.h"
#include "core/sdr_dsp.h"
#include "runtime/app.h"

#include <string.h>

/*
 * What the Settings panel says, decided without a screen.
 *
 * This panel is a form with a commit button, not a chart to hit-test -- the
 * reason it is reachable from a browser at all (`web-visualization/17`). Two
 * things it decides are worth a check. That the **staged** set and the
 * **applied** one are different facts and both travel: a reader has to be
 * able to see that they have typed 32 into a field whose applied value is
 * still 0. And that a gain reads in the *profile's own unit*, because a
 * tuner's tenths of a decibel and an AD9361's gain-table index look alike
 * and are not.
 */

static const int r820t_gains[] = {
    0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280,
    297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496
};

static struct settings_panel set;
static struct device_profile device;
static int receiver_mode = 1;
static int manual_gain;
static int applied_gain_tenths;
static int applied_ppm;
static int applied_fft = SDR_DSP_FFT_SIZE;
static int applied_dc = 1;
static int applied_drift;

static struct settings_view_model build(void) {
    struct settings_view_model out;

    settings_view_model_build(&set, &device, receiver_mode, manual_gain,
                              applied_gain_tenths, applied_ppm, applied_fft,
                              applied_dc, applied_drift, 2000000u, &out);
    return out;
}

static void blank(void) {
    memset(&set, 0, sizeof(set));
    /* A stock R820T's own gain list, in tenths of a decibel -- the unit
       matters, which is half of what this suite is about. */
    device = device_profile_rtlsdr("test", DEVICE_TUNER_R820T, r820t_gains,
                                   (int)(sizeof(r820t_gains) /
                                         sizeof(r820t_gains[0])));
    receiver_mode = 1;
    manual_gain = 0;
    applied_gain_tenths = 0;
    applied_ppm = 0;
    applied_fft = SDR_DSP_FFT_SIZE;
    applied_dc = 1;
    applied_drift = 0;
    /* What `open_settings()` seeds: the field starts at what is applied. */
    snprintf(set.ppm, sizeof(set.ppm), "%d", applied_ppm);
    set.fft_choice = sdr_dsp_fft_choice_of(applied_fft);
    set.remove_dc = applied_dc;
    set.auto_drift = applied_drift;
}

/*
 * A panel freshly opened stages exactly what is applied, so it is not dirty
 * -- opening Settings must not make it look as though somebody had changed
 * something.
 */
static void test_a_fresh_panel_is_not_dirty(void) {
    struct settings_view_model m;

    blank();
    m = build();
    check_int("nothing staged differs", m.dirty, 0);
    check_str("the field holds what is applied", m.staged_ppm, "0");
    check_int("and the sizes agree", m.staged_fft_size, m.applied_fft_size);
}

/*
 * The two columns are different facts.
 *
 * The window shows this by having a text field in front of the reader; a
 * second reader with no field of their own would be told one number and have
 * no way to know which.
 */
static void test_staged_and_applied_are_both_carried(void) {
    struct settings_view_model m;

    blank();
    snprintf(set.ppm, sizeof(set.ppm), "32");
    m = build();
    check_str("what has been typed", m.staged_ppm, "32");
    check_int("what is in force", m.applied_ppm, 0);
    check_int("and that Apply has work to do", m.dirty, 1);

    applied_ppm = 32;
    m = build();
    check_int("applied, and they agree again", m.dirty, 0);
}

/*
 * A field that does not parse counts as dirty.
 *
 * "3-" is a state a reader can be in and no integer represents it. Calling
 * it clean would show a panel that looks settled over a value that cannot be
 * applied -- and `dirty` is what a reader acts on.
 */
static void test_a_field_that_cannot_parse_is_not_settled(void) {
    struct settings_view_model m;

    blank();
    snprintf(set.ppm, sizeof(set.ppm), "3-");
    m = build();
    check_int("half-typed is not applied", m.dirty, 1);

    set.ppm[0] = '\0';
    m = build();
    check_int("nor is empty", m.dirty, 1);
    check_str("and the text travels as text", m.staged_ppm, "");
}

/*
 * Gain in the profile's own unit, and the three cases that are not the same
 * answer: a capture has one baked in, automatic is the receiver choosing,
 * and a chosen step is the device's own number.
 */
static void test_gain_reads_in_the_profiles_own_unit(void) {
    struct settings_view_model m;

    blank();
    m = build();
    check_str("nobody has chosen one", m.staged_gain, "automatic");
    check_int("and there are steps to choose from", m.gain_adjustable, 1);
    check_true("several of them", m.gain_option_count > 1);

    set.gain_choice = 1;
    m = build();
    check_true("a chosen step says a number",
               strcmp(m.staged_gain, "automatic") != 0);
    check_int("which Apply has yet to take", m.dirty, 1);

    /* A capture: not adjustable, which is a different answer from
       automatic, and the stepper is not offered at all. */
    blank();
    receiver_mode = 0;
    m = build();
    check_str("a capture has one baked in", m.staged_gain,
              "capture (not adjustable)");
    check_int("so there is nothing to step", m.gain_adjustable, 0);
    check_int("and no options to offer", m.gain_option_count, 0);
}

/*
 * The transform size says what it costs, because it is a trade rather than
 * an improvement: the window count is `pair_count / size`, so a longer
 * transform buys resolution and spends averaging.
 */
static void test_the_transform_says_what_it_costs(void) {
    struct settings_view_model m;

    blank();
    set.fft_choice = sdr_dsp_fft_choice_of(2048);
    m = build();
    check_int("two thousand and forty-eight points", m.staged_fft_size, 2048);
    check_true("and the line names the bin width and the averaging",
               strstr(m.staged_fft, "2048 points") != NULL &&
               strstr(m.staged_fft, "Hz bins") != NULL &&
               strstr(m.staged_fft, "averaged") != NULL);

    /* Eight times the points is an eighth of the averaging, and the line has
       to say so -- a reader who picks 16384 should be told they have gone
       from 64 averages to 8 rather than discover it as a trace that
       suddenly looks worse. */
    set.fft_choice = sdr_dsp_fft_choice_of(16384);
    m = build();
    check_int("sixteen thousand", m.staged_fft_size, 16384);
    check_true("and eight averages rather than sixty-four",
               strstr(m.staged_fft, "8 averaged") != NULL);
}

/* The panel's own failure line travels verbatim, so a command's refusal and
   the window's notice are the same sentence rather than two wordings of it. */
static void test_the_error_travels_verbatim(void) {
    struct settings_view_model m;

    blank();
    m = build();
    check_str("nothing has failed", m.error, "");

    snprintf(set.error, sizeof(set.error),
             "PPM must be a signed integer from -1000 to 1000");
    m = build();
    check_str("and when it has, in the panel's own words", m.error,
              "PPM must be a signed integer from -1000 to 1000");
}

int main(void) {
    test_a_fresh_panel_is_not_dirty();
    test_staged_and_applied_are_both_carried();
    test_a_field_that_cannot_parse_is_not_settled();
    test_gain_reads_in_the_profiles_own_unit();
    test_the_transform_says_what_it_costs();
    test_the_error_travels_verbatim();
    return check_report("what the Settings panel says, decided without a "
                        "screen");
}
