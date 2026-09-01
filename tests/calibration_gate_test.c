#include "calibration_gate.h"
#include "check.h"

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
    check_msg(spread <= 0.15, "steady run reported a spread of %.3f\n", spread);

    /* The point of a median and a MAD: one wild block must not move either
       much. A mean would be dragged to 5.3 by that outlier. */
    robust_center_spread(with_outlier, 9, &centre, &spread);
    check_close("one outlier does not move the centre", centre, 1.0, 0.001);
    check_msg(spread <= 0.15, "one outlier moved the spread to %.3f\n", spread);

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
    check_msg(calibration_standard_error(2.0, 64) <
                  calibration_standard_error(2.0, 16),
              "the standard error did not fall with more residuals\n");

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
    check_msg(error <= CALIBRATION_MAX_SEM_PPM && from_either >= 1.0,
              "a mixed buffer no longer demonstrates the hazard "
              "(centre %.2f, %.2f PPM from the nearer source, sem %.2f); "
              "re-read ADR-0004 before relaxing the source reset\n",
              centre, from_either, error);
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

/* ---- the state machine around the gate ------------------------------ */

/*
 * A block arrives; the tracker says what it may contribute. These are the
 * transitions the gate depends on -- it can only be as good as the buffer it
 * reads, and keeping that buffer honest is what this machine does.
 */
static void test_the_source_machine(void) {
    struct calibration_tracker t;

    calibration_tracker_init(&t);
    check_int("nothing found yet, and no carrier",
              calibration_track(&t, 0, 0), CALIBRATION_NOTHING);
    check_int("still on the centroid", t.source, CALIBRATION_SOURCE_CENTROID);

    check_int("a carrier and no tone is a centroid measurement",
              calibration_track(&t, 0, 1), CALIBRATION_USE_CENTROID);
    check_int("and no tone lock", t.fcch_locked, 0);

    check_int("a tone takes over", calibration_track(&t, 1, 1),
              CALIBRATION_USE_FCCH);
    check_int("the source is the tone", t.source, CALIBRATION_SOURCE_FCCH);
    check_int("and the lock is on", t.fcch_locked, 1);
    check_int("hits counted", t.fcch_hits, 1);
}

/*
 * The mixing hazard, from the other side: switching to the tone must empty
 * the buffer. A buffer holding both sources has a centre belonging to neither
 * and a standard error tight enough to pass the gate -- the correction then
 * goes green while being wrong by half the gap (ADR-0004).
 */
static void test_switching_source_empties_the_buffer(void) {
    struct calibration_tracker t;

    calibration_tracker_init(&t);
    for (int i = 0; i < 40; i++) {
        calibration_track(&t, 0, 1);
        calibration_tracker_observe(&t, 4.0 + 0.01 * (i % 3));
    }
    check_int("forty centroid residuals", t.recent_count, 40);
    check_close("centred on the centroid's answer", t.recent_center, 4.0, 0.05);

    check_int("a tone arrives", calibration_track(&t, 1, 1),
              CALIBRATION_USE_FCCH);
    check_int("the buffer is empty", t.recent_count, 0);
    check_int("the measurement count restarts", t.measurements, 0);
    check_close("and nothing is left of the old centre", t.recent_center, 0.0,
                1e-9);
    check_int("and the lock is not carried over", t.stable, 0);

    /* And back the other way, after the miss limit. */
    calibration_tracker_observe(&t, 0.1);
    for (int i = 0; i < CALIBRATION_FCCH_MISS_LIMIT - 1; i++)
        check_msg(calibration_track(&t, 0, 1) == CALIBRATION_HOLD_TONE,
                  "miss %d did not hold the tone lock\n", i + 1);
    check_int("the tone residual survived the gap", t.recent_count, 1);
    check_int("the limit gives up the lock", calibration_track(&t, 0, 1),
              CALIBRATION_USE_CENTROID);
    check_int("back on the centroid", t.source, CALIBRATION_SOURCE_CENTROID);
    check_int("with an empty buffer", t.recent_count, 0);
}

/*
 * While the lock holds through a gap, nothing is recorded at all -- not even
 * the centroid that is sitting right there. Recording it is exactly the
 * mixing above, and GSM sends an FCCH only ten times a multiframe, so these
 * gaps are the normal case rather than an edge one.
 */
static void test_a_gap_records_nothing(void) {
    struct calibration_tracker t;

    calibration_tracker_init(&t);
    calibration_track(&t, 1, 1);
    calibration_tracker_observe(&t, 0.5);
    check_int("one tone residual", t.recent_count, 1);

    for (int i = 0; i < CALIBRATION_FCCH_MISS_LIMIT - 1; i++) {
        enum calibration_action action = calibration_track(&t, 0, 1);
        check_msg(action == CALIBRATION_HOLD_TONE,
                  "a burst-free block returned %d, not a hold\n",
                  (int)action);
        check_msg(t.source == CALIBRATION_SOURCE_FCCH,
                  "the source changed during a gap\n");
    }
    check_int("and the buffer still holds only the tone residual",
              t.recent_count, 1);

    /* The tone comes back before the limit: the lock never broke, the buffer
       was never emptied, and the miss counter starts again. */
    check_int("the tone returns", calibration_track(&t, 1, 1),
              CALIBRATION_USE_FCCH);
    check_int("the residual is still there", t.recent_count, 1);
    check_int("and the miss counter is cleared", t.fcch_miss, 0);
}

/* A signal that goes away entirely empties the buffer rather than leaving it
   to age: those residuals describe something no longer there. */
static void test_losing_the_signal_empties_the_buffer(void) {
    struct calibration_tracker t;

    calibration_tracker_init(&t);
    for (int i = 0; i < 10; i++) {
        calibration_track(&t, 0, 1);
        calibration_tracker_observe(&t, 3.0);
    }
    check_int("ten residuals", t.recent_count, 10);
    check_int("the carrier goes", calibration_track(&t, 0, 0),
              CALIBRATION_NOTHING);
    check_int("and takes the buffer with it", t.recent_count, 0);
}

/* The ring: it holds the most recent CALIBRATION_RECENT residuals and no
   more, and the statistics follow what is in it. */
static void test_the_residual_ring(void) {
    struct calibration_tracker t;

    calibration_tracker_init(&t);
    /* Fill it twice over with a value, then flood it with another: the old
       value must be entirely gone. */
    for (int i = 0; i < CALIBRATION_RECENT * 2; i++)
        calibration_tracker_observe(&t, 9.0);
    check_int("the ring stops at its size", t.recent_count,
              CALIBRATION_RECENT);
    check_int("but the measurement count does not",
              t.measurements, CALIBRATION_RECENT * 2);
    check_close("centred on what is in it", t.recent_center, 9.0, 1e-9);

    for (int i = 0; i < CALIBRATION_RECENT; i++)
        calibration_tracker_observe(&t, 1.0);
    check_close("and it forgets what has scrolled out", t.recent_center, 1.0,
                1e-9);
    check_close("with no spread left over", t.recent_spread, 0.0, 1e-9);
}

/*
 * The whole thing end to end: a tone that settles, seen through the gate the
 * program actually consults. This is the sequence that turns the lock green.
 */
static void test_a_lock_settles(void) {
    struct calibration_tracker t;
    double elapsed = 0.0;
    double locked_at = -1.0;

    calibration_tracker_init(&t);
    for (int block = 0; block < 200; block++) {
        /* A tone, missing every fifth block the way FCCH does. */
        int have_fcch = (block % 5) != 4;
        elapsed += 0.0655;
        if (calibration_track(&t, have_fcch, 1) == CALIBRATION_HOLD_TONE)
            continue;
        calibration_tracker_observe(&t, 1.5 + ((block % 7) - 3) * 0.02);
        t.stable = calibration_is_stable(elapsed, t.measurements,
                                         t.recent_count, t.recent_sem,
                                         t.source, 0.0f);
        if (t.stable && locked_at < 0.0)
            locked_at = elapsed;
    }
    check_msg(t.stable, "a clean tone never locked in 200 blocks\n");
    check_msg(locked_at >= CALIBRATION_MIN_SECONDS,
              "the lock came at %.2f s, before the %.1f s the gate "
              "requires\n",
              locked_at, CALIBRATION_MIN_SECONDS);
    check_close("and settles on the residual it was fed", t.recent_center, 1.5,
                0.05);
    check_int("having stayed on the tone throughout", t.source,
              CALIBRATION_SOURCE_FCCH);
}

int main(void) {
    test_a_good_lock_passes();
    test_each_clause_refuses();
    test_prominence_only_gates_the_centroid();
    test_robust_statistics();
    test_standard_error();
    test_sources_never_mix();
    test_tone_lock_holds_through_gaps();
    test_the_source_machine();
    test_switching_source_empties_the_buffer();
    test_a_gap_records_nothing();
    test_losing_the_signal_empties_the_buffer();
    test_the_residual_ring();
    test_a_lock_settles();

    return check_report("calibration gate");
}
