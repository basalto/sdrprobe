#include "calibration_gate.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * The rule that decides whether a frequency correction may be applied.
 *
 * This is worth checking harder than most things here. A correction accepted
 * too early is wrong by however far the estimate had not settled, and nothing
 * on screen distinguishes it from a good one -- the circle goes green either
 * way, and every frequency the program reports afterwards is off by that
 * amount. ADR-0004 exists because such a correction was once accepted.
 */

static int failures;

static void check_int(const char *name, long actual, long expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %ld, expected %ld\n", name, actual, expected);
        failures++;
    }
}

static void check_close(const char *name, double actual, double expected,
                        double tolerance) {
    double difference = actual - expected;

    if (difference < 0.0)
        difference = -difference;
    if (difference > tolerance) {
        fprintf(stderr, "%s: got %.6f, expected %.6f (+/- %.6f)\n", name,
                actual, expected, tolerance);
        failures++;
    }
}

/* A lock that should pass every clause, for the cases below to spoil one at a
   time. */
static int stable_with(double elapsed, int measurements, int residuals,
                       double sem, int source, float prominence) {
    return calibration_is_stable(elapsed, measurements, residuals, sem, source,
                                 prominence);
}

static void test_a_good_lock_passes(void) {
    check_int("a settled FCCH lock is stable",
              stable_with(20.0, 200, 64, 0.2, CALIBRATION_SOURCE_FCCH, 0.0f),
              1);
    check_int("a settled centroid lock with prominence is stable",
              stable_with(20.0, 200, 64, 0.2, CALIBRATION_SOURCE_CENTROID,
                          14.0f),
              1);
}

/* Each clause, refused on its own. If any of these starts passing, a
   correction is being trusted that has not earned it. */
static void test_each_clause_refuses(void) {
    check_int("too soon",
              stable_with(CALIBRATION_MIN_SECONDS - 0.1, 200, 64, 0.2,
                          CALIBRATION_SOURCE_FCCH, 0.0f),
              0);
    check_int("too few measurements",
              stable_with(20.0, CALIBRATION_MIN_MEASUREMENTS - 1, 64, 0.2,
                          CALIBRATION_SOURCE_FCCH, 0.0f),
              0);
    check_int("too few residuals",
              stable_with(20.0, 200, CALIBRATION_MIN_RESIDUALS - 1, 0.2,
                          CALIBRATION_SOURCE_FCCH, 0.0f),
              0);
    check_int("uncertainty too high",
              stable_with(20.0, 200, 64, CALIBRATION_MAX_SEM_PPM + 0.01,
                          CALIBRATION_SOURCE_FCCH, 0.0f),
              0);
    check_int("a centroid without prominence",
              stable_with(20.0, 200, 64, 0.2, CALIBRATION_SOURCE_CENTROID,
                          CALIBRATION_MIN_PROMINENCE_DB - 0.1f),
              0);
    /* Exactly at each threshold is enough: the clauses are inclusive, and a
       lock that hovers on the boundary should settle rather than flicker. */
    check_int("exactly at the time bound",
              stable_with(CALIBRATION_MIN_SECONDS, CALIBRATION_MIN_MEASUREMENTS,
                          CALIBRATION_MIN_RESIDUALS, CALIBRATION_MAX_SEM_PPM,
                          CALIBRATION_SOURCE_CENTROID,
                          CALIBRATION_MIN_PROMINENCE_DB),
              1);
}

/* Prominence gates the centroid and not the tone, which is the asymmetry a
   careless simplification would remove. */
static void test_prominence_only_gates_the_centroid(void) {
    check_int("a tone with no prominence at all still locks",
              stable_with(20.0, 200, 64, 0.2, CALIBRATION_SOURCE_FCCH, 0.0f),
              1);
    check_int("a centroid with the same prominence does not",
              stable_with(20.0, 200, 64, 0.2, CALIBRATION_SOURCE_CENTROID,
                          0.0f),
              0);
}

static void test_robust_statistics(void) {
    double centre;
    double spread;
    double steady[9] = { 1.0, 1.1, 0.9, 1.0, 1.05, 0.95, 1.0, 1.02, 0.98 };
    double with_outlier[9] = { 1.0, 1.1, 0.9, 1.0, 1.05, 0.95, 1.0, 1.02,
                               40.0 };
    double identical[8] = { 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0, 3.0 };
    double pair[2] = { 2.0, 4.0 };

    robust_center_spread(steady, 9, &centre, &spread);
    check_close("centre of a steady run", centre, 1.0, 0.001);
    if (spread > 0.15) {
        fprintf(stderr, "steady run reported a spread of %.3f\n", spread);
        failures++;
    }

    /* The point of a median and a MAD: one wild block must not move either
       much. A mean would be dragged to 5.3 by that outlier. */
    robust_center_spread(with_outlier, 9, &centre, &spread);
    check_close("one outlier does not move the centre", centre, 1.0, 0.001);
    if (spread > 0.15) {
        fprintf(stderr, "one outlier moved the spread to %.3f\n", spread);
        failures++;
    }

    robust_center_spread(identical, 8, &centre, &spread);
    check_close("identical residuals centre", centre, 3.0, 0.0001);
    check_close("identical residuals have no spread", spread, 0.0, 0.0001);

    robust_center_spread(pair, 2, &centre, &spread);
    check_close("an even count averages the middle pair", centre, 3.0, 0.0001);

    /* Nothing measured yet must not look like a perfect lock. */
    centre = 99.0;
    spread = 99.0;
    robust_center_spread(steady, 0, &centre, &spread);
    check_close("no residuals: centre", centre, 0.0, 0.0001);
    check_close("no residuals: spread", spread, 0.0, 0.0001);
}

/*
 * The standard error is what the gate reads, and it must fall as residuals
 * accumulate -- that is the whole reason the gate waits.
 */
static void test_standard_error(void) {
    check_close("sem of four", calibration_standard_error(2.0, 4), 1.0, 1e-9);
    check_close("sem of sixty-four", calibration_standard_error(2.0, 64), 0.25,
                1e-9);
    check_close("sem with nothing measured",
                calibration_standard_error(2.0, 0), 0.0, 1e-9);
    if (!(calibration_standard_error(2.0, 64) <
          calibration_standard_error(2.0, 16))) {
        fprintf(stderr, "the standard error did not fall with more residuals\n");
        failures++;
    }

    /* The case the gate is built around: per-block scatter far too wide to
       trust, whose centre is nevertheless known well enough after enough
       blocks. */
    double spread = 6.0;
    check_int("a wide spread is not yet trustworthy",
              calibration_standard_error(spread, 16) <= CALIBRATION_MAX_SEM_PPM,
              0);
    check_int("the same spread over a full buffer is",
              calibration_standard_error(spread, 64) <= CALIBRATION_MAX_SEM_PPM,
              1);
}

/*
 * Mixing FCCH and centroid residuals is the bug ADR-0004 exists to prevent:
 * they differ by many PPM, so a buffer holding both has a centre belonging to
 * neither and a spread that can still look tight.
 */
static void test_sources_never_mix(void) {
    check_int("the same source matches",
              calibration_source_matches(CALIBRATION_SOURCE_FCCH,
                                         CALIBRATION_SOURCE_FCCH),
              1);
    check_int("a different source does not",
              calibration_source_matches(CALIBRATION_SOURCE_CENTROID,
                                         CALIBRATION_SOURCE_FCCH),
              0);

    /* What it would cost if they did mix: half a buffer of each, four PPM
       apart, still reports a standard error inside the gate. */
    double mixed[64];
    double centre;
    double spread;

    const double from_fcch = 0.0;
    const double from_centroid = 4.0;

    for (int i = 0; i < 64; i++)
        mixed[i] = (i % 2) ? from_fcch : from_centroid;
    robust_center_spread(mixed, 64, &centre, &spread);

    /* The centre of a mixed buffer sits between the two answers, belonging to
       neither, and its standard error is still inside the gate -- so the lock
       would go green on a correction that is wrong by half the gap. That is
       the hazard, and it is why the buffer is emptied when the source
       changes. If this stops being true, the reset may look unnecessary to
       someone reading the code; the check is here so the reasoning cannot be
       lost. */
    double error = calibration_standard_error(spread, 64);
    double from_either = fabs(centre - from_fcch) < fabs(centre - from_centroid)
                             ? fabs(centre - from_fcch)
                             : fabs(centre - from_centroid);
    if (error > CALIBRATION_MAX_SEM_PPM || from_either < 1.0) {
        fprintf(stderr,
                "a mixed buffer no longer demonstrates the hazard "
                "(centre %.2f, %.2f PPM from the nearer source, sem %.2f); "
                "re-read ADR-0004 before relaxing the source reset\n",
                centre, from_either, error);
        failures++;
    }
}

/* A tone lock rides out the gaps between bursts rather than thrashing. */
static void test_tone_lock_holds_through_gaps(void) {
    check_int("holds on the first miss", calibration_hold_through_miss(1), 1);
    check_int("holds just short of the limit",
              calibration_hold_through_miss(CALIBRATION_FCCH_MISS_LIMIT - 1),
              1);
    check_int("gives up at the limit",
              calibration_hold_through_miss(CALIBRATION_FCCH_MISS_LIMIT), 0);
}

int main(void) {
    test_a_good_lock_passes();
    test_each_clause_refuses();
    test_prominence_only_gates_the_centroid();
    test_robust_statistics();
    test_standard_error();
    test_sources_never_mix();
    test_tone_lock_holds_through_gaps();

    if (failures) {
        fprintf(stderr, "%d calibration gate check(s) failed\n", failures);
        return 1;
    }
    puts("calibration gate checks passed");
    return 0;
}
