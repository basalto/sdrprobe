#include "survey_sweep.h"
#include "check.h"

#include <math.h>
#include <stdio.h>

/*
 * The sweep, step by step, and what measuring one candidate adds up to.
 *
 * None of this is visible when it is wrong, which is why it is worth checking
 * hard. A step plan that leaves a gap between steps hides whatever transmits
 * in the gap and the chart looks exactly as it should; a fold that writes a
 * block into the wrong bin draws a carrier at a frequency nothing is on; a
 * duty of 3/10 and a duty of 3/100 both read "bursty" but mean different
 * things about the transmitter (ADR-0012, layer 1).
 */

#define RATE 2000000.0
#define FFT 2048

static struct survey_plan plan_or_die(const char *what, double from, double to,
                                      double dwell) {
    struct survey_plan plan;
    enum survey_plan_status status =
        survey_plan_make(from, to, RATE, FFT, dwell, &plan);

    check_msg(status == SURVEY_PLAN_OK, "%s: planning refused (%d)\n", what,
              (int)status);
    if (status != SURVEY_PLAN_OK)
        survey_plan_make(88e6, 108e6, RATE, FFT, 0.1, &plan);
    return plan;
}

/* The range the R820T can reach, which is what the view opens on. */
static void test_full_tuner_sweep(void) {
    struct survey_plan p = plan_or_die("full tuner", 24e6, 1766e6, 0.10);

    check_close("step span is the usable part of the rate", p.step_span_hz,
                RATE * SURVEY_USABLE_SPAN, 0.5);
    check_int("bins are capped by the array", p.bins, SURVEY_BINS);
    /* 1742 MHz of range in 1.6 MHz steps. The count has to round up: 1088
       steps would leave the top 0.9 MHz never tuned to. */
    check_int("steps cover the range", p.step_count, 1089);
    check_close("the estimate is steps times settle-plus-dwell", p.seconds,
                1089.0 * (SURVEY_SETTLE_SECONDS + 0.10), 0.001);
}

/*
 * The property the step count exists for: every frequency in the range must
 * fall inside some step's kept middle. This is the one that would have caught
 * a rounded step count, and it is checked over ranges that divide evenly and
 * ranges that do not.
 */
static void test_every_frequency_is_covered(void) {
    const double ranges[][2] = {
        { 24e6, 1766e6 },   /* the whole tuner */
        { 88e6, 108e6 },    /* FM: 12.5 steps, so the last one hangs over */
        { 935e6, 960e6 },   /* GSM 900 downlink */
        { 100e6, 101.6e6 }, /* exactly one step */
        { 100e6, 100.1e6 }, /* narrower than one step */
    };

    for (size_t r = 0; r < sizeof(ranges) / sizeof(*ranges); r++) {
        struct survey_plan p =
            plan_or_die("coverage", ranges[r][0], ranges[r][1], 0.10);
        int gaps = 0;
        double worst = 0.0;

        for (int i = 0; i <= 500; i++) {
            double hz = ranges[r][0] +
                        (ranges[r][1] - ranges[r][0]) * (double)i / 500.0;
            int covered = 0;

            for (int step = 0; step < p.step_count && !covered; step++)
                covered = survey_fold_keeps(
                    hz, survey_plan_step_centre(&p, step), RATE);
            if (!covered) {
                gaps++;
                worst = hz;
            }
        }
        check_msg(gaps == 0,
                  "%.1f-%.1f MHz: %d of 501 frequencies fall in no step "
                  "(e.g. %.3f MHz)\n",
                  ranges[r][0] / 1e6, ranges[r][1] / 1e6, gaps, worst / 1e6);
    }
}

/* Bins: fine where the range is narrow, capped where it is wide, and never
   finer than the FFT that fills them. */
static void test_bin_resolution(void) {
    struct survey_plan wide = plan_or_die("wide", 24e6, 1766e6, 0.10);
    struct survey_plan fm = plan_or_die("fm", 88e6, 108e6, 0.10);
    struct survey_plan narrow = plan_or_die("narrow", 100e6, 100.02e6, 0.10);
    double fft_bin = RATE / (double)FFT;

    check_close("a wide sweep spreads the array over the range", wide.bin_hz,
                (1766e6 - 24e6) / (double)SURVEY_BINS, 1.0);
    check_close("a narrow sweep gets finer bins", fm.bin_hz,
                (108e6 - 88e6) / (double)SURVEY_BINS, 1.0);
    check_msg(fm.bin_hz < wide.bin_hz,
              "the FM sweep's bins (%.0f Hz) are no finer than the full "
              "tuner's (%.0f Hz)\n",
              fm.bin_hz, wide.bin_hz);
    /* 20 kHz over 8192 bins would be 2.4 Hz, far finer than the 976 Hz the
       FFT resolves: interpolated noise dressed as resolution. */
    check_msg(narrow.bin_hz >= fft_bin - 0.5,
              "bins of %.1f Hz are finer than the %.1f Hz the FFT resolves\n",
              narrow.bin_hz, fft_bin);
    check_msg(narrow.bins >= SURVEY_MIN_BINS,
              "a narrow range got only %d bins\n", narrow.bins);
}

/* What the planner must refuse. Each of these reaches the operator as a
   sentence rather than a sweep that quietly does nothing. */
static void test_planning_refusals(void) {
    struct survey_plan p;

    check_int("an inverted range", survey_plan_make(108e6, 88e6, RATE, FFT,
                                                    0.1, &p),
              SURVEY_PLAN_BAD_RANGE);
    check_int("an empty range",
              survey_plan_make(100e6, 100e6, RATE, FFT, 0.1, &p),
              SURVEY_PLAN_BAD_RANGE);
    check_int("a dwell below the floor",
              survey_plan_make(88e6, 108e6, RATE, FFT,
                               SURVEY_DWELL_MIN - 0.001, &p),
              SURVEY_PLAN_BAD_DWELL);
    check_int("a dwell above the ceiling",
              survey_plan_make(88e6, 108e6, RATE, FFT,
                               SURVEY_DWELL_MAX + 0.001, &p),
              SURVEY_PLAN_BAD_DWELL);
    check_int("a sample rate that cannot step",
              survey_plan_make(88e6, 108e6, 1.0, FFT, 0.1, &p),
              SURVEY_PLAN_BAD_RATE);
    /* And the bounds themselves are allowed: a dwell exactly at the floor is
       the default the field opens on. */
    check_int("the floor itself",
              survey_plan_make(88e6, 108e6, RATE, FFT, SURVEY_DWELL_MIN, &p),
              SURVEY_PLAN_OK);
    check_int("the ceiling itself",
              survey_plan_make(88e6, 108e6, RATE, FFT, SURVEY_DWELL_MAX, &p),
              SURVEY_PLAN_OK);
}

/* Folding: which bin a frequency lands in, and what the edges do. */
static void test_fold_mapping(void) {
    struct survey_plan p = plan_or_die("fold", 88e6, 108e6, 0.10);

    check_int("the bottom edge is the first bin", survey_plan_bin_at(&p, 88e6),
              0);
    check_int("below the range is nowhere",
              survey_plan_bin_at(&p, 87.999e6), -1);
    check_int("the top edge is past the last bin",
              survey_plan_bin_at(&p, 108e6), -1);
    check_int("just inside the top is the last bin",
              survey_plan_bin_at(&p, 108e6 - p.bin_hz / 2.0), p.bins - 1);
    check_int("the middle is the middle bin",
              survey_plan_bin_at(&p, 98e6), p.bins / 2);
    /* Every bin must be reachable and no two frequencies a bin apart may land
       in the same one, or part of the array is never written and reads as
       empty spectrum. */
    for (int bin = 0; bin < p.bins; bin += 97) {
        double hz = p.lower_hz + ((double)bin + 0.5) * p.bin_hz;
        check_msg(survey_plan_bin_at(&p, hz) == bin,
                  "the middle of bin %d maps to bin %d\n", bin,
                  survey_plan_bin_at(&p, hz));
    }
}

/* The roll-off: the outer fifth of a step is discarded because the tuner
   reads it low, and the next step covers it properly. */
static void test_fold_discards_the_edges(void) {
    double centre = 100e6;
    double half_kept = RATE * SURVEY_USABLE_SPAN / 2.0;

    check_int("the centre is kept", survey_fold_keeps(centre, centre, RATE), 1);
    check_int("just inside the kept edge",
              survey_fold_keeps(centre + half_kept - 1.0, centre, RATE), 1);
    check_int("just outside it",
              survey_fold_keeps(centre + half_kept + 1.0, centre, RATE), 0);
    check_int("and symmetrically below",
              survey_fold_keeps(centre - half_kept - 1.0, centre, RATE), 0);
    /* The far edge of the tuned span is inside the FFT but outside the fold:
       this is the part that reads low. */
    check_int("the edge of the span is discarded",
              survey_fold_keeps(centre + RATE / 2.0 - 1.0, centre, RATE), 0);
}

/* Peak-hold, which is what makes a dwell worth more than a single block. */
static void test_fold_holds_the_peak(void) {
    check_close("an empty bin takes whatever arrives",
                survey_fold_hold(SURVEY_SENTINEL_DBFS, -90.0f), -90.0, 0.001);
    check_close("a stronger block wins", survey_fold_hold(-90.0f, -40.0f),
                -40.0, 0.001);
    check_close("a weaker one does not", survey_fold_hold(-40.0f, -90.0f),
                -40.0, 0.001);
    check_close("an equal one changes nothing",
                survey_fold_hold(-40.0f, -40.0f), -40.0, 0.001);
    /* The case the sentinel exists for: a real reading below it must still
       replace it, or a very quiet band never fills in. */
    check_close("a reading below the sentinel still fills an empty bin",
                survey_fold_hold(SURVEY_SENTINEL_DBFS,
                                 SURVEY_SENTINEL_DBFS - 10.0f),
                SURVEY_SENTINEL_DBFS - 10.0, 0.001);
}

/* The step machine: settle, dwell, advance, finish. */
static void test_step_phases(void) {
    const double dwell = 0.5;
    const double settle = SURVEY_SETTLE_SECONDS;

    check_int("a block during the settle is discarded",
              survey_step_phase_at(settle / 2.0, dwell, 0, 10),
              SURVEY_STEP_SETTLING);
    check_int("the instant the settle ends, folding starts",
              survey_step_phase_at(settle, dwell, 0, 10),
              SURVEY_STEP_DWELLING);
    check_int("still dwelling near the end",
              survey_step_phase_at(settle + dwell - 0.001, dwell, 0, 10),
              SURVEY_STEP_DWELLING);
    check_int("then the next step",
              survey_step_phase_at(settle + dwell, dwell, 0, 10),
              SURVEY_STEP_NEXT);
    check_int("the last step finishes instead",
              survey_step_phase_at(settle + dwell, dwell, 9, 10),
              SURVEY_STEP_FINISHED);
    /* A single-step sweep is finished by its first dwell, not sent to a step
       that does not exist. */
    check_int("a one-step sweep finishes",
              survey_step_phase_at(settle + dwell, dwell, 0, 1),
              SURVEY_STEP_FINISHED);
    /* A block that arrives very late -- the renderer was busy -- must still
       advance rather than fold into a step whose dwell is long over. */
    check_int("a very late block advances",
              survey_step_phase_at(settle + dwell * 20.0, dwell, 3, 10),
              SURVEY_STEP_NEXT);
}

/* Walking a whole sweep the way update_survey does, to check the two agree
   about how many steps there are and where they sit. */
static void test_a_whole_sweep_walks_the_range(void) {
    struct survey_plan p = plan_or_die("walk", 88e6, 108e6, 0.10);
    double dwell = p.dwell_seconds;
    int step = 0;
    int folds = 0;
    int guard = 0;

    while (step < p.step_count && guard++ < 10000) {
        enum survey_step_phase phase =
            survey_step_phase_at(SURVEY_SETTLE_SECONDS + dwell / 2.0, dwell,
                                 step, p.step_count);
        check_msg(phase == SURVEY_STEP_DWELLING,
                  "mid-dwell of step %d reported phase %d\n", step,
                  (int)phase);
        folds++;
        phase = survey_step_phase_at(SURVEY_SETTLE_SECONDS + dwell, dwell,
                                     step, p.step_count);
        if (phase == SURVEY_STEP_FINISHED)
            break;
        step++;
    }
    check_int("the walk visited every step", folds, p.step_count);
    check_int("and stopped on the last one", step, p.step_count - 1);
    /* The first and last tunings bracket the range: the first step's kept
       middle must reach the bottom edge, the last one's the top. */
    check_msg(survey_fold_keeps(p.lower_hz, survey_plan_step_centre(&p, 0),
                                RATE),
              "the first step does not reach the bottom of the range\n");
    check_msg(survey_fold_keeps(p.upper_hz - 1.0,
                                survey_plan_step_centre(&p, p.step_count - 1),
                                RATE),
              "the last step does not reach the top of the range\n");
}

/* Measuring one candidate over many blocks. */
static void test_measurement_duty(void) {
    struct survey_measurement m;

    survey_measure_reset(&m);
    check_close("nothing measured is no duty", survey_measure_duty(&m), 0.0,
                1e-9);
    check_close("and no spread", survey_measure_spread_hz(&m), 0.0, 1e-9);

    /* A carrier that is always there. */
    for (int i = 0; i < 20; i++)
        check_int("a steady carrier counts every block",
                  survey_measure_observe(&m, 1, 30.0f, 100e6), 1);
    check_close("duty is one", survey_measure_duty(&m), 1.0, 1e-9);
    check_str("and reads as continuous",
              survey_measure_duty_label(survey_measure_duty(&m)),
              "continuous");
    check_close("a carrier that never moved has no spread",
                survey_measure_spread_hz(&m), 0.0, 1.0);

    /* A transmitter that keys up now and then: blocks where nothing was
       characterised still count as looked at, which is what makes the duty a
       duty rather than a count of sightings. */
    survey_measure_reset(&m);
    for (int i = 0; i < 10; i++)
        survey_measure_observe(&m, i < 2, 30.0f, 100e6);
    check_int("blocks looked at", m.blocks, 10);
    check_int("blocks it was up in", m.hits, 2);
    check_close("duty is the fraction", survey_measure_duty(&m), 0.2, 1e-9);
    check_str("and reads as bursty",
              survey_measure_duty_label(survey_measure_duty(&m)), "bursty");
}

/*
 * "Up" is relative to how strong the candidate was when first seen. An
 * absolute threshold cannot work: candidates are found at every level, and the
 * question is whether this one is transmitting now, not whether it is loud.
 */
static void test_up_is_relative_to_the_first_sighting(void) {
    struct survey_measurement strong;
    struct survey_measurement weak;

    survey_measure_reset(&strong);
    survey_measure_observe(&strong, 1, 40.0f, 100e6);
    check_int("half the first prominence still counts",
              survey_measure_observe(&strong, 1, 20.0f, 100e6), 1);
    check_int("below half does not",
              survey_measure_observe(&strong, 1, 19.0f, 100e6), 0);

    /* The same 19 dB block, on a candidate first seen at 12 dB, is the
       candidate being up -- and would be missed by any fixed threshold set
       for the strong one. */
    survey_measure_reset(&weak);
    survey_measure_observe(&weak, 1, 12.0f, 100e6);
    check_int("a weak candidate is judged against itself",
              survey_measure_observe(&weak, 1, 19.0f, 100e6), 1);

    check_int("a block with nothing found is looked at but not a hit",
              survey_measure_observe(&weak, 0, 0.0f, 0.0), 0);
    check_int("and it did not disturb the reference",
              (long)(weak.first_prominence * 10.0f), 120);
}

/* Stability: how far the measured centre wandered. */
static void test_measurement_spread(void) {
    struct survey_measurement m;

    survey_measure_reset(&m);
    survey_measure_observe(&m, 1, 30.0f, 100e6 - 1000.0);
    survey_measure_observe(&m, 1, 30.0f, 100e6 + 1000.0);
    check_close("two readings a kilohertz either side",
                survey_measure_spread_hz(&m), 1000.0, 1.0);

    /* One hit cannot have a spread, and must not report one. */
    survey_measure_reset(&m);
    survey_measure_observe(&m, 1, 30.0f, 100e6);
    check_close("a single reading has no spread", survey_measure_spread_hz(&m),
                0.0, 1e-9);

    /* The rounding case: a carrier that did not move at all, measured at a
       frequency large enough that the sums lose precision. Variance can come
       out slightly negative, and a square root of that is a NaN on screen. */
    survey_measure_reset(&m);
    for (int i = 0; i < 64; i++)
        survey_measure_observe(&m, 1, 30.0f, 1090000000.0);
    check_close("a rock-steady carrier at 1090 MHz reports no spread",
                survey_measure_spread_hz(&m), 0.0, 1.0);
    check_msg(isfinite(survey_measure_spread_hz(&m)),
              "the spread of an unmoving carrier is not a number\n");
}

/*
 * When a step is over.
 *
 * Two conditions, and the second is the one that was missing. The dwell is a
 * floor on how long to listen, not a promise that anything was heard: blocks
 * arrive every 65.5 ms, the settle takes the first of them, and a step can
 * pass through both phases having folded nothing at all. Its bins would then
 * be left unmeasured, which is indistinguishable on the chart from a band
 * with nothing in it.
 */
static void test_leaving_a_step(void) {
    check_true("not while the tuner is settling",
               !survey_step_may_advance(SURVEY_STEP_SETTLING, 0));
    check_true("not even with a block already folded",
               !survey_step_may_advance(SURVEY_STEP_SETTLING, 3));
    check_true("nor during the dwell",
               !survey_step_may_advance(SURVEY_STEP_DWELLING, 3));
    check_true("and not when the dwell is over with nothing measured",
               !survey_step_may_advance(SURVEY_STEP_NEXT, 0));
    check_true("the last step is no exception",
               !survey_step_may_advance(SURVEY_STEP_FINISHED, 0));
    check_true("but yes once the dwell is over and a block was folded",
               survey_step_may_advance(SURVEY_STEP_NEXT, 1));
    check_true("and yes at the end of the sweep",
               survey_step_may_advance(SURVEY_STEP_FINISHED, 1));
}

int main(void) {
    test_full_tuner_sweep();
    test_every_frequency_is_covered();
    test_bin_resolution();
    test_planning_refusals();
    test_fold_mapping();
    test_fold_discards_the_edges();
    test_fold_holds_the_peak();
    test_step_phases();
    test_leaving_a_step();
    test_a_whole_sweep_walks_the_range();
    test_measurement_duty();
    test_up_is_relative_to_the_first_sighting();
    test_measurement_spread();

    return check_report("survey sweep");
}
