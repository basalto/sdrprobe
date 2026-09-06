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

static void test_the_profile_is_refused(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    /*
     * The measurement this exists to refuse. A spread of 400 ns is squarely
     * EVA's 357 -- and it is also under the 1010 ns these references can
     * resolve, so EVA cannot be told from EPA's 45 and naming either would be
     * inventing the answer a reader wanted.
     */
    fill(&st, -31.8f, 28.0f, -17.0f, 400.0f, -50.0f, 60.0f);
    check_true("findings are produced", lte_findings_from(&st, 927.5e6, &f) > 0);
    check_true("the refusal is explicit about which profiles",
               mentions(&f, "no 3GPP profile follows"));
    /*
     * EPA and EVA appear -- in the refusal, which names what it ruled out.
     * The thing that must not happen is a line *attributing* the channel to
     * one, so the test is that they occur nowhere else.
     */
    {
        int i, named = 0, refused = 0;
        for (i = 0; i < f.count; i++) {
            if (strstr(f.line[i], "EVA") || strstr(f.line[i], "EPA")) {
                if (strstr(f.line[i], "no 3GPP profile follows"))
                    refused++;
                else
                    named++;
            }
        }
        check_int("the profiles are named once, in the refusal", refused, 1);
        check_int("and nowhere else", named, 0);
    }
    check_true("a spread under the floor is called flat",
               mentions(&f, "Flat:"));
    /*
     * And the caveat travels with the claim rather than on its own line. It
     * did not, and the panel ran out of room after the claim -- so a reader
     * saw the spread and never the sentence saying no profile follows from
     * it. A caveat that can be dropped separately is not a caveat.
     */
    {
        int i, together = 0;
        for (i = 0; i < f.count; i++)
            if ((strstr(f.line[i], "Flat:") || strstr(f.line[i], "Dispersive"))
                && strstr(f.line[i], "no 3GPP profile follows"))
                together++;
        check_int("the spread and its limit are one line", together, 1);
    }
}

static void test_a_dispersive_channel_is_reported(void) {
    struct lte_cell_stats st;
    struct lte_findings f;

    /* Above the resolution floor, so there is something real to say. */
    fill(&st, -31.8f, 28.0f, -17.0f, 1400.0f, -50.0f, 60.0f);
    lte_findings_from(&st, 927.5e6, &f);
    check_true("a spread above the floor is a finding",
               mentions(&f, "Dispersive"));
    check_true("and the profile is still refused, on the same line",
               mentions(&f, "no 3GPP profile follows"));
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
    test_the_profile_is_refused();
    test_a_dispersive_channel_is_reported();
    test_motion_is_refused();
    test_the_crystal_is_judged_in_ppm();
    test_quality_has_three_verdicts();
    test_nothing_overruns_its_line();
    return check_report("what the LTE measurements amount to");
}
