#ifndef SURVEY_CONFIRM_H
#define SURVEY_CONFIRM_H

#include "signal_probe.h"
#include "survey_suspect.h"
#include "survey_sweep.h"

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

/*
 * The pass tunes this far below each target rather than onto it.
 *
 * `signal_find_carrier()` guards a band around zero so it cannot lock onto
 * the receiver's own DC offset -- the strongest thing in any capture, at an
 * empty frequency as readily as an occupied one, and the mistake that cost
 * `.scratch/signal-probe/` ticket 01 four attempts. Tuned onto the target,
 * a guarded search would skip the very signal it was pointed at.
 *
 * `survey_select()` already does this and says so. What it costs was measured
 * rather than assumed, because an earlier reading of this had it backwards:
 * the same 75.0005 MHz carrier recorded at both tunings reads 21.9 dB of
 * prominence tuned onto it and 20.1 dB tuned 300 kHz below. Two decibels, and
 * in that direction -- the DC filter subtracts the sample mean, and a carrier
 * at the tuned frequency sits at the tuning error rather than at exactly
 * zero, so its mean is near nothing and the filter does not touch it.
 *
 * Two decibels against a 6 dB bar that the sweep's own threshold already
 * clears with margin.
 */
#define SURVEY_CONFIRM_OFFSET_HZ SURVEY_OFFSET_HZ

/* What the sweep claimed about a frequency. */
enum survey_claim {
    SURVEY_CLAIM_NEW = 0,      /* this site has not heard it before */
    SURVEY_CLAIM_MISSING       /* this site has heard it, but not this time */
};

/*
 * Three answers, because a closer look has three things it can find.
 *
 * "It was there" and "it was not" are the two the pass started with, and they
 * are not enough for anything that transmits in bursts. Five identical sweeps
 * of 1550-1766 MHz minutes apart found 0, 6, 6, 2 and 2 candidates at 30 dB
 * and more above their floors, at fifteen frequencies and no frequency twice:
 * Iridium and the mobile-satellite uplinks, which are short bursts on channels
 * that move. Asked once, six blocks each, the pass refuted nine of ten of
 * them.
 *
 * That is the expensive direction to be wrong in, and this file already said
 * so about the threshold: refuting a real signal "teaches the site that a real
 * transmitter is noise". Worse, it teaches it permanently -- a refuted "new"
 * is never recorded, so the next sweep calls it new again and the next pass
 * refutes it again, and the mobile-satellite allocations can never enter the
 * history however many sweeps hear them.
 */
enum survey_verdict {
    SURVEY_VERDICT_PENDING = 0,
    SURVEY_VERDICT_CONFIRMED,    /* there every time the pass looked */
    SURVEY_VERDICT_INTERMITTENT, /* there some of the times it looked */
    SURVEY_VERDICT_REFUTED       /* not there at all */
};

struct survey_confirm_target {
    double hz;
    /*
     * Where the energy is, when the caller knows -- zero when it does not.
     *
     * `hz` is the carrier's `centre_hz`, the middle of its extent, and that
     * is the right thing to *identify* a signal by: it is what the history
     * matches on and what every report prints. It is the wrong thing to
     * point a carrier search at, and survey_carrier.h says so outright --
     * the middle and the centre of energy "part company on a lopsided
     * carrier".
     *
     * Measured: on one sweep of 74-76 MHz the middle of the 75.000 MHz clock
     * harmonic's extent landed 42 kHz from the line, and the search looked
     * past it and measured the noise beside it -- 15.4 dB over the floor with
     * 1% of the channel standing still, where the same signal searched around
     * its own energy reads 44.2 dB and 92%. Widening the search does not fix
     * it: the extent is 2.9 kHz, so there is nothing in the width to widen by.
     */
    double power_centre_hz;
    signed char claim;         /* enum survey_claim */
    signed char verdict;       /* enum survey_verdict */
    float prominence_db;       /* what the closer look measured */
    /* How many of the looks it was up in, and how many there were. The
       verdict is the decision; this is the measurement behind it, and it is
       what a reader needs to tell one burst in six from five. */
    int hits;
    int looks;
    /*
     * The width the closer look measured, and zero when it measured none.
     *
     * The sweep that raised the candidate could not supply this: a full-tuner
     * sweep is 1742 MHz in 8192 bins, so a bin is 212 kHz -- wider than a
     * land-mobile channel -- and a maximum in one says only that something is
     * there. The pass revisits at 2 MS/s where a bin is 244 Hz, already
     * computes a width to decide presence, and used to discard it.
     *
     * It is what makes the receiver's own comb detectable at all on a coarse
     * sweep: `survey_suspect()` needs a width, and `survey_comb_harmonic()`
     * refuses to answer when the tolerance a bin implies is a large fraction
     * of the comb spacing. Both objections are about the *sweep's*
     * resolution, and neither applies to a measurement taken at 244 Hz.
     */
    double bandwidth_hz;
    unsigned suspicion;        /* enum survey_suspicion, at the finer look */
    /*
     * The centre the pass actually measured, and **the frequency
     * `suspicion` is about** -- 0 when nothing was measured.
     *
     * It is not `hz`. `hz` is what the sweep asked about and what the row is
     * keyed by; the pass retunes to it, searches around it and reports where
     * it found the carrier, and the two can be far apart. One live row read
     * `confirm 136079346 ... reference,unresolved,clocked-here` for a
     * measurement near 136.004 -- 75 kHz away, three times
     * `RECEIVER_COMB_TOLERANCE_HZ` and eighty times the tolerance
     * `clocked-here` is allowed, so neither flag could be true of the number
     * printed beside them.
     *
     * The fault is older than those flags: `reference` has had it since the
     * pass existed, and a tighter measurement is what made it visible
     * (`.scratch/reading-origin/issues/03-*`). Both are printed now, which is
     * what makes the gap answerable rather than invisible -- whether a large
     * gap means the same carrier measured properly or the search finding a
     * stronger neighbour is a separate question and is not settled here.
     */
    double measured_hz;
    /*
     * And what kind of thing it is, from `signal_probe` -- is there a
     * standing carrier, does it transmit in bursts, how much does its
     * envelope vary. Taken here rather than only in the window because a
     * saved survey that records level and width and nothing about kind
     * cannot answer "was this carrier bare in June and modulated in
     * September", which is the comparison the history exists for.
     *
     * `kind_measured` is separate from the numbers and load-bearing: a target
     * the pass never caught must read as never measured rather than as zero,
     * and zero is the most misleading value each of these can take -- a
     * carrier fraction of 0 is "heavily modulated" and a burst count of 0 is
     * "continuous".
     */
    int kind_measured;
    struct signal_carrier carrier;
    struct signal_bursts bursts;
    struct signal_envelope envelope;
};

/*
 * How close to Rayleigh an envelope has to read before it is called noise's.
 *
 * Rayleigh is 0.5227 exactly -- the coefficient of variation of a complex
 * Gaussian's magnitude -- and it depends on nothing at all: not on level, not
 * on gain, not on bandwidth. That is what makes it usable as a reference and
 * what makes five independent frequencies landing on it evidence rather than
 * coincidence.
 *
 * The window is measured from both sides. The five noise readings that raised
 * this ran 0.520 to 0.539, at most 0.017 out. The nearest thing that must
 * *not* be caught is a GSM carrier at 0.792, which is 0.27 out, and every
 * other real signal measured here is further: an LTE downlink 1.098, Mode S
 * 1.057, TETRA 0.252-0.272, a bare carrier 0.137-0.248, FM broadcast 0.032.
 * A tenth sits five times the noise spread away from the noise and less than
 * a third of the way to the nearest signal.
 */
#define SURVEY_NOISE_ENVELOPE_TOLERANCE 0.10

/*
 * Nothing here, however often it was seen.
 *
 * Two independent statistics have to agree, and the pass has both: no
 * standing line above the floor beside it -- SIGNAL_CARRIER_PRESENT_DB is 15
 * because a search over thousands of frequencies reliably reaches 8 to 14 on
 * pure noise -- *and* an envelope varying as much as noise and no more.
 *
 * Either alone is not enough and both failures are on record. A pulsed
 * transmission has real energy and no standing carrier, so the first test
 * alone would call Mode S empty. A weak signal buried at its own noise floor
 * reads Rayleigh, so the second alone would call it empty too. Together they
 * describe a frequency where a closer look found a prominence and nothing
 * else, which is what noise structure looks like and what nothing else does.
 *
 * `measured` is whether the kind was measured at all; a target the pass never
 * caught is not evidence of emptiness, it is evidence of nothing.
 */
static inline int survey_confirm_is_empty(
    int measured, const struct signal_carrier *carrier,
    const struct signal_envelope *envelope) {
    double distance;

    if (!measured || !carrier || !envelope || !envelope->found)
        return 0;
    if (signal_carrier_verdict(carrier) != SIGNAL_NOTHING)
        return 0;
    distance = envelope->variation - SIGNAL_ENVELOPE_RAYLEIGH;
    if (distance < 0.0)
        distance = -distance;
    return distance <= SURVEY_NOISE_ENVELOPE_TOLERANCE;
}

/*
 * Should this look replace the one being kept?
 *
 * The pass keeps the best single look rather than an average, and everything
 * it reports about a target has to come from *that* look -- the level, the
 * width, and now the kind. Taking the kind from whichever look happened to be
 * last would put a carrier fraction beside a prominence measured in a
 * different block, and for anything bursty those are different signals: one
 * caught the transmission and one caught the gap.
 *
 * `measured` is whether anything is being kept yet, so the first look that
 * finds the signal always wins.
 */
static inline int survey_confirm_better(int measured, float kept_db,
                                        float look_db) {
    return !measured || look_db > kept_db;
}

/*
 * Was it there, and how often?
 *
 * The thresholds are survey_measure_duty_label()'s, not new ones: a signal up
 * in more than nine looks in ten is continuous, and anything less that is
 * still up sometimes is intermittent. Reusing them is deliberate -- the survey
 * already describes a candidate's duty in those words when it measures one for
 * two seconds, and a pass that used different boundaries would print
 * "intermittent" for a duty the rest of the program calls continuous.
 */
static inline enum survey_verdict survey_confirm_presence(int hits,
                                                          int looks) {
    if (looks <= 0 || hits <= 0)
        return SURVEY_VERDICT_REFUTED;
    if ((double)hits / (double)looks > 0.9)
        return SURVEY_VERDICT_CONFIRMED;
    return SURVEY_VERDICT_INTERMITTENT;
}

/*
 * Did the closer look agree with the claim?
 *
 * "New" is confirmed by finding it and refuted by not; "missing" is the other
 * way round. Writing it out rather than inlining the two cases because getting
 * the sense backwards for one of them would produce a pass that looks like it
 * is working and quietly inverts half the answers -- and a third value doubles
 * the ways that can happen, which is why `intermittent` is deliberately its
 * own answer rather than a claim about the claim: a signal heard in two looks
 * of six is intermittent whether the sweep called it new or called it missing.
 */
static inline enum survey_verdict survey_confirm_verdict(int claim,
                                                         int present) {
    if (claim == SURVEY_CLAIM_MISSING)
        return present ? SURVEY_VERDICT_REFUTED : SURVEY_VERDICT_CONFIRMED;
    return present ? SURVEY_VERDICT_CONFIRMED : SURVEY_VERDICT_REFUTED;
}

/*
 * The verdict from a count of looks, which is the one the pass uses now.
 * Intermittent belongs to the signal rather than to the claim, so it survives
 * the inversion that "new" and "missing" put on the other two.
 */
static inline enum survey_verdict survey_confirm_verdict_from(int claim,
                                                              int hits,
                                                              int looks) {
    enum survey_verdict presence = survey_confirm_presence(hits, looks);

    if (presence == SURVEY_VERDICT_INTERMITTENT)
        return SURVEY_VERDICT_INTERMITTENT;
    return survey_confirm_verdict(claim,
                                  presence == SURVEY_VERDICT_CONFIRMED);
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

/*
 * What kind of thing was found at a frequency, or NULL when nothing was --
 * either because no target sits there or because the pass never caught the
 * one that does.
 *
 * NULL rather than a zeroed struct, so a caller cannot write a kind it does
 * not have. Every field of that struct has a plausible-looking zero: a
 * standing fraction of 0.000 reads as "heavily modulated" and a burst count
 * of zero as "continuous".
 */
static inline const struct survey_confirm_target *survey_confirm_kind_at(
    const struct survey_confirm_target *targets, int count, double hz,
    double tolerance_hz) {
    const struct survey_confirm_target *target =
        survey_confirm_for(targets, count, hz, tolerance_hz);

    return target && target->kind_measured ? target : NULL;
}

/* The word for the envelope's own verdict, spelled once so the report, the
   saved file and the screen cannot drift apart. */
static inline const char *survey_burst_name(int verdict) {
    switch (verdict) {
    case SIGNAL_BURST_SEPARABLE: return "bursts";
    case SIGNAL_BURST_BUSY:      return "busy";
    default:                     return "level";
    }
}

/* The word for a verdict, as the reports and the saved file both spell it. */
static inline const char *survey_verdict_name(int verdict) {
    switch (verdict) {
    case SURVEY_VERDICT_CONFIRMED:    return "confirmed";
    case SURVEY_VERDICT_INTERMITTENT: return "intermittent";
    case SURVEY_VERDICT_REFUTED:      return "refuted";
    default:                          return "unconfirmed";
    }
}

/*
 * What the pass changes about the history: a refuted "new" was noise and
 * should not be remembered, and a refuted "missing" was heard after all. Both
 * are the opposite of what the sweep alone would have recorded.
 *
 * And an intermittent one was heard, whichever the claim was. That is the
 * whole point of the third verdict: what the history needs to know is whether
 * the site heard it, not whether it was up the whole time the pass was
 * listening. Left out, a bursty transmitter is refuted on every sweep for
 * ever and the history never learns it exists.
 */
static inline int survey_confirm_should_record(int claim, int verdict,
                                              unsigned suspicion) {
    /*
     * Nothing empty enters the history, however many looks saw it.
     *
     * This is the fault the flag was added for: five frequencies confirmed
     * six looks out of six, each with no standing carrier and an envelope at
     * Rayleigh, all five on their way into the site's memory as signals --
     * where they would be remembered for ever and reported "gone" whenever a
     * later sweep failed to find the same noise.
     *
     * Checked before the verdict rather than after, because it overrides all
     * three: a confirmed empty frequency is still empty, and an intermittent
     * one is noise that came and went.
     *
     * That last case is an amendment to ADR-0019, which makes three verdicts
     * first class and says an intermittent new carrier may enter the history
     * -- otherwise every later sweep rediscovers and rejects the same bursty
     * transmitter. The amendment is recorded in the ADR itself: a verdict
     * answers *was it there when I looked* and all three assume something was
     * there to look at, so a measurement establishing there is nothing
     * overrides rather than competes. The verdict is still reported; only the
     * history entry is barred.
     */
    if (survey_suspect_empty(suspicion))
        return 0;
    if (verdict == SURVEY_VERDICT_INTERMITTENT)
        return 1;
    if (claim == SURVEY_CLAIM_NEW)
        return verdict == SURVEY_VERDICT_CONFIRMED;
    /* A missing entry that turned up is worth recording as heard. */
    return verdict == SURVEY_VERDICT_REFUTED;
}

#endif
