#ifndef SURVEY_BANDS_H
#define SURVEY_BANDS_H

#include <stddef.h>

#include "band_plan.h"
#include "survey_sweep.h"

/*
 * Which allocations the survey can be pointed at, and what range each means.
 *
 * The survey sweeps whatever its two frequency fields say, and the default is
 * the tuner's whole span -- minutes. Sweeping one band was always possible and
 * always meant typing its edges from memory, or from band_plan.c, which the
 * program is already carrying. This turns that table into a list to choose
 * from.
 *
 * The tuner's reach is the filter. The band plan runs from long wave to the
 * top of the microwave allocations because it is a reference, and offering a
 * sweep of something this receiver cannot tune is offering a button that
 * cannot work -- the same objection as an Inspect that names a decoder the
 * program does not have.
 *
 * Pure: a lookup and some arithmetic, no raylib and no receiver (ADR-0012).
 */

/* A little air either side, so a carrier at the very edge of an allocation is
   measured rather than sitting on the shoulder of the swept range. */
#define SURVEY_BAND_MARGIN_HZ 200000.0

/*
 * What this receiver reaches. An R820T covers 24 MHz to about 1766, which is
 * where the survey's own default range comes from -- named here so the band
 * list and that default cannot disagree about which is which.
 */
#define SURVEY_TUNER_LOWER_HZ 24000000.0
#define SURVEY_TUNER_UPPER_HZ 1766000000.0

/*
 * How long a chosen band should take. Not a limit -- the operator can still
 * type a longer dwell -- but the dwell that comes with a band should make it
 * a thing worth waiting for rather than a thing to regret.
 */
#define SURVEY_BAND_TARGET_SECONDS 20.0

/* Whether an allocation is worth offering at all. */
static inline int survey_band_reachable(const struct band_plan_entry *entry,
                                        double tuner_lower_hz,
                                        double tuner_upper_hz) {
    if (!entry)
        return 0;
    return entry->upper_hz > tuner_lower_hz && entry->lower_hz < tuner_upper_hz;
}

/* How many there are. */
static inline int survey_band_count(double tuner_lower_hz,
                                    double tuner_upper_hz) {
    int i, count = 0;

    for (i = 0; i < band_plan_entry_count(); i++)
        if (survey_band_reachable(band_plan_entry_at(i), tuner_lower_hz,
                                  tuner_upper_hz))
            count++;
    return count;
}

/* The nth of them, counting from zero, or NULL. */
static inline const struct band_plan_entry *survey_band_at(
        int n, double tuner_lower_hz, double tuner_upper_hz) {
    int i, seen = 0;

    if (n < 0)
        return NULL;
    for (i = 0; i < band_plan_entry_count(); i++) {
        const struct band_plan_entry *entry = band_plan_entry_at(i);
        if (!survey_band_reachable(entry, tuner_lower_hz, tuner_upper_hz))
            continue;
        if (seen == n)
            return entry;
        seen++;
    }
    return NULL;
}

/*
 * The range a chosen allocation should sweep: its own edges with a little
 * air, clipped to what the receiver can reach.
 *
 * Clipped rather than refused, because plenty of allocations run off the end
 * of the tuner -- band 20's uplink starts below 24 MHz on some plans, and the
 * top of the table runs past 1766 -- and sweeping the reachable part of one
 * is the right answer, not an error.
 */
static inline int survey_band_range(const struct band_plan_entry *entry,
                                    double tuner_lower_hz,
                                    double tuner_upper_hz,
                                    double *from_hz, double *to_hz) {
    double from, to;

    if (!entry || !from_hz || !to_hz)
        return -1;
    from = entry->lower_hz - SURVEY_BAND_MARGIN_HZ;
    to = entry->upper_hz + SURVEY_BAND_MARGIN_HZ;
    if (from < tuner_lower_hz)
        from = tuner_lower_hz;
    if (to > tuner_upper_hz)
        to = tuner_upper_hz;
    if (to <= from)
        return -1;
    *from_hz = from;
    *to_hz = to;
    return 0;
}

/*
 * And the dwell to go with it.
 *
 * A band is anywhere from a few hundred kilohertz to twenty megahertz, and
 * one dwell does not suit both: the tenth of a second that makes a whole-tuner
 * sweep bearable wastes a narrow band, and a second that suits a narrow band
 * makes a wide one take a quarter of an hour. So the dwell follows the number
 * of steps, aiming at a fixed total.
 *
 * Bounded at both ends, and the upper bound does most of the work: at half a
 * second a step sees eight blocks, which is enough averaging that another
 * half buys very little. So every band narrower than about fifty megahertz
 * gets the same generous dwell and finishes inside the budget anyway; the
 * arithmetic only starts shortening things for the genuinely wide
 * allocations, which is where it is needed. Below the settle time a step
 * would be mostly the tuner moving.
 */
#define SURVEY_BAND_MAX_DWELL 0.5
static inline double survey_band_dwell(double from_hz, double to_hz,
                                       double sample_rate) {
    double span = to_hz - from_hz;
    double step = sample_rate * SURVEY_USABLE_SPAN;
    double steps, dwell;

    if (span <= 0.0 || step <= 0.0)
        return SURVEY_DWELL_DEFAULT;
    steps = span / step;
    if (steps < 1.0)
        steps = 1.0;
    dwell = SURVEY_BAND_TARGET_SECONDS / steps - SURVEY_SETTLE_SECONDS;
    if (dwell < SURVEY_DWELL_DEFAULT)
        dwell = SURVEY_DWELL_DEFAULT;
    if (dwell > SURVEY_BAND_MAX_DWELL)
        dwell = SURVEY_BAND_MAX_DWELL;
    return dwell;
}

#endif
