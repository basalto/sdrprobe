#include "survey_suspect.h"
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
        int harmonic = survey_reference_harmonic(seen[i].hz, tolerance);

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
    check_int("647.9819 MHz, read low", survey_reference_harmonic(647.9819e6,
                                                                  tolerance),
              45);
    check_int("648.0115 MHz, the same tone read high",
              survey_reference_harmonic(648.0115e6, tolerance), 45);

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

    check_close("the comb's tolerance is its floor on this sweep", tolerance,
                RECEIVER_COMB_TOLERANCE_HZ, 1.0);
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
              survey_reference_harmonic(590.4053e6, tolerance), 41);

    /* The other three from the same sweep, which the bin-width tolerance did
       catch, must not stop being recognised. */
    check_int("576.0010", survey_reference_harmonic(576.0010e6, tolerance), 40);
    check_int("604.7998", survey_reference_harmonic(604.7998e6, tolerance), 42);
    check_int("619.2041", survey_reference_harmonic(619.2041e6, tolerance), 43);
    /* 561.5771 is 22.9 kHz below harmonic 39, so the narrow sweep did not mark
       it -- but the wide sweep of 470-690 MHz reported the same tone at
       561.5906 and did. One tone the bin-width tolerance saw only when the
       bins happened to be coarse enough. */
    check_int("561.5771, missed by the bin-width tolerance",
              survey_reference_harmonic(561.5771e6, tolerance), 39);

    /* And the twelve from that sweep that are not on the comb must still not
       be. These are the real false-positive risk of a wider tolerance. */
    {
        const double others[] = { 544.9365e6, 553.2568e6, 569.8975e6,
                                  582.2510e6, 587.0264e6, 588.7256e6,
                                  589.7510e6, 600.0049e6 };
        int flagged = 0;
        double worst = 0.0;

        for (size_t i = 0; i < sizeof(others) / sizeof(*others); i++)
            if (survey_reference_harmonic(others[i], tolerance)) {
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
                RECEIVER_COMB_HZ / RECEIVER_FINE_COMB_HZ, 9.0, 1e-9);
    check_int("244.8 MHz is on the coarse comb",
              survey_reference_harmonic(244.8e6, vhf_tol), 17);
    check_int("and on the fine one",
              survey_fine_harmonic(244.8e6, vhf_tol), 153);
    /* And the eight between them are on the fine comb only, which is the
       whole point: the old test saw one tone in nine. */
    check_int("243.2 MHz is not on the coarse comb",
              survey_reference_harmonic(243.2e6, vhf_tol), 0);
    check_int("but is on the fine one",
              survey_fine_harmonic(243.2e6, vhf_tol), 152);

    /*
     * The confound, measured. 94.4 MHz is 59 x 1.6 and it is the loudest FM
     * station at this site -- a confirmation pass put it 46 dB above its
     * floor. Its extent on an 88-108 MHz sweep was 27 survey bins, 66 kHz.
     */
    check_int("94.4 MHz is on the fine comb",
              survey_fine_harmonic(94.4e6, fm_tol), 59);
    check_int("but it is not narrow",
              survey_is_unresolved(66e3, fm.bin_hz, RATE, FFT), 0);
    check_int("so it is not flagged",
              (survey_suspect(&fm, 94.4e6, 66e3, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              0);
    /* Nor 107.2 MHz (67 x 1.6), 72 bins wide, nor 92.8 (58 x 1.6), 45 bins. */
    check_int("nor 107.2 MHz",
              (survey_suspect(&fm, 107.2e6, 176e3, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              0);
    check_int("nor 92.8 MHz",
              (survey_suspect(&fm, 92.8e6, 110e3, RATE, FFT, 1) &
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
                  survey_fine_harmonic(102.4e6, fine), 64);
        check_int("four bins there is a tone",
                  (survey_suspect(&narrow, 102.4e6, 4.0 * narrow.bin_hz, RATE,
                                  FFT, 1) & SURVEY_SUSPECT_REFERENCE) != 0,
                  1);
        check_int("the same tone at 88-108 MHz's bins is not claimed",
                  (survey_suspect(&fm, 102.4e6, 14.6e3, RATE, FFT, 1) &
                   SURVEY_SUSPECT_REFERENCE) != 0,
                  0);
    }

    /* The tones at 240-270, one and two survey bins wide. */
    check_int("a one-bin comb tone at 243.2 MHz",
              (survey_suspect(&vhf, 243.2e6, vhf.bin_hz, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              1);
    check_int("a two-bin one at 259.2 MHz",
              (survey_suspect(&vhf, 259.2e6, 2.0 * vhf.bin_hz, RATE, FFT, 1) &
               SURVEY_SUSPECT_REFERENCE) != 0,
              1);
    /* A carrier of real width on a fine-comb multiple is left alone. */
    check_int("but a 200 kHz carrier at 246.4 MHz is not",
              (survey_suspect(&vhf, 246.4e6, 200e3, RATE, FFT, 1) &
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
    check_msg(tolerance > RECEIVER_FINE_COMB_HZ * RECEIVER_COMB_MAX_FRACTION,
              "a full-tuner sweep's %.0f Hz tolerance should be too coarse "
              "for a 1.6 MHz comb\n", tolerance);
    check_int("so the fine comb declines to answer",
              survey_fine_harmonic(259.2e6, tolerance), 0);
    /* The coarse comb still answers, because 106 kHz of 14.4 MHz is 1.5%. */
    check_int("while the coarse one still does",
              survey_reference_harmonic(259.2e6, tolerance), 18);

    /* And nothing narrow on a fine-comb multiple is flagged by it either. */
    for (i = 0; i < 1000; i++) {
        double hz = 24e6 + (1766e6 - 24e6) * (double)i / 1000.0;

        if (survey_fine_harmonic(hz, tolerance))
            flagged++;
    }
    check_int("not one frequency in a thousand", flagged, 0);

    /* At a resolution that can place a candidate, it answers again. */
    {
        struct survey_plan band;
        double fine;

        survey_plan_make(240e6, 270e6, RATE, FFT, 0.20, &band);
        fine = survey_comb_tolerance(&band, RATE, FFT);
        check_msg(fine <= RECEIVER_FINE_COMB_HZ * RECEIVER_COMB_MAX_FRACTION,
                  "a 30 MHz sweep's %.0f Hz tolerance should be fine enough\n",
                  fine);
        check_int("259.2 MHz is tone 162", survey_fine_harmonic(259.2e6, fine),
                  162);
        /* The false-hit rate that tolerance buys: 2*25k/1.6M is about 3%. */
        flagged = 0;
        for (i = 0; i < 1000; i++) {
            double hz = 240e6 + 30e6 * (double)i / 1000.0;

            if (survey_fine_harmonic(hz, fine))
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

int main(void) {
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

    return check_report("suspicious candidates");
}
