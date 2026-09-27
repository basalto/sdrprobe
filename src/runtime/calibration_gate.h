#ifndef CALIBRATION_GATE_H
#define CALIBRATION_GATE_H

#include <math.h>
#include <stdlib.h>

/*
 * When a frequency correction may be trusted, and the robust statistics behind
 * that judgement.
 *
 * This is the arithmetic ADR-0004 is about, and it decides something the
 * operator cannot check by looking: a correction applied too early is wrong by
 * however far the estimate had not yet settled, and the program will report it
 * with the same green circle as a good one. It lived inside the calibration
 * overlay, which links raylib, so nothing could reach it. It is plain doubles
 * here, checked by tests/calibration_gate_test.c (ADR-0012).
 *
 * Nothing about the rule changed in the move. If a future change makes the
 * gate easier to pass, read ADR-0004 first: every clause below is there
 * because a correction was once accepted that should not have been.
 */

#define CALIBRATION_RECENT 64
#define CALIBRATION_SETTLE_SECONDS 2.0
#define CALIBRATION_MIN_SECONDS 8.0
#define CALIBRATION_MAX_SEM_PPM 1.0
#define CALIBRATION_VIEW_HALF_WIDTH_HZ 250000.0
#define CALIBRATION_SOURCE_CENTROID 0
#define CALIBRATION_SOURCE_FCCH 1
/*
 * The offset an LTE cell search measures.
 *
 * A third source, and the source rule below matters more with three than it
 * did with two: an FCCH residual and an LTE residual are measurements of the
 * same crystal, but taken at different frequencies against different
 * references, and a buffer holding both has a centre belonging to neither.
 */
#define CALIBRATION_SOURCE_LTE 2
#define CALIBRATION_FCCH_MISS_LIMIT 12

/*
 * How good an LTE cell has to be before its offset is worth believing.
 *
 * The correlation, not the margin: a weak lock on the right cell still
 * measures the right offset, while a lock this poor is as likely to be noise
 * as a cell -- and a noise "offset" is a number with no crystal behind it.
 */
#define CALIBRATION_MIN_PSS 0.55f

/* How many residuals a lock needs. Both counts were bare 32s inside the
   expression; a lock that needs "enough" measurements should say how many. */
#define CALIBRATION_MIN_MEASUREMENTS 32
#define CALIBRATION_MIN_RESIDUALS 32

/* A centroid estimate has to stand this far above its guard-band floor before
   its residuals count. An FCCH tone does not: it is its own quality proof, and
   its prominence dips momentarily between bursts. */
#define CALIBRATION_MIN_PROMINENCE_DB 8.0f

static inline int calibration_compare_double(const void *left,
                                             const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

/*
 * Median and a normal-consistent scale estimate (1.4826 x MAD) of the recent
 * residuals. Both resist a peak that hops to an adjacent feature for a few
 * blocks, which a mean and a standard deviation would follow.
 */
static inline void robust_center_spread(const double *values, int count,
                                        double *center, double *spread) {
    double sorted[CALIBRATION_RECENT];
    double deviations[CALIBRATION_RECENT];
    double median;
    double mad;
    int i;

    if (count <= 0) {
        *center = 0.0;
        *spread = 0.0;
        return;
    }
    if (count > CALIBRATION_RECENT)
        count = CALIBRATION_RECENT;
    for (i = 0; i < count; i++)
        sorted[i] = values[i];
    qsort(sorted, (size_t)count, sizeof(*sorted), calibration_compare_double);
    median = (count % 2) ? sorted[count / 2]
                         : 0.5 * (sorted[count / 2 - 1] + sorted[count / 2]);
    for (i = 0; i < count; i++)
        deviations[i] = fabs(values[i] - median);
    qsort(deviations, (size_t)count, sizeof(*deviations),
          calibration_compare_double);
    mad = (count % 2)
              ? deviations[count / 2]
              : 0.5 * (deviations[count / 2 - 1] + deviations[count / 2]);
    *center = median;
    *spread = 1.4826 * mad;
}

/*
 * The uncertainty the gate judges: the standard error of the centre, not the
 * per-block spread. Individual 65 ms blocks scatter widely on a modulated
 * channel, but the correction applied is the centre of many of them, and how
 * well *that* is known is what decides whether it can be trusted.
 */
static inline double calibration_standard_error(double spread, int count) {
    if (count <= 0)
        return 0.0;
    return spread / sqrt((double)count);
}

/*
 * Every clause a correction must satisfy before it may be applied. Kept as one
 * function so the rule can be read, and tested, in one place.
 */
static inline int calibration_is_stable(double elapsed_seconds,
                                        int measurements, int residual_count,
                                        double standard_error_ppm, int source,
                                        float quality) {
    /*
     * What "good enough" means depends on what measured it. A tone lock is
     * its own quality gate -- the detector would not have locked otherwise.
     * A centroid needs the carrier to stand clear of the floor. An LTE offset
     * needs the cell search to have actually found a cell.
     */
    int quality_ok;
    switch (source) {
    case CALIBRATION_SOURCE_FCCH:
        quality_ok = 1;
        break;
    case CALIBRATION_SOURCE_LTE:
        quality_ok = quality >= CALIBRATION_MIN_PSS;
        break;
    default:
        quality_ok = quality >= CALIBRATION_MIN_PROMINENCE_DB;
        break;
    }

    return elapsed_seconds >= CALIBRATION_MIN_SECONDS &&
           measurements >= CALIBRATION_MIN_MEASUREMENTS &&
           residual_count >= CALIBRATION_MIN_RESIDUALS &&
           standard_error_ppm <= CALIBRATION_MAX_SEM_PPM && quality_ok;
}

/*
 * Whether a residual measured by `source` may join a buffer holding residuals
 * from `buffered`. It may not, unless they agree: FCCH and centroid estimates
 * differ by many PPM, so a buffer holding both has a centre belonging to
 * neither, and a standard error small enough to pass the gate while the
 * correction is nonsense. This is the mistake ADR-0004 exists to prevent.
 */
static inline int calibration_source_matches(int buffered, int source) {
    return buffered == source;
}

/* A tone lock survives this many bursts-free blocks before falling back to the
   centroid: GSM sends an FCCH ten times a multiframe, so a gap is normal and
   dropping the lock on the first one would thrash between sources. */
static inline int calibration_hold_through_miss(int consecutive_misses) {
    return consecutive_misses < CALIBRATION_FCCH_MISS_LIMIT;
}

/*
 * The state around the gate: which source the residuals came from, how long a
 * tone lock has been without a burst, and the ring of recent residuals with
 * the statistics computed over it.
 *
 * The gate above is only as good as the buffer it reads, and keeping that
 * buffer honest is a state machine, not an expression: the source can change
 * under it, and when it does every residual already in it belongs to a
 * different measurement of a different thing. ADR-0004 exists because a
 * correction was once accepted from a buffer holding both.
 */
struct calibration_tracker {
    int source;
    int fcch_locked;
    int fcch_miss;   /* consecutive blocks with no tone, while locked to one */
    int fcch_hits;
    int measurements;
    double recent_ppm[CALIBRATION_RECENT];
    int recent_count;
    int recent_head;
    double recent_center;
    double recent_spread;
    double recent_sem;
    int stable;
};

/* Empty the residual buffer and everything derived from it, keeping the
   source. Called whenever what is being measured changes. */
static inline void calibration_tracker_reset(struct calibration_tracker *t) {
    t->measurements = 0;
    t->recent_count = 0;
    t->recent_head = 0;
    t->recent_center = 0.0;
    t->recent_spread = 0.0;
    t->recent_sem = 0.0;
    t->stable = 0;
}

/* Start again from nothing: a new channel, or a calibration just opened. */
static inline void calibration_tracker_init(struct calibration_tracker *t) {
    calibration_tracker_reset(t);
    t->source = CALIBRATION_SOURCE_CENTROID;
    t->fcch_locked = 0;
    t->fcch_miss = 0;
    t->fcch_hits = 0;
}

/*
 * Point the buffer at a source, clearing it if it was holding another.
 *
 * The switch is the dangerous moment: residuals from two references have
 * different centres, and a buffer that keeps the old ones while filling with
 * new has a standard error small enough to pass the gate while the correction
 * it suggests belongs to neither. That is ADR-0004's mistake, and with three
 * sources there are now six ways to make it.
 */
static inline void calibration_tracker_use(struct calibration_tracker *t,
                                           int source) {
    if (!t)
        return;
    if (!calibration_source_matches(t->source, source)) {
        calibration_tracker_reset(t);
        t->source = source;
    }
}


/* What this block can contribute. */
enum calibration_action {
    CALIBRATION_USE_FCCH,     /* a tone was found: record its residual */
    CALIBRATION_HOLD_TONE,    /* no tone this block, but the lock holds */
    CALIBRATION_USE_CENTROID, /* record the centroid's residual */
    CALIBRATION_NOTHING       /* neither: record nothing */
};

/*
 * Advance the tracker for one block, and say what it may record.
 *
 * The rules, in the order they matter:
 *
 * - A tone always wins, and arriving on a centroid buffer empties it. FCCH and
 *   centroid residuals differ by many PPM; a buffer holding both has a centre
 *   belonging to neither and a standard error small enough to pass the gate.
 * - A tone lock rides out CALIBRATION_FCCH_MISS_LIMIT burst-free blocks
 *   without recording anything, because GSM sends an FCCH ten times a
 *   multiframe and a gap is normal. Recording a centroid residual during that
 *   gap would be the mixing above; dropping the lock at the first gap would
 *   thrash between sources every multiframe.
 * - Past the limit the lock is given up and the buffer emptied, because what
 *   follows is a different measurement.
 * - With no tone and no carrier there is nothing to record, and the buffer is
 *   emptied rather than left to age: the residuals in it describe a signal
 *   that is no longer there.
 */
static inline enum calibration_action
calibration_track(struct calibration_tracker *t, int have_fcch,
                  int have_centroid) {
    if (have_fcch) {
        if (!calibration_source_matches(t->source, CALIBRATION_SOURCE_FCCH)) {
            t->source = CALIBRATION_SOURCE_FCCH;
            calibration_tracker_reset(t);
            t->fcch_hits = 0;
        }
        t->fcch_miss = 0;
        t->fcch_locked = 1;
        t->fcch_hits++;
        return CALIBRATION_USE_FCCH;
    }
    if (t->source == CALIBRATION_SOURCE_FCCH) {
        t->fcch_miss++;
        if (calibration_hold_through_miss(t->fcch_miss))
            return CALIBRATION_HOLD_TONE;
        t->source = CALIBRATION_SOURCE_CENTROID;
        t->fcch_locked = 0;
        calibration_tracker_reset(t);
        t->fcch_hits = 0;
        return have_centroid ? CALIBRATION_USE_CENTROID : CALIBRATION_NOTHING;
    }
    t->fcch_locked = 0;
    if (!have_centroid) {
        calibration_tracker_reset(t);
        return CALIBRATION_NOTHING;
    }
    return CALIBRATION_USE_CENTROID;
}

/* Record one residual and recompute what the gate reads. */
static inline void calibration_tracker_observe(struct calibration_tracker *t,
                                               double observed_ppm) {
    t->measurements++;
    t->recent_ppm[t->recent_head] = observed_ppm;
    t->recent_head = (t->recent_head + 1) % CALIBRATION_RECENT;
    if (t->recent_count < CALIBRATION_RECENT)
        t->recent_count++;
    robust_center_spread(t->recent_ppm, t->recent_count, &t->recent_center,
                         &t->recent_spread);
    t->recent_sem = calibration_standard_error(t->recent_spread,
                                               t->recent_count);
}

#endif
