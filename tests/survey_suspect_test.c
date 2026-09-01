#include "survey_suspect.h"
#include "check.h"

#include <stdio.h>
#include <string.h>

/*
 * Which candidates the survey should warn about.
 *
 * This exists because of a sweep of 470-690 MHz taken with the antenna
 * disconnected: the DVB-T multiplexes vanished and a comb of narrow carriers
 * 20 dB above the floor did not. Nearly all of them were exact multiples of
 * 14.4 MHz -- 489.6, 518.4, 547.2, 576.0, 590.4, 604.8, 633.6, 662.4 -- and
 * the band plan was calling every one "UHF television", correctly and
 * uselessly.
 *
 * The two failure directions matter differently. A missed warning costs an
 * operator an afternoon chasing a transmitter that does not exist; a false one
 * costs them their trust in the line, after which the warning may as well not
 * be there. The checks below push on both (ADR-0012, layer 1).
 */

#define RATE 2000000.0
#define FFT 2048

static struct survey_plan uhf_plan(void) {
    struct survey_plan plan;

    survey_plan_make(470e6, 690e6, RATE, FFT, 0.10, &plan);
    return plan;
}

static double tolerance_of(const struct survey_plan *plan) {
    return survey_suspect_tolerance(plan, RATE, FFT);
}

/*
 * The comb that was actually measured, at the frequencies it was measured at.
 * These are the survey's own bin centres, so they are up to half a bin off the
 * exact multiple -- which is the whole reason the test takes a tolerance.
 */
static void test_the_comb_that_was_measured(void) {
    struct survey_plan plan = uhf_plan();
    double tolerance = tolerance_of(&plan);
    const struct { double hz; int harmonic; } seen[] = {
        { 489.6104e6, 34 }, { 518.3925e6, 36 }, { 547.1929e6, 38 },
        { 576.0130e6, 40 }, { 590.3896e6, 41 }, { 604.8021e6, 42 },
        { 633.6037e6, 44 }, { 662.4130e6, 46 },
    };

    for (size_t i = 0; i < sizeof(seen) / sizeof(*seen); i++) {
        int harmonic = survey_reference_harmonic(seen[i].hz, tolerance);

        check_msg(harmonic == seen[i].harmonic,
                  "%.4f MHz: reported harmonic %d, expected %d\n",
                  seen[i].hz / 1e6, harmonic, seen[i].harmonic);
    }

    /*
     * 647.9819 MHz was in the same list and is deliberately *not* here. It is
     * 18 kHz below 648.0, and half a survey bin is 13 -- so bin quantisation
     * does not explain it and the comb test rightly does not claim it. Widening
     * the tolerance to swallow it would buy one uncertain candidate and cost
     * the selectivity the whole warning rests on.
     */
    check_int("647.9819 MHz is too far off the comb to claim",
              survey_reference_harmonic(647.9819e6, tolerance), 0);

    /* And the measured refinements of two of them, which land within 400 Hz
       of the exact multiple. */
    check_int("547.2004 MHz measured", survey_reference_harmonic(547.2004e6,
                                                                 tolerance),
              38);
    check_int("576.0004 MHz measured", survey_reference_harmonic(576.0004e6,
                                                                 tolerance),
              40);
}

/* Frequencies that are not on the comb must not be dragged onto it. */
static void test_real_signals_are_left_alone(void) {
    struct survey_plan plan = uhf_plan();
    double tolerance = tolerance_of(&plan);
    /* DVB-T channel centres in this range: 474 + 8n. None coincides with a
       14.4 MHz multiple, which is what makes the warning usable here. */
    const double channels[] = { 474e6, 482e6, 490e6, 498e6, 506e6, 514e6,
                                522e6, 530e6, 538e6, 546e6, 554e6, 562e6,
                                570e6, 578e6, 586e6, 594e6, 602e6, 610e6,
                                618e6, 626e6, 634e6, 642e6, 650e6, 658e6,
                                666e6, 674e6, 682e6 };
    int flagged = 0;
    double worst = 0.0;

    for (size_t i = 0; i < sizeof(channels) / sizeof(*channels); i++) {
        if (survey_reference_harmonic(channels[i], tolerance)) {
            flagged++;
            worst = channels[i];
        }
    }
    check_msg(flagged == 0,
              "%d DVB-T channel centres were called reference harmonics "
              "(e.g. %.1f MHz)\n",
              flagged, worst / 1e6);

    /* Nor the FM band's stations, nor the GSM downlink grid. */
    check_int("101.5 MHz is not on the comb",
              survey_reference_harmonic(101.5e6, tolerance), 0);
    check_int("949.6 MHz is not on the comb",
              survey_reference_harmonic(949.6e6, tolerance), 0);
    /* 1090 MHz is not either, which matters: Mode S sits there and a warning
       on it would be read as the receiver inventing aircraft. */
    check_int("1090 MHz is not on the comb",
              survey_reference_harmonic(1090e6, tolerance), 0);
}

/* The edges of the tolerance, where a comb test either over- or under-reaches
   by half a bin. */
static void test_the_tolerance(void) {
    struct survey_plan plan = uhf_plan();
    double tolerance = tolerance_of(&plan);
    double exact = 40.0 * RECEIVER_COMB_HZ;

    check_msg(tolerance > 1000.0 && tolerance < 100000.0,
              "a tolerance of %.0f Hz is not half a survey bin\n", tolerance);
    check_int("exactly on the multiple",
              survey_reference_harmonic(exact, tolerance), 40);
    check_int("just inside the tolerance",
              survey_reference_harmonic(exact + tolerance * 0.99, tolerance),
              40);
    check_int("just outside it",
              survey_reference_harmonic(exact + tolerance * 1.01, tolerance),
              0);
    check_int("and below", survey_reference_harmonic(exact - tolerance * 1.01,
                                                     tolerance),
              0);
    /* Zero and negatives are not harmonics of anything. */
    check_int("zero", survey_reference_harmonic(0.0, tolerance), 0);
    check_int("negative", survey_reference_harmonic(-576e6, tolerance), 0);
    /* Below the first harmonic there is no comb to be on. */
    check_int("7 MHz, under the first multiple",
              survey_reference_harmonic(7e6, tolerance), 0);
    check_int("14.4 MHz itself is the first",
              survey_reference_harmonic(RECEIVER_COMB_HZ, tolerance), 1);
}

/*
 * A wide sweep has coarse bins and so a wide tolerance. Even at the full
 * tuner's 213 kHz bins the comb test must stay selective: 213 kHz either side
 * of a 14.4 MHz spacing is under 3% of the band.
 */
static void test_a_coarse_sweep_stays_selective(void) {
    struct survey_plan plan;
    double tolerance;
    int flagged = 0;

    survey_plan_make(24e6, 1766e6, RATE, FFT, 0.10, &plan);
    tolerance = survey_suspect_tolerance(&plan, RATE, FFT);
    check_msg(tolerance > 100000.0,
              "a full-tuner sweep should have coarse bins, not %.0f Hz\n",
              tolerance);

    /* A thousand frequencies spread across the tuner: only a few per cent may
       fall on the comb, or the warning is noise. */
    for (int i = 0; i < 1000; i++) {
        double hz = 24e6 + (1766e6 - 24e6) * (double)i / 1000.0;

        if (survey_reference_harmonic(hz, tolerance))
            flagged++;
    }
    check_msg(flagged <= 50, "%d of 1000 frequencies were flagged\n", flagged);
    check_msg(flagged >= 1, "none of 1000 frequencies hit the comb, so the "
                            "test is not exercising anything\n");
}

/* The DC offset at the middle of every survey step. */
static void test_step_centres(void) {
    struct survey_plan plan = uhf_plan();
    double tolerance = tolerance_of(&plan);

    for (int step = 0; step < plan.step_count; step += 7) {
        double centre = survey_plan_step_centre(&plan, step);

        check_msg(survey_at_step_centre(&plan, centre, tolerance),
                  "step %d's own centre (%.4f MHz) was not recognised\n", step,
                  centre / 1e6);
    }
    /* Between two steps is not a step centre: that is where the fold hands
       over from one tuning to the next, and nothing lives there. */
    check_int("halfway between two steps",
              survey_at_step_centre(&plan,
                                    survey_plan_step_centre(&plan, 3) +
                                        plan.step_span_hz / 2.0,
                                    tolerance),
              0);
    check_int("well outside the swept range",
              survey_at_step_centre(&plan, 100e6, tolerance), 0);
    /* A plan with no steps cannot have a step centre, and must not divide by
       its span to find out. */
    {
        struct survey_plan empty;
        memset(&empty, 0, sizeof(empty));
        check_int("an unplanned sweep", survey_at_step_centre(&empty, 500e6,
                                                              1000.0),
                  0);
    }
}

/* Unresolvable narrowness: what a bare carrier measures at. */
static void test_unresolved_width(void) {
    double floor_hz = survey_resolution_hz(RATE, FFT);

    check_close("four bins at 2 MS/s", floor_hz, 3906.25, 0.5);
    /* The 3.9 kHz that every one of those comb tones reported. */
    check_int("3.9 kHz is at the floor", survey_is_unresolved(3900.0, RATE,
                                                              FFT),
              1);
    check_int("6 kHz is still within the slack",
              survey_is_unresolved(4800.0, RATE, FFT), 1);
    check_int("20 kHz is a real width", survey_is_unresolved(20000.0, RATE,
                                                             FFT),
              0);
    /* An FM station and a DVB-T multiplex are nowhere near it. */
    check_int("a 180 kHz FM signal", survey_is_unresolved(180000.0, RATE, FFT),
              0);
    check_int("an 8 MHz multiplex", survey_is_unresolved(8e6, RATE, FFT), 0);
    /* Nothing measured yet is not a narrow signal. */
    check_int("no measurement", survey_is_unresolved(0.0, RATE, FFT), 0);
    check_int("no FFT", survey_is_unresolved(3900.0, RATE, 0), 0);
}

/*
 * What earns a warning, and what does not. Narrowness alone must not: a pager
 * on 466 MHz, a telemetry link, a beacon are all legitimately this narrow, and
 * warning about them would teach the operator to ignore the line -- after
 * which the comb warning is worthless too.
 */
static void test_what_warns(void) {
    check_int("nothing", survey_suspect_warns(SURVEY_SUSPECT_NONE), 0);
    check_int("narrow alone does not warn",
              survey_suspect_warns(SURVEY_SUSPECT_UNRESOLVED), 0);
    check_int("the comb warns",
              survey_suspect_warns(SURVEY_SUSPECT_REFERENCE), 1);
    check_int("a step centre warns",
              survey_suspect_warns(SURVEY_SUSPECT_STEP_CENTRE), 1);
    check_int("and both together",
              survey_suspect_warns(SURVEY_SUSPECT_REFERENCE |
                                   SURVEY_SUSPECT_UNRESOLVED),
              1);

    /* Every warning has words, and a non-warning has none to print. */
    check_msg(survey_suspect_reason(SURVEY_SUSPECT_REFERENCE) != NULL,
              "the comb warning has no sentence\n");
    check_msg(survey_suspect_reason(SURVEY_SUSPECT_STEP_CENTRE) != NULL,
              "the step-centre warning has no sentence\n");
    check_msg(survey_suspect_reason(SURVEY_SUSPECT_REFERENCE |
                                    SURVEY_SUSPECT_STEP_CENTRE) != NULL,
              "the combined warning has no sentence\n");
    check_msg(survey_suspect_reason(SURVEY_SUSPECT_UNRESOLVED) == NULL,
              "narrowness alone produced a warning sentence\n");
    check_msg(survey_suspect_reason(SURVEY_SUSPECT_NONE) == NULL,
              "a clean candidate produced a warning sentence\n");
}

/* The whole judgement, on the two candidates from the disconnected sweep. */
static void test_the_measured_candidates(void) {
    struct survey_plan plan = uhf_plan();
    unsigned tone = survey_suspect(&plan, 547.2004e6, 3900.0, RATE, FFT, 1);
    unsigned television = survey_suspect(&plan, 578.0e6, 7.6e6, RATE, FFT, 1);

    check_int("the 547.2 MHz tone is on the comb",
              (tone & SURVEY_SUSPECT_REFERENCE) != 0, 1);
    check_int("and unresolvably narrow",
              (tone & SURVEY_SUSPECT_UNRESOLVED) != 0, 1);
    check_int("so it warns", survey_suspect_warns(tone), 1);

    check_int("a real multiplex is on no comb",
              (television & SURVEY_SUSPECT_REFERENCE) != 0, 0);
    check_int("and is not narrow",
              (television & SURVEY_SUSPECT_UNRESOLVED) != 0, 0);
    check_int("so it does not warn", survey_suspect_warns(television), 0);
}

/* Counting them across a sweep, the way the candidate list's header does. */
static void test_counting_a_sweep(void) {
    struct survey_plan plan = uhf_plan();
    struct sdr_peak peaks[8];
    const double frequencies[8] = {
        489.6e6,  /* comb */
        474.0e6,  /* DVB-T channel 21 */
        547.2e6,  /* comb */
        578.0e6,  /* DVB-T channel 34 */
        604.8e6,  /* comb */
        610.0e6,  /* DVB-T channel 38 */
        633.6e6,  /* comb */
        682.0e6   /* DVB-T channel 48 */
    };

    memset(peaks, 0, sizeof(peaks));
    for (int i = 0; i < 8; i++) {
        peaks[i].index = survey_plan_bin_at(&plan, frequencies[i]);
        check_msg(peaks[i].index >= 0, "%.1f MHz is outside the plan\n",
                  frequencies[i] / 1e6);
    }
    check_int("four of the eight look like the receiver",
              survey_suspect_count(&plan, peaks, 8, RATE, FFT, 1), 4);
    check_int("an empty sweep has none",
              survey_suspect_count(&plan, peaks, 0, RATE, FFT, 1), 0);
}

/*
 * Why the step-centre test is gated on the DC-spike filter.
 *
 * Steps are 1.6 MHz apart and DVB-T channels are 8 MHz apart -- exactly five
 * steps -- so every channel centre in this band sits on a step centre. Applied
 * unconditionally the test would warn about all of them, which is worse than
 * useless: it is the warning crying wolf on precisely the signals the operator
 * came for. With the filter on there is no DC offset to land there, so the
 * test is not applied and the collision cannot happen.
 */
static void test_the_step_centre_test_is_gated(void) {
    struct survey_plan plan = uhf_plan();
    const double channels[4] = { 474e6, 578e6, 610e6, 682e6 };
    double tolerance = tolerance_of(&plan);
    int collisions = 0;

    for (int i = 0; i < 4; i++)
        if (survey_at_step_centre(&plan, channels[i], tolerance))
            collisions++;
    check_msg(collisions == 4,
              "only %d of 4 DVB-T channel centres collide with a step centre, "
              "so this check is no longer demonstrating the hazard\n",
              collisions);

    for (int i = 0; i < 4; i++) {
        unsigned filtered = survey_suspect(&plan, channels[i], 7.6e6, RATE,
                                           FFT, 1);
        unsigned unfiltered = survey_suspect(&plan, channels[i], 7.6e6, RATE,
                                             FFT, 0);

        check_msg(!survey_suspect_warns(filtered),
                  "%.0f MHz warns with the DC filter on\n",
                  channels[i] / 1e6);
        check_msg(survey_suspect_warns(unfiltered),
                  "%.0f MHz does not warn with the DC filter off, where the "
                  "offset really is sitting on it\n",
                  channels[i] / 1e6);
    }

    /* The comb test is not gated: a reference harmonic is there whatever the
       filter is doing. */
    check_int("the comb warns with the filter on",
              survey_suspect_warns(survey_suspect(&plan, 547.2e6, 0.0, RATE,
                                                  FFT, 1)),
              1);
    check_int("and with it off",
              survey_suspect_warns(survey_suspect(&plan, 547.2e6, 0.0, RATE,
                                                  FFT, 0)),
              1);
}

/*
 * The round trip the counting depends on: a peak's bin index must map back to
 * the frequency it was found at, within half a bin. If it does not, the comb
 * test is being applied to the wrong frequency and every answer above is
 * meaningless.
 */
static void test_bin_centre_round_trip(void) {
    struct survey_plan plan = uhf_plan();
    int wrong = 0;

    for (int bin = 0; bin < plan.bins; bin += 53) {
        double hz = survey_plan_bin_centre(&plan, bin);

        if (survey_plan_bin_at(&plan, hz) != bin)
            wrong++;
    }
    check_int("every bin centre maps back to its bin", wrong, 0);
    check_close("and the first bin's centre is half a bin in",
                survey_plan_bin_centre(&plan, 0) - plan.lower_hz,
                plan.bin_hz / 2.0, 1.0);
}

int main(void) {
    test_the_comb_that_was_measured();
    test_real_signals_are_left_alone();
    test_the_tolerance();
    test_a_coarse_sweep_stays_selective();
    test_step_centres();
    test_unresolved_width();
    test_what_warns();
    test_the_measured_candidates();
    test_counting_a_sweep();
    test_the_step_centre_test_is_gated();
    test_bin_centre_round_trip();

    return check_report("suspicious candidates");
}
