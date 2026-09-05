#include "check.h"

#include "survey_confirm.h"

/*
 * Asking again about what a sweep called new or missing.
 *
 * The sense of the verdict is the whole of it, and it is the easy thing to get
 * backwards: "new" is confirmed by finding the signal, "missing" by not
 * finding it. Invert one of the two and the pass still runs, still reports
 * numbers, and quietly turns half its answers upside down.
 */

static void test_the_sense_of_a_verdict(void) {
    check_int("a new signal that is there is confirmed",
              (int)survey_confirm_verdict(SURVEY_CLAIM_NEW, 1),
              (int)SURVEY_VERDICT_CONFIRMED);
    check_int("a new signal that is not was noise",
              (int)survey_confirm_verdict(SURVEY_CLAIM_NEW, 0),
              (int)SURVEY_VERDICT_REFUTED);
    check_int("a missing signal that is absent is confirmed missing",
              (int)survey_confirm_verdict(SURVEY_CLAIM_MISSING, 0),
              (int)SURVEY_VERDICT_CONFIRMED);
    check_int("a missing signal that turns up was a miss, not an absence",
              (int)survey_confirm_verdict(SURVEY_CLAIM_MISSING, 1),
              (int)SURVEY_VERDICT_REFUTED);
}

static void test_what_gets_remembered(void) {
    /*
     * The history must learn what the closer look found, not what the sweep
     * guessed. A new signal that did not hold up must never enter the history:
     * once it is in, the next sweep calls it missing and the noise becomes a
     * permanent ghost the site never stops reporting.
     */
    check_true("a confirmed new signal is remembered",
               survey_confirm_should_record(SURVEY_CLAIM_NEW,
                                            SURVEY_VERDICT_CONFIRMED));
    check_true("a refuted one is not",
               !survey_confirm_should_record(SURVEY_CLAIM_NEW,
                                             SURVEY_VERDICT_REFUTED));
    check_true("a missing signal that turned up is recorded as heard",
               survey_confirm_should_record(SURVEY_CLAIM_MISSING,
                                            SURVEY_VERDICT_REFUTED));
    check_true("one that really is gone changes nothing",
               !survey_confirm_should_record(SURVEY_CLAIM_MISSING,
                                             SURVEY_VERDICT_CONFIRMED));
}

static void test_presence(void) {
    check_true("a clear carrier is present",
               survey_confirm_present(20.0f));
    check_true("and one just over the bar",
               survey_confirm_present(SURVEY_CONFIRM_PROMINENCE_DB));
    check_true("noise is not",
               !survey_confirm_present(2.0f));
    check_true("nor is nothing at all", !survey_confirm_present(0.0f));
    /*
     * The bar is lower than a sweep's, on purpose. This looks at one frequency
     * already singled out, where the sweep had three hundred chances to be
     * wrong -- and refuting a real signal is the more expensive error, because
     * it teaches the site that a transmitter is noise.
     */
    check_true("and it is a forgiving bar",
               SURVEY_CONFIRM_PROMINENCE_DB <= 10.0f);
}

static void test_how_long_it_takes(void) {
    check_close("nothing to ask about costs nothing",
                survey_confirm_seconds(0), 0.0, 1e-9);
    /* The point of the pass is that it is short: a handful of targets against
       a sweep that ran for minutes. */
    check_true("five changes take under five seconds",
               survey_confirm_seconds(5) < 5.0);
    check_true("and it grows with the work, not with the band",
               survey_confirm_seconds(10) > survey_confirm_seconds(5));
    check_true("a full set is still under half a minute",
               survey_confirm_seconds(SURVEY_CONFIRM_MAX) < 30.0);
    check_true("each target gets more looks than a sweep step could spare",
               SURVEY_CONFIRM_LOOKS >= 4);
}

/*
 * Which target speaks for a frequency, which is how a saved sweep says per
 * signal whether anybody asked again.
 */
static void test_matching_a_target_to_a_frequency(void) {
    struct survey_confirm_target targets[3];
    const struct survey_confirm_target *found;

    memset(targets, 0, sizeof(targets));
    targets[0].hz = 94400000.0;
    targets[0].verdict = SURVEY_VERDICT_CONFIRMED;
    targets[1].hz = 94410000.0;
    targets[1].verdict = SURVEY_VERDICT_REFUTED;
    targets[2].hz = 100000000.0;
    targets[2].verdict = SURVEY_VERDICT_CONFIRMED;

    found = survey_confirm_for(targets, 3, 94400500.0, 20000.0);
    check_true("the nearest target answers, not the first in the list",
               found == &targets[0]);
    /* Both are within the tolerance of this one; the closer must win, or a
       candidate in a crowded band inherits its neighbour's verdict. */
    found = survey_confirm_for(targets, 3, 94408000.0, 20000.0);
    check_true("and when two are in reach, the closer of them",
               found == &targets[1]);
    check_true("nothing within reach is nothing",
               survey_confirm_for(targets, 3, 96000000.0, 20000.0) == NULL);
    check_true("an empty pass answers for nothing",
               survey_confirm_for(targets, 0, 94400000.0, 20000.0) == NULL);

    /*
     * A frequency nobody asked about reads as unconfirmed rather than as
     * refuted. The two are different findings: one says a closer look
     * disagreed, the other says there was no closer look, and writing them
     * the same way is what leaves a reader unable to tell a carrier that held
     * up from one that was never checked.
     */
    check_str("asked and found",
              survey_verdict_name(survey_confirm_verdict_at(targets, 3,
                                                            94400000.0,
                                                            20000.0)),
              "confirmed");
    check_str("asked and not found",
              survey_verdict_name(survey_confirm_verdict_at(targets, 3,
                                                            94410000.0,
                                                            20000.0)),
              "refuted");
    check_str("never asked",
              survey_verdict_name(survey_confirm_verdict_at(targets, 3,
                                                            96000000.0,
                                                            20000.0)),
              "unconfirmed");
    check_str("and no pass at all leaves everything unconfirmed",
              survey_verdict_name(survey_confirm_verdict_at(NULL, 0,
                                                            94400000.0,
                                                            20000.0)),
              "unconfirmed");
}

int main(void) {
    test_the_sense_of_a_verdict();
    test_what_gets_remembered();
    test_presence();
    test_how_long_it_takes();
    test_matching_a_target_to_a_frequency();

    return check_report("asking again about what changed");
}
