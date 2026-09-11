#include "survey_suspect.h"
#include "device_profile.h"

/*
 * The RTL2832U's crystal, which this header used to define as
 * RECEIVER_REFERENCE_HZ. It is `device_profile.reference_clock_hz` now, so the
 * suite states the value it is testing against -- and `device_profile_rtlsdr()`
 * is asserted to agree with it below, which is what keeps the two from
 * drifting apart.
 */
#define RTL_REFERENCE_HZ 28800000.0
#include "check.h"

#include <stdio.h>
#include <string.h>

/*
 * Which candidates the survey should warn about.
 *
 * This exists because of a sweep of 470-690 MHz that turned up twelve narrow
 * carriers 20 dB above the floor on exact multiples of 14.4 MHz -- 489.6,
 * 518.4, 547.2, 561.6, 576.0, 590.4, 604.8, 619.2, 633.6, 648.0, 662.4, 676.8
 * -- with the band plan calling every one "UHF television", correctly and
 * uselessly. Unplugging the antenna removed every candidate that was *not* on
 * the comb, and left three of the twelve standing unchanged.
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

/* The comb's tolerance, which is what every check below is about. The
   quantisation-only one is exercised through the step-centre tests. */
static double tolerance_of(const struct survey_plan *plan) {
    return survey_comb_tolerance(plan, RATE, FFT);
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
        int harmonic = survey_reference_harmonic(RTL_REFERENCE_HZ, seen[i].hz, tolerance);

        check_msg(harmonic == seen[i].harmonic,
                  "%.4f MHz: reported harmonic %d, expected %d\n",
                  seen[i].hz / 1e6, harmonic, seen[i].harmonic);
    }

    /*
     * 647.9819 MHz is 18.1 kHz below 648.0, which half a survey bin (13.4 kHz)
     * does not explain -- and for a while this check asserted it was therefore
     * not claimable. Then the same tone came back at 648.0115 in the next
     * sweep of the same range, 11.5 kHz the other way. One tone, two readings
     * 30 kHz apart: the reported frequency is the peak-held maximum bin, not
     * the centre, and noise moves it. Both readings are harmonic 45.
     */
    check_int("647.9819 MHz, read low", survey_reference_harmonic(RTL_REFERENCE_HZ, 647.9819e6,
                                                                  tolerance),
              45);
    check_int("648.0115 MHz, the same tone read high",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 648.0115e6, tolerance), 45);

    /* And the measured refinements of two of them, which land within 400 Hz
       of the exact multiple. */
    check_int("547.2004 MHz measured", survey_reference_harmonic(RTL_REFERENCE_HZ, 547.2004e6,
                                                                 tolerance),
              38);
    check_int("576.0004 MHz measured", survey_reference_harmonic(RTL_REFERENCE_HZ, 576.0004e6,
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
        if (survey_reference_harmonic(RTL_REFERENCE_HZ, channels[i], tolerance)) {
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
              survey_reference_harmonic(RTL_REFERENCE_HZ, 101.5e6, tolerance), 0);
    check_int("949.6 MHz is not on the comb",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 949.6e6, tolerance), 0);
    /* 1090 MHz is not either, which matters: Mode S sits there and a warning
       on it would be read as the receiver inventing aircraft. */
    check_int("1090 MHz is not on the comb",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 1090e6, tolerance), 0);
}

/* The edges of the tolerance, where a comb test either over- or under-reaches
   by half a bin. */
static void test_the_tolerance(void) {
    struct survey_plan plan = uhf_plan();
    double tolerance = tolerance_of(&plan);
    double exact = 40.0 * survey_comb_spacing_hz(RTL_REFERENCE_HZ);

    check_close("the comb's tolerance is its floor on this sweep", tolerance,
                RECEIVER_COMB_TOLERANCE_HZ, 1.0);
    check_int("exactly on the multiple",
              survey_reference_harmonic(RTL_REFERENCE_HZ, exact, tolerance), 40);
    check_int("just inside the tolerance",
              survey_reference_harmonic(RTL_REFERENCE_HZ, exact + tolerance * 0.99, tolerance),
              40);
    check_int("just outside it",
              survey_reference_harmonic(RTL_REFERENCE_HZ, exact + tolerance * 1.01, tolerance),
              0);
    check_int("and below", survey_reference_harmonic(RTL_REFERENCE_HZ, exact - tolerance * 1.01,
                                                     tolerance),
              0);
    /* Zero and negatives are not harmonics of anything. */
    check_int("zero", survey_reference_harmonic(RTL_REFERENCE_HZ, 0.0, tolerance), 0);
    check_int("negative", survey_reference_harmonic(RTL_REFERENCE_HZ, -576e6, tolerance), 0);
    /* Below the first harmonic there is no comb to be on. */
    check_int("7 MHz, under the first multiple",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 7e6, tolerance), 0);
    check_int("14.4 MHz itself is the first",
              survey_reference_harmonic(RTL_REFERENCE_HZ, survey_comb_spacing_hz(RTL_REFERENCE_HZ), tolerance), 1);
}

/*
 * The tolerance floor, and why it is a floor.
 *
 * A narrow sweep has fine bins -- an 80 MHz range gives 9.8 kHz of them, so
 * half a bin is 4.9 kHz. A live sweep of exactly that range reported a comb
 * tone at 590.4053 MHz, 5.3 kHz above the multiple, and a tolerance tied to
 * the bin missed it by four hundred hertz. The floor is what stops the test
 * getting *less* able to recognise the comb the closer it looks.
 */
static void test_the_tolerance_has_a_floor(void) {
    struct survey_plan narrow;
    double tolerance;

    survey_plan_make(540e6, 620e6, RATE, FFT, 0.05, &narrow);
    check_msg(survey_suspect_tolerance(&narrow, RATE, FFT) <
                  RECEIVER_COMB_TOLERANCE_HZ,
              "an 80 MHz sweep's bins are no longer finer than the floor, so "
              "this check is not exercising it\n");
    tolerance = survey_comb_tolerance(&narrow, RATE, FFT);
    check_close("the floor governs", tolerance, RECEIVER_COMB_TOLERANCE_HZ,
                1.0);
    check_int("and 590.4053 MHz is recognised",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 590.4053e6, tolerance), 41);

    /* The other three from the same sweep, which the bin-width tolerance did
       catch, must not stop being recognised. */
    check_int("576.0010", survey_reference_harmonic(RTL_REFERENCE_HZ, 576.0010e6, tolerance), 40);
    check_int("604.7998", survey_reference_harmonic(RTL_REFERENCE_HZ, 604.7998e6, tolerance), 42);
    check_int("619.2041", survey_reference_harmonic(RTL_REFERENCE_HZ, 619.2041e6, tolerance), 43);
    /* 561.5771 is 22.9 kHz below harmonic 39, so the narrow sweep did not mark
       it -- but the wide sweep of 470-690 MHz reported the same tone at
       561.5906 and did. One tone the bin-width tolerance saw only when the
       bins happened to be coarse enough. */
    check_int("561.5771, missed by the bin-width tolerance",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 561.5771e6, tolerance), 39);

    /* And the twelve from that sweep that are not on the comb must still not
       be. These are the real false-positive risk of a wider tolerance. */
    {
        const double others[] = { 544.9365e6, 553.2568e6, 569.8975e6,
                                  582.2510e6, 587.0264e6, 588.7256e6,
                                  589.7510e6, 600.0049e6 };
        int flagged = 0;
        double worst = 0.0;

        for (size_t i = 0; i < sizeof(others) / sizeof(*others); i++)
            if (survey_reference_harmonic(RTL_REFERENCE_HZ, others[i], tolerance)) {
                flagged++;
                worst = others[i];
            }
        check_msg(flagged == 0,
                  "%d of the sweep's real candidates were called harmonics "
                  "(e.g. %.4f MHz)\n",
                  flagged, worst / 1e6);
    }
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
    tolerance = survey_comb_tolerance(&plan, RATE, FFT);
    check_msg(tolerance > 100000.0,
              "a full-tuner sweep should have coarse bins, not %.0f Hz\n",
              tolerance);

    /* A thousand frequencies spread across the tuner: only a few per cent may
       fall on the comb, or the warning is noise. */
    for (int i = 0; i < 1000; i++) {
        double hz = 24e6 + (1766e6 - 24e6) * (double)i / 1000.0;

        if (survey_reference_harmonic(RTL_REFERENCE_HZ, hz, tolerance))
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
    /* A survey measuring out of one tuning: its bins are the transform's. */
    double fine = RATE / (double)FFT;
    double floor_hz = survey_tone_width_hz(fine, RATE, FFT);

    check_close("four bins at 2 MS/s", floor_hz, 3906.25, 0.5);
    /* The 3.9 kHz that every one of those comb tones reported. */
    check_int("3.9 kHz is at the floor",
              survey_is_unresolved(3900.0, fine, RATE, FFT), 1);
    check_int("6 kHz is still within the slack",
              survey_is_unresolved(4800.0, fine, RATE, FFT), 1);
    check_int("20 kHz is a real width",
              survey_is_unresolved(20000.0, fine, RATE, FFT), 0);
    /* An FM station and a DVB-T multiplex are nowhere near it. */
    check_int("a 180 kHz FM signal",
              survey_is_unresolved(180000.0, fine, RATE, FFT), 0);
    check_int("an 8 MHz multiplex",
              survey_is_unresolved(8e6, fine, RATE, FFT), 0);
    /* Nothing measured yet is not a narrow signal. */
    check_int("no measurement", survey_is_unresolved(0.0, fine, RATE, FFT), 0);
    /* No transform is not a resolution, however well the survey binned: a
       configuration that cannot have produced a measurement must not have one
       read out of it. */
    check_int("no FFT", survey_is_unresolved(3900.0, fine, RATE, 0), 0);
    check_close("nor a tone width", survey_tone_width_hz(fine, RATE, 0), 0.0,
                1e-9);

    /*
     * And a swept survey, whose bins are coarser than the transform's. The
     * width comes out of the survey array, so it is quantised to those bins,
     * and judging it against the transform's resolution answers "resolved" for
     * every tone in every swept survey -- which is the case the narrowness
     * observation exists for.
     *
     * These are measurements, not choices. On a 240-270 MHz sweep at 3.66 kHz
     * bins, the comb tones came back one or two bins wide; on an 88-108 MHz
     * sweep at 2.44 kHz bins, the broadcast stations came back twenty-three to
     * seventy-four.
     */
    {
        double coarse = 30e6 / 8192.0;      /* 3.66 kHz */
        check_close("a swept survey resolves no finer than its own bins",
                    survey_tone_width_hz(coarse, RATE, FFT), 4.0 * coarse,
                    1.0);
        check_int("a comb tone, one survey bin wide",
                  survey_is_unresolved(coarse, coarse, RATE, FFT), 1);
        check_int("and at two bins",
                  survey_is_unresolved(2.0 * coarse, coarse, RATE, FFT), 1);
        check_int("a station twenty-three bins wide is not a tone",
                  survey_is_unresolved(23.0 * coarse, coarse, RATE, FFT), 0);
        /* The extent walk hits its bound on a candidate with no -20 dB point
           and reports something enormous. That must read as "not narrow". */
        check_int("an extent that ran to its bound is not a tone",
                  survey_is_unresolved(2049.0 * coarse, coarse, RATE, FFT), 0);
    }
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
    unsigned tone = survey_suspect(&plan, RTL_REFERENCE_HZ, 547.2004e6, 3900.0, RATE, FFT, 1);
    unsigned television = survey_suspect(&plan, RTL_REFERENCE_HZ, 578.0e6, 7.6e6, RATE, FFT, 1);

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
              survey_suspect_count(&plan, RTL_REFERENCE_HZ, peaks, 8, RATE, FFT, 1), 4);
    check_int("an empty sweep has none",
              survey_suspect_count(&plan, RTL_REFERENCE_HZ, peaks, 0, RATE, FFT, 1), 0);
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
        unsigned filtered = survey_suspect(&plan, RTL_REFERENCE_HZ, channels[i], 7.6e6, RATE,
                                           FFT, 1);
        unsigned unfiltered = survey_suspect(&plan, RTL_REFERENCE_HZ, channels[i], 7.6e6, RATE,
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
              survey_suspect_warns(survey_suspect(&plan, RTL_REFERENCE_HZ, 547.2e6, 0.0, RATE,
                                                  FFT, 1)),
              1);
    check_int("and with it off",
              survey_suspect_warns(survey_suspect(&plan, RTL_REFERENCE_HZ, 547.2e6, 0.0, RATE,
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

/*
 * The fine comb, and the confound that kept it out of the survey until it
 * could be told apart from broadcast.
 *
 * Every number here is a measurement from 2026-09-05 and both halves matter.
 * 1.6 MHz is sixteen times the 100 kHz raster that broadcast services sit on,
 * so a frequency test alone flags one FM channel in sixteen -- and flagged
 * candidates are set aside from a report's per-allocation bests, so it would
 * hide a real transmitter rather than a spur.
 */
static void test_the_fine_comb(void) {
    struct survey_plan fm, vhf;
    double fm_tol, vhf_tol;

    /* The two sweeps the measurements came from. */
    survey_plan_make(88e6, 108e6, RATE, FFT, 0.20, &fm);
    survey_plan_make(240e6, 270e6, RATE, FFT, 0.20, &vhf);
    fm_tol = survey_comb_tolerance(&fm, RATE, FFT);
    vhf_tol = survey_comb_tolerance(&vhf, RATE, FFT);

    /* The spacing is a ninth of the coarse comb, and the coarse tones are on
       both -- 244.8 is 17 x 14.4 and 153 x 1.6. */
    check_close("nine fine tones to a coarse one",
                survey_comb_spacing_hz(RTL_REFERENCE_HZ) / survey_fine_comb_spacing_hz(RTL_REFERENCE_HZ), 9.0, 1e-9);
    check_int("244.8 MHz is on the coarse comb",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 244.8e6, vhf_tol), 17);
    check_int("and on the fine one",
              survey_fine_harmonic(RTL_REFERENCE_HZ, 244.8e6, vhf_tol), 153);
    /* And the eight between them are on the fine comb only, which is the
       whole point: the old test saw one tone in nine. */
    check_int("243.2 MHz is not on the coarse comb",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 243.2e6, vhf_tol), 0);
    check_int("but is on the fine one",
              survey_fine_harmonic(RTL_REFERENCE_HZ, 243.2e6, vhf_tol), 152);

    /*
     * The confound, measured. 94.4 MHz is 59 x 1.6 and it is the loudest FM
     * station at this site -- a confirmation pass put it 46 dB above its
     * floor. Its extent on an 88-108 MHz sweep was 27 survey bins, 66 kHz.
     */
    check_int("94.4 MHz is on the fine comb",
              survey_fine_harmonic(RTL_REFERENCE_HZ, 94.4e6, fm_tol), 59);
    check_int("but it is not narrow",
              survey_is_unresolved(66e3, fm.bin_hz, RATE, FFT), 0);
    check_int("so it is not flagged",
              (survey_suspect(&fm, RTL_REFERENCE_HZ, 94.4e6, 66e3, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              0);
    /* Nor 107.2 MHz (67 x 1.6), 72 bins wide, nor 92.8 (58 x 1.6), 45 bins. */
    check_int("nor 107.2 MHz",
              (survey_suspect(&fm, RTL_REFERENCE_HZ, 107.2e6, 176e3, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              0);
    check_int("nor 92.8 MHz",
              (survey_suspect(&fm, RTL_REFERENCE_HZ, 92.8e6, 110e3, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              0);

    /*
     * And the case in the other direction, which is where the resolution
     * decides. 102.4 MHz is 64 x 1.6 and there is a comb tone on it: swept at
     * the transform's own 977 Hz it comes back four bins wide and is flagged
     * `reference,unresolved`. Swept as part of 88-108 MHz, whose bins are
     * 2.44 kHz, the same tone quantises to six bins -- 14.6 kHz, half as wide
     * again as four bins of that sweep -- and is not called a tone.
     *
     * That is the sweep declining to claim, not a miss, and it degrades the
     * same way the frequency test does: a coarse sweep can place a candidate
     * less precisely and can resolve it less finely, so it says less about it.
     * The alternative is a tone rule loose enough to catch a pager.
     */
    {
        struct survey_plan narrow;
        double fine;

        survey_plan_make(101.8e6, 103.4e6, RATE, FFT, 0.30, &narrow);
        fine = survey_comb_tolerance(&narrow, RATE, FFT);
        check_close("a 1.6 MHz sweep bins at the transform's resolution",
                    narrow.bin_hz, RATE / (double)FFT, 1.0);
        check_int("102.4 MHz is on the fine comb",
                  survey_fine_harmonic(RTL_REFERENCE_HZ, 102.4e6, fine), 64);
        check_int("four bins there is a tone",
                  (survey_suspect(&narrow, RTL_REFERENCE_HZ, 102.4e6, 4.0 * narrow.bin_hz, RATE,
                                  FFT, 1) & SURVEY_SUSPECT_REFERENCE) != 0,
                  1);
        check_int("the same tone at 88-108 MHz's bins is not claimed",
                  (survey_suspect(&fm, RTL_REFERENCE_HZ, 102.4e6, 14.6e3, RATE, FFT, 1) &
                   SURVEY_SUSPECT_REFERENCE) != 0,
                  0);
    }

    /* The tones at 240-270, one and two survey bins wide. */
    check_int("a one-bin comb tone at 243.2 MHz",
              (survey_suspect(&vhf, RTL_REFERENCE_HZ, 243.2e6, vhf.bin_hz, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              1);
    check_int("a two-bin one at 259.2 MHz",
              (survey_suspect(&vhf, RTL_REFERENCE_HZ, 259.2e6, 2.0 * vhf.bin_hz, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              1);
    /* A carrier of real width on a fine-comb multiple is left alone. */
    check_int("but a 200 kHz carrier at 246.4 MHz is not",
              (survey_suspect(&vhf, RTL_REFERENCE_HZ, 246.4e6, 200e3, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              0);
}

/*
 * A comb test that cannot place a candidate says nothing rather than saying
 * something one time in eight.
 *
 * The chance a real signal lands within `tolerance` of a multiple is
 * 2*tolerance/spacing. At 14.4 MHz even a full-tuner sweep's 106 kHz half-bin
 * is 1.5%; at 1.6 MHz it is 13%, and a flag that is wrong one candidate in
 * eight is not evidence.
 */
static void test_the_fine_comb_refuses_a_coarse_sweep(void) {
    struct survey_plan wide;
    double tolerance;
    int flagged = 0;
    int i;

    survey_plan_make(24e6, 1766e6, RATE, FFT, 0.10, &wide);
    tolerance = survey_comb_tolerance(&wide, RATE, FFT);
    check_msg(tolerance > survey_fine_comb_spacing_hz(RTL_REFERENCE_HZ) * RECEIVER_COMB_MAX_FRACTION,
              "a full-tuner sweep's %.0f Hz tolerance should be too coarse "
              "for a 1.6 MHz comb\n", tolerance);
    check_int("so the fine comb declines to answer",
              survey_fine_harmonic(RTL_REFERENCE_HZ, 259.2e6, tolerance), 0);
    /* The coarse comb still answers, because 106 kHz of 14.4 MHz is 1.5%. */
    check_int("while the coarse one still does",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 259.2e6, tolerance), 18);

    /* And nothing narrow on a fine-comb multiple is flagged by it either. */
    for (i = 0; i < 1000; i++) {
        double hz = 24e6 + (1766e6 - 24e6) * (double)i / 1000.0;

        if (survey_fine_harmonic(RTL_REFERENCE_HZ, hz, tolerance))
            flagged++;
    }
    check_int("not one frequency in a thousand", flagged, 0);

    /* At a resolution that can place a candidate, it answers again. */
    {
        struct survey_plan band;
        double fine;

        survey_plan_make(240e6, 270e6, RATE, FFT, 0.20, &band);
        fine = survey_comb_tolerance(&band, RATE, FFT);
        check_msg(fine <= survey_fine_comb_spacing_hz(RTL_REFERENCE_HZ) * RECEIVER_COMB_MAX_FRACTION,
                  "a 30 MHz sweep's %.0f Hz tolerance should be fine enough\n",
                  fine);
        check_int("259.2 MHz is tone 162", survey_fine_harmonic(RTL_REFERENCE_HZ, 259.2e6, fine),
                  162);
        /* The false-hit rate that tolerance buys: 2*25k/1.6M is about 3%. */
        flagged = 0;
        for (i = 0; i < 1000; i++) {
            double hz = 240e6 + 30e6 * (double)i / 1000.0;

            if (survey_fine_harmonic(RTL_REFERENCE_HZ, hz, fine))
                flagged++;
        }
        check_msg(flagged <= 50,
                  "%d of 1000 frequencies land on the fine comb by chance\n",
                  flagged);
        check_msg(flagged >= 5,
                  "only %d of 1000 hit the comb, so nothing is being "
                  "exercised\n", flagged);
    }
}

/*
 * Whether a sweep could resolve what it found, which is not the same question
 * as whether the thing is narrow.
 */
static void test_an_extent_needs_its_bin(void) {
    /*
     * The case the survey baseline is built on: 1742 MHz in 8192 bins puts
     * 212.6 kHz in one, and a 25 kHz TETRA carrier occupies a fraction of it.
     * Whatever comes back is a floor.
     */
    const double coarse = 212646.5;
    check_true("a full-tuner sweep cannot resolve a 25 kHz carrier",
               survey_extent_is_floor(25000.0, coarse));
    check_true("nor one of its own bins",
               survey_extent_is_floor(coarse, coarse));
    check_true("nor two",
               survey_extent_is_floor(2.0 * coarse, coarse));
    /* Wide enough to have been measured rather than merely noticed. */
    check_true("but a DVB-T multiplex is wider than its own bins",
               !survey_extent_is_floor(7600000.0, coarse));

    /*
     * The same extent from a sweep that could see it. 148-175 MHz in 8192
     * bins is 3.3 kHz, and 25 kHz across it is real.
     */
    {
        const double fine = 3295.9;
        check_true("a 27 MHz sweep resolves the same 25 kHz carrier",
                   !survey_extent_is_floor(25000.0, fine));
        check_true("and still cannot resolve a bare tone",
                   survey_extent_is_floor(2.0 * fine, fine));
    }

    /* Nothing measured is not something resolved -- the distinction the
       report could not make while every unmeasured width was zero. */
    check_true("no extent is a floor, not a width",
               survey_extent_is_floor(0.0, 212646.5));
    check_true("and no bin width means nothing can be claimed",
               survey_extent_is_floor(25000.0, 0.0));
}


/*
 * The reference comes from the device now, and a device that has not said
 * gets no comb tests at all.
 *
 * This is the capture case and it is the point of the change: whichever
 * receiver recorded a file had a clock, but the file does not, so nothing may
 * attribute a comb to it. `device_profile_capture()` sets
 * `reference_clock_hz` to 0 for exactly this reason.
 */
static void test_no_clock_means_no_comb(void) {
    struct survey_plan plan = uhf_plan();
    double tolerance = tolerance_of(&plan);
    /* A frequency that IS on the RTL's comb: 34 x 14.4 MHz, one of the three
       harmonics the unplug test showed staying put. */
    double on_comb = 34.0 * survey_comb_spacing_hz(RTL_REFERENCE_HZ);

    check_int("on an RTL it is the 34th harmonic",
              survey_reference_harmonic(RTL_REFERENCE_HZ, on_comb, tolerance),
              34);
    check_int("with no clock, it is nothing",
              survey_reference_harmonic(0.0, on_comb, tolerance), 0);
    check_int("and the fine comb says nothing either",
              survey_fine_harmonic(0.0, on_comb, tolerance), 0);
    check_int("a negative reference too",
              survey_reference_harmonic(-1.0, on_comb, tolerance), 0);

    check_int("so the flags carry no receiver suspicion",
              (int)(survey_suspect(&plan, 0.0, on_comb, 0.0, RATE, FFT, 1) &
                    SURVEY_SUSPECT_REFERENCE),
              0);
    check_true("where an RTL's flags do",
               (survey_suspect(&plan, RTL_REFERENCE_HZ, on_comb, 0.0, RATE,
                               FFT, 1) &
                SURVEY_SUSPECT_REFERENCE) != 0);

    /* And the capture profile is the source that has no clock. */
    struct device_profile capture = device_profile_capture(
        "x", SAMPLE_FORMAT_U8, 127.5f, 948.4e6, 2000000);
    check_close("a capture has no reference clock", capture.reference_clock_hz,
                0.0, 1e-9);
    check_int("so a capture flags no comb",
              survey_reference_harmonic(capture.reference_clock_hz, on_comb,
                                        tolerance),
              0);
}

/*
 * A different clock is a different comb, which is the other half of why this
 * is the device's number. 26 MHz is a common SDR reference and shares no
 * harmonic with 28.8 anywhere near these frequencies.
 */
static void test_a_different_clock_is_a_different_comb(void) {
    struct survey_plan plan = uhf_plan();
    double tolerance = tolerance_of(&plan);
    double rtl_comb = 34.0 * survey_comb_spacing_hz(RTL_REFERENCE_HZ);
    double other = 26000000.0;
    double other_comb = 34.0 * survey_comb_spacing_hz(other);

    check_close("14.4 MHz from 28.8", survey_comb_spacing_hz(RTL_REFERENCE_HZ),
                14400000.0, 1.0);
    check_close("13.0 MHz from 26.0", survey_comb_spacing_hz(other),
                13000000.0, 1.0);
    check_close("1.6 MHz fine, from 28.8",
                survey_fine_comb_spacing_hz(RTL_REFERENCE_HZ), 1600000.0, 1.0);

    check_int("the RTL's harmonic is not the other's",
              survey_reference_harmonic(other, rtl_comb, tolerance), 0);
    check_int("and the other's is not the RTL's",
              survey_reference_harmonic(RTL_REFERENCE_HZ, other_comb,
                                        tolerance),
              0);
    check_int("each finds its own", survey_reference_harmonic(other, other_comb,
                                                              tolerance),
              34);
}

/*
 * The suite states 28.8 MHz and the profile supplies it. Pinning them together
 * is what stops the two drifting apart now that the header no longer defines
 * the constant.
 */
static void test_the_profile_supplies_the_reference(void) {
    struct device_profile rtl = device_profile_rtlsdr(NULL, NULL, 0);
    check_close("the RTL profile's reference is what this suite tests",
                rtl.reference_clock_hz, RTL_REFERENCE_HZ, 0.5);
    check_close("and the comb is it halved",
                survey_comb_spacing_hz(rtl.reference_clock_hz), 14400000.0,
                1.0);
}


/*
 * The second kind of evidence: where a candidate reads, rather than which
 * multiple it is near. `.scratch/device-model/issues/11-*`.
 *
 * A comb flag is a coincidence argument -- this frequency is a multiple of
 * that spacing. This is a cancellation argument, shares no arithmetic with it,
 * and so can corroborate *or* contradict it. Both directions are asserted
 * here, because a test that only ever agrees with the comb is not a second
 * opinion.
 *
 * The airband pass of 2026-09-11 is the fixture throughout: a narrow sweep
 * binning at about 2 kHz, and a receiver measured at -30.8 to -35.96 ppm.
 */
#define AIRBAND_BIN_HZ 1953.125          /* a 128-137 MHz sweep in 4096 bins */
/*
 * The crystal's own error, which is the *negation* of the residual
 * `cal-measure` prints: `--calibrate gsm --arfcn 113` reported
 * `observed_ppm -31.84` and `suggested_ppm 32` on 2026-09-11, so the reference
 * is **fast** by 31.84 ppm and an uncorrected reading of a real transmitter
 * comes back low. Ticket 11 took the printed residual for the error and had
 * every displacement the wrong way round; see
 * `tests/reading_origin_test.c`.
 */
#define AIRBAND_PPM 31.84

/* Uncorrected, as every reading in that pass was. The crystal's error is what
   separates the two hypotheses; the correction only decides which of them
   reads on the nominal. */
static struct reading_clock raw_clock(void) {
    struct reading_clock clock = { AIRBAND_PPM, 0.0 };
    return clock;
}

static struct survey_plan airband_plan(void) {
    struct survey_plan plan;

    memset(&plan, 0, sizeof(plan));
    plan.lower_hz = 128e6;
    plan.upper_hz = 136e6;
    plan.bins = 4096;
    plan.bin_hz = AIRBAND_BIN_HZ;
    plan.step_count = 4;
    plan.step_span_hz = 2e6;
    return plan;
}

/* One comb tone, its own bin, and the reading the pass actually got. */
/* No service raster: the comb grids alone, which is most of the spectrum. */
static const struct survey_raster no_raster = { 0.0, 0.0 };
/* And the airband's, the one grid the band plan knows. */
static const struct survey_raster airband_raster = { 25000.0 / 3.0,
                                                     118000000.0 };

static unsigned origin_at(double hz, struct reading_clock clock, double bin_hz,
                          unsigned base) {
    return survey_suspect_origin(base, RTL_REFERENCE_HZ, hz, clock,
                                 survey_coherent_tolerance(bin_hz), no_raster,
                                 0.0);
}

static void test_a_comb_tone_reads_exact(void) {
    /*
     * 129.600159 MHz is 14.4 x 9 and read +159 Hz from it. At -30.8 ppm an
     * external transmitter on that multiple would read about 3993 Hz high, so
     * +159 is not a near miss, it is the cancellation.
     */
    unsigned flags = origin_at(129600159.0, raw_clock(), AIRBAND_BIN_HZ,
                               SURVEY_SUSPECT_UNRESOLVED);

    check_int("129.600159 reads exact: clocked here",
              (flags & SURVEY_SUSPECT_CLOCK_COHERENT) != 0, 1);
    check_int("and is not also called unexplained",
              (flags & SURVEY_SUSPECT_UNEXPLAINED) != 0, 0);
    check_msg(survey_suspect_reason(flags | SURVEY_SUSPECT_REFERENCE) != NULL,
              "comb plus coherence produced no sentence\n");

    /* The fine comb too: 136.000793 is 1.6 x 85, read +793 Hz. */
    check_int("136.000793 on the fine comb reads exact",
              (origin_at(136000793.0, raw_clock(), AIRBAND_BIN_HZ,
                         SURVEY_SUSPECT_UNRESOLVED) &
               SURVEY_SUSPECT_CLOCK_COHERENT) != 0,
              1);
}

/*
 * And the contradiction, which is the half that makes it worth having.
 *
 * 94.4 MHz is 1.6 x 59 and is also the loudest FM station at this site,
 * confirmed at 46 dB. The comb flags it, correctly by their own lights and
 * wrongly about the world. A real transmitter there reads displaced -- 2907 Hz
 * high at -30.8 ppm -- so the coherence test does not flag it, and says
 * EXTERNAL positively for a caller that asks.
 *
 * **At a confirmation pass's resolution and not at a sweep's**, and that was
 * written the other way round here and failed. 2907 Hz of displacement wants
 * twice the tolerance to clear it, so a sweep binning at 1953 Hz cannot
 * separate the hypotheses at 94 MHz and correctly says nothing; a pass binning
 * at 977 Hz can. Both are asserted, because "the sweep says nothing" is the
 * refusal working rather than the test failing -- and the displacement grows
 * with frequency while a bin does not, which is why the airband candidates
 * forty megahertz higher are reachable from a sweep and this one is not.
 */
#define PASS_BIN_HZ (RATE / (double)FFT)          /* 977 Hz at 2 MS/s */

static void test_a_real_station_on_the_comb_is_not_flagged(void) {
    double displaced = reading_external_hz(94400000.0, raw_clock());
    unsigned flags;

    check_close("a station on the comb reads about 3.0 kHz low",
                displaced - 94400000.0, -3005.0, 5.0);
    check_int("a band II sweep cannot separate the two there",
              (int)survey_suspect_origin_at(
                  RTL_REFERENCE_HZ, displaced, raw_clock(),
                  survey_coherent_tolerance(AIRBAND_BIN_HZ),
                  no_raster, 0.0),
              (int)READING_ORIGIN_UNKNOWN);
    /* As the real callers compose it: `c->suspect |= survey_suspect_origin(
       c->suspect, ...)`, so the word carries both what the grids said and what
       the reading said. */
    flags = SURVEY_SUSPECT_UNRESOLVED | SURVEY_SUSPECT_REFERENCE;
    flags |= origin_at(displaced, raw_clock(), PASS_BIN_HZ, flags);
    check_int("so it is not called clocked here",
              (flags & SURVEY_SUSPECT_CLOCK_COHERENT) != 0, 0);
    check_int("and a pass says so positively",
              (int)survey_suspect_origin_at(
                  RTL_REFERENCE_HZ, displaced, raw_clock(),
                  survey_coherent_tolerance(PASS_BIN_HZ),
                  no_raster, 0.0),
              (int)READING_ORIGIN_EXTERNAL);
    /*
     * And the operator is told. `SURVEY_SUSPECT_DISPLACED` is the half of
     * this that reaches a screen; without it the verdict was computed and
     * thrown away, which is a feature that works and says nothing.
     */
    check_int("which sets the displaced flag",
              (flags & SURVEY_SUSPECT_DISPLACED) != 0, 1);
    check_int("beside the comb flag, not instead of it",
              (flags & SURVEY_SUSPECT_REFERENCE) != 0, 1);
    check_str("and the sentence leads with the contradiction",
              survey_suspect_reason(flags),
              "on the receiver's comb, but reads displaced: something real "
              "is here");
    /*
     * The accepted cost, asserted so nobody "fixes" it by accident: such a
     * candidate still counts as suspicious. The decision was to add evidence
     * rather than clear a mark, and `survey_suspect_contested()` is what a
     * caller reports the other number with.
     */
    check_int("it still warns, which is the cost of not suppressing",
              survey_suspect_warns(flags), 1);
    check_int("and is separately reportable as contested",
              survey_suspect_contested(flags), 1);
    check_int("where a plain comb tone is not contested",
              survey_suspect_contested(SURVEY_SUSPECT_REFERENCE |
                                       SURVEY_SUSPECT_CLOCK_COHERENT),
              0);
    check_int("nor is a displaced signal that was never comb-flagged",
              survey_suspect_contested(SURVEY_SUSPECT_DISPLACED), 0);
    /* The comb flag itself is untouched: this adds evidence, it does not
       silently overrule a mark the operator has learned to read. */
    check_int("while the comb flag stands",
              (survey_suspect(&(struct survey_plan){ .lower_hz = 88e6,
                                                     .upper_hz = 108e6,
                                                     .bins = 4096,
                                                     .bin_hz = 4882.8 },
                              RTL_REFERENCE_HZ, 94400000.0, 16000.0, RATE, FFT,
                              1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              1);
}

/*
 * 150.0009 MHz: confirmed 6 of 6 at 37.7 dB, 70% of the channel standing
 * still, on no multiple of 14.4 or 1.6, and filed under "Mobile-satellite
 * uplink". It was indexed exactly like a frequency nobody had asked about.
 */
static void test_a_bare_carrier_nothing_explains(void) {
    unsigned flags = origin_at(150000900.0, raw_clock(), AIRBAND_BIN_HZ,
                               SURVEY_SUSPECT_UNRESOLVED);

    check_int("150.0009 is on neither comb",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 150000900.0,
                                        RECEIVER_COMB_TOLERANCE_HZ) |
                  survey_fine_harmonic(RTL_REFERENCE_HZ, 150000900.0, 2000.0),
              0);
    check_int("and is reported unexplained rather than clean",
              (flags & SURVEY_SUSPECT_UNEXPLAINED) != 0, 1);
    check_int("not as the receiver, which would need a grid containing it",
              (flags & SURVEY_SUSPECT_CLOCK_COHERENT) != 0, 0);
    check_msg(survey_suspect_reason(flags) != NULL,
              "an unexplained bare carrier has no sentence\n");

    /* Narrow, because an ordinary modulated service on no comb is not a
       mystery and flagging every one of them would empty the flag of
       meaning. */
    check_int("a modulated signal on no comb is not unexplained",
              (origin_at(150000900.0, raw_clock(), AIRBAND_BIN_HZ,
                         SURVEY_SUSPECT_NONE) &
               SURVEY_SUSPECT_UNEXPLAINED) != 0,
              0);

    /*
     * **And there has to be a carrier**, which the first live run of this flag
     * found out the expensive way. A noise maximum is narrow, so it carries
     * `UNRESOLVED` exactly as a tone does, and a 128-137 MHz sweep turned five
     * refuted peaks at 1.9 to 4.4 dB into "unexplained bare carriers" -- a
     * false warning in the one direction that costs an operator their trust in
     * the line, and one no unit check was asking about. `NO_CARRIER` is the
     * confirmation pass's own answer to that question.
     */
    /*
     * And `displaced` needs something to be positive about, for the same
     * reason and found the same way: a live sweep produced
     * `confirm 128569641 new refuted 2.2 0/6 977 unresolved,displaced`, which
     * claims a real signal with its own oscillator at a frequency found in
     * none of six looks. The verdict gate is in `survey_session.c`; this is
     * the property it enforces.
     */
    check_int("a refused reading claims no oscillator",
              (origin_at(150000900.0, raw_clock(), AIRBAND_BIN_HZ,
                         SURVEY_SUSPECT_UNRESOLVED |
                             SURVEY_SUSPECT_NO_CARRIER) &
               SURVEY_SUSPECT_DISPLACED) != 0,
              0);
    check_int("a noise maximum the pass found empty is not unexplained",
              (origin_at(150000900.0, raw_clock(), AIRBAND_BIN_HZ,
                         SURVEY_SUSPECT_UNRESOLVED |
                             SURVEY_SUSPECT_NO_CARRIER) &
               SURVEY_SUSPECT_UNEXPLAINED) != 0,
              0);
    /* Nor is one the comb already accounts for. */
    check_int("nor is one on the comb",
              (origin_at(129600159.0, raw_clock(), AIRBAND_BIN_HZ,
                         SURVEY_SUSPECT_UNRESOLVED |
                             SURVEY_SUSPECT_REFERENCE) &
               SURVEY_SUSPECT_UNEXPLAINED) != 0,
              0);
}

/*
 * The refusals, which are three different situations that must produce the
 * same silence -- and one of them is the opposite of what "calibrate first"
 * would suggest.
 */
static void test_without_a_measured_crystal_it_says_nothing(void) {
    unsigned bare = SURVEY_SUSPECT_UNRESOLVED;
    struct reading_clock unmeasured = { 0.0, 0.0 };

    /* An uncalibrated receiver: nobody has measured the error, so nothing can
       be concluded from where anything reads. */
    check_int("no measured crystal, no verdict",
              origin_at(129600159.0, unmeasured, AIRBAND_BIN_HZ, bare), 0u);
    /* A capture, which has no clock at all: the comb machinery already
       refuses, and this must too. */
    check_int("no reference clock, no verdict",
              survey_suspect_origin(bare, 0.0, 129600159.0, raw_clock(),
                                    survey_coherent_tolerance(AIRBAND_BIN_HZ),
                                    no_raster, 0.0),
              0u);

    /*
     * And the case that is **not** a refusal, which the first version of this
     * got wrong and which made the whole measurement dead code in the shipping
     * program: a *correctly calibrated* receiver. The program restores a
     * stored calibration at startup and applies it, so `crystal - applied` is
     * then zero -- and taking that one number as the input meant the flag
     * could never be set on any receiver, calibrated or not, while this suite
     * stayed green because a unit hands the number in.
     *
     * The separation is the crystal's own error and the correction does not
     * touch it. What the correction changes is which hypothesis reads on the
     * nominal, so the *same* comb frequency now reads displaced -- which is
     * why the correction has to be an input rather than an assumption.
     */
    {
        struct reading_clock calibrated = { AIRBAND_PPM, AIRBAND_PPM };
        double tol = survey_coherent_tolerance(PASS_BIN_HZ);

        check_int("a calibrated receiver can still be asked",
                  reading_origin_separable(129600000.0, calibrated, tol), 1);
        check_int("the old reading would now be called external",
                  (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, 129600159.0,
                                                calibrated, tol, no_raster, 0.0),
                  (int)READING_ORIGIN_EXTERNAL);
        check_int("and the reading it would actually get is coherent",
                  (int)survey_suspect_origin_at(
                      RTL_REFERENCE_HZ,
                      reading_coherent_hz(129600000.0, calibrated), calibrated,
                      tol, no_raster, 0.0),
                  (int)READING_ORIGIN_RECEIVER);
    }

    /* A coarse sweep cannot separate the hypotheses however good the ppm is:
       the whole tuner in 8192 bins is 212 kHz, wanting 424 kHz of
       separation. */
    check_int("and a whole-tuner sweep is too coarse to ask",
              origin_at(129600159.0, raw_clock(), 212000.0, bare), 0u);
    check_int("while the narrow sweep that raised the question is not",
              origin_at(129600159.0, raw_clock(), AIRBAND_BIN_HZ, bare) != 0,
              1);
}

/*
 * The tolerance is one bin and not the comb's, and this is the assertion that
 * keeps somebody from "simplifying" the two into one constant. Borrowing
 * RECEIVER_COMB_TOLERANCE_HZ does not loosen this test, it abolishes it.
 */
static void test_the_comb_tolerance_would_abolish_this(void) {
    unsigned bare = SURVEY_SUSPECT_UNRESOLVED;

    check_int("at the comb's 25 kHz nothing is separable below 1.6 GHz",
              reading_origin_separable(150000000.0, raw_clock(),
                                       RECEIVER_COMB_TOLERANCE_HZ),
              0);
    check_int("so every verdict would be silence",
              survey_suspect_origin(bare, RTL_REFERENCE_HZ, 129600159.0,
                                    raw_clock(), RECEIVER_COMB_TOLERANCE_HZ,
                                    no_raster, 0.0),
              0u);
    check_int("where one bin answers",
              origin_at(129600159.0, raw_clock(), AIRBAND_BIN_HZ, bare) != 0,
              1);
    check_close("one bin is the pass's own measured precision",
                survey_coherent_tolerance(RATE / (double)FFT), 976.5625, 0.01);
}

/* And the whole judgement over one plan, so the flags are seen to compose. */
static void test_the_airband_candidates(void) {
    struct survey_plan plan = airband_plan();
    unsigned comb = survey_suspect(&plan, RTL_REFERENCE_HZ, 129600159.0,
                                   2.0 * AIRBAND_BIN_HZ, RATE, FFT, 1);

    check_int("the comb tone is narrow", (comb & SURVEY_SUSPECT_UNRESOLVED) != 0,
              1);
    check_int("and on the comb", (comb & SURVEY_SUSPECT_REFERENCE) != 0, 1);
    comb |= origin_at(129600159.0, raw_clock(), AIRBAND_BIN_HZ, comb);
    check_int("and reads exact", (comb & SURVEY_SUSPECT_CLOCK_COHERENT) != 0,
              1);
    check_int("which warns", survey_suspect_warns(comb), 1);
    check_str("with both facts in one sentence",
              survey_suspect_reason(comb),
              "on the receiver's reference comb, and reads exact: clocked "
              "here");

    /* Coherence alone warns too: it is stronger evidence than the comb, not
       weaker, so a candidate carrying only it must not read as clean. */
    check_int("coherence alone is a warning",
              survey_suspect_warns(SURVEY_SUSPECT_CLOCK_COHERENT), 1);
    /* Unexplained is not a warning: it says nobody knows, not "beware". */
    check_int("unexplained is not a warning",
              survey_suspect_warns(SURVEY_SUSPECT_UNEXPLAINED), 0);
}


/*
 * The service's own grid, which is what makes `unexplained` mean "no comb
 * *and* no channel" rather than only the first half.
 *
 * The airband is the one allocation the band plan knows a raster for, and it
 * is also the one with a measurement behind it: 132.062744 MHz is the only
 * external carrier this site has ever recorded.
 */
static void test_a_service_raster_explains_what_the_comb_cannot(void) {
    struct reading_clock rx = raw_clock();
    double tol = survey_coherent_tolerance(PASS_BIN_HZ);
    unsigned bare = SURVEY_SUSPECT_UNRESOLVED;

    /*
     * On no comb, and on a real channel -- so it is a signal with an
     * oscillator of its own, said positively rather than by elimination.
     */
    check_int("132.062744 is on no comb",
              survey_reference_harmonic(RTL_REFERENCE_HZ, 132062744.0,
                                        RECEIVER_COMB_TOLERANCE_HZ) |
                  survey_fine_harmonic(RTL_REFERENCE_HZ, 132062744.0, 2000.0),
              0);
    check_int("and with no raster it is merely unexplained",
              (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, 132062744.0, rx,
                                            tol, no_raster, 0.0),
              (int)READING_ORIGIN_UNEXPLAINED);
    check_int("with the airband's raster it is an external signal",
              (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, 132062744.0, rx,
                                            tol, airband_raster, 0.0),
              (int)READING_ORIGIN_EXTERNAL);
    check_int("so it is flagged displaced rather than unexplained",
              survey_suspect_origin(bare, RTL_REFERENCE_HZ, 132062744.0, rx,
                                    tol, airband_raster, 0.0),
              SURVEY_SUSPECT_DISPLACED);
    check_int("where without the raster it would read unexplained",
              survey_suspect_origin(bare, RTL_REFERENCE_HZ, 132062744.0, rx,
                                    tol, no_raster, 0.0),
              SURVEY_SUSPECT_UNEXPLAINED);

    /*
     * **The raster answers the external hypothesis only.** A channel grid
     * says where a transmitter may sit; "a tone clocked by this receiver that
     * happens to land on an airband channel" is not a hypothesis anybody
     * holds, and at 8333 Hz spacing with a 977 Hz tolerance it would fire by
     * chance 23% of the time.
     *
     * Found on air: a 128-152 MHz sweep produced
     * `confirm 134758789 new refuted 2.2 0/6 977 unresolved,clocked-here` --
     * a noise maximum found in none of six looks, called a tone clocked by
     * this receiver, because its measured centre lands 433 Hz from where a
     * coherent source on airband channel 1990 would read.
     */
    {
        /* A reading exactly where a *coherent* source on a channel would be.
           The comb would call that RECEIVER; the raster must not. */
        double channel = reading_nearest_channel_hz(134587207.0, 118000000.0,
                                                    25000.0 / 3.0);
        double as_coherent = reading_coherent_hz(channel, rx);

        check_true("the coherent reading of a channel is a real frequency",
                   as_coherent > 0.0);
        check_int("and the raster does not call it the receiver's",
                  (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, as_coherent,
                                                rx, tol, airband_raster, 0.0),
                  (int)READING_ORIGIN_UNEXPLAINED);
        /* While the same reading on a comb multiple still is. */
        check_int("where a comb multiple still answers both ways",
                  (int)survey_suspect_origin_at(
                      RTL_REFERENCE_HZ, reading_coherent_hz(136000000.0, rx),
                      rx, tol, no_raster, 0.0),
                  (int)READING_ORIGIN_RECEIVER);
    }

    /*
     * And a raster too fine to resolve names channels by rounding, so it is
     * refused rather than believed -- at a pass's 977 Hz that is anything
     * under about 2 kHz.
     */
    {
        struct survey_raster fine = { 1000.0, 118000000.0 };

        check_int("a 1 kHz raster is not consulted",
                  (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, 132062744.0,
                                                rx, tol, fine, 0.0),
                  (int)READING_ORIGIN_UNEXPLAINED);
    }
}


/*
 * The octave chain, which is the gap this whole flag was opened about.
 *
 * 150.0009 MHz: confirmed 6 of 6 at 37.7 dB, 70% of the channel standing
 * still, on no multiple of 14.4 or 1.6 MHz, filed by the band plan under
 * "Mobile-satellite uplink". Before `clock_chain.h` the best this could say
 * was `unexplained`, which was the right answer to a question that had not
 * been asked properly. With the family measured, it is the receiver's.
 */
static void test_the_octave_chain_closes_the_gap(void) {
    struct reading_clock rx = raw_clock();
    double tol = survey_coherent_tolerance(AIRBAND_BIN_HZ);
    unsigned bare = SURVEY_SUSPECT_UNRESOLVED;
    const double chain = CLOCK_CHAIN_MEASURED_FUNDAMENTAL_HZ;

    check_int("with no chain modelled it is unexplained",
              (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, 150000900.0, rx,
                                            tol, no_raster, 0.0),
              (int)READING_ORIGIN_UNEXPLAINED);
    check_int("and with the measured chain it is the receiver's",
              (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, 150000900.0, rx,
                                            tol, no_raster, chain),
              (int)READING_ORIGIN_RECEIVER);
    check_int("so it is flagged clocked-here rather than unexplained",
              survey_suspect_origin(bare, RTL_REFERENCE_HZ, 150000900.0, rx,
                                    tol, no_raster, chain),
              SURVEY_SUSPECT_CLOCK_COHERENT);

    /*
     * **225 MHz must not be**, and this is the assertion that keeps the model
     * from quietly becoming a harmonic comb. It is 75 x 3, it was swept and
     * found empty at a 12 dB bar, and a harmonic model would claim it.
     */
    check_int("an odd multiple is not on the chain",
              (int)survey_suspect_origin_at(RTL_REFERENCE_HZ, 225000000.0, rx,
                                            tol, no_raster, chain),
              (int)READING_ORIGIN_UNEXPLAINED);

    /* And a capture, which passes no fundamental, is told nothing -- the same
       refusal it gets from the comb, for the same reason. */
    check_int("a source with no clock gets no chain test",
              survey_suspect_origin(bare, 0.0, 150000900.0, rx, tol,
                                    no_raster, 0.0),
              0u);
}

int main(void) {
    test_no_clock_means_no_comb();
    test_a_different_clock_is_a_different_comb();
    test_the_profile_supplies_the_reference();
    test_an_extent_needs_its_bin();
    test_the_comb_that_was_measured();
    test_real_signals_are_left_alone();
    test_the_tolerance();
    test_the_tolerance_has_a_floor();
    test_a_coarse_sweep_stays_selective();
    test_step_centres();
    test_unresolved_width();
    test_what_warns();
    test_the_measured_candidates();
    test_counting_a_sweep();
    test_the_step_centre_test_is_gated();
    test_bin_centre_round_trip();

    test_the_fine_comb();
    test_the_fine_comb_refuses_a_coarse_sweep();

    test_a_comb_tone_reads_exact();
    test_a_real_station_on_the_comb_is_not_flagged();
    test_a_bare_carrier_nothing_explains();
    test_without_a_measured_crystal_it_says_nothing();
    test_the_comb_tolerance_would_abolish_this();
    test_the_airband_candidates();
    test_a_service_raster_explains_what_the_comb_cannot();
    test_the_octave_chain_closes_the_gap();

    return check_report("suspicious candidates");
}
