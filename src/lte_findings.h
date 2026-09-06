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
 * **The 3GPP channel profile cannot be named.** EPA, EVA and ETU (36.104
 * annex B.2) have rms delay spreads of 45, 357 and 991 nanoseconds, and this
 * receiver resolves about 1010: reference signals sit six subcarriers apart,
 * so twelve of them span 990 kHz and the delay resolution is its reciprocal.
 * **All three profiles sit under the floor.** Printing "EVA" from a
 * measurement that cannot separate it from EPA would be inventing the answer
 * a reader wanted.
 *
 * **Motion cannot be separated from the crystal.** A Doppler shift and a
 * residual tuning error are the same phase. At 927 MHz one hertz is 1.16 km/h
 * and the drift here spans tens to hundreds of hertz, so what is measured is
 * the receiver's own oscillator with any motion buried inside it.
 *
 * And indoor against outdoor is not measured at all: what arrives describes
 * the path it took, not where either end of it stands.
 *
 * Pure arithmetic over the statistics -- no window, no receiver, no samples
 * (ADR-0012).
 */

#define LTE_FINDING_LINES 8
#define LTE_FINDING_TEXT 120

/* References six subcarriers apart, twelve of them: 11 * 90 kHz of span, and
   the delay resolution is one over that. */
#define LTE_DELAY_RESOLUTION_NS 1010.0

/* 36.104 annex B.2, rms delay spread. Named so the refusal can be specific:
   a reader deserves to know which profiles were ruled out and by what. */
#define LTE_PROFILE_EPA_NS 45.0
#define LTE_PROFILE_EVA_NS 357.0
#define LTE_PROFILE_ETU_NS 991.0

/* A crystal this close to nominal has been calibrated or is a better part
   than a stock dongle ships with; ADR-0004's calibration works in ppm and
   this is the same unit. */
#define LTE_CRYSTAL_GOOD_PPM 2.0

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
         * The claim and its limit are one line, not two.
         *
         * They were two, and the panel ran out of room after the first: a
         * reader saw "dispersive, 1.4 us" and never the sentence saying no
         * 3GPP profile follows from it. A caveat that can be dropped
         * separately from the claim it qualifies is not a caveat.
         */
        spread = (double)lte_stat_mean(&st->spread_ns);
        if (spread >= LTE_DELAY_RESOLUTION_NS)
            lte_finding_add(out, "Dispersive: %.1f us of spread, over the "
                                 "%.1f us resolved -- so no 3GPP profile "
                                 "follows; EPA and EVA are both finer.",
                            spread / 1e3, LTE_DELAY_RESOLUTION_NS / 1e3);
        else
            lte_finding_add(out, "Flat: %.0f ns of spread, under the %.0f ns "
                                 "resolved -- so no 3GPP profile follows; "
                                 "EPA and EVA are both finer.",
                            spread, LTE_DELAY_RESOLUTION_NS);
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
