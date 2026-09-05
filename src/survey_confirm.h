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

/*
 * Which target spoke for this frequency, or NULL when the pass never asked
 * about it.
 *
 * The saved sweep has to say, per signal, whether a closer look agreed --
 * otherwise a reader cannot tell a carrier that held up from one nobody
 * checked, and both look like findings. The pass asks about carriers and the
 * file lists candidates as well, so the match is by frequency: the nearest
 * target within `tolerance_hz`, which the caller sets from the sweep's own bin
 * width because that is how far from the truth a reported frequency can be.
 *
 * Nearest rather than first: two targets can both be within a bin of a
 * candidate in a crowded band, and taking whichever came first in the list
 * would hand a candidate the verdict of its neighbour.
 */
static inline const struct survey_confirm_target *
survey_confirm_for(const struct survey_confirm_target *targets, int count,
                   double hz, double tolerance_hz) {
    const struct survey_confirm_target *best = 0;
    double closest = 0.0;
    int i;

    if (!targets || count <= 0 || !(tolerance_hz >= 0.0))
        return 0;
    for (i = 0; i < count; i++) {
        double away = targets[i].hz - hz;

        if (away < 0.0)
            away = -away;
        if (away > tolerance_hz)
            continue;
        if (!best || away < closest) {
            best = &targets[i];
            closest = away;
        }
    }
    return best;
}

/* The verdict a pass reached about `hz`, or SURVEY_VERDICT_PENDING when it
   never asked -- which is what "unconfirmed" means in the saved file. */
static inline int survey_confirm_verdict_at(
    const struct survey_confirm_target *targets, int count, double hz,
    double tolerance_hz) {
    const struct survey_confirm_target *target =
        survey_confirm_for(targets, count, hz, tolerance_hz);

    return target ? target->verdict : SURVEY_VERDICT_PENDING;
}

/* The word for a verdict, as the reports and the saved file both spell it. */
static inline const char *survey_verdict_name(int verdict) {
    switch (verdict) {
    case SURVEY_VERDICT_CONFIRMED: return "confirmed";
    case SURVEY_VERDICT_REFUTED:   return "refuted";
    default:                       return "unconfirmed";
    }
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
