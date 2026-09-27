#include "check.h"

#include "model/calibration_view_model.h"
#include "runtime/app.h"
#include "runtime/calibration_gate.h"

#include <string.h>

/*
 * What the Calibration overlay says, decided without a screen.
 *
 * Three decisions, and each was somewhere a second reader could not reach
 * it (`web-visualization/17`): what measured the residuals -- which ADR-0004
 * makes load-bearing and which was a ternary chain inside one `printf`;
 * which clause of the gate is still unsatisfied, which nothing named at all;
 * and what the two references make of each other.
 */

static struct calibration cal;
static double now = 100.0;
static int applied_ppm;

static struct calibration_view_model build(void) {
    struct calibration_view_model out;

    calibration_view_model_build(&cal, now, applied_ppm, &out);
    return out;
}

static void blank(void) {
    memset(&cal, 0, sizeof(cal));
    now = 100.0;
    applied_ppm = 0;
    cal.started_at = 90.0;      /* ten seconds in: past CALIBRATION_MIN_SECONDS */
}

/* Fill the ring as a real run would, so the gate has something to weigh. */
static void residuals(int count, double sem) {
    cal.track.measurements = count;
    cal.track.recent_count = count;
    cal.track.recent_sem = sem;
    cal.track.recent_center = -31.8;
    cal.track.recent_spread = sem * 3.0;
}

/*
 * What measured them, by name.
 *
 * ADR-0004's rule is that the residual buffer stays source-homogeneous,
 * because a buffer holding two sources passes the gate while suggesting a
 * correction belonging to neither. So the source is not a detail a reader
 * can be left to infer.
 */
static void test_the_source_is_named(void) {
    struct calibration_view_model m;

    blank();
    cal.track.source = CALIBRATION_SOURCE_FCCH;
    m = build();
    check_str("a GSM tone", m.source, "fcch");

    cal.track.source = CALIBRATION_SOURCE_LTE;
    m = build();
    check_str("an LTE cell", m.source, "lte");

    cal.track.source = CALIBRATION_SOURCE_CENTROID;
    m = build();
    check_str("and a carrier's centroid", m.source, "centroid");

    /* A value the enum does not hold reads "unknown" rather than falling
       through to the last branch of a chain, which is what the ternary it
       replaced would have done. */
    cal.track.source = 99;
    m = build();
    check_str("anything else is not the last one", m.source, "unknown");
}

/*
 * Which clause of the gate is unsatisfied.
 *
 * `calibration_is_stable()` returns one bit, so a reader watching a
 * calibration that will not lock could not tell "it needs four more seconds"
 * from "the scatter is too wide to ever settle" -- different situations, and
 * only one is worth waiting out. The clauses are asked in the order the gate
 * ands them, so the name is the reason that will clear first.
 */
static void test_the_gate_says_which_clause_is_unmet(void) {
    struct calibration_view_model m;

    blank();
    cal.track.source = CALIBRATION_SOURCE_FCCH;

    /* One second in, with everything else satisfied. */
    cal.started_at = now - 1.0;
    residuals(CALIBRATION_MIN_RESIDUALS, 0.2);
    m = build();
    check_int("not yet", m.locked, 0);
    check_str("because it has not listened long enough", m.unmet, "too-soon");

    /* Long enough, and not enough of them. */
    cal.started_at = now - 20.0;
    residuals(4, 0.2);
    m = build();
    check_str("too few readings", m.unmet, "too-few-measurements");

    /* Enough of them, scattered too widely to mean anything. */
    residuals(CALIBRATION_MIN_RESIDUALS, CALIBRATION_MAX_SEM_PPM + 0.5);
    m = build();
    check_int("still not locked", m.locked, 0);
    check_str("and this one will not clear by waiting", m.unmet,
              "scatter-too-wide");

    /* And a lock names no clause at all, because there is none. */
    residuals(CALIBRATION_MIN_RESIDUALS, 0.2);
    m = build();
    check_int("locked", m.locked, 1);
    check_str("so nothing is outstanding", m.unmet, "");
}

/*
 * A weak reference is its own clause, and which one depends on the source --
 * an LTE offset needs the cell search to have found a cell, a centroid needs
 * the carrier to stand clear of its floor, and a tone lock is its own
 * quality gate because the detector would not have locked otherwise.
 */
static void test_a_weak_reference_is_named_for_what_it_is(void) {
    struct calibration_view_model m;

    blank();
    cal.started_at = now - 20.0;
    residuals(CALIBRATION_MIN_RESIDUALS, 0.2);

    cal.track.source = CALIBRATION_SOURCE_LTE;
    cal.fcch_confidence = CALIBRATION_MIN_PSS - 0.1f;
    m = build();
    check_str("a cell the search barely found", m.unmet, "weak-cell");

    cal.track.source = CALIBRATION_SOURCE_CENTROID;
    cal.prominence_db = CALIBRATION_MIN_PROMINENCE_DB - 1.0f;
    m = build();
    check_str("a carrier barely over its floor", m.unmet, "weak-carrier");

    /* The tone, which needs nothing further: the detector locking *is* the
       quality gate. */
    cal.track.source = CALIBRATION_SOURCE_FCCH;
    cal.fcch_confidence = 0.0f;
    m = build();
    check_int("a tone lock needs no second opinion on quality", m.locked, 1);
    check_str("and names no clause", m.unmet, "");
}

/*
 * Two references measuring one crystal.
 *
 * When they agree the correction is worth trusting; when they do not, that
 * is the most useful thing either has said. One that has measured is
 * **not** a disagreement, and reporting a gap of `0 - 32` as though it were
 * would be the most misleading number on the screen.
 */
static void test_one_reference_is_not_a_disagreement(void) {
    struct calibration_view_model m;

    blank();
    cal.gsm_valid = 1;
    cal.gsm_ppm = -32;
    cal.gsm_arfcn = 113;
    m = build();
    check_int("one reference has measured", m.gsm_valid, 1);
    check_int("the other has not", m.lte_valid, 0);
    check_int("so there is nothing to compare", m.have_both, 0);
    check_close("and no gap is claimed", m.references_apart_ppm, 0.0, 1e-12);

    cal.lte_valid = 1;
    cal.lte_ppm = -33;
    cal.lte_earfcn = 6200;
    m = build();
    check_int("now there are two", m.have_both, 1);
    check_close("a ppm apart, as measured on air",
                m.references_apart_ppm, 1.0, 1e-9);

    /* And the sign does not matter: what is wanted is how far apart. */
    cal.gsm_ppm = 32;
    m = build();
    check_close("distance, not difference", m.references_apart_ppm, 65.0,
                1e-9);
}

/*
 * Both numbers, because the sequence is what shows whether the scatter is
 * the estimator or the crystal -- which is why `--calibrate` prints every
 * residual rather than a summary.
 */
static void test_this_block_and_the_run_are_both_carried(void) {
    struct calibration_view_model m;

    blank();
    cal.expected_hz = 948400000u;
    cal.offset_hz = -30000.0;
    residuals(40, 0.2);
    m = build();
    check_close("this block's residual", m.observed_ppm,
                -30000.0 / 948400000.0 * 1e6, 1e-9);
    check_close("and the median over the ring", m.centre_ppm, -31.8, 1e-9);
    check_int("with how many are behind it", m.residuals, 40);

    /* No carrier yet is a refusal, not a division. */
    cal.expected_hz = 0;
    m = build();
    check_close("nothing to measure against, no residual", m.observed_ppm,
                0.0, 1e-12);
}

/* The health is where the verdict lives after the overlay closes, and
   nothing here dismisses itself on a timer: a correction is a standing fact
   about the receiver. */
static void test_the_health_travels_by_name(void) {
    struct calibration_view_model m;

    blank();
    m = build();
    check_str("nobody has calibrated this receiver", m.health, "unknown");

    cal.drift_health = CAL_HEALTH_GOOD;
    m = build();
    check_str("backed by a stable lock", m.health, "good");

    cal.drift_health = CAL_HEALTH_DRIFT;
    snprintf(cal.drift_notice, sizeof(cal.drift_notice),
             "Re-check found 4.1 ppm of drift");
    m = build();
    check_str("a re-check found drift", m.health, "drifting");
    check_str("and says what it found", m.drift_notice,
              "Re-check found 4.1 ppm of drift");

    cal.drift_health = CAL_HEALTH_CHECKING;
    m = build();
    check_str("a re-check under way", m.health, "checking");
}

int main(void) {
    test_the_source_is_named();
    test_the_gate_says_which_clause_is_unmet();
    test_a_weak_reference_is_named_for_what_it_is();
    test_one_reference_is_not_a_disagreement();
    test_this_block_and_the_run_are_both_carried();
    test_the_health_travels_by_name();
    return check_report("what the Calibration overlay says, decided without "
                        "a screen");
}
