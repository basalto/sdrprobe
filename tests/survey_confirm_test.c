#include "check.h"

#include "survey_confirm.h"
/* For survey_measure_duty_label(): the boundary between continuous and
   intermittent is that function's, and this asserts the two agree. */
#include "survey_sweep.h"

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

/*
 * The third verdict, and the count behind it.
 *
 * Two answers cannot describe a burst. Measured on air: five identical sweeps
 * of 1550-1766 MHz found 0, 6, 6, 2 and 2 candidates, fifteen frequencies and
 * no frequency twice, all at 30 dB and more -- Iridium and the
 * mobile-satellite uplinks. Asked once each, the pass refuted nine of ten.
 */
static void test_intermittent_is_its_own_answer(void) {
    check_int("up in every look is continuous",
              survey_confirm_presence(6, 6), SURVEY_VERDICT_CONFIRMED);
    check_int("up in none of them is not there",
              survey_confirm_presence(0, 6), SURVEY_VERDICT_REFUTED);
    check_int("one look in six is intermittent",
              survey_confirm_presence(1, 6), SURVEY_VERDICT_INTERMITTENT);
    check_int("and so is five",
              survey_confirm_presence(5, 6), SURVEY_VERDICT_INTERMITTENT);
    /* The boundary is survey_measure_duty_label()'s, not a new one: the rest
       of the program calls a duty over 0.9 continuous and this must agree, or
       the same signal is described two ways in one window. */
    check_str("and the words match the ones the survey already uses",
              survey_measure_duty_label(1.0), "continuous");
    check_str("five in six", survey_measure_duty_label(5.0 / 6.0),
              "intermittent");
    check_int("nothing looked at cannot be intermittent",
              survey_confirm_presence(0, 0), SURVEY_VERDICT_REFUTED);

    /*
     * Intermittent belongs to the signal, not to the claim. "New" and
     * "missing" invert the other two verdicts between them, and a third value
     * doubles the ways that inversion can go wrong -- so this one does not
     * invert: a signal heard in two looks of six came and went, whichever the
     * sweep had said about it.
     */
    check_int("a new signal, up sometimes",
              survey_confirm_verdict_from(SURVEY_CLAIM_NEW, 2, 6),
              SURVEY_VERDICT_INTERMITTENT);
    check_int("a missing one, up sometimes",
              survey_confirm_verdict_from(SURVEY_CLAIM_MISSING, 2, 6),
              SURVEY_VERDICT_INTERMITTENT);
    /* And the two that do invert still do. */
    check_int("a new signal, up every time",
              survey_confirm_verdict_from(SURVEY_CLAIM_NEW, 6, 6),
              SURVEY_VERDICT_CONFIRMED);
    check_int("a new signal, never up",
              survey_confirm_verdict_from(SURVEY_CLAIM_NEW, 0, 6),
              SURVEY_VERDICT_REFUTED);
    check_int("a missing one that stayed missing",
              survey_confirm_verdict_from(SURVEY_CLAIM_MISSING, 0, 6),
              SURVEY_VERDICT_CONFIRMED);
    check_int("a missing one that turned up every time",
              survey_confirm_verdict_from(SURVEY_CLAIM_MISSING, 6, 6),
              SURVEY_VERDICT_REFUTED);

    check_str("and it has a word of its own",
              survey_verdict_name(SURVEY_VERDICT_INTERMITTENT),
              "intermittent");
}

/*
 * What the history is told, which is the half that decides whether a bursty
 * transmitter can ever be known at all.
 *
 * A refuted "new" is never recorded, so before this the next sweep called it
 * new again, the next pass refuted it again, and the mobile-satellite
 * allocations could not enter the history however many sweeps heard them.
 */
static void test_a_burst_is_recorded(void) {
    check_int("a new signal heard some of the time is heard",
              survey_confirm_should_record(SURVEY_CLAIM_NEW,
                                           SURVEY_VERDICT_INTERMITTENT), 1);
    check_int("and so is a missing one that came back sometimes",
              survey_confirm_should_record(SURVEY_CLAIM_MISSING,
                                           SURVEY_VERDICT_INTERMITTENT), 1);
    /* The two that were already right stay right. */
    check_int("a new signal that held up",
              survey_confirm_should_record(SURVEY_CLAIM_NEW,
                                           SURVEY_VERDICT_CONFIRMED), 1);
    check_int("a new signal that was noise",
              survey_confirm_should_record(SURVEY_CLAIM_NEW,
                                           SURVEY_VERDICT_REFUTED), 0);
    check_int("a missing one that really is gone",
              survey_confirm_should_record(SURVEY_CLAIM_MISSING,
                                           SURVEY_VERDICT_CONFIRMED), 0);
    check_int("a missing one that turned up",
              survey_confirm_should_record(SURVEY_CLAIM_MISSING,
                                           SURVEY_VERDICT_REFUTED), 1);
}

int main(void) {
    test_the_sense_of_a_verdict();
    test_what_gets_remembered();
    test_presence();
    test_how_long_it_takes();
    test_matching_a_target_to_a_frequency();
    test_intermittent_is_its_own_answer();
    test_a_burst_is_recorded();

    return check_report("asking again about what changed");
}
