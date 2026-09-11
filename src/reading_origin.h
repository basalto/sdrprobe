#ifndef READING_ORIGIN_H
#define READING_ORIGIN_H

#include <math.h>

/*
 * Whose oscillator made this signal, from where its frequency reads.
 *
 * A survey reports a candidate at a frequency, and the frequency is not the
 * truth: an uncalibrated receiver is wrong about where everything is. The
 * usual reading of that is a blur -- "the ppm error makes this uncertain by
 * 4.6 kHz, so we cannot say" -- and it is the wrong way round. The error does
 * not blur a reading, it *displaces* it by a known amount, and the
 * displacement is the measurement: a tone generated from the receiver's own
 * reference does not get displaced at all, because the same error is in the
 * tuning, in the sample rate and in the tone, and it cancels.
 *
 * So one subtraction separates a signal clocked inside this receiver from one
 * that arrived through the antenna, needs no second room and no second
 * receiver, and works on evidence already recorded.
 * `.scratch/device-model/issues/11-*` is the derivation and the measurement.
 *
 * This is arithmetic over three numbers and nothing else. No receiver, no
 * window, no samples: ADR-0012 is satisfied by construction, and
 * tests/reading_origin_test.c is the whole of it.
 */

#define READING_PPM 1e-6

/*
 * The two numbers the whole of this needs, and it is two rather than one.
 *
 * `crystal_ppm` is what the receiver's *own reference* is doing -- the value a
 * calibration converges to, which `sdr_dsp_corrected_ppm()` produces and
 * `installation_ppm()` remembers. The RTL2832U here measures -31 to -36
 * depending on the day. **0 means nobody has measured it**, which is a refusal
 * and not a perfect crystal.
 *
 * `applied_ppm` is the correction in force, which librtlsdr applies to the
 * tuner and the resampler alike. It is usually the calibrated value, because
 * the program restores a stored calibration at startup and applies it.
 *
 * **Taking one number -- `crystal - applied` -- was the first version of this
 * and it was dead code.** A correction that is applied is applied *because* it
 * was measured, so that difference is zero on every calibrated receiver and
 * zero again on every uncalibrated one, and the whole test never fires. The
 * unit suite passed throughout, because a unit test hands the number in. What
 * is wanted is the crystal's error for *how far apart* the two hypotheses are
 * and the correction for *which of them sits where*, and those are different
 * questions.
 */
struct reading_clock {
    double crystal_ppm;
    double applied_ppm;
};

/*
 * The model, exactly.
 *
 * Write `k` for the crystal's fractional error and `c` for the correction in
 * force, so the receiver's *residual* error is `e = k - c`. One crystal clocks
 * both the tuner's synthesiser and the ADC, which is what makes the rest exact
 * rather than approximate.
 *
 * A tone at true frequency `f` mixes to a baseband of `f - f_cmd*(1 + e)`,
 * where `f_cmd` is the tuning the program asked for. It is digitised at
 * `rate*(1 + e)` and the program divides the bin by `rate`, so it reports
 *
 *     f_cmd + (f - f_cmd*(1 + e)) / (1 + e)  =  f / (1 + e)
 *
 * **The tuning cancels.** That is worth stating because ticket 11 derived a
 * first-order form carrying an `f_cmd * e` term and called the remainder
 * negligible; it is not merely negligible, it is absent, and the two forms
 * differ by `(f - f_cmd) * e` -- under 31 Hz at a megahertz of baseband and
 * 31 ppm, against the several hundred hertz a confirmation pass can actually
 * place a candidate to.
 *
 * Now the two hypotheses for a candidate near a nominal frequency `N`.
 *
 *   **External.** Its true frequency is `N`, so it reads `N / (1 + e)`.
 *   **Clock-coherent.** Its true frequency is `N * (1 + k)` -- the crystal's,
 *   whatever correction the software applies afterwards -- so it reads
 *   `N * (1 + k) / (1 + e)`.
 *
 * Subtract: the two readings are `N * k / (1 + e)` apart, which to first order
 * is **`N * k` and does not involve the correction at all.** Correcting the
 * ppm slides both readings together and separates them by not one hertz more
 * or less. What it does change is which one sits on `N`: uncorrected (`c = 0`)
 * the coherent tone reads exactly `N` and the external one is displaced;
 * correctly corrected (`c = k`) the external one reads exactly `N` and the
 * coherent one is displaced. **The discriminator inverts rather than
 * improving**, and that is ticket 11's warning made executable.
 */

/* The residual error actually in force: what is left after the correction. */
static inline double reading_residual_ppm(struct reading_clock clock) {
    return clock.crystal_ppm - clock.applied_ppm;
}

/* Where a tone of true frequency `true_hz` is reported, whatever made it. */
static inline double reading_from_true_hz(double true_hz,
                                          struct reading_clock clock) {
    double e = reading_residual_ppm(clock) * READING_PPM;

    if (!(true_hz > 0.0) || e <= -1.0)
        return 0.0;
    return true_hz / (1.0 + e);
}

/* And back: the true frequency a reading implies. The inverse of the line
   above, and the reason both exist is that a caller usually has one and wants
   the other. */
static inline double reading_true_hz(double reading_hz,
                                     struct reading_clock clock) {
    double e = reading_residual_ppm(clock) * READING_PPM;

    if (!(reading_hz > 0.0) || e <= -1.0)
        return 0.0;
    return reading_hz * (1.0 + e);
}

/* Where an external signal on the nominal frequency `hz` reads. */
static inline double reading_external_hz(double hz,
                                         struct reading_clock clock) {
    return reading_from_true_hz(hz, clock);
}

/* And where one clocked by this receiver's own reference reads: its true
   frequency is the crystal's, so the `(1 + k)` is applied before the
   receiver's `(1 + e)` is divided out. */
static inline double reading_coherent_hz(double hz,
                                         struct reading_clock clock) {
    if (!(hz > 0.0))
        return 0.0;
    return reading_from_true_hz(hz * (1.0 + clock.crystal_ppm * READING_PPM),
                                clock);
}

/*
 * How far apart the two hypotheses put a candidate near `hz`: the whole
 * quantity being tested for.
 *
 * `hz * k`, to first order, and **independent of the correction**. A reading
 * is at one of the two, or at neither, and there is nothing in between that
 * either explains.
 */
static inline double reading_separation_hz(double hz,
                                           struct reading_clock clock) {
    double coherent = reading_coherent_hz(hz, clock);
    double external = reading_external_hz(hz, clock);

    if (!(coherent > 0.0) || !(external > 0.0))
        return 0.0;
    return coherent - external;
}

/*
 * What a reading says about whose oscillator produced it.
 *
 * Four answers and not two, because "cannot say" and "neither hypothesis fits"
 * are different things and a reader acts differently on each.
 */
enum reading_origin {
    /*
     * The question cannot be asked here. Either there is no residual to
     * displace anything by -- see the refusal below -- or an argument is
     * missing.
     */
    READING_ORIGIN_UNKNOWN = 0,
    /*
     * The reading sits at its nominal frequency, where a displaced tone
     * cannot. Whatever produces it is clocked coherently with this receiver's
     * reference.
     *
     * It says *coherent with this reference*, and deliberately not which
     * oscillator or which divider: 75.000000 and 150.000000 MHz read exact
     * here and 25 MHz is not 28.8/n, so how a 28.8 MHz reference comes to
     * produce them is unexplained. A fractional-N synthesiser would do it, and
     * so would the family being something other than 25 MHz x n that happens
     * to hit both. "Belongs to the receiver" can only mean this operationally.
     */
    READING_ORIGIN_RECEIVER,
    /*
     * The reading sits where a tone at the nominal frequency would be
     * displaced to. Its oscillator is not this receiver's.
     *
     * Which is **not** the same as "a transmitter". A second receiver on the
     * desk, a powered hub, a monitor -- anything with its own crystal reads
     * this way. What has been established is that the error in this reading is
     * not the error in this receiver.
     */
    READING_ORIGIN_EXTERNAL,
    /*
     * Neither. The reading is not at the nominal frequency and not at the
     * displacement from it either, so the nominal offered does not explain it.
     *
     * This is a finding rather than a default, and having it is the point:
     * without it a frequency on no comb and no channel raster is indexed the
     * same as one nobody asked about, and "unremarked" carries two meanings.
     */
    READING_ORIGIN_UNEXPLAINED
};

/*
 * How much displacement it takes before the two hypotheses are separable.
 *
 * The coherent answer sits at `nominal +/- tolerance` and the external one at
 * `nominal + displacement +/- tolerance`. Those windows are disjoint exactly
 * when the displacement exceeds twice the tolerance, and below that a reading
 * in the overlap belongs to both -- so two is the honest bar and not a chosen
 * one.
 *
 * The measured case clears it comfortably rather than narrowly, which is the
 * only reason any of this works at the precision available: a confirmation
 * pass places a candidate to about one 977 Hz bin -- measured on the three
 * comb families, which are internal by construction and so read their own
 * precision, at +159, +526 and +793 Hz -- against 4.1 to 4.6 kHz of
 * separation at 132 to 150 MHz. Four times over, not two.
 *
 * And the bar is a *frequency* bar, so it is not cleared at all below a few
 * tens of megahertz: at 31 ppm, twice a 977 Hz tolerance needs 63 MHz of
 * carrier. Low enough and this says UNKNOWN, correctly.
 */
#define READING_SEPARABLE_TOLERANCES 2.0

/*
 * Whether a reading near `nominal_hz` can be asked the question at all.
 *
 * The refusal that matters is `crystal_ppm == 0`, and it is **not** the same
 * as "the correction is right". A receiver whose reference has never been
 * measured has no discriminator, because the separation is the crystal's own
 * error and nobody knows it. A receiver corrected to its own measurement has
 * one and it is exactly as good -- the two hypotheses have simply swapped
 * which of them sits on the nominal.
 *
 * Getting that backwards is what made the first version of this dead code; see
 * `struct reading_clock`.
 */
static inline int reading_origin_separable(double nominal_hz,
                                           struct reading_clock clock,
                                           double tolerance_hz) {
    if (!(nominal_hz > 0.0) || !(tolerance_hz > 0.0))
        return 0;
    return fabs(reading_separation_hz(nominal_hz, clock)) >
           tolerance_hz * READING_SEPARABLE_TOLERANCES;
}

/*
 * The verdict for one reading against one nominal frequency the caller
 * proposes for it -- a comb multiple, a channel on a raster, whatever the
 * caller has a reason to name.
 *
 * The nominal is the caller's because this header has no business inventing
 * grids. A "round frequency" grid would catch 150.000000 and 135.000000 and
 * would also catch one candidate in however many by luck, and choosing its
 * spacing is a decision with evidence behind it
 * (`.scratch/device-model/issues/10-*`), not a default.
 */
static inline enum reading_origin reading_origin_for(double reading_hz,
                                                     double nominal_hz,
                                                     struct reading_clock clock,
                                                     double tolerance_hz) {
    if (!(reading_hz > 0.0) || !(nominal_hz > 0.0) || !(tolerance_hz > 0.0))
        return READING_ORIGIN_UNKNOWN;
    if (!reading_origin_separable(nominal_hz, clock, tolerance_hz))
        return READING_ORIGIN_UNKNOWN;
    /*
     * Both hypotheses are asked where they actually land, rather than one of
     * them being assumed to sit on the nominal. That assumption holds only on
     * an uncorrected receiver and is the thing that inverts under calibration.
     */
    if (fabs(reading_hz - reading_coherent_hz(nominal_hz, clock)) <=
        tolerance_hz)
        return READING_ORIGIN_RECEIVER;
    if (fabs(reading_hz - reading_external_hz(nominal_hz, clock)) <=
        tolerance_hz)
        return READING_ORIGIN_EXTERNAL;
    return READING_ORIGIN_UNEXPLAINED;
}

/*
 * The best of several nominals, for a caller with more than one grid to offer.
 *
 * Precedence is RECEIVER, then EXTERNAL, then UNEXPLAINED, then UNKNOWN --
 * an explanation beats no explanation, and a reading that both a comb and a
 * channel could account for is the receiver's, because a comb multiple is
 * evidence a channel number is not.
 *
 * UNEXPLAINED outranks UNKNOWN deliberately: a nominal that was separable and
 * fitted neither hypothesis has said something, and a nominal too low in
 * frequency to separate anything has not.
 */
static inline enum reading_origin reading_origin_best(enum reading_origin a,
                                                      enum reading_origin b) {
    if (a == READING_ORIGIN_RECEIVER || b == READING_ORIGIN_RECEIVER)
        return READING_ORIGIN_RECEIVER;
    if (a == READING_ORIGIN_EXTERNAL || b == READING_ORIGIN_EXTERNAL)
        return READING_ORIGIN_EXTERNAL;
    if (a == READING_ORIGIN_UNEXPLAINED || b == READING_ORIGIN_UNEXPLAINED)
        return READING_ORIGIN_UNEXPLAINED;
    return READING_ORIGIN_UNKNOWN;
}

/* The nearest multiple of `spacing_hz`, or 0 when there is no grid or the
   nearest multiple is not a frequency. */
static inline double reading_nearest_multiple_hz(double hz,
                                                 double spacing_hz) {
    double n;

    if (!(hz > 0.0) || !(spacing_hz > 0.0))
        return 0.0;
    n = floor(hz / spacing_hz + 0.5);
    return n >= 1.0 ? n * spacing_hz : 0.0;
}

/*
 * The nearest channel of a raster that starts at `base_hz` and steps by
 * `spacing_hz`.
 *
 * A base as well as a spacing, because a channel raster is rarely a set of
 * multiples: band II runs 87.5 MHz plus a multiple of 100 kHz, and the
 * airband 118 MHz plus a multiple of 25/3 kHz. Getting the base wrong puts
 * every channel half a raster out, which reads as an unexplained signal
 * everywhere.
 */
static inline double reading_nearest_channel_hz(double hz, double base_hz,
                                                double spacing_hz) {
    double n;

    if (!(hz > 0.0) || !(spacing_hz > 0.0) || base_hz < 0.0)
        return 0.0;
    n = floor((hz - base_hz) / spacing_hz + 0.5);
    if (n < 0.0)
        return 0.0;
    return base_hz + n * spacing_hz;
}

/*
 * The grid point each hypothesis would put a reading on, which is **not** the
 * same grid point.
 *
 * This is the trap in the whole method and it is not the obvious one. The
 * coherent hypothesis says the reading is already at its nominal, so its grid
 * point is the one nearest the reading. The external hypothesis says the
 * reading is displaced, so its grid point is the one nearest the *inverted*
 * reading -- and taking the nearest to the raw reading instead picks the wrong
 * channel whenever the raster is finer than twice the displacement.
 *
 * It is not a hypothetical. 132.062744 MHz on the airband's 8.33 kHz raster:
 * the nearest channel to the reading is **132.066667**, 3.9 kHz below it,
 * while the channel an external transmitter would have to be on is
 * **132.058333**, which the inverted reading lands 343 Hz from. Ticket 11's
 * own table names the second and the arithmetic that finds it is this. One
 * step of 8333 Hz against 4067 Hz of displacement is all it takes, and every
 * narrow-channel service is in that regime.
 *
 * The reading is inverted rather than the channel displaced because the
 * caller is asking "which channel could this be", and there is exactly one
 * answer; displacing candidate channels forward asks the question once per
 * channel.
 */
static inline double reading_external_channel_hz(double reading_hz,
                                                 double base_hz,
                                                 double spacing_hz,
                                                 struct reading_clock clock) {
    return reading_nearest_channel_hz(reading_true_hz(reading_hz, clock),
                                      base_hz, spacing_hz);
}

/* The same, for a grid of multiples -- a reference comb rather than a channel
   raster. A comb is a receiver's own, so this is the *contradiction*: the
   multiple an external source would have to sit on to read where a comb tone
   reads. Worth having so a caller can put both hypotheses on one grid. */
static inline double reading_external_multiple_hz(double reading_hz,
                                                  double spacing_hz,
                                                  struct reading_clock clock) {
    return reading_nearest_multiple_hz(reading_true_hz(reading_hz, clock),
                                       spacing_hz);
}

/*
 * Whether a raster is coarse enough for a channel to be named at all.
 *
 * Two channels are told apart only if the tolerance fits between them, so a
 * raster finer than twice the tolerance names a channel by rounding rather
 * than by evidence. At a confirmation pass's 977 Hz that admits the airband's
 * 8.33 kHz and band II's 100 kHz comfortably and refuses anything under about
 * 2 kHz -- which is the right answer for, say, a 1 kHz-stepped raster nobody
 * here can resolve.
 */
static inline int reading_raster_is_resolvable(double spacing_hz,
                                               double tolerance_hz) {
    return spacing_hz > 0.0 && tolerance_hz > 0.0 &&
           spacing_hz > tolerance_hz * 2.0;
}

/* Short enough for a column, and worded as what was measured. */
static inline const char *reading_origin_name(enum reading_origin origin) {
    switch (origin) {
    case READING_ORIGIN_RECEIVER:
        return "clocked here";
    case READING_ORIGIN_EXTERNAL:
        return "not clocked here";
    case READING_ORIGIN_UNEXPLAINED:
        return "unexplained";
    default:
        return "";
    }
}

/*
 * The sentence, for a panel or a report line. NULL when there is nothing to
 * say, so a caller can leave the line out rather than print "unknown".
 *
 * Every one of these is a statement about a *reading*, never about a
 * transmitter: ADR-0015's rule that a measurement is reported as a measurement
 * applies with more force here than on the band plan, because "this is the
 * receiver" is the kind of sentence that stops somebody looking.
 */
static inline const char *reading_origin_reason(enum reading_origin origin) {
    switch (origin) {
    case READING_ORIGIN_RECEIVER:
        return "reads at its exact nominal, where an external signal "
               "could not: clocked with this receiver";
    case READING_ORIGIN_EXTERNAL:
        return "reads displaced by this receiver's own error: "
               "its oscillator is not this one";
    case READING_ORIGIN_UNEXPLAINED:
        return "on no modelled comb and no channel raster";
    default:
        return NULL;
    }
}

#endif
