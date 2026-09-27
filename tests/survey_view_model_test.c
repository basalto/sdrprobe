#include "check.h"

#include "runtime/survey_record.h"
#include "runtime/survey_session.h"
#include "core/survey_suspect.h"
#include "model/survey_view_model.h"

#include <stdio.h>
#include <string.h>

/*
 * The inputs are a `struct survey_session` and the four tuning facts
 * `survey_tuning_from()` gathers -- not a `struct app`. The builder used to
 * take the whole application, so this suite zeroed nine megabytes to set five
 * fields (`.scratch/layer-boundaries/issues/03-*`). The session stays
 * `static`: it carries the swept spectrum and 512 peaks.
 */
static void zero_sweep(struct survey_session *ss,
                       struct survey_record_tuning *t) {
    memset(ss, 0, sizeof(*ss));
    memset(t, 0, sizeof(*t));
}

/* A minimal swept range: 100-102 MHz over 200 bins, 10 kHz each. No
   reference clock and no step plan, so the sweep's own suspicion is
   SURVEY_SUSPECT_NONE unless a test arranges otherwise -- the baseline every
   other test in this file assumes. */
static void set_minimal_sweep(struct survey_session *ss,
                              struct survey_record_tuning *t) {
    ss->lower_hz = 100e6;
    ss->upper_hz = 102e6;
    ss->bins = 200;
    ss->plan.lower_hz = 100e6;
    ss->plan.bin_hz = 10000.0;
    t->sample_rate_hz = 2000000;
    t->remove_dc = 1;
}

static void test_candidate_count_mirrors_peak_count(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 2;
    ss.peaks[0].index = 10;
    ss.peaks[1].index = 50;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("candidate_count mirrors peak_count", svm.candidate_count, 2);
}

static void test_hz_is_the_bin_centre(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 50; /* bin 50 of 200 */

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    /* 100e6 + (50 + 0.5) * 10000 = 100,505,000 Hz. */
    check_close("hz is the swept bin's centre", svm.candidates[0].hz,
                100505000.0, 1.0);
}

static void test_power_dbfs_passes_through(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 0;
    ss.peaks[0].power_dbfs = -23.5f;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_close("power_dbfs passes through", svm.candidates[0].power_dbfs,
                -23.5, 1e-6);
}

static void test_no_carrier_holding_the_peak(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 50;
    /* carrier_count is 0: nothing was ever grouped into a signal here. */

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_true("has_carrier is false with no carriers",
              !svm.candidates[0].has_carrier);
}

/*
 * A peak inside a carrier's extent reads that carrier's centre, width and
 * shape -- not its own frequency, which is what draw_peak_list() used to
 * look up a second time for exactly this reason.
 */
static void test_carrier_holding_the_peak(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 50; /* hz = 100,505,000 */

    ss.carrier_count = 1;
    ss.carriers[0].centre_hz = 100600000.0;
    ss.carriers[0].lower_hz = 100400000.0;
    ss.carriers[0].upper_hz = 100700000.0; /* holds the peak */
    ss.carriers[0].width_hz = 300000.0;    /* medium shape */

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_true("has_carrier is true", svm.candidates[0].has_carrier);
    check_close("carrier_centre_hz is the carrier's own centre",
                svm.candidates[0].carrier_centre_hz, 100600000.0, 1.0);
    check_close("width_hz is the carrier's own width",
                svm.candidates[0].width_hz, 300000.0, 1.0);
    check_int("shape follows the width", (int)svm.candidates[0].shape,
              (int)SURVEY_SHAPE_MEDIUM);
}

/*
 * The sweep's own suspicion: a candidate sitting exactly on the receiver's
 * 14.4 MHz reference comb reads SURVEY_SUSPECT_REFERENCE, with no
 * confirmation pass involved at all.
 */
static void test_flags_carry_the_sweeps_own_suspicion(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    tuning.reference_clock_hz = 28800000.0; /* 14.4 MHz comb spacing */
    ss.lower_hz = 93e6;
    ss.upper_hz = 108e6; /* 100.8 MHz -- 7 x 14.4 MHz -- inside */
    ss.bins = 1500;      /* 10 kHz a bin, same as elsewhere */
    ss.plan.lower_hz = 93e6;
    ss.plan.bin_hz = 10000.0;
    ss.peak_count = 1;
    /* bin centred on 100,805,000, 5 kHz from the multiple -- inside the
       comb's tolerance (25 kHz, wider than the 5 kHz quantisation here). */
    ss.peaks[0].index =
        (int)((100800000.0 - 5000.0 - 93e6) / 10000.0);

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_true("on the reference comb reads SURVEY_SUSPECT_REFERENCE",
              (svm.candidates[0].flags & SURVEY_SUSPECT_REFERENCE) != 0);
}

/*
 * A confirmation pass's own suspicion ORs into the same flag word, asked
 * through the carrier's centre rather than the peak's own frequency when
 * there is a carrier -- the second half of what the two drawings used to
 * compute separately.
 */
static void test_flags_or_in_the_confirmed_verdict_through_the_carrier(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 50; /* hz = 100,505,000 */

    ss.carrier_count = 1;
    ss.carriers[0].centre_hz = 100600000.0;
    ss.carriers[0].lower_hz = 100400000.0;
    ss.carriers[0].upper_hz = 100700000.0;
    ss.carriers[0].width_hz = 40000.0;

    /* Asked about the *carrier's* centre, not the peak's own frequency --
       placing the target at the peak's hz instead would not be found. */
    ss.confirm.count = 1;
    ss.confirm.target[0].hz = 100600000.0;
    ss.confirm.target[0].suspicion = SURVEY_SUSPECT_NO_CARRIER;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_true("the confirmed flag reaches the candidate",
              (svm.candidates[0].flags & SURVEY_SUSPECT_NO_CARRIER) != 0);
}

static void test_seen_is_unknown_with_no_history_loaded(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 0;
    ss.history_loaded = 0;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("seen is SITE_SEEN_UNKNOWN", (int)svm.candidates[0].seen,
              (int)SITE_SEEN_UNKNOWN);
}

static void test_seen_new_with_no_matching_entry(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 50;
    ss.history_loaded = 1;
    ss.history.sweeps = 4; /* history exists, entry does not */

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("an unmatched frequency reads new", (int)svm.candidates[0].seen,
              (int)SITE_SEEN_NEW);
}

/* An entry recorded at exactly the candidate's frequency, in a young history
   (fewer than SITE_ENOUGH_SWEEPS), reads steady -- site_history_seen()'s own
   rule for "not enough history to call it anything finer". */
static void test_seen_reads_the_matching_entry(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.peak_count = 1;
    ss.peaks[0].index = 50; /* hz = 100,505,000 */
    ss.history_loaded = 1;
    ss.history.sweeps = 1;
    ss.history.count = 1;
    ss.history.entries[0].hz = 100505000.0;
    ss.history.entries[0].bin_hz = 10000.0;
    ss.history.entries[0].sweeps = 1;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("a fresh history reads steady, not intermittent",
              (int)svm.candidates[0].seen, (int)SITE_SEEN_STEADY);
}

/*
 * The four fields ticket 07's own comment named as still missing on
 * `survey_candidate_view`'s day: sweep status, sweeping, the range, and the
 * power array. Each read straight off `struct survey_session`, the same
 * source `draw_survey()` used to read directly.
 */
static void test_sweeping_mirrors_the_session_state(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.state = SURVEY_SESSION_SWEEPING;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("sweeping is true while the session is",
              svm.sweeping, 1);
}

static void test_idle_is_not_sweeping(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.state = SURVEY_SESSION_IDLE;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("idle is not sweeping", svm.sweeping, 0);
}

static void test_status_is_copied_verbatim(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    snprintf(ss.status, sizeof(ss.status),
            "Swept 88.000 - 108.000 MHz in 13 steps; 36 candidates found.");

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_str("the window's own status line, unchanged", svm.status,
             "Swept 88.000 - 108.000 MHz in 13 steps; 36 candidates found.");
}

static void test_the_swept_range_passes_through(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_close("lower_hz", svm.lower_hz, 100e6, 1.0);
    check_close("upper_hz", svm.upper_hz, 102e6, 1.0);
}

static void test_the_power_array_is_copied(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;
    int i;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    for (i = 0; i < 200; i++)
        ss.power[i] = -90.0f + (float)i * 0.1f;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("bins mirrors the session's own count", svm.bins, 200);
    check_close("the first bin", (double)svm.power[0], -90.0, 1e-6);
    check_close("a bin in the middle", (double)svm.power[100], -80.0, 1e-6);
    check_close("the last bin filled", (double)svm.power[199], -70.1, 1e-3);
}

/* A survey nobody has swept yet -- SURVEY_BINS is 8192, and `bins` reads 0
   before anything has measured, not garbage from an uninitialised power
   array. Zero bins is the honest report: nothing has been measured. */
static void test_nothing_swept_reads_zero_bins(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("no bins reported", svm.bins, 0);
    check_int("not sweeping", svm.sweeping, 0);
}

/* The cap this ticket added to protect a fixed-size wire message: `bins`
   above SURVEY_VIEW_MODEL_MAX_BINS (== SURVEY_BINS, so this can only be
   reached if the session's own field is ever widened past its buffer) is
   clamped rather than overrunning `power[]`. */
static void test_bins_past_the_cap_are_clamped(void) {
    static struct survey_session ss;
    struct survey_record_tuning tuning;

    zero_sweep(&ss, &tuning);
    set_minimal_sweep(&ss, &tuning);
    ss.bins = SURVEY_VIEW_MODEL_MAX_BINS + 1000;

    struct survey_view_model svm;
    survey_view_model_build(&ss, &tuning, &svm);

    check_int("clamped to the cap", svm.bins, SURVEY_VIEW_MODEL_MAX_BINS);
}

int main(void) {
    test_candidate_count_mirrors_peak_count();
    test_hz_is_the_bin_centre();
    test_power_dbfs_passes_through();
    test_no_carrier_holding_the_peak();
    test_carrier_holding_the_peak();
    test_flags_carry_the_sweeps_own_suspicion();
    test_flags_or_in_the_confirmed_verdict_through_the_carrier();
    test_seen_is_unknown_with_no_history_loaded();
    test_seen_new_with_no_matching_entry();
    test_seen_reads_the_matching_entry();
    test_sweeping_mirrors_the_session_state();
    test_idle_is_not_sweeping();
    test_status_is_copied_verbatim();
    test_the_swept_range_passes_through();
    test_the_power_array_is_copied();
    test_nothing_swept_reads_zero_bins();
    test_bins_past_the_cap_are_clamped();
    return check_report(
        "the survey's candidate view model, built from known inputs");
}
