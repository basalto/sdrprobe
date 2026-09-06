#ifndef SIGNAL_PROBE_H
#define SIGNAL_PROBE_H

#include <stddef.h>

/*
 * What a signal is, for a signal nobody has identified.
 *
 * The survey says something is transmitting; the decode views say what a
 * *known* technology is saying. Between the two there is a gap, and this is
 * it: measurements that need nothing transcribed from any standard, so they
 * work before anybody knows what they are looking at.
 *
 * That is the rule for what belongs here and it is worth stating, because
 * without it this becomes a junk drawer: **a measurement belongs in
 * signal_probe when it needs no sequence.** Oerder-Meyr symbol timing needs
 * none; a cyclic-prefix autocorrelation needs none; a Zadoff-Chu correlation
 * needs the sequence and stays in lte_dsp.
 *
 * No window, no receiver, no samples beyond the ones handed in (ADR-0012).
 */

/*
 * Where a carrier is, and how much of the channel is standing in it.
 *
 * `in_line` is the fraction of the channel's energy inside the carrier's own
 * line, from 0 to 1. It is the measurement that separates two signals with
 * identical power and identical prominence: a bare tone puts nearly all of
 * its energy in one line, and anything carrying information spreads it across
 * the bandwidth it occupies.
 *
 * On air: a recording of 75.000 MHz reads a carrier 57 dB over its floor with
 * no sideband at any ILS marker tone -- a clock harmonic reaching the antenna,
 * with nothing on it to decode -- where the survey could only say that
 * something was there and that the band plan calls the frequency
 * aeronautical.
 */
struct signal_carrier {
    int found;
    double offset_hz;     /* from the centre the samples were taken at */
    double magnitude;     /* of the line itself */
    double line_over_floor_db;
    double in_line;       /* 0 to 1 */
};

/*
 * The strongest line between `low_hz` and `high_hz`, **skipping `guard_hz`
 * either side of zero**.
 *
 * The guard is not optional politeness. The receiver's own DC offset sits at
 * exactly +0 Hz and is the strongest thing in any capture, at an empty
 * frequency as readily as an occupied one -- a search over a window that
 * contains zero finds it every time and then measures its sidebands. Three
 * runs of this analysis were thrown away to that before the guard existed,
 * and the tell was a control at an empty frequency reporting a *stronger*
 * carrier than the signal under test.
 *
 * So: tune off the signal, or pass a guard. A caller that genuinely wants to
 * look at zero passes 0 and means it.
 *
 * `channel_hz` is the width the energy is compared against -- the occupied
 * bandwidth the survey measured, or a few times the widest thing expected.
 *
 * Returns 1 when a carrier was found.
 */
int signal_find_carrier(const float *i_samples, const float *q_samples,
                        size_t pair_count, double sample_rate,
                        double low_hz, double high_hz, double guard_hz,
                        double channel_hz, struct signal_carrier *out);

/*
 * Above this fraction of the channel standing in one constant, there is
 * nothing riding the carrier worth looking for. Measured: a synthetic tone in
 * no noise reads 1.00 and the 75.000 MHz recording 0.87, against 0.00 for the
 * modulated 1090 MHz carrier and 0.00 for an empty frequency.
 */
/* Probes across the search window, medianed. Enough that a handful landing
   on other signals cannot move the answer. */
#define SIGNAL_FLOOR_PROBES 33

#define SIGNAL_BARE_TONE 0.80

/*
 * And a line has to *be* there before its shape means anything. This is the
 * mistake the tool made on its first run over real captures: an empty
 * frequency has no constant in it either, so `in_line` reads 0.00 there
 * exactly as it does for a busy channel, and calling that "modulated" is
 * reporting a signal where there is none.
 *
 * The threshold is measured, not chosen. A search over thousands of
 * frequencies takes the largest of thousands of noise samples, so pure noise
 * reliably produces a "line": ten draws gave 8.2 to 13.6 dB over the median
 * floor. Fifteen sits above that, against 49.8 dB for the bare carrier at
 * 75.000 MHz. Below it the honest answer is that nothing was found -- which
 * is also the right answer for a *pulsed* transmission like Mode S, whose
 * energy is real and whose standing carrier is not: adsb_cpr_pair.bin reads
 * -2.0 dB and is correctly reported as no carrier rather than as a bare one.
 */
#define SIGNAL_LINE_PRESENT_DB 15.0

enum signal_verdict {
    SIGNAL_NOTHING = 0,   /* no line stands above the floor beside it */
    SIGNAL_BARE,          /* a line, and the channel is almost all of it */
    SIGNAL_MODULATED      /* a line, and there is much else beside it */
};

static inline enum signal_verdict
signal_carrier_verdict(const struct signal_carrier *c) {
    if (!c || !c->found || c->line_over_floor_db < SIGNAL_LINE_PRESENT_DB)
        return SIGNAL_NOTHING;
    return c->in_line >= SIGNAL_BARE_TONE ? SIGNAL_BARE : SIGNAL_MODULATED;
}

static inline const char *signal_verdict_name(enum signal_verdict v) {
    switch (v) {
    case SIGNAL_BARE:      return "a bare carrier";
    case SIGNAL_MODULATED: return "a modulated carrier";
    default:               return "no carrier";
    }
}

static inline int signal_is_bare_tone(const struct signal_carrier *c) {
    return signal_carrier_verdict(c) == SIGNAL_BARE;
}

#endif
