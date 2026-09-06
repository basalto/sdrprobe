/*
 * What the LTE measurements amount to in words, and what they refuse to say.
 */

#include "check.h"
#include <string.h>

#include "lte_findings.h"

static void fill(struct lte_cell_stats *st, float ppm_khz, float sinr,
                 float rsrq, float spread, float drift_lo, float drift_hi) {
    memset(st, 0, sizeof(*st));
    lte_stats_for_cell(st, 330);
    lte_stat_add(&st->pss, 0.9f);
    lte_stat_add(&st->frequency_khz, ppm_khz);
    lte_stat_add(&st->sinr_db, sinr);
    lte_stat_add(&st->rsrq_db, rsrq);
    lte_stat_add(&st->spread_ns, spread);
    lte_stat_add(&st->drift_hz, drift_lo);
    lte_stat_add(&st->drift_hz, drift_hi);
}

static int mentions(const struct lte_findings *f, const char *needle) {
    int i;
    for (i = 0; i < f->count; i++)
        if (strstr(f->line[i], needle))
            return 1;
    return 0;
}

static void test_nothing_measured_says_nothing(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    memset(&st, 0, sizeof(st));
    check_int("no statistics, no findings",
              lte_findings_from(&st, 927.5e6, &f), 0);
    check_int("and none written", f.count, 0);
    check_int("a null carrier is refused too",
              lte_findings_from(&st, 0.0, &f), 0);
}

/*
 * Where a spread sits among the 36.104 profiles, and the floor that decides
 * whether it can be placed at all.
 *
 * An earlier version refused to say anything about EPA, EVA or ETU, on the
 * grounds that all three are finer than the ~1010 ns given by one over the
 * references' span. That was the wrong criterion: 1/span resolves individual
 * taps and this estimator does not resolve taps, it measures the scatter of
 * the phase steps. The floor is the noise -- 1/sqrt(rho) of phase error per
 * step -- so it moves with the signal, and these tests are built on that.
 */
static void test_the_floor_moves_with_the_noise(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    /*
     * The same 400 ns of spread, twice. At 10 dB the floor is 559 ns and the
     * channel cannot be told from noise; at 28 dB the floor is 70 ns and it
     * can be placed. One measurement, two honest answers, and the difference
     * is the receiver rather than the channel.
     */
    fill(&st, -31.8f, 10.0f, -17.0f, 400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("400 ns at 10 dB is under the floor", mentions(&f, "Flat:"));

    fill(&st, -31.8f, 28.0f, -17.0f, 400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("400 ns at 28 dB is a measurement", !mentions(&f, "Flat:"));
    check_true("and it is placed between EVA and ETU",
               mentions(&f, "between EVA's 357 ns and ETU's 991"));
}

static void test_the_profiles_are_positions_not_identities(void) {
    struct lte_cell_stats st;
    struct lte_findings f;
    int i;

    /*
     * 36.104's profiles are conformance models, not a taxonomy of places, so
     * a channel is never said to *be* one. Every mention is positional.
     */
    fill(&st, -31.8f, 28.0f, -17.0f, 400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    for (i = 0; i < f.count; i++) {
        check_true("no line claims the channel is a profile",
                   !strstr(f.line[i], "is EVA") &&
                   !strstr(f.line[i], "is EPA") &&
                   !strstr(f.line[i], "is ETU"));
    }
    check_true("the placement is relative",
               mentions(&f, "between") || mentions(&f, "past") ||
               mentions(&f, "flatter than"));
}

static void test_saturation_is_reported_as_a_bound(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    /*
     * A step reaches a radian at 1768 ns and stops growing with the channel.
     * Near it the number is a lower bound, and saying so is the difference
     * between a measurement and a guess dressed as one -- the reading on air
     * is 1.4 us, which is 79% of the way there.
     */
    fill(&st, -31.8f, 28.0f, -17.0f, 1400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("a spread near saturation says so", mentions(&f, "a floor"));
    check_true("and is still placed among the profiles",
               mentions(&f, "past ETU's 991 ns"));

    /*
     * And past the wrap the number is not a delay at all. Wrapped phase steps
     * can scatter up to pi, so the arithmetic keeps producing values long
     * after they mean anything -- 2.4 us came off the air and read "within a
     * tenth of the 1.8 us saturation", which is not where 2.4 sits.
     */
    fill(&st, -31.8f, 28.0f, -17.0f, 2400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("past the wrap it is named for what it is",
               mentions(&f, "Beyond measure"));
    check_true("and is not described as near the wrap",
               !mentions(&f, "nears the"));

    /* Comfortably inside the window: a value, not a bound. */
    fill(&st, -31.8f, 28.0f, -17.0f, 600.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("a spread inside the window is not called a floor",
               !mentions(&f, "a floor"));
    /* The threshold is the error, not a round number: three quarters of a
       radian is where a tenth of the reading has gone. */
    check_close("compression is flagged where 9% is lost",
                LTE_SPREAD_COMPRESSED_NS, 1326.0, 5.0);
}

static void test_motion_is_refused(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    fill(&st, -31.8f, 28.0f, -17.0f, 1400.0f, -90.0f, 90.0f);
    lte_findings_from(&st, 927.5e6, &f);
    /* A Doppler and a residual tuning error are the same phase, so the drift
       is reported as the crystal and never as a speed the receiver is
       travelling at. */
    check_true("the drift is attributed to the crystal",
               mentions(&f, "It is the crystal"));
    check_true("indoor and outdoor are refused",
               mentions(&f, "Indoor or outdoor is not measured"));
}

static void test_the_crystal_is_judged_in_ppm(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    /* -31.8 kHz at 927.5 MHz is about -34 ppm: a stock part. */
    fill(&st, -31.8f, 28.0f, -17.0f, 1400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("a stock crystal is named as one",
               mentions(&f, "stock crystal"));

    /* And one within two parts per million is not. */
    fill(&st, -0.9f, 28.0f, -17.0f, 1400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("a good crystal is not called stock",
               !mentions(&f, "stock crystal"));
    check_true("it is called calibrated or better",
               mentions(&f, "calibrated or better"));
}

static void test_quality_has_three_verdicts(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    fill(&st, -31.8f, 28.0f, -17.0f, 1400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("28 dB is clean", mentions(&f, "Clean"));

    fill(&st, -31.8f, 14.0f, -17.0f, 1400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("14 dB is workable", mentions(&f, "Workable"));

    fill(&st, -31.8f, 4.0f, -17.0f, 1400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("4 dB is marginal", mentions(&f, "Marginal"));
}

static void test_nothing_overruns_its_line(void) {
    struct lte_cell_stats st;
    struct lte_findings f;
    int i;

    fill(&st, -31.8f, 28.0f, -17.0f, 1400.0f, -900.0f, 900.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("no more lines than there is room for",
               f.count <= LTE_FINDING_LINES);
    for (i = 0; i < f.count; i++) {
        /* snprintf terminates inside the buffer; what matters is that the
           string ends before the end of it, not that the last byte is zero. */
        check_true("every line ends inside its buffer",
                   strnlen(f.line[i], LTE_FINDING_TEXT) < LTE_FINDING_TEXT);
        check_true("and none is empty", f.line[i][0] != '\0');
    }
    /* Lines past the count are cleared, so handing the struct on cannot leak
       whatever was on the stack. */
    for (i = f.count; i < LTE_FINDING_LINES; i++)
        check_true("unused lines are empty", f.line[i][0] == '\0');
}

int main(void) {
    test_nothing_measured_says_nothing();
    test_the_floor_moves_with_the_noise();
    test_the_profiles_are_positions_not_identities();
    test_saturation_is_reported_as_a_bound();
    test_motion_is_refused();
    test_the_crystal_is_judged_in_ppm();
    test_quality_has_three_verdicts();
    test_nothing_overruns_its_line();
    return check_report("what the LTE measurements amount to");
}
