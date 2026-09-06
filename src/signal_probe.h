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
 * Where a carrier is, and how much of the channel is standing still in it.
 *
 * Neither field name is a term of art, because neither measurement is a
 * standard one, and borrowing a standard name for a near neighbour is how a
 * reader ends up trusting the wrong thing:
 *
 *   `carrier_over_noise_db` is *not* carrier-to-noise ratio. C/N is a carrier
 *   against noise integrated over a stated bandwidth; this is the carrier's
 *   line against the median of SIGNAL_FLOOR_PROBES single-frequency probes
 *   spread across the capture. It answers "is there a line here at all",
 *   and it is not comparable to a C/N quoted anywhere else.
 *
 *   `carrier_power_fraction` is the fraction of the channel's power sitting
 *   in the unmodulated part of the carrier, 0 to 1 -- the closest standard
 *   idea is the residual-carrier ratio of an AM signal, and this is measured
 *   over whatever channel width the caller passes rather than over a
 *   modulation the receiver knows. It separates two signals with identical
 *   power and identical prominence: a bare tone puts nearly all of its power
 *   in one constant, and anything carrying information spreads it across the
 *   bandwidth it occupies.
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
    double carrier_over_noise_db;  /* the line, over the median probe */
    double carrier_power_fraction; /* 0 to 1, how much of it stands still */
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

#define SIGNAL_BARE_FRACTION 0.80

/*
 * And a line has to *be* there before its shape means anything. This is the
 * mistake the tool made on its first run over real captures: an empty
 * frequency has no constant in it either, so `carrier_power_fraction` reads 0.00 there
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
#define SIGNAL_CARRIER_PRESENT_DB 15.0

enum signal_verdict {
    SIGNAL_NOTHING = 0,   /* no line stands above the floor beside it */
    SIGNAL_BARE,          /* a line, and the channel is almost all of it */
    SIGNAL_MODULATED      /* a line, and there is much else beside it */
};

static inline enum signal_verdict
signal_carrier_verdict(const struct signal_carrier *c) {
    if (!c || !c->found || c->carrier_over_noise_db < SIGNAL_CARRIER_PRESENT_DB)
        return SIGNAL_NOTHING;
    return c->carrier_power_fraction >= SIGNAL_BARE_FRACTION ? SIGNAL_BARE : SIGNAL_MODULATED;
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

/* ------------------------------------------------------------------ *
 * Does it have a symbol rate?
 * ------------------------------------------------------------------ */

/*
 * Oerder and Meyr, out of `tetra_dsp` where it was welded to 18 kBd.
 *
 * A linearly modulated signal with excess bandwidth puts a line at its symbol
 * rate in the squared magnitude, and the *phase* of that line is where the
 * symbols are. `tetra_dsp.c`'s own comment says this "is the same statistic
 * that says a carrier is TETRA at all", which is exactly why it belongs a
 * layer down: for a signal nobody has identified, the question is not "is the
 * line at 18 kBd strong" but "is there a line, and where".
 *
 * `signal_symbol_line()` is the measurement at one rate -- the generalisation,
 * with the two rates passed rather than compiled in. It returns the timing as
 * a fraction of a symbol period and, through `strength`, the size of the line
 * against the mean power: near zero for noise and for a constant-envelope
 * modulation, well above it for a filtered one.
 */
double signal_symbol_line(const float *i_samples, const float *q_samples,
                          size_t pair_count, double sample_rate,
                          double symbol_rate_bd, double *strength);

/*
 * There is deliberately no blind search for the rate here, and the reason is
 * measured rather than assumed.
 *
 * A scan of this line over 1-250 kBd, scored against a *local* floor so the
 * envelope's own low-frequency skirt cannot win, finds both TETRA captures at
 * 17998 Bd -- 0.01% off, at 37.8 and 37.5 times the floor beside them. It
 * also finds a line at 3466.9 Bd, at 28.6, in a 25 kHz slice of a GSM
 * capture. That is not an error: 3466.9 is twice GSM's 1733 Hz burst rate,
 * and it is a real periodicity in the envelope.
 *
 * Which is the finding. Oerder-Meyr detects periodicity in the squared
 * magnitude, and a burst grid is periodicity in the squared magnitude. At a
 * *known* rate the statistic answers "are the symbols here", which is what
 * tetra_dsp asks it and why that works; searched blind it cannot tell a symbol
 * rate from a frame rate, and 28.6 against 37.5 is not a separation anything
 * can be built on. `.scratch/signal-probe/issues/02-*.md` has the table.
 */

/* ------------------------------------------------------------------ *
 * Does it repeat?
 * ------------------------------------------------------------------ */

/*
 * A burst grid found from decided symbols alone, out of `tetra_dsp` where the
 * profile was sized to one TETRA timeslot.
 *
 * Its own comment there already said it "works before anybody knows which
 * technology this is", and needing nothing transcribed from a standard is the
 * rule for what belongs in this file. `profile[k]` is how often the symbol at
 * position k of the period equals the one a period earlier: a burst is a
 * *mixture* -- a training sequence near 1, data near the chance rate -- and
 * it is the mixture rather than any one value that says a grid is there.
 *
 * The caller must hand in symbols demodulated **contiguously**. This counts
 * matches at a fixed lag, so a stream stitched from chunks that each began at
 * their own timing phase has a discontinuity at every join and the profile
 * smears every position together.
 */
#define SIGNAL_PROFILE_MAX 512

struct signal_repeat {
    int period;         /* symbols, 0 when none stands out */
    float repeat;       /* matches at that period */
    float runner_up;    /* the best period that is not a multiple of it */
    int fixed;          /* profile positions above 0.9 */
    int varying;        /* profile positions below 0.4 */
    int profile_len;    /* 0 when the period is longer than the profile */
    float profile[SIGNAL_PROFILE_MAX];
};

int signal_repeat_find(const unsigned char *symbols, int count,
                       int low, int high, struct signal_repeat *out);

/* ------------------------------------------------------------------ *
 * Does it repeat at a period, in the samples themselves?
 * ------------------------------------------------------------------ */

/*
 * Both out of `scripts/signal_periodicity.c`, which was a `main()`, so nothing
 * in the program could call either.
 *
 * Neither has a sequence model in it, and both are immune to the receiver's
 * tuning error: a frequency offset contributes the same constant phase to
 * every term of a lag correlation, and the magnitude discards it. That is what
 * makes them usable on a signal nothing here can demodulate -- folding at a
 * period is how band 28 was found to be carrying 5G NR rather than a weak LTE
 * cell.
 */
double signal_lag_correlation(const float *i_samples, const float *q_samples,
                              size_t at, size_t lag, size_t window);

/*
 * Correlate against a copy `lag` later, fold the result over the lag, and see
 * whether one phase stands out. A burst that really sits at a fixed phase of
 * the period averages up while everything else averages down.
 *
 * `step` is how finely the period is divided, and it is raised when necessary
 * so the fold fits SIGNAL_FOLD_SLOTS: a fixed ceiling keeps this
 * allocation-free, and raising the step *lowers* the work, since the number of
 * correlations is `pair_count / step`.
 */
#define SIGNAL_FOLD_SLOTS 512

struct signal_fold {
    double peak;    /* the best slot's mean correlation */
    double floor;   /* the median slot -- what the peak must stand above */
    double ratio;   /* peak over floor: the number to read */
    size_t phase;   /* samples into the period where the peak sits */
    int slots;      /* how many the period was divided into */
    size_t step;    /* what the step was raised to, if it was */
};

int signal_fold_at(const float *i_samples, const float *q_samples,
                   size_t pair_count, size_t lag, size_t window, size_t step,
                   struct signal_fold *out);

#endif
