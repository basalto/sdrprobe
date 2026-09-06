#ifndef LTE_FINDINGS_H
#define LTE_FINDINGS_H

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lte_stats.h"

/*
 * What the measurements amount to, in words -- and, as often, what they do
 * not.
 *
 * The panel above this is ten numbers and a reader has to hold the whole
 * chain in their head to know what they mean. These are the conclusions the
 * numbers support. **Every one of them names the measurement it rests on**,
 * because a sentence without its number is an assertion, and this program has
 * spent a long time learning the difference.
 *
 * Two of the conclusions somebody would most like are refusals, and they are
 * here rather than omitted, because a reader who is not told cannot know the
 * question was asked.
 *
 * **What the delay spread can and cannot reach.** An earlier version of this
 * refused to say anything about the 3GPP profiles at all, on the grounds that
 * EPA (45 ns), EVA (357) and ETU (991) all sit under the ~1010 ns given by
 * one over the references' 990 kHz span. That was the wrong criterion and the
 * refusal was too strong: 1/span is the resolution for separating individual
 * multipath taps, and this estimator does not separate taps. It measures the
 * *scatter* of the phase steps, which for an rms delay spread tau is
 * 2*pi*90kHz*tau, and its floor is set by the noise rather than by the span:
 *
 *   RS-SINR 10 dB -> 0.32 rad of noise per step -> 559 ns
 *   RS-SINR 20 dB -> 0.10                       -> 177 ns
 *   RS-SINR 28 dB -> 0.040                      ->  70 ns
 *
 * At the 28 dB the cells here read, EVA and ETU are comfortably measurable --
 * EVA's scatter is five times the noise -- and only EPA is out of reach.
 *
 * The real limit is at the other end. A step reaches a radian at 1768 ns and
 * the estimate saturates, so a spread near that is a **lower bound**: a more
 * dispersive channel reads compressed towards it and cannot be told from one
 * sitting exactly there. That is fixed by the 90 kHz grid and no receiver
 * changes it.
 *
 * And indoor against outdoor is not measured at all: what arrives describes
 * the path it took, not where either end of it stands.
 *
 * Pure arithmetic over the statistics -- no window, no receiver, no samples
 * (ADR-0012).
 */

#define LTE_FINDING_LINES 8
#define LTE_FINDING_TEXT 120

/*
 * Nanoseconds of rms delay spread per radian of step scatter: the references
 * are 90 kHz apart and the scatter is 2*pi*df*tau, so one radian is
 * 1/(2*pi*90kHz).
 */
#define LTE_SPREAD_PER_RADIAN_NS 1768.4

/* Where a step reaches a radian and the estimate stops growing with the
   channel. Read anything near it as a lower bound. */
#define LTE_SPREAD_SATURATION_NS LTE_SPREAD_PER_RADIAN_NS

/*
 * And where it has started to matter, chosen by the error rather than by
 * taste. The scatter follows sin(phi) rather than phi, so the reading falls
 * short by 1 - sin(phi)/phi: 4.1% at half a radian, 9.1% at three quarters,
 * 15.9% at one. Three quarters is where a tenth of the answer has gone, which
 * is the point at which calling it a value rather than a bound would be
 * misleading.
 */
#define LTE_SPREAD_COMPRESSED_NS (LTE_SPREAD_PER_RADIAN_NS * 0.75)

/* 36.104 annex B.2, rms delay spread. Named so the refusal can be specific:
   a reader deserves to know which profiles were ruled out and by what. */
#define LTE_PROFILE_EPA_NS 45.0
#define LTE_PROFILE_EVA_NS 357.0
#define LTE_PROFILE_ETU_NS 991.0

/* A crystal this close to nominal has been calibrated or is a better part
   than a stock dongle ships with; ADR-0004's calibration works in ppm and
   this is the same unit. */
#define LTE_CRYSTAL_GOOD_PPM 2.0

/*
 * The smallest delay spread this can distinguish from noise, at a given
 * signal to noise: each step carries about 1/sqrt(rho) of phase error, and
 * below that the scatter is the receiver rather than the channel.
 */
static inline double lte_spread_floor_ns(double sinr_db) {
    double rho = pow(10.0, sinr_db / 10.0);

    if (!(rho > 0.0))
        return LTE_SPREAD_SATURATION_NS;
    return LTE_SPREAD_PER_RADIAN_NS / sqrt(rho);
}

/* Where a spread sits among the 36.104 annex B.2 profiles. They are conformance
   models rather than a taxonomy of places, so this says where the measurement
   falls among them and never that the channel "is" one. */
static inline const char *lte_spread_against_profiles(double ns) {
    if (ns < LTE_PROFILE_EPA_NS)
        return "flatter than EPA's 45 ns";
    if (ns < LTE_PROFILE_EVA_NS)
        return "between EPA's 45 ns and EVA's 357";
    if (ns < LTE_PROFILE_ETU_NS)
        return "between EVA's 357 ns and ETU's 991";
    return "past ETU's 991 ns";
}

struct lte_findings {
    char line[LTE_FINDING_LINES][LTE_FINDING_TEXT];
    int count;
};

static inline void lte_finding_add(struct lte_findings *f, const char *fmt,
                                   double a, double b) {
    if (!f || f->count >= LTE_FINDING_LINES)
        return;
    snprintf(f->line[f->count], LTE_FINDING_TEXT, fmt, a, b);
    f->count++;
}

/* A finding with no number in it. There is exactly one, and it says so. */
static inline void lte_finding_text(struct lte_findings *f, const char *text) {
    if (!f || f->count >= LTE_FINDING_LINES)
        return;
    snprintf(f->line[f->count], LTE_FINDING_TEXT, "%s", text);
    f->count++;
}

/*
 * `carrier_hz` is what the receiver is tuned to; it turns an offset in
 * kilohertz into parts per million and a drift in hertz into a speed, and
 * neither conversion means anything without it.
 *
 * Returns how many lines were written.
 */
static inline int lte_findings_from(const struct lte_cell_stats *st,
                                    double carrier_hz,
                                    struct lte_findings *out) {
    double ppm, spread, drift_span, kmh_per_hz;

    if (!out)
        return 0;
    /* Cleared whole, not just the counter: the struct is handed to a drawing
       path and a caller that trusted `count` was the only guard would still
       be carrying whatever was on the stack in the lines past it. */
    memset(out, 0, sizeof(*out));
    if (!st || !st->valid || !st->pss.count || !(carrier_hz > 0.0))
        return 0;

    /* Tuning, in the unit that transfers between technologies: the GSM
       calibration reads ppm too, so the two can be compared. */
    if (st->frequency_khz.count) {
        ppm = (double)lte_stat_mean(&st->frequency_khz) * 1e3 / carrier_hz
              * 1e6;
        if (fabs(ppm) <= LTE_CRYSTAL_GOOD_PPM)
            lte_finding_add(out, "Tuning %+.1f ppm: within %.0f, so the "
                                 "crystal is calibrated or better than stock.",
                            ppm, LTE_CRYSTAL_GOOD_PPM);
        else
            lte_finding_add(out, "Tuning %+.1f ppm: a stock crystal, "
                                 "uncalibrated -- %.0f kHz here.",
                            ppm, fabs(ppm) * carrier_hz / 1e6 / 1e3);
    }

    /* How well the cell was heard. RSRQ and RS-SINR are ratios and carry no
       calibration; the level does, so it is deliberately not judged here. */
    if (st->sinr_db.count) {
        double sinr = (double)lte_stat_mean(&st->sinr_db);
        double rsrq = (double)lte_stat_mean(&st->rsrq_db);
        if (sinr >= 20.0)
            lte_finding_add(out, "Clean: RS-SINR %.0f dB, RSRQ %.0f dB. Far "
                                 "more than the broadcast channel needs.",
                            sinr, rsrq);
        else if (sinr >= 10.0)
            lte_finding_add(out, "Workable: RS-SINR %.0f dB, RSRQ %.0f dB. "
                                 "The broadcast decodes; little to spare.",
                            sinr, rsrq);
        else
            lte_finding_add(out, "Marginal: RS-SINR %.0f dB, RSRQ %.0f dB. "
                                 "Expect the broadcast to come and go.",
                            sinr, rsrq);
    }

    /*
     * The channel's dispersion, against what can be resolved rather than
     * against a profile -- see the header. Above the floor is a real finding;
     * below it the only honest statement is that it is below it.
     */
    if (st->spread_ns.count) {
        /*
         * The claim and its limit are one line, not two. They were two, the
         * panel ran out of room after the first, and a reader saw
         * "dispersive, 1.4 us" and never the sentence qualifying it. A caveat
         * that can be dropped separately from its claim is not a caveat.
         */
        char text[LTE_FINDING_TEXT];
        double sinr = st->sinr_db.count
                          ? (double)lte_stat_mean(&st->sinr_db) : 0.0;
        double floor_ns = lte_spread_floor_ns(sinr);

        spread = (double)lte_stat_mean(&st->spread_ns);
        if (spread < floor_ns)
            snprintf(text, sizeof(text),
                     "Flat: %.0f ns of spread, under the %.0f ns that %.0f dB "
                     "of RS-SINR can tell from noise.",
                     spread, floor_ns, sinr);
        else if (spread >= LTE_SPREAD_SATURATION_NS)
            /*
             * Past a radian the steps wrap, and a wrapped scatter is not a
             * delay at all. The number is reported because hiding it would
             * leave a reader wondering, but it is named for what it is.
             */
            snprintf(text, sizeof(text),
                     "Beyond measure: %.1f us is past the %.1f us where "
                     "90 kHz references wrap. More dispersive than this says.",
                     spread / 1e3, LTE_SPREAD_SATURATION_NS / 1e3);
        else if (spread >= LTE_SPREAD_COMPRESSED_NS)
            /* Approaching a radian a step stops growing with the channel, so
               this is where the number is a bound and not a value. */
            snprintf(text, sizeof(text),
                     "Dispersive: %.1f us, %s -- reading low as it nears the "
                     "%.1f us wrap, so a floor.",
                     spread / 1e3, lte_spread_against_profiles(spread),
                     LTE_SPREAD_SATURATION_NS / 1e3);
        else
            snprintf(text, sizeof(text),
                     "Spread %.0f ns, %s. Above the %.0f ns floor that "
                     "%.0f dB of RS-SINR sets.",
                     spread, lte_spread_against_profiles(spread), floor_ns,
                     sinr);
        lte_finding_text(out, text);
    }

    /* Motion, and why it is not measurable here. */
    if (st->drift_hz.count) {
        drift_span = (double)(st->drift_hz.max - st->drift_hz.min);
        kmh_per_hz = 299792458.0 / carrier_hz * 3.6;
        lte_finding_add(out, "Drift spans %.0f Hz: %.0f km/h if it were "
                             "Doppler. It is the crystal -- the two are one "
                             "phase.",
                        drift_span, drift_span * kmh_per_hz);
    }

    /* And the question the numbers cannot reach at all. */
    lte_finding_text(out, "Indoor or outdoor is not measured: this describes "
                          "the path, not where either end stands.");
    return out->count;
}

#endif
