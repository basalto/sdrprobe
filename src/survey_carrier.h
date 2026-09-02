#ifndef SURVEY_CARRIER_H
#define SURVEY_CARRIER_H

#include <math.h>

#include "sdr_dsp.h"

/*
 * From peaks to signals.
 *
 * A peak finder returns local maxima, and a real transmission has more than
 * one. A 200 kHz FM carrier shows three or four; a 9 MHz LTE downlink shows a
 * dozen. Reported as they are, one station arrives as five candidates at five
 * frequencies, and every one of them is a separate thing to remember, to
 * confirm, and to be surprised by next time.
 *
 * Two maxima are one signal when nothing separates them: the power between
 * them never drops far below the lower of the two. That is the test, and it is
 * the only one that works in a crowded band. Overlapping the extents the peak
 * finder measures does not: it marks where a peak falls 20 dB below its own
 * top, and a strong FM station in a full band never gets 20 dB down before the
 * next station starts, so the extents run together and one "carrier" swallows
 * a whole megahertz. Measured on air: 11 maxima in 94-98 MHz became one
 * 2.4 MHz carrier holding five stations.
 *
 * A dip is what a band edge looks like. If the trough between two maxima is
 * deeper than SURVEY_CARRIER_SPLIT_DB below the weaker of them, they are two
 * signals however close together they sit; if it is shallower, they are ripple
 * on one.
 *
 * The trough is not the lowest bin between them. A modulated carrier's own
 * spectrum swings several decibels bin to bin, so the lowest bin is a noise
 * notch and taking it splits every station into its own shoulders -- measured
 * on air, it turned 11 maxima into 11 "carriers", three of them sharing one
 * extent. What matters is a drop that *lasts*: each position is scored by the
 * strongest bin within SURVEY_CARRIER_NOTCH bins of it, which a one-bin notch
 * cannot depress, and the trough is the lowest of those scores.
 *
 * What a carrier then knows about itself is more than any one peak did: where
 * it starts and ends, how wide it is, where its power actually sits, and how
 * many maxima it accounts for. The last of those is worth reporting on its own
 * -- a "carrier" made of one peak and one made of eleven are different
 * claims.
 *
 * Pure arithmetic over an array. No receiver, no window, checked by
 * tests/survey_carrier_test.c (ADR-0012).
 */

#define SURVEY_CARRIER_MAX 64

/*
 * How deep the trough between two maxima must be for them to be two signals.
 *
 * Six decibels is a quarter of the power, which no carrier's own ripple
 * reaches and which every gap between two transmissions clears. Lower and one
 * station splits into its own shoulders; much higher and adjacent stations
 * merge, which is the failure this replaced.
 */
#define SURVEY_CARRIER_SPLIT_DB 6.0f

/* How many bins either side a dip must hold to count as a gap rather than a
   notch in the modulation. */
#define SURVEY_CARRIER_NOTCH 2

struct survey_carrier {
    /*
     * What identifies the signal: the middle of its extent.
     *
     * Not the power-weighted centre, which is below and is the better answer
     * to "where is the energy" and the worse one to "which signal is this". A
     * loaded LTE downlink carries more traffic on one side than the other and
     * its power centre moves with the loading -- measured on air, a 9 MHz
     * carrier's power sat a megahertz above its middle. A site history
     * matching on that would call the same carrier new every sweep.
     */
    double centre_hz;
    /* Where the power actually sits, which is what a receiver tuning to hear
       it wants, and what tells a symmetric carrier from a lopsided one. */
    double power_centre_hz;
    double lower_hz;
    double upper_hz;
    double width_hz;
    float peak_dbfs;       /* the strongest bin in it */
    float floor_dbfs;      /* the quietest local floor of the peaks it holds */
    float prominence_db;
    int peaks;             /* local maxima it accounts for */
    int strongest;         /* index into the peak array of the tallest */
};

/*
 * Group `peaks` into carriers. `power` is the array they were found in,
 * `first_bin_hz` the centre of bin 0 and `bin_hz` the spacing.
 *
 * Peaks are taken in the order given, which the finder leaves strongest first,
 * so a carrier is named by its tallest peak and absorbs the shoulders rather
 * than the other way round.
 *
 * Returns how many carriers were filled in.
 */
/* The strongest bin within SURVEY_CARRIER_NOTCH of `at`: a single-bin notch in
   the modulation cannot pull this down, a band edge can. */
static inline float survey_carrier_smoothed(const float *power, int bins,
                                            float sentinel, int at) {
    float best = sentinel;
    int w;
    for (w = at - SURVEY_CARRIER_NOTCH; w <= at + SURVEY_CARRIER_NOTCH; w++) {
        if (w < 0 || w >= bins || power[w] <= sentinel)
            continue;
        if (power[w] > best)
            best = power[w];
    }
    return best;
}

/*
 * Walk out from a peak to where its signal ends: the trough before the next
 * one begins.
 *
 * Follow the spectrum down, remembering the lowest it has been. The moment it
 * climbs back SURVEY_CARRIER_SPLIT_DB above that low point it is rising into
 * something else, and the low point was the boundary. Stop also once it is
 * `edge_db` below the peak, which is the occupied width the survey reports
 * elsewhere, and at the end of the array.
 *
 * This is what makes the extent mean something for a weak peak. Marking where
 * it falls 20 dB below itself does not: a peak already near the floor never
 * gets there, so its "extent" runs to the edge of the band and every weak
 * candidate appears to overlap every other.
 */
static inline int survey_carrier_edge(const float *power, int bins,
                                      float sentinel, int from,
                                      float peak_dbfs, float edge_db,
                                      int direction) {
    float lowest = peak_dbfs;
    int edge = from, b = from;

    for (;;) {
        float value;
        b += direction;
        if (b < 0 || b >= bins)
            break;
        value = survey_carrier_smoothed(power, bins, sentinel, b);
        if (value <= sentinel)
            break;
        if (value < lowest) {
            lowest = value;
            edge = b;
        } else if (value > lowest + SURVEY_CARRIER_SPLIT_DB) {
            break;              /* climbing into the next signal */
        }
        if (value < peak_dbfs - edge_db) {
            edge = b;
            break;
        }
    }
    return edge;
}

/*
 * Group `peaks` into carriers. `power` is the array they were found in,
 * `first_bin_hz` the centre of bin 0 and `bin_hz` the spacing. `edge_db` is
 * how far below a peak its occupied width is taken, the same figure the survey
 * uses for a candidate's bandwidth.
 *
 * Peaks are taken in the order given, which the finder leaves strongest first,
 * so a carrier is named by its tallest and absorbs the shoulders rather than
 * the other way round. A peak falling inside a carrier already claimed is one
 * of its maxima; anything else starts a carrier of its own.
 *
 * Returns how many carriers were filled in.
 */
static inline int survey_carriers_from(const float *power, int bins,
                                       float sentinel, double first_bin_hz,
                                       double bin_hz, float edge_db,
                                       const struct sdr_peak *peaks,
                                       int peak_count,
                                       struct survey_carrier *out, int max) {
    int i, count = 0;
    int low_bin[SURVEY_CARRIER_MAX], high_bin[SURVEY_CARRIER_MAX];

    if (!power || !peaks || !out || bins <= 0 || max <= 0)
        return 0;
    if (max > SURVEY_CARRIER_MAX)
        max = SURVEY_CARRIER_MAX;

    for (i = 0; i < peak_count; i++) {
        const struct sdr_peak *p = &peaks[i];
        int joined = -1, k;

        if (p->index < 0 || p->index >= bins)
            continue;
        for (k = 0; k < count; k++)
            if (p->index >= low_bin[k] && p->index <= high_bin[k]) {
                joined = k;
                break;
            }
        if (joined < 0) {
            if (count >= max)
                continue;
            joined = count++;
            low_bin[joined] = survey_carrier_edge(power, bins, sentinel,
                                                  p->index, p->power_dbfs,
                                                  edge_db, -1);
            high_bin[joined] = survey_carrier_edge(power, bins, sentinel,
                                                   p->index, p->power_dbfs,
                                                   edge_db, +1);
            out[joined].peak_dbfs = p->power_dbfs;
            out[joined].floor_dbfs = p->floor_dbfs;
            out[joined].strongest = i;
            out[joined].peaks = 0;
        } else {
            if (p->power_dbfs > out[joined].peak_dbfs) {
                out[joined].peak_dbfs = p->power_dbfs;
                out[joined].strongest = i;
            }
            /* The quietest floor either side of any of its maxima: a shoulder
               sits on the skirt of its own carrier and reports a floor the
               carrier itself raised. */
            if (p->floor_dbfs < out[joined].floor_dbfs)
                out[joined].floor_dbfs = p->floor_dbfs;
        }
        out[joined].peaks++;
    }

    for (i = 0; i < count; i++) {
        struct survey_carrier *c = &out[i];
        double weight = 0.0, moment = 0.0;
        int low = low_bin[i], high = high_bin[i], k;

        c->lower_hz = first_bin_hz + (double)low * bin_hz;
        c->upper_hz = first_bin_hz + (double)high * bin_hz;
        c->width_hz = (double)(high - low + 1) * bin_hz;
        c->prominence_db = c->peak_dbfs - c->floor_dbfs;

        /*
         * The centre is where the power is, not where the tallest bin is. A
         * carrier with a strong pilot off to one side has its maximum there
         * and its middle elsewhere, and it is the middle a receiver has to be
         * tuned to. Weighted in linear units: in decibels a quiet bin would
         * pull as hard as a loud one.
         */
        for (k = low; k <= high; k++) {
            double above, linear;
            if (k < 0 || k >= bins || power[k] <= sentinel)
                continue;
            above = (double)(power[k] - c->floor_dbfs);
            if (above <= 0.0)
                continue;
            linear = pow(10.0, above / 10.0) - 1.0;
            if (linear <= 0.0)
                continue;
            weight += linear;
            moment += linear * (first_bin_hz + (double)k * bin_hz);
        }
        c->centre_hz = (c->lower_hz + c->upper_hz) / 2.0;
        c->power_centre_hz = weight > 0.0 ? moment / weight : c->centre_hz;
    }
    return count;
}

#endif
