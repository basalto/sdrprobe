#ifndef SURVEY_CONFIRM_H
#define SURVEY_CONFIRM_H

/*
 * Asking again about the handful of things that changed.
 *
 * A sweep of the whole tuner spends about a tenth of a second on each step,
 * which is enough to notice a carrier and not enough to be sure of one. So the
 * marks it leaves -- this is new here, that has gone quiet -- are claims, and
 * over three hundred steps some of them will be wrong: a transmitter that was
 * between bursts reads as missing, and a moment of noise reads as new.
 *
 * By the time the sweep ends there are a few such claims rather than three
 * hundred steps, and each can afford a proper look: tune to it, sit there, and
 * see. That is cheap for the same reason the LTE band scan's confirmation pass
 * is cheap (`src/lte_scan.h`), and it is the same argument -- a wide search has
 * to be generous, so something narrower has to have the last word.
 *
 * Plain arithmetic, no receiver and no window, checked by
 * tests/survey_confirm_test.c (ADR-0012).
 */

#define SURVEY_CONFIRM_MAX 24
/* The tuner needs a moment, and the pipeline still holds the last channel. */
#define SURVEY_CONFIRM_SETTLE_SECONDS 0.25
/* Blocks folded into one target's spectrum. Six against the sweep's one or
   two: the whole point is to look harder than the sweep could afford to. */
#define SURVEY_CONFIRM_LOOKS 6
/*
 * How far above its local floor a carrier must sit to count as present.
 *
 * Deliberately lower than the sweep's own bar. The sweep must be selective
 * because it has three hundred chances to be wrong; this has one chance at a
 * frequency already singled out, so it can afford to believe a weaker signal
 * -- and being too strict here would refute real signals, which is the more
 * expensive error: it teaches the history that a real transmitter is noise.
 */
#define SURVEY_CONFIRM_PROMINENCE_DB 6.0f

/* What the sweep claimed about a frequency. */
enum survey_claim {
    SURVEY_CLAIM_NEW = 0,      /* this site has not heard it before */
    SURVEY_CLAIM_MISSING       /* this site has heard it, but not this time */
};

enum survey_verdict {
    SURVEY_VERDICT_PENDING = 0,
    SURVEY_VERDICT_CONFIRMED,  /* the closer look agreed */
    SURVEY_VERDICT_REFUTED     /* it did not */
};

struct survey_confirm_target {
    double hz;
    signed char claim;         /* enum survey_claim */
    signed char verdict;       /* enum survey_verdict */
    float prominence_db;       /* what the closer look measured */
};

/*
 * Did the closer look agree with the claim?
 *
 * "New" is confirmed by finding it and refuted by not; "missing" is the other
 * way round. Writing it out rather than inlining the two cases because getting
 * the sense backwards for one of them would produce a pass that looks like it
 * is working and quietly inverts half the answers.
 */
static inline enum survey_verdict survey_confirm_verdict(int claim,
                                                         int present) {
    if (claim == SURVEY_CLAIM_MISSING)
        return present ? SURVEY_VERDICT_REFUTED : SURVEY_VERDICT_CONFIRMED;
    return present ? SURVEY_VERDICT_CONFIRMED : SURVEY_VERDICT_REFUTED;
}

/* Whether a measured prominence counts as the signal being there. */
static inline int survey_confirm_present(float prominence_db) {
    return prominence_db >= SURVEY_CONFIRM_PROMINENCE_DB;
}

/* Roughly how long the pass takes, so the button can say before it is
   pressed. A block is 65.5 ms at the house rate. */
static inline double survey_confirm_seconds(int count) {
    if (count <= 0)
        return 0.0;
    return (double)count * (SURVEY_CONFIRM_SETTLE_SECONDS +
                            SURVEY_CONFIRM_LOOKS * 0.0655);
}

/* What the pass changes about the history: a refuted "new" was noise and
   should not be remembered, and a refuted "missing" was heard after all. Both
   are the opposite of what the sweep alone would have recorded. */
static inline int survey_confirm_should_record(int claim, int verdict) {
    if (claim == SURVEY_CLAIM_NEW)
        return verdict == SURVEY_VERDICT_CONFIRMED;
    /* A missing entry that turned up is worth recording as heard. */
    return verdict == SURVEY_VERDICT_REFUTED;
}

#endif
