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
#define CALIBRATION_FCCH_MISS_LIMIT 12

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
                                        float prominence_db) {
    int quality_ok = source == CALIBRATION_SOURCE_FCCH
                         ? 1
                         : prominence_db >= CALIBRATION_MIN_PROMINENCE_DB;

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

#endif
