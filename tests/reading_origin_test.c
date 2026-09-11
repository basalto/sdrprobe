/*
 * check-reading-origin -- whose oscillator made this signal, from where its
 * frequency reads.
 *
 * `.scratch/device-model/issues/11-*`. The whole of it is arithmetic over
 * three numbers, so every case here is exact and none of it needs a receiver,
 * a window or a sample (ADR-0012).
 *
 * The three properties the ticket asks for, and they are asserted separately
 * because they fail separately:
 *
 *   1. **The forward model, both cases.** A tone at a known true frequency
 *      reports where the formula says, and a *clock-coherent* tone at the same
 *      tuning reports its exact nominal. They differ by which frequency
 *      carries the `(1 + d)`, and a sign slip makes one right and the other
 *      wrong.
 *   2. **The inversion is bounded.** At `d = 0` the two cases are identical,
 *      so the unit must refuse rather than return a verdict.
 *   3. **Unexplained is a verdict.** A reading that fits neither hypothesis is
 *      reported as such rather than as clean.
 *
 * And then the real capture, which is what makes the synthetic cases worth
 * anything: a synthetic signal agrees with whatever assumption built it.
 */

#include "check.h"

#include "reading_origin.h"

/*
 * This receiver, and **the sign is not the one ticket 11 wrote down.**
 *
 * `crystal_ppm` is the reference's own fractional error: the correction that,
 * applied, zeroes the residual. `installation_ppm()` stores exactly that, and
 * on 2026-09-11 `--calibrate gsm --arfcn 113` locked at `suggested_ppm 32`
 * over 874 measurements, sem 0.22. **This crystal is fast**, so an uncorrected
 * reading of a real transmitter comes back 4.2 kHz **low** at 132 MHz.
 *
 * The ticket said high, and the reason is a sign this repository has two
 * conventions for. `cal-measure` prints `observed_ppm -31.84` -- the
 * *residual*, `(measured - expected) / expected` -- and the ticket took that
 * number for the crystal error. It is its negation. Reported frequency is
 * `f / (1 + k)`, so a residual of -31.84 ppm is a crystal error of +31.84, and
 * everything moves the other way.
 *
 * Nothing about ticket 11's conclusions changes: every "reads exact, therefore
 * clocked here" verdict is about being *at* the nominal and cannot care which
 * way the other hypothesis lies. What changes is which channel its one
 * external carrier is on -- see `test_the_airband_pass()`.
 *
 * The historical figures are all residuals as printed, so they are negated
 * here: GSM ARFCN 113 gave -31.3 and later -31.84 and -35.96, LTE EARFCN 6200
 * gave -32.5. The spread is temperature.
 */
static const double this_receiver_ppm[] = { 30.8, 31.3, 31.84, 32.5, 35.96 };

/* Uncorrected, which is what every reading in ticket 11 was taken with. */
static struct reading_clock raw(double crystal_ppm) {
    struct reading_clock clock = { crystal_ppm, 0.0 };
    return clock;
}

/* And corrected to its own measurement, which is the case the first version of
   this header could not express and silently refused. */
static struct reading_clock corrected(double crystal_ppm) {
    struct reading_clock clock = { crystal_ppm, crystal_ppm };
    return clock;
}

/*
 * What a confirmation pass can actually place a candidate to: **one bin**,
 * 977 Hz at 2 MS/s over 2048 points.
 *
 * Not a guess and not a round number. The two comb families are internal by
 * construction, so whatever they read from exact *is* the pass's precision:
 * 129.600159 against 14.4 x 9 is +159 Hz, 131.200526 against 1.6 x 82 is
 * +526, 136.000793 against 1.6 x 85 is +793. Quantisation alone would be half
 * a bin; the rest is the peak being pulled off centre, which is the same
 * effect `RECEIVER_COMB_TOLERANCE_HZ` exists for.
 *
 * Ticket 11 wrote this as "about +/-800 Hz" from those three and then admitted
 * a +900 Hz reading as inside it. That is the ordinary shape of a wrong claim
 * beside right arithmetic, and it failed here on the first run: 800 is a
 * round-down of the largest observation rather than a bound, and it excludes
 * 150.000900, which is one of the readings the ticket exists to explain. One
 * bin covers all four with margin and is still 4.2 times smaller than the
 * 4.1 kHz being tested for, so the separability bar below is untouched.
 */
#define PASS_TOLERANCE_HZ 977.0

/*
 * 1. The forward model, and the two cases that differ by which frequency
 *    carries the (1 + d).
 */
static void test_an_external_tone_is_displaced(void) {
    const double truth = 132066666.7;    /* an 8.33 kHz airband channel */
    double reading = reading_external_hz(truth, raw(31.84));

    /*
     * **A fast crystal reads everything low**, and this receiver's is fast.
     * Getting it backwards is invisible in a round trip and invisible in the
     * "clocked here" verdict -- it shows up only as a signal attributed to the
     * wrong channel, which is what happened.
     */
    check_true("a fast crystal reads an external tone low", reading < truth);
    check_close("by about 4.2 kHz at 132 MHz", truth - reading, 4204.0, 3.0);
    check_close("and that is the separation between the hypotheses",
                reading_separation_hz(truth, raw(31.84)), truth - reading,
                1.0);

    /* And a slow crystal the other way, because the direction must follow the
       sign of the error rather than this dongle. */
    check_true("a slow crystal reads it high",
               reading_external_hz(truth, raw(-31.84)) > truth);

    /* Correct the ppm and the external tone snaps onto its channel. */
    check_close("a corrected receiver reads it where it is",
                reading_external_hz(truth, corrected(31.84)), truth, 1e-3);
}

/*
 * The separation is the crystal's error and **not** what is left after the
 * correction, which is the whole of what the first version of this header got
 * wrong. It made the measurement unreachable in the shipping program -- which
 * restores and applies a stored calibration at startup, so `crystal - applied`
 * is always zero -- while every unit check stayed green, because a unit hands
 * the number in.
 */
static void test_the_correction_does_not_narrow_the_gap(void) {
    const double at = 150000000.0;
    double uncorrected = reading_separation_hz(at, raw(31.84));
    double calibrated = reading_separation_hz(at, corrected(31.84));
    struct reading_clock half = { 31.84, 15.92 };

    check_close("uncorrected, the two hypotheses are 4.8 kHz apart",
                fabs(uncorrected), 4776.0, 5.0);
    check_close("corrected, they are the same distance apart",
                fabs(calibrated), fabs(uncorrected), 1.0);
    check_close("and half corrected, still",
                fabs(reading_separation_hz(at, half)), fabs(uncorrected), 1.0);

    /* What the correction changes is which of them sits on the nominal. */
    check_close("uncorrected, the coherent tone reads exactly nominal",
                reading_coherent_hz(at, raw(31.84)), at, 1e-3);
    check_close("corrected, the external one does",
                reading_external_hz(at, corrected(31.84)), at, 1e-3);
    check_true("so a calibrated receiver can still answer",
               reading_origin_separable(at, corrected(31.84),
                                        PASS_TOLERANCE_HZ));
    check_int("and it answers the other way round",
              (int)reading_origin_for(at, at, corrected(31.84),
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_EXTERNAL);
    check_int("while uncorrected the same reading is the receiver",
              (int)reading_origin_for(at, at, raw(31.84), PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_RECEIVER);
}

static void test_a_coherent_tone_reads_exact(void) {
    /*
     * The source's own frequency carries the error, so the error cancels. This
     * is exact -- not "to first order", not "within the baseband term" -- and
     * it holds at every ppm this receiver has ever measured and at absurd ones
     * too, which is the claim the discriminator rests on.
     */
    const double nominal[] = { 75000000.0, 135000000.0, 150000000.0,
                               129600000.0 };
    const double silly_ppm[] = { -35.96, -1000.0, 0.0, +1000.0, +12345.0 };
    size_t i, k;

    for (i = 0; i < sizeof(nominal) / sizeof(nominal[0]); i++)
        for (k = 0; k < sizeof(silly_ppm) / sizeof(silly_ppm[0]); k++) {
            /* The source is at nominal*(1 + d): that is what coherent means. */
            check_close("a clock-coherent tone reads its exact nominal",
                        reading_coherent_hz(nominal[i], raw(silly_ppm[k])),
                        nominal[i], 1e-3);
        }
}

static void test_the_inverse_undoes_the_forward(void) {
    size_t i;

    for (i = 0; i < sizeof(this_receiver_ppm) / sizeof(this_receiver_ppm[0]);
         i++) {
        double ppm = this_receiver_ppm[i];
        double truth = 942500000.0;

        check_close("true -> reading -> true",
                    reading_true_hz(reading_from_true_hz(truth, raw(ppm)),
                                    raw(ppm)),
                    truth, 1e-3);
    }
}

/*
 * 2. The refusal. Two shapes of it, and the second is the one that surprises.
 */
static void test_an_unmeasured_crystal_has_no_discriminator(void) {
    struct reading_clock unmeasured = { 0.0, 0.0 };

    /*
     * A crystal error of 0 means nobody has measured this receiver. An
     * external tone and a coherent one then report identically -- a coherent
     * source is *defined* by carrying the crystal's error, and an error of
     * zero is no error to carry -- so there is nothing to separate.
     */
    check_close("with no measured error there is no separation",
                reading_separation_hz(150000000.0, unmeasured), 0.0, 1e-9);
    check_int("so the question cannot be asked",
              reading_origin_separable(150000000.0, unmeasured,
                                       PASS_TOLERANCE_HZ), 0);
    check_int("and a reading exactly on nominal is still UNKNOWN",
              (int)reading_origin_for(150000000.0, 150000000.0, unmeasured,
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_UNKNOWN);

    /*
     * And an *applied* correction with no measured crystal is the same
     * refusal, not a better one. Somebody may pass `--ppm` from memory; that
     * is a tuning adjustment, not a measurement of this reference.
     */
    {
        struct reading_clock guessed = { 0.0, -31.0 };

        check_int("a correction applied without a measurement says nothing",
                  reading_origin_separable(150000000.0, guessed,
                                           PASS_TOLERANCE_HZ),
                  0);
    }

    /*
     * The inversion, stated as the ticket states it: correct the ppm and the
     * *coherent* tone is the one that moves off exact, because its true
     * frequency is still the crystal's.
     */
    /* Above nominal, because this crystal is fast and the coherent source is
       fast with it -- the mirror of the uncorrected case, where it is the
       external signal that reads 4.8 kHz *below*. */
    check_close("a corrected receiver reads a coherent tone 4.8 kHz high",
                reading_coherent_hz(150000000.0, corrected(31.84)) -
                    150000000.0,
                4776.0, 5.0);
    check_close("where uncorrected an external one reads 4.8 kHz low",
                reading_external_hz(150000000.0, raw(31.84)) - 150000000.0,
                -4776.0, 5.0);
}

static void test_the_bar_is_a_frequency_bar(void) {
    /*
     * Separability needs the displacement to clear twice the tolerance, and
     * the displacement is proportional to frequency -- so below a few tens of
     * megahertz this says nothing however well calibrated the ppm figure is.
     * At 31.84 ppm, twice 977 Hz needs 61 MHz.
     */
    check_int("at 150 MHz the two hypotheses are separable",
              reading_origin_separable(150000000.0, raw(31.84),
                                       PASS_TOLERANCE_HZ),
              1);
    check_int("at 132 MHz too",
              reading_origin_separable(132066666.7, raw(31.84),
                                       PASS_TOLERANCE_HZ),
              1);
    check_int("at 40 MHz they are not",
              reading_origin_separable(40000000.0, raw(31.84),
                                       PASS_TOLERANCE_HZ),
              0);
    /* The boundary, from both sides, so the comparison cannot quietly become
       >= and lose the overlap it exists to refuse. */
    {
        double just_under = 2.0 * PASS_TOLERANCE_HZ / (31.84 * READING_PPM);

        check_int("just under the bar is a refusal",
                  reading_origin_separable(just_under * 0.998, raw(31.84),
                                           PASS_TOLERANCE_HZ), 0);
        check_int("and just over it is not",
                  reading_origin_separable(just_under * 1.002, raw(31.84),
                                           PASS_TOLERANCE_HZ), 1);
    }
}

/*
 * 3. The three verdicts over the candidates actually measured, which is the
 *    table `.scratch/am-airband/spec.md` had wrong.
 */
static void test_the_airband_pass(void) {
    /* What this receiver measured on the day, uncorrected as the pass was:
       `--calibrate gsm --arfcn 113` locked at 31.84 ppm over 874
       measurements, sem 0.22. */
    const struct reading_clock clock = { 31.84, 0.0 };

    /* The two comb families, which are internal by construction. They must
       come back RECEIVER or the precision figure is wrong. */
    check_int("129.600159 on 14.4 x 9 is clocked here",
              (int)reading_origin_for(129600159.0, 129600000.0, clock,
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_RECEIVER);
    check_int("136.000793 on 1.6 x 85 is clocked here",
              (int)reading_origin_for(136000793.0, 136000000.0, clock,
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_RECEIVER);

    /* 135.000 was called "a real AM carrier" until this subtraction existed.
       It reads 61 Hz from exact where a transmitter must read 4.2 kHz off. */
    check_int("134.999939 is the receiver, not an AM carrier",
              (int)reading_origin_for(134999939.0, 135000000.0, clock,
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_RECEIVER);

    /* 150.0009 is ticket 10's unmodelled family: on no comb, and exact. */
    check_int("150.000900 is clocked here despite fitting no comb",
              (int)reading_origin_for(150000900.0, 150000000.0, clock,
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_RECEIVER);

    /*
     * And the one survivor -- **on 132.066667, not the 132.058333 ticket 11
     * names.** With the crystal fast an external transmitter reads low, so the
     * channel has to be above the reading rather than below it: 132.066667
     * predicts 132.062462 against 132.062744 observed, 282 Hz. The channel the
     * ticket chose predicts 132.054129, which is 8.6 kHz out and explains
     * nothing.
     *
     * The verdict is unchanged -- it was external then and is external now --
     * and so is the conclusion that draws on it, because 132.066667 is not on
     * the 25 kHz raster either. Only the channel number moves.
     */
    check_close("an external source on 132.066667 reads 132.062462",
                reading_external_hz(132066666.7, clock), 132062461.9, 1.0);
    check_int("132.062744 is not clocked here",
              (int)reading_origin_for(132062744.0,
                                      reading_external_channel_hz(132062744.0,
                                                                  118000000.0,
                                                                  25000.0 / 3.0,
                                                                  clock),
                                      clock, PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_EXTERNAL);
    check_close("and the channel it is on is the 8.33 kHz one above",
                reading_external_channel_hz(132062744.0, 118000000.0,
                                            25000.0 / 3.0, clock),
                132066666.7, 0.5);
    /*
     * The 25 kHz raster explains nothing, which is why refusing 8.33 kHz would
     * have refused the only traffic ever measured here.
     */
    check_int("against the 25 kHz raster it is unexplained",
              (int)reading_origin_for(132062744.0,
                                      reading_external_channel_hz(132062744.0,
                                                                  118000000.0,
                                                                  25000.0,
                                                                  clock),
                                      clock, PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_UNEXPLAINED);
}

static void test_unexplained_is_a_verdict_and_not_a_default(void) {
    struct reading_clock rx = raw(31.84);
    struct reading_clock unmeasured = { 0.0, 0.0 };
    /* Halfway between the two hypotheses: neither explains it. */
    double nominal = 150000000.0;
    double coherent = reading_coherent_hz(nominal, rx);
    double external = reading_external_hz(nominal, rx);
    double between = (coherent + external) / 2.0;

    check_true("the two hypotheses are far apart here",
               fabs(coherent - external) > 4.0 * PASS_TOLERANCE_HZ);
    check_int("and a reading between them fits neither",
              (int)reading_origin_for(between, nominal, rx,
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_UNEXPLAINED);
    /* This is the distinction the flag exists for: a reading nobody could ask
       about, against a reading that was asked about and said no. */
    check_int("which is not the same answer as not being asked",
              (int)reading_origin_for(between, nominal, unmeasured,
                                      PASS_TOLERANCE_HZ),
              (int)READING_ORIGIN_UNKNOWN);
    check_int("and unexplained outranks unknown when grids disagree",
              (int)reading_origin_best(READING_ORIGIN_UNEXPLAINED,
                                       READING_ORIGIN_UNKNOWN),
              (int)READING_ORIGIN_UNEXPLAINED);
    check_int("while a comb multiple beats a channel number",
              (int)reading_origin_best(READING_ORIGIN_RECEIVER,
                                       READING_ORIGIN_EXTERNAL),
              (int)READING_ORIGIN_RECEIVER);
}

/*
 * The real capture, which is what stops all of the above from being a model
 * agreeing with itself.
 *
 * `testfiles/carrier_75000_bare.bin`: tuned 74 700 500 Hz, `ppm 0` in its
 * sidecar, and `signal_find_carrier()` puts the carrier at +299 478.2 Hz --
 * stable to about a hertz from 65 536 pairs to 400 000. That is an absolute
 * 74 999 978.2 Hz.
 *
 * The numbers are pinned here rather than re-measured because this check links
 * nothing and reads no file; `check-signal-probe` is what holds the capture to
 * that offset, and `probe-signal` prints it.
 */
#define BARE_CARRIER_READING_HZ 74999978.2
#define BARE_CARRIER_NOMINAL_HZ 75000000.0

static void test_the_capture_that_establishes_it(void) {
    size_t i;
    double off = BARE_CARRIER_NOMINAL_HZ - BARE_CARRIER_READING_HZ;

    check_close("the capture reads 21.8 Hz below 25 MHz x 3", off, 21.8, 0.1);

    for (i = 0; i < sizeof(this_receiver_ppm) / sizeof(this_receiver_ppm[0]);
         i++) {
        double ppm = this_receiver_ppm[i];
        double predicted = reading_separation_hz(BARE_CARRIER_NOMINAL_HZ,
                                                 raw(ppm));

        /* 2.3 to 2.7 kHz of separation predicted for an external source at
           this frequency, against 21.8 Hz observed: about one per cent. */
        check_true("an external source would be displaced kilohertz",
                   fabs(predicted) > 2200.0);
        check_true("and the observation is a per cent of that",
                   off < fabs(predicted) * 0.02);
        check_int("so the capture is clocked with this receiver",
                  (int)reading_origin_for(BARE_CARRIER_READING_HZ,
                                          BARE_CARRIER_NOMINAL_HZ, raw(ppm),
                                          PASS_TOLERANCE_HZ),
                  (int)READING_ORIGIN_RECEIVER);
    }

    /*
     * The capture's own sidecar says `ppm 0`, and that is the *correction*
     * rather than the crystal: whatever the reference was doing when the file
     * was recorded, nothing was applied to it. Taking the sidecar's number for
     * the crystal -- which is the obvious mistake -- makes the same reading
     * unanswerable, because a source coherent with a perfect reference is at
     * the same frequency as an external one. The refusal, on a real file.
     */
    {
        struct reading_clock unmeasured = { 0.0, 0.0 };

        check_int("read with the sidecar's own ppm it refuses",
                  (int)reading_origin_for(BARE_CARRIER_READING_HZ,
                                          BARE_CARRIER_NOMINAL_HZ, unmeasured,
                                          PASS_TOLERANCE_HZ),
                  (int)READING_ORIGIN_UNKNOWN);
    }
}

/*
 * The grid helpers, including the base that is the easy thing to get wrong.
 */
static void test_the_rasters(void) {
    check_close("the nearest 1.6 MHz multiple",
                reading_nearest_multiple_hz(136000793.0, 1600000.0),
                136000000.0, 1.0);
    check_close("and the nearest 14.4 MHz one",
                reading_nearest_multiple_hz(129600159.0, 14400000.0),
                129600000.0, 1.0);

    /* Band II is 87.5 plus a multiple of 100 kHz, so TSF at 89.5 is on it. */
    check_close("89.5 is a band II channel",
                reading_nearest_channel_hz(89500000.0, 87500000.0, 100000.0),
                89500000.0, 1.0);

    /*
     * The airband's 8.33 kHz raster is 25/3 kHz from 118 MHz, and every
     * 25 kHz channel is on it because 25000 is exactly three steps. One
     * raster covers both, which is why there is one field for it.
     */
    check_close("a 25 kHz channel is on the 8.33 kHz raster",
                reading_nearest_channel_hz(121500000.0, 118000000.0,
                                           25000.0 / 3.0),
                121500000.0, 0.5);

    /*
     * And the trap. The nearest channel to the *reading* is not the channel an
     * external transmitter would be on, because the reading is displaced --
     * 132.062744 is nearest to 132.066667 and an external source would have
     * to be on 132.058333, which is where the inverted reading lands. This
     * failed on the first run of this suite, having been written the obvious
     * way.
     */
    check_close("the nearest channel to a reading",
                reading_nearest_channel_hz(132058400.0, 118000000.0,
                                           25000.0 / 3.0),
                132058333.3, 0.5);
    check_close("is not the channel an external source would be on",
                reading_external_channel_hz(132058400.0, 118000000.0,
                                            25000.0 / 3.0, raw(31.84)),
                132066666.7, 0.5);
    check_true("and they are different channels",
               reading_nearest_channel_hz(132058400.0, 118000000.0,
                                          25000.0 / 3.0) !=
               reading_external_channel_hz(132058400.0, 118000000.0,
                                           25000.0 / 3.0, raw(31.84)));

    /* A raster finer than twice the tolerance names a channel by rounding. */
    check_int("8.33 kHz is resolvable at a pass's precision",
              reading_raster_is_resolvable(25000.0 / 3.0, PASS_TOLERANCE_HZ),
              1);
    check_int("100 kHz certainly is",
              reading_raster_is_resolvable(100000.0, PASS_TOLERANCE_HZ), 1);
    check_int("a 1 kHz raster is not",
              reading_raster_is_resolvable(1000.0, PASS_TOLERANCE_HZ), 0);

    /* A base that is wrong by half a raster puts every channel half out,
       which reads as unexplained everywhere. Worth one assertion. */
    check_true("the base is not decoration",
               fabs(reading_nearest_channel_hz(121500000.0, 118004166.0,
                                               25000.0 / 3.0) -
                    121500000.0) > 4000.0);
}

static void test_nothing_is_asserted_from_nothing(void) {
    struct reading_clock impossible = { -1000000.0, 0.0 };

    check_close("no frequency, no reading",
                reading_from_true_hz(0.0, raw(31.3)), 0.0, 1e-9);
    check_close("nor a negative one", reading_from_true_hz(-1.0, raw(31.3)),
                0.0, 1e-9);
    check_close("a residual of -1e6 ppm is refused rather than dividing by 0",
                reading_from_true_hz(1e8, impossible), 0.0, 1e-9);
    check_int("no tolerance, no verdict",
              (int)reading_origin_for(150000000.0, 150000000.0, raw(31.84),
                                      0.0),
              (int)READING_ORIGIN_UNKNOWN);
    check_close("no grid, no multiple",
                reading_nearest_multiple_hz(150000000.0, 0.0), 0.0, 1e-9);
    check_close("a frequency below the first channel has none",
                reading_nearest_channel_hz(100000000.0, 118000000.0, 25000.0),
                0.0, 1e-9);
    check_str("and an unknown origin has no sentence to print",
              reading_origin_reason(READING_ORIGIN_UNKNOWN) ? "something"
                                                            : "nothing",
              "nothing");
}

int main(void) {
    test_an_external_tone_is_displaced();
    test_a_coherent_tone_reads_exact();
    test_the_inverse_undoes_the_forward();
    test_the_correction_does_not_narrow_the_gap();
    test_an_unmeasured_crystal_has_no_discriminator();
    test_the_bar_is_a_frequency_bar();
    test_the_airband_pass();
    test_unexplained_is_a_verdict_and_not_a_default();
    test_the_capture_that_establishes_it();
    test_the_rasters();
    test_nothing_is_asserted_from_nothing();
    return check_report("whose oscillator a reading belongs to");
}
