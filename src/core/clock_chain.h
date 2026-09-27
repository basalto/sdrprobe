#ifndef CLOCK_CHAIN_H
#define CLOCK_CHAIN_H

#include <math.h>

/*
 * A clock family that goes up in octaves rather than in harmonics.
 *
 * `survey_suspect.h`'s reference comb is "a tone every reference/n", which is
 * what a divider leaves across the band and what an RTL2832U's 14.4 MHz and
 * 1.6 MHz families are. This is a different shape and needs a different rule:
 * **f, 2f, 4f, 8f and never 3f**, which is what a doubler or divider chain
 * produces and what harmonic distortion of one oscillator does not.
 *
 * The distinction is not decoration; it is the measurement.
 * `.scratch/device-model/issues/10-*` swept seven 2 MHz windows at `--ppm 0`
 * on 2026-09-11, a 976.6 Hz bin, an 8 dB prominence bar:
 *
 *     75.000000   present  +488 Hz   17.0 dB
 *     150.000000  present  +488 Hz   11.2 dB
 *     300.000000  present  +488 Hz   17.0 dB
 *     37.5, 175, 225, 600   absent, nearest 61 to 369 kHz away
 *
 * All three present members read **+488 Hz, exactly half a bin** -- the
 * quantisation of a tone reported at its bin's centre, the same at all three,
 * so it is the grid and not the sources. An external source would have read
 * 2.4, 4.8 and 9.6 kHz low.
 *
 * **225 MHz is the finding.** It is 75 x 3, and it is absent at a 12 dB bar
 * while 75 x 1, x 2 and x 4 stand at 11 to 17 dB. A harmonic model would flag
 * it; an octave model does not. That asymmetry is why this file exists rather
 * than a second `survey_comb_spacing_hz()`.
 *
 * Plain arithmetic, no device and no table, checked by
 * tests/clock_chain_test.c (ADR-0012).
 */

/*
 * **The fundamental is a parameter and there is no `device_profile` field.**
 *
 * What has been established is a family on *this* receiver at *this* site.
 * Putting it in `device_profile` would assert it of the part, which is the
 * mistake `.scratch/calibrating-the-flags/` was opened to record and the one
 * `.scratch/device-model/issues/02-*` refuses on the rate-range hole. A second
 * receiver is what turns "this chip does this" into a fact about a chip.
 *
 * The measured value is named below so a caller has something to pass, and it
 * is named as a *site* measurement rather than as a device constant. Read the
 * caveat before using it anywhere a second receiver might appear.
 */
#define CLOCK_CHAIN_MEASURED_FUNDAMENTAL_HZ 75000000.0

/*
 * How far up the chain to look.
 *
 * Four octaves covers 75 to 600 MHz from the measured fundamental, which is
 * one step past the highest member found -- 600 MHz was swept and came back
 * empty, though only at the default 8 dB bar, which is a weaker statement than
 * the four absences above. Looking further up costs nothing and finds nothing;
 * the bound exists so an absurd fundamental cannot loop for ever.
 */
#define CLOCK_CHAIN_MAX_OCTAVES 8

/*
 * Which octave of `fundamental_hz` the frequency sits on, or 0 for none.
 *
 * Returns the multiplier -- 1, 2, 4, 8 ... -- so a caller can say *which*
 * member, the way `survey_reference_harmonic()` returns a harmonic number. A
 * fundamental of 0 means no chain is modelled and the answer is always 0,
 * which is the case a receiver nobody has measured is in, and a capture.
 */
static inline int clock_chain_octave(double hz, double fundamental_hz,
                                     double tolerance_hz) {
    int octave;

    if (!(hz > 0.0) || !(fundamental_hz > 0.0) || !(tolerance_hz >= 0.0))
        return 0;
    for (octave = 1; octave <= (1 << CLOCK_CHAIN_MAX_OCTAVES); octave *= 2) {
        double member = fundamental_hz * (double)octave;

        if (member > hz + tolerance_hz)
            break;
        if (fabs(hz - member) <= tolerance_hz)
            return octave;
    }
    return 0;
}

/*
 * The member of the chain nearest `hz`, or 0 when there is no chain.
 *
 * For a caller that wants a nominal frequency to test a reading against --
 * `reading_origin_for()` takes one -- rather than a yes or no. Unlike a comb,
 * a chain is sparse, so "nearest" can be a long way away and the caller is
 * expected to let the tolerance decide rather than trusting the distance.
 */
static inline double clock_chain_nearest_hz(double hz, double fundamental_hz) {
    double best = 0.0, best_gap = 0.0;
    int octave;

    if (!(hz > 0.0) || !(fundamental_hz > 0.0))
        return 0.0;
    for (octave = 1; octave <= (1 << CLOCK_CHAIN_MAX_OCTAVES); octave *= 2) {
        double member = fundamental_hz * (double)octave;
        double gap = fabs(hz - member);

        if (best == 0.0 || gap < best_gap) {
            best = member;
            best_gap = gap;
        }
    }
    return best;
}

/*
 * Whether a frequency is an *odd* multiple of the fundamental -- 3f, 5f, 6f --
 * which is the thing an octave chain does **not** produce and a harmonic comb
 * does.
 *
 * It exists so a check can assert the difference rather than describe it, and
 * so a future reader who finds 225 MHz on air has a name for what that would
 * mean: not this chain, and evidence that the model is wrong.
 */
static inline int clock_chain_is_off_octave(double hz, double fundamental_hz,
                                            double tolerance_hz) {
    double n;

    if (!(hz > 0.0) || !(fundamental_hz > 0.0))
        return 0;
    n = floor(hz / fundamental_hz + 0.5);
    if (n < 1.0 || fabs(hz - n * fundamental_hz) > tolerance_hz)
        return 0;   /* not a multiple at all */
    return clock_chain_octave(hz, fundamental_hz, tolerance_hz) == 0;
}

#endif /* CLOCK_CHAIN_H */
