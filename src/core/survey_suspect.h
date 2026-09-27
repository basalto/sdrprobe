#ifndef SURVEY_SUSPECT_H
#define SURVEY_SUSPECT_H

#include <math.h>
#include <string.h>

#include "band_plan.h"
#include "clock_chain.h"
#include "reading_origin.h"
#include "sdr_dsp.h"
#include "survey_sweep.h"

/*
 * Why a survey candidate might have been made by the receiver rather than
 * received by it.
 *
 * The survey finds peaks; some of them are not signals at all. A sweep of
 * 470-690 MHz turns up a dozen narrow carriers standing 20 dB above the floor
 * whose frequencies are exact multiples of 14.4 MHz -- half the RTL2832U's
 * 28.8 MHz reference clock. They sit inside the UHF television allocation, so
 * the band plan dutifully labels them "UHF television", and nothing else on
 * the panel contradicts it.
 *
 * Unplugging the antenna sorts them into two kinds, which is worth knowing
 * before trusting the obvious test. Three -- 489.6, 547.2 and 604.8 MHz,
 * harmonics 34, 38 and 42 -- stay exactly where they were, within half a
 * decibel, run after run: those are made and heard entirely inside the
 * receiver. The other nine go with the antenna, because the dongle radiates
 * its clock and hears itself coming back. Both are the receiver's doing, but
 * "unplug it and an artifact stays" holds only for the first kind.
 *
 * This is the contradiction, and it is careful about what it claims. It never
 * removes a candidate and never says a peak *is* an artifact: it says the
 * frequency has the signature of one, and leaves the operator to decide.
 * Removing peaks would hide a real transmitter that happens to sit on a
 * harmonic, which is exactly the kind of silent editing ADR-0015 refuses.
 *
 * Plain arithmetic, checked by tests/survey_suspect_test.c (ADR-0012).
 */

/*
 * The RTL2832U's reference crystal, and the **spacing** of the comb its
 * harmonics land on: a divider somewhere in the chain puts a tone every
 * 14.4 MHz, which is what a disconnected sweep shows.
 *
 * `_SPACING_` and not just `_HZ`, because a comb has no single frequency --
 * that is what makes it a comb -- and this was called `RECEIVER_COMB_HZ`,
 * which reads like the frequency of one. CONTEXT.md carries both terms:
 * "reference comb" for the set of tones, "comb spacing" for the interval.
 */
/*
 * **The reference is the device's, not a constant here.** It was
 * `RECEIVER_REFERENCE_HZ 28800000.0` -- an RTL2832U's crystal, compiled into a
 * header that knows nothing about which receiver is attached. It comes from
 * `device_profile.reference_clock_hz` now, because a B210-class part has a
 * different clock and therefore different spurs, or none here at all
 * (`.scratch/device-model/issues/08-*`).
 *
 * A reference of **0 means no comb tests at all**, and that is the case a
 * capture is in: whichever device recorded a file had a clock, but the file
 * does not, so nothing may attribute a comb to it.
 */
static inline double survey_comb_spacing_hz(double reference_hz) {
    return reference_hz > 0.0 ? reference_hz / 2.0 : 0.0;
}

/*
 * And the comb underneath that one, which is nine times finer.
 *
 * 14.4 MHz is real and it is every ninth tone. The spacing is 1.6 MHz, which
 * is 28.8/18, and three things say so. Sweeping the same air with the step
 * grid deliberately moved -- lower edges of 240.0, 239.2 and 239.5 MHz, so the
 * boundaries fall in three different places -- puts the candidates at the same
 * absolute frequencies every time, on step boundaries in one and step centres
 * in the next, so the sweep is not making them. With `--ppm 0` they land on
 * exact multiples, within 2.5 kHz of a 3.7 kHz bin, and with this site's
 * +35 ppm correction applied they read about +35 ppm high: a spur divided down
 * from the crystal that clocks both the tuner and the ADC keeps its ratio to
 * the nominal grid, and a transmitter moves the other way. And every ninth
 * tone -- 244.8 = 17 x 14.4, 259.2 = 18 x 14.4 -- is on the comb the unplug
 * test already established.
 *
 * Across the whole-tuner sweep of 2026-09-03, 43% of 289 candidates sit within
 * half a bin of a 1.6 MHz multiple against 13% by chance. On 240-270 MHz it is
 * 11 of 14, of which the old test flagged the two that are also multiples of
 * 14.4.
 */
/*
 * **Both divisors were measured on an RTL2832U and are unverified anywhere
 * else.** That a divider puts a tone every reference/2, and a finer one every
 * reference/18, is a fact about this chip's clock tree rather than about
 * reference oscillators in general. On another device the comb may have a
 * different ratio, or be absent. Measure before trusting either -- the
 * evidence above is what measuring looked like.
 */
static inline double survey_fine_comb_spacing_hz(double reference_hz) {
    return reference_hz > 0.0 ? reference_hz / 18.0 : 0.0;
}

/*
 * The widest a comb test may reach before the flag is guessing.
 *
 * A real signal lands within `tolerance` of a multiple by chance
 * 2*tolerance/spacing of the time. At 14.4 MHz spacing even a full-tuner
 * sweep's 106 kHz half-bin is 1.5%. At 1.6 MHz the same tolerance is 13% --
 * one candidate in eight flagged by luck -- so the finer comb is only tested
 * when the sweep can place a candidate to a fortieth of its spacing, which is
 * 40 kHz, which is a sweep no wider than about 650 MHz. Wider than that, this
 * says nothing rather than saying something one time in eight.
 */
#define RECEIVER_COMB_MAX_FRACTION (1.0 / 40.0)

/*
 * How far a comb tone's *reported* frequency can sit from the multiple it
 * really is on.
 *
 * Half a survey bin is the obvious answer and it is not enough. A candidate is
 * reported at the bin holding the peak-held maximum, which noise and the
 * tuner's own error can pull a bin or two off centre: the same 648 MHz tone
 * came back 18.1 kHz low in one sweep of 470-690 MHz and 11.5 kHz high in the
 * next, and a 590.4 MHz one 5.3 kHz high in an 80 MHz sweep whose bins are
 * only 9.8 kHz wide. Tying the tolerance to the bin missed all three.
 *
 * 25 kHz covers what has been measured with margin, and it costs almost
 * nothing: the comb is spaced 14.4 MHz, so a real signal falling inside 25 kHz
 * of a multiple by chance is one candidate in about three hundred.
 */
#define RECEIVER_COMB_TOLERANCE_HZ 25000.0

/*
 * The width a pure carrier measures at, given the FFT looking at it. A tone
 * has no bandwidth of its own; what is measured is the window's response,
 * about four bins of it at the -20 dB point. A candidate this narrow is a
 * carrier with nothing on it -- which is what a reference harmonic looks like,
 * and what a modulated service never does.
 */
#define RECEIVER_TONE_BINS 4.0

static inline double survey_resolution_hz(double sample_rate, int fft_size) {
    if (fft_size <= 0)
        return 0.0;
    return RECEIVER_TONE_BINS * sample_rate / (double)fft_size;
}

enum survey_suspicion {
    SURVEY_SUSPECT_NONE = 0,
    /* On the receiver's reference comb. The strong one: the spacing is
       14.4 MHz and a survey bin is tens of kilohertz, so a real signal landing
       on a multiple by chance is a fraction of a per cent. */
    SURVEY_SUSPECT_REFERENCE = 1 << 0,
    /* At the middle of a survey step, where the receiver's own DC offset
       lands. Only ever set when the DC-spike filter is off, because with it on
       there is no offset to land there -- and step centres are 1.6 MHz apart,
       so an unconditional test would flag every DVB-T channel centre in the
       band (8 MHz is exactly five steps). Even when it is set, something real
       may be underneath: what the flag says is that the measurement at this
       frequency has the receiver's offset added to it. */
    SURVEY_SUSPECT_STEP_CENTRE = 1 << 1,
    /* Narrower than the FFT can resolve: a bare carrier. On its own this is an
       observation rather than a suspicion -- plenty of real services are
       narrow -- but it is what makes the two above worth believing. */
    SURVEY_SUSPECT_UNRESOLVED = 1 << 2,
    /*
     * The closer look found no standing carrier here, and the envelope varies
     * exactly as much as noise does.
     *
     * Unlike the three above this is not about the receiver, and it is set
     * only by the confirmation pass -- a sweep has nothing to set it from.
     * It exists because presence and reality are different questions and the
     * pass only ever answered the first: five frequencies in one 290-310 MHz
     * sweep were confirmed **six looks out of six** while reading no carrier,
     * under one per cent of the channel standing still, and an envelope
     * variation of 0.520 to 0.539 against Rayleigh's 0.5227. A prominence bar
     * is cleared by noise structure every time it is offered, so counting
     * looks cannot separate them; a second, independent statistic can.
     *
     * What it claims is "indistinguishable from noise", not "is noise". A
     * real spread signal buried at its own noise floor would read the same,
     * and nothing here can tell those apart -- which is why the words on
     * screen say what was measured rather than what it is (ADR-0015).
     */
    SURVEY_SUSPECT_NO_CARRIER = 1 << 3,
    /*
     * It reads at its exact nominal frequency, where a signal arriving through
     * the antenna could not.
     *
     * The comb flags above are a coincidence argument -- this frequency is a
     * multiple of that spacing, and a real signal would land there rarely. This
     * is a different kind of evidence entirely and shares no arithmetic with
     * them: an uncalibrated receiver *displaces* everything it hears by
     * `f * d`, about 4.4 kHz at 132 MHz here, and a tone generated from its own
     * reference is displaced by nothing at all because the error is in the
     * tuning, the sample rate and the tone alike. So a reading sitting on its
     * nominal to within a bin is not coincidence, it is the cancellation.
     *
     * Two things follow and both matter. It **corroborates** a comb flag
     * without sharing its assumptions, which is the only kind of second opinion
     * worth having. And it **contradicts** one: a real transmitter that happens
     * to sit on a comb multiple reads displaced, which is the 94.4 MHz case --
     * the loudest station at this site, on the fine comb by coincidence. The
     * flag is not set there, and `survey_suspect_origin_at()` says so
     * positively for a caller that wants to.
     *
     * Requires a *measured, non-zero* residual. See
     * `survey_suspect_origin()`, which refuses without one --
     * `.scratch/device-model/issues/11-*`.
     */
    SURVEY_SUSPECT_CLOCK_COHERENT = 1 << 4,
    /*
     * A bare carrier that none of the modelled grids explains, at a frequency
     * where the question could have been asked.
     *
     * It exists because "unremarked" was carrying two meanings: a candidate
     * nobody could say anything about read identically to one that had been
     * asked and had answered nothing. 150.0009 MHz is the case -- confirmed 6
     * of 6 at 37.7 dB, 70% of the channel standing still, on no multiple of
     * 14.4 or 1.6 MHz, and filed by the band plan under "Mobile-satellite
     * uplink". It is a finding and it was indexed as clean.
     *
     * Deliberately narrow. It wants the candidate to be a bare carrier
     * (`SURVEY_SUSPECT_UNRESOLVED`), because an ordinary modulated service on
     * no comb is not a mystery, and it wants separability, because a reading
     * nobody could interrogate has not answered anything. Without those two it
     * would fire on nearly every candidate in every sweep and teach the
     * operator to ignore it.
     *
     * **A channel raster sharpens it**, and one is supplied where the band
     * plan knows a grid: a bare carrier on no comb *and* no channel is a
     * stronger finding than one on no comb alone. Most allocations have no
     * raster and never will -- a gap is preferred to a guess -- so for most
     * of the spectrum this still means the first half only, and says so.
     */
    SURVEY_SUSPECT_UNEXPLAINED = 1 << 5,
    /*
     * It reads **displaced** by this receiver's own error, where a tone
     * clocked here could not. Whatever is at this frequency has its own
     * oscillator.
     *
     * This is the flag that **contradicts** a comb mark rather than
     * corroborating one, and it is the reason a second kind of evidence was
     * worth having. 94.4 MHz is 1.6 x 59 and is also the loudest FM broadcast
     * station at this site, confirmed at 46 dB: the fine comb flags it,
     * correctly by its own lights and wrongly about the world, and a real
     * transmitter there reads about 3.0 kHz off exact.
     *
     * **It does not clear `SURVEY_SUSPECT_REFERENCE`**, and that was decided
     * rather than defaulted (`.scratch/reading-origin/issues/01-*`). This file
     * never removes a candidate and never says a peak *is* an artifact;
     * clearing a mark an operator has learned to read is a larger act than
     * adding one beside it, and stronger evidence is not the same as evidence
     * entitled to overrule silently. The accepted cost is that
     * `survey_suspect_warns()` keeps counting such a candidate -- see there.
     *
     * It is **not** a warning. It says a real signal is here, which is the
     * opposite of what the other flags say.
     */
    SURVEY_SUSPECT_DISPLACED = 1 << 6
};

/* Which tone of a comb spaced `spacing_hz` the frequency sits on, or 0. */
static inline int survey_comb_harmonic(double hz, double spacing_hz,
                                       double tolerance_hz) {
    double harmonic;
    double nearest;

    if (!(hz > 0.0) || !(spacing_hz > 0.0) || !(tolerance_hz >= 0.0))
        return 0;
    /* Beyond this the flag is chance rather than evidence. */
    if (tolerance_hz > spacing_hz * RECEIVER_COMB_MAX_FRACTION)
        return 0;
    harmonic = floor(hz / spacing_hz + 0.5);
    if (harmonic < 1.0)
        return 0;
    nearest = harmonic * spacing_hz;
    if (fabs(hz - nearest) > tolerance_hz)
        return 0;
    return (int)harmonic;
}

/*
 * Which harmonic of the coarse reference comb `hz` sits on, or 0 for none. The
 * tolerance should be about half a survey bin: a candidate is reported at its
 * bin's centre, so that is how far from the truth it can be before it has been
 * measured.
 */
static inline int survey_reference_harmonic(double reference_hz, double hz,
                                            double tolerance_hz) {
    double spacing = survey_comb_spacing_hz(reference_hz);
    if (spacing <= 0.0)
        return 0; /* no clock, no comb -- a capture, or a device that has not
                     said. Saying nothing beats attributing a comb to a file. */
    return survey_comb_harmonic(hz, spacing, tolerance_hz);
}

/* And of the fine one, which needs a tighter tolerance to mean anything --
   survey_comb_harmonic() refuses rather than guessing when it does not have
   one. */
static inline int survey_fine_harmonic(double reference_hz, double hz,
                                       double tolerance_hz) {
    double spacing = survey_fine_comb_spacing_hz(reference_hz);
    if (spacing <= 0.0)
        return 0;
    return survey_comb_harmonic(hz, spacing, tolerance_hz);
}

/* Whether `hz` sits where a survey step was tuned, within `tolerance_hz`. */
static inline int survey_at_step_centre(const struct survey_plan *plan,
                                        double hz, double tolerance_hz) {
    double step;
    double nearest;

    if (plan->step_count <= 0 || !(plan->step_span_hz > 0.0))
        return 0;
    step = floor((hz - plan->lower_hz) / plan->step_span_hz);
    for (double s = step - 1.0; s <= step + 1.0; s += 1.0) {
        if (s < 0.0 || s >= (double)plan->step_count)
            continue;
        nearest = survey_plan_step_centre(plan, (int)s);
        if (fabs(hz - nearest) <= tolerance_hz)
            return 1;
    }
    return 0;
}

/*
 * The narrowest thing this *sweep* can tell apart, which is not the same as
 * what the transform can.
 *
 * A swept survey's bins are usually coarser than the transform's -- 3.7 kHz
 * against 977 Hz on a 30 MHz range -- and a candidate's width comes out of the
 * survey array, so it is quantised to survey bins. Judging it against the
 * transform's resolution calls every tone in a swept survey "resolved", which
 * is how the narrowness observation came to be unavailable in exactly the case
 * it is needed.
 */
static inline double survey_tone_width_hz(double bin_hz, double sample_rate,
                                          int fft_size) {
    double fine;

    /* No transform means nothing was measured with anything; answering with
       the survey's bin width alone would be claiming a resolution from a
       configuration that cannot have produced a measurement. */
    if (fft_size <= 0 || !(sample_rate > 0.0))
        return 0.0;
    fine = sample_rate / (double)fft_size;
    return RECEIVER_TONE_BINS * (bin_hz > fine ? bin_hz : fine);
}

/* Whether a measured bandwidth is at the floor of what this sweep can resolve.
   A quarter of slack: the measurement is itself a few bins wide. */
static inline int survey_is_unresolved(double bandwidth_hz, double bin_hz,
                                       double sample_rate, int fft_size) {
    double floor_hz = survey_tone_width_hz(bin_hz, sample_rate, fft_size);

    if (!(floor_hz > 0.0) || !(bandwidth_hz > 0.0))
        return 0;
    return bandwidth_hz <= floor_hz * 1.25;
}

/*
 * How far from a reported frequency the truth may be: half a survey bin, but
 * never finer than the FFT that filled it. This is the quantisation alone --
 * the step-centre test uses it, because a step centre is an exact frequency
 * the receiver was told to tune to.
 */
static inline double survey_suspect_tolerance(const struct survey_plan *plan,
                                              double sample_rate,
                                              int fft_size) {
    double half_bin = plan->bin_hz / 2.0;
    double resolution = survey_resolution_hz(sample_rate, fft_size) / 2.0;

    return half_bin > resolution ? half_bin : resolution;
}

/* The comb needs more room than quantisation explains: see
   RECEIVER_COMB_TOLERANCE_HZ. */
static inline double survey_comb_tolerance(const struct survey_plan *plan,
                                           double sample_rate, int fft_size) {
    double quantisation = survey_suspect_tolerance(plan, sample_rate,
                                                   fft_size);

    return quantisation > RECEIVER_COMB_TOLERANCE_HZ
               ? quantisation
               : RECEIVER_COMB_TOLERANCE_HZ;
}

/*
 * Everything suspicious about one candidate, as a set of flags. Pass
 * `bandwidth_hz` of 0 when the candidate has not been measured yet; the
 * frequency tests do not need it. `dc_filtered` says whether the DC-spike
 * filter is removing the receiver's centre-frequency offset, which decides
 * whether a step centre means anything.
 */
static inline unsigned survey_suspect(const struct survey_plan *plan,
                                      double reference_hz, double hz,
                                      double bandwidth_hz, double sample_rate,
                                      int fft_size, int dc_filtered) {
    double tolerance = survey_suspect_tolerance(plan, sample_rate, fft_size);
    double comb = survey_comb_tolerance(plan, sample_rate, fft_size);
    int bare = survey_is_unresolved(bandwidth_hz, plan->bin_hz, sample_rate,
                                    fft_size);
    unsigned flags = SURVEY_SUSPECT_NONE;

    if (survey_reference_harmonic(reference_hz, hz, comb))
        flags |= SURVEY_SUSPECT_REFERENCE;
    /*
     * The fine comb needs the narrowness as well, and this is the one place
     * the file's own remark about narrowness "making the two above worth
     * believing" is load-bearing rather than decorative.
     *
     * 1.6 MHz is sixteen times the 100 kHz raster broadcast services sit on,
     * so one FM channel in sixteen falls on the comb exactly -- including
     * 94.4 MHz, the loudest station at this site, confirmed at 46 dB. Flagged
     * candidates are set aside from a report's per-allocation bests, so a
     * frequency test alone would hide a real transmitter, which is worse than
     * the fault it fixes. The width settles it and the gap is not close: on a
     * 240-270 MHz sweep the comb tones measure one or two survey bins and on
     * an 88-108 MHz sweep the stations measure twenty-three to seventy-four.
     * The one narrow candidate in band II is 102.4 MHz, which is 64 x 1.6 and
     * six bins wide, and is a comb tone sitting in the broadcast band.
     */
    if (bare && survey_fine_harmonic(reference_hz, hz, comb))
        flags |= SURVEY_SUSPECT_REFERENCE;
    if (!dc_filtered && survey_at_step_centre(plan, hz, tolerance))
        flags |= SURVEY_SUSPECT_STEP_CENTRE;
    if (bare)
        flags |= SURVEY_SUSPECT_UNRESOLVED;
    return flags;
}

/*
 * Whether a sweep could have resolved a candidate at all, as opposed to
 * whether the candidate is narrow.
 *
 * These are different questions and the report answered neither. A full-tuner
 * sweep is 1742 MHz in 8192 bins, so a bin is 212 kHz and a 25 kHz carrier
 * occupies a fraction of one: its extent comes back as one or two bins
 * whatever it really is. The same one-or-two-bin extent from a 27 MHz sweep,
 * where a bin is 3.3 kHz, is a genuine measurement of something narrow.
 *
 * So the extent alone cannot be read without the bin it was measured in, and
 * this is the test that says which case a reader is looking at: an extent
 * this close to the floor is a lower bound and not a width.
 *
 * Two bins rather than one, because a maximum sitting between two bins
 * occupies both, and calling that resolved would let exactly the narrowest
 * things through as though they had been measured.
 */
#define SURVEY_RESOLVED_BINS 2.5

static inline int survey_extent_is_floor(double extent_hz, double bin_hz) {
    if (!(extent_hz > 0.0) || !(bin_hz > 0.0))
        return 1;      /* nothing measured is not something resolved */
    return extent_hz <= bin_hz * SURVEY_RESOLVED_BINS;
}

/*
 * The same tests, at the resolution a confirmation look has rather than the
 * one the sweep had.
 *
 * A sweep across the whole tuner puts 212 kHz in a bin, and half of that is
 * 106 kHz -- more than a fortieth of the 1.6 MHz comb, so
 * `survey_comb_harmonic()` correctly refuses to answer and the fine comb goes
 * untested exactly where the baseline is built. Fourteen of the 2026-09-05
 * sweep's on-comb candidates were flagged and thirty were not, and every one
 * of the fourteen was the one tone in nine that is also a 14.4 MHz harmonic.
 *
 * A confirmation look is one tuning at the receiver's own rate, so its bin is
 * `sample_rate / fft_size` -- 244 Hz at 2 MS/s and 8192 bins. At that
 * resolution the guard does not trip and the width is real, so both tests
 * mean what they say.
 *
 * Pass the frequency and width the *pass* measured, not the sweep's.
 */
static inline unsigned survey_suspect_confirmed(double reference_hz,
                                                double hz,
                                                double bandwidth_hz,
                                                double sample_rate,
                                                int fft_size) {
    struct survey_plan plan;

    if (fft_size <= 0 || !(sample_rate > 0.0))
        return SURVEY_SUSPECT_NONE;
    memset(&plan, 0, sizeof(plan));
    plan.bins = fft_size;
    plan.bin_hz = sample_rate / (double)fft_size;
    /*
     * The step-centre test is left out rather than given a plan to work with.
     * It asks whether a frequency sits where the receiver's own offset lands,
     * which is a property of how the *sweep* was stepped; a confirmation look
     * is tuned to the candidate, so every candidate is at its centre and the
     * test would flag all of them.
     */
    return survey_suspect(&plan, reference_hz, hz, bandwidth_hz, sample_rate,
                          fft_size, 1);
}


/*
 * How far a reading may sit from exact and still count as exact.
 *
 * **One bin of whatever measured it**, and deliberately not
 * `RECEIVER_COMB_TOLERANCE_HZ`. The two look like the same quantity and are
 * not: a comb multiple is 14.4 MHz from the next one, so 25 kHz of slack there
 * costs a chance in six hundred and buys margin against a peak pulled off
 * centre. Here the whole measurement is a subtraction of a few kilohertz, and
 * 25 kHz of slack does not loosen the test, it **abolishes** it -- the
 * separability bar becomes 50 kHz of displacement, which at 31 ppm wants a
 * carrier at 1.6 GHz. Every verdict would be UNKNOWN and the suite would look
 * fine.
 *
 * One bin is what the ticket's own precision figure comes to. The three comb
 * families read +159, +526 and +793 Hz from exact through a confirmation pass
 * binning at 2 MS/s over 2048 points, which is 977 Hz: quantisation is half of
 * that and the rest is the peak being pulled, so one whole bin covers what was
 * measured with a little margin and nothing like the 4.1 kHz being tested for.
 *
 * It follows that a **swept** survey mostly cannot ask this question at all. A
 * whole-tuner sweep bins at 212 kHz and would need 424 kHz of displacement;
 * the 128-137 MHz sweep that raised the question bins at about 2 kHz and can.
 * That is the right behaviour and not a limitation to work around: the flag
 * appears where the evidence is.
 */
#define SURVEY_COHERENT_BINS 1.0

static inline double survey_coherent_tolerance(double bin_hz) {
    return bin_hz > 0.0 ? bin_hz * SURVEY_COHERENT_BINS : 0.0;
}

/*
 * What the receiver's own frequency error says about a candidate, over the
 * grids this file already models.
 *
 * `clock` is the receiver's own reference error and the correction in force.
 * A **crystal error of 0 means nobody has measured this receiver**, which is a
 * refusal rather than an assumption -- and a capture is in that case too,
 * since a file does not carry its recorder's crystal. A receiver whose
 * correction is right is *not* in that case: it can still answer, with the two
 * hypotheses the other way round.
 *
 * The grids are the two combs, and that is the honest limit of what this can
 * reach today: a candidate at 150.000000 is on neither, so it comes back
 * UNEXPLAINED rather than RECEIVER however exactly it reads. That is the right
 * answer -- naming it the receiver's would need a grid containing it, and
 * choosing that grid is ticket 10's decision with evidence behind it, not a
 * default this file may invent.
 */
/*
 * `raster` is a **service's** channel grid -- a spacing and a base -- and both
 * 0 when none is known, which is most of the spectrum. The caller looks it up,
 * usually through `band_plan_raster_at()`.
 *
 * The **raster** and not one channel, because the two hypotheses need
 * different channels: on an uncorrected receiver the coherent one is nearest
 * the reading and the external one is nearest the *inverted* reading, and for
 * any grid finer than twice the separation those differ. At 132 MHz that is
 * 4.2 kHz against an 8.33 kHz step, so the airband is exactly in that regime
 * -- handing one channel in would pick a plausible wrong answer half the time.
 *
 * It is an argument rather than a lookup so this header stays pure arithmetic
 * with no table behind it, and so the band plan's meaning is not quietly
 * widened: ADR-0015 says the plan names what a band is *for*, and using its
 * channel grid to test a hypothesis about a reading is a different act from
 * using its name to identify a signal. Keeping the lookup in the caller is
 * what keeps those two apart.
 */
struct survey_raster {
    double spacing_hz;
    double base_hz;
};

/*
 * The grid the band plan knows at `hz`, as this header's own little struct.
 *
 * The one place `survey_suspect.h` touches the table, and it touches it for a
 * *number* rather than for a name -- ADR-0015's line is that the plan says
 * what a band is for and never what a signal is, and a channel spacing is the
 * former. A caller with no band plan, or testing a grid of its own, uses
 * `survey_suspect_origin_at()` directly and passes whatever raster it means.
 */
static inline struct survey_raster survey_raster_at(double hz) {
    struct survey_raster raster = { 0.0, 0.0 };

    band_plan_channel_grid(hz, &raster.spacing_hz, &raster.base_hz);
    return raster;
}

static inline enum reading_origin survey_suspect_origin_at(
    double reference_hz, double hz, struct reading_clock clock,
    double tolerance_hz, struct survey_raster raster,
    double chain_fundamental_hz) {
    double coarse = survey_comb_spacing_hz(reference_hz);
    double fine = survey_fine_comb_spacing_hz(reference_hz);
    enum reading_origin origin = READING_ORIGIN_UNKNOWN;

    /*
     * Each grid is asked twice, once for each hypothesis's own nearest
     * multiple, because those are not the same multiple when the raster is
     * finer than the separation -- see `reading_external_multiple_hz()`. The
     * coarse comb is 14.4 MHz and never has that problem; the fine one is
     * 1.6 MHz and does not either at these errors, but asking both costs four
     * multiplications and removes a standing assumption about the ratio.
     */
    if (coarse > 0.0) {
        origin = reading_origin_best(
            origin, reading_origin_for(
                        hz, reading_nearest_multiple_hz(hz, coarse), clock,
                        tolerance_hz));
        origin = reading_origin_best(
            origin, reading_origin_for(
                        hz, reading_external_multiple_hz(hz, coarse, clock),
                        clock, tolerance_hz));
    }
    if (fine > 0.0) {
        origin = reading_origin_best(
            origin, reading_origin_for(
                        hz, reading_nearest_multiple_hz(hz, fine), clock,
                        tolerance_hz));
        origin = reading_origin_best(
            origin, reading_origin_for(
                        hz, reading_external_multiple_hz(hz, fine, clock),
                        clock, tolerance_hz));
    }
    /*
     * And the octave chain, which is the family the comb cannot express --
     * f, 2f, 4f and never 3f (`clock_chain.h`). 150.000000 MHz is on no
     * multiple of 14.4 or 1.6 and reads exact, and without this it comes back
     * `unexplained` however precisely it is measured.
     *
     * The fundamental is the caller's and is 0 for a caller that models no
     * chain, which is the honest default for any receiver but the one it was
     * measured on.
     */
    if (chain_fundamental_hz > 0.0)
        origin = reading_origin_best(
            origin, reading_origin_for(
                        hz, clock_chain_nearest_hz(hz, chain_fundamental_hz),
                        clock, tolerance_hz));
    /*
     * And the service's own grid, if the caller had one. A candidate that
     * reads where an external transmitter on a real channel would is
     * EXTERNAL on evidence rather than by elimination -- which is what makes
     * the UNEXPLAINED below mean "no comb *and* no channel" rather than only
     * the first half.
     */
    /*
     * **A service raster answers the external hypothesis only**, and that is
     * not a simplification -- testing the coherent one against it is both
     * meaningless and wrong often enough to matter.
     *
     * Meaningless because a channel raster says where a *transmitter* may
     * sit; "a tone clocked by this receiver that happens to land on an
     * airband channel" is not a hypothesis anybody holds. Wrong often enough
     * because the grid is fine: at the airband's 8333 Hz and a pass's 977 Hz
     * tolerance, a reading lands within tolerance of some channel
     * 2*977/8333 = **23% of the time**. The comb is 1.6 MHz and the same
     * arithmetic gives 0.12%, which is why the comb may answer both.
     *
     * It was found on air rather than here: a 128-152 MHz sweep produced
     * `confirm 134758789 new refuted 2.2 0/6 977 unresolved,clocked-here`,
     * calling a noise maximum found in none of six looks a tone clocked by
     * this receiver, because 134.587 lands 433 Hz from where a coherent
     * source on airband channel 1990 would read. `RECEIVER_COMB_MAX_FRACTION`
     * is the same argument for the comb, and this is its raster twin.
     */
    if (reading_raster_is_resolvable(raster.spacing_hz, tolerance_hz)) {
        double channel = reading_external_channel_hz(hz, raster.base_hz,
                                                     raster.spacing_hz, clock);

        /*
         * **`reading_origin_separable()` guards this too**, and leaving it out
         * was a real fault rather than a tidy-up waiting to happen. The comb
         * and chain above reach it through `reading_origin_for()`; this branch
         * is written out because it answers one hypothesis, and the first
         * version wrote out the comparison and not the refusal.
         *
         * With no measured crystal there is no displacement, so
         * `reading_external_hz()` returns the channel unchanged and every
         * reading within a tolerance of a channel came back EXTERNAL -- 23% of
         * them by chance on an 8333 Hz grid. A capture is in that case, and so
         * is any receiver nobody has calibrated. It was caught on air by a
         * sweep run under a scratch site with no calibration, which flagged a
         * clock-coherent tone at 135.000488 as `displaced`.
         */
        if (channel > 0.0 &&
            reading_origin_separable(channel, clock, tolerance_hz)) {
            if (fabs(hz - reading_external_hz(channel, clock)) <= tolerance_hz)
                origin = reading_origin_best(origin, READING_ORIGIN_EXTERNAL);
            else
                origin = reading_origin_best(origin,
                                             READING_ORIGIN_UNEXPLAINED);
        }
    }
    return origin;
}

/*
 * The two flags that adds, given the ones the frequency grids produced.
 *
 * Kept apart from `survey_suspect()` rather than threaded through it, because
 * the two answer different questions from different evidence and only one of
 * them needs a calibrated receiver. A caller with no residual calls
 * `survey_suspect()` alone and loses nothing it could have had.
 */
static inline unsigned survey_suspect_origin(unsigned flags,
                                             double reference_hz, double hz,
                                             struct reading_clock clock,
                                             double tolerance_hz,
                                             struct survey_raster raster,
                                             double chain_fundamental_hz) {
    enum reading_origin origin = survey_suspect_origin_at(
        reference_hz, hz, clock, tolerance_hz, raster, chain_fundamental_hz);

    if (origin == READING_ORIGIN_RECEIVER)
        return SURVEY_SUSPECT_CLOCK_COHERENT;
    /*
     * Displaced from a grid this file models: something is here and it is not
     * clocked by this receiver. Reported beside a comb flag rather than
     * instead of it.
     */
    if (origin == READING_ORIGIN_EXTERNAL)
        return SURVEY_SUSPECT_DISPLACED;
    /*
     * A bare carrier the grids do not explain. Not merely "no verdict": the
     * question has to have been askable, which is what UNEXPLAINED means and
     * UNKNOWN does not.
     *
     * **And there has to be a carrier.** A noise maximum is narrow, so it
     * carries `UNRESOLVED` like a tone does, and the first live sweep with
     * this flag turned five refuted noise peaks at 1.9 to 4.4 dB into
     * "unexplained bare carriers" -- a false warning in the one direction that
     * costs the operator their trust in the line. `NO_CARRIER` is the
     * confirmation pass's own answer to exactly that question, and a caller
     * that has one must pass it in.
     */
    if (origin == READING_ORIGIN_UNEXPLAINED &&
        (flags & SURVEY_SUSPECT_UNRESOLVED) &&
        !(flags & (SURVEY_SUSPECT_REFERENCE | SURVEY_SUSPECT_NO_CARRIER)))
        return SURVEY_SUSPECT_UNEXPLAINED;
    return SURVEY_SUSPECT_NONE;
}

/*
 * Whether the flags amount to a warning. Narrowness alone does not: a pager, a
 * telemetry link and a beacon are all legitimately narrow, and crying wolf on
 * them would teach the operator to ignore the line.
 */
static inline int survey_suspect_warns(unsigned flags) {
    return (flags & (SURVEY_SUSPECT_REFERENCE | SURVEY_SUSPECT_STEP_CENTRE |
                     SURVEY_SUSPECT_CLOCK_COHERENT)) != 0;
}

/*
 * Whether the flags say a real signal is here despite one of those warnings.
 *
 * Kept out of `survey_suspect_warns()` on purpose, and the consequence is
 * accepted rather than hidden: a candidate on the comb that reads displaced
 * **still counts as suspicious**. On band II, where one channel in sixteen
 * falls on the fine comb, that means a caption can say "mostly the receiver"
 * about a band where the program has positive evidence the loudest thing is a
 * station.
 *
 * That is the price of not suppressing, and if it misleads in practice the
 * fix is to report both numbers -- `survey_suspect_contested_count()` is
 * here for exactly that -- and not to start clearing marks.
 */
static inline int survey_suspect_contested(unsigned flags) {
    return (flags & SURVEY_SUSPECT_DISPLACED) != 0 &&
           survey_suspect_warns(flags);
}

/*
 * Whether the flags say there is nothing here at all.
 *
 * Kept apart from survey_suspect_warns() because the two say different things
 * and a reader acts differently on each: "this looks like the receiver" means
 * unplug the antenna and sweep again, and "there is no carrier here" means
 * the frequency is empty however often it was seen. Marked differently on
 * screen for the same reason.
 */
static inline int survey_suspect_empty(unsigned flags) {
    return (flags & SURVEY_SUSPECT_NO_CARRIER) != 0;
}

/*
 * How many of a sweep's candidates warn. Shown beside the candidate count, so
 * a sweep that is mostly the receiver talking to itself says so before anyone
 * clicks into it.
 */
static inline int survey_suspect_count(const struct survey_plan *plan,
                                      double reference_hz,
                                       const struct sdr_peak *peaks, int count,
                                       double sample_rate, int fft_size,
                                       int dc_filtered) {
    int suspicious = 0;

    for (int i = 0; i < count; i++) {
        double hz = survey_plan_bin_centre(plan, peaks[i].index);

        if (survey_suspect_warns(survey_suspect(plan, reference_hz, hz, 0.0,
                                                sample_rate,
                                                fft_size, dc_filtered)))
            suspicious++;
    }
    return suspicious;
}

/*
 * The sentence for a candidate's flags, or NULL when there is nothing to say.
 * Worded as a resemblance, never as a finding: the operator is being told what
 * to suspect, not what is true.
 */
static inline const char *survey_suspect_reason(unsigned flags) {
    /* The coherence reading first when it is there, because it is the stronger
       evidence: a comb multiple is a coincidence argument and this is a
       cancellation. Paired with the comb it says so. */
    /* The contradiction first: a reader who is told "on the comb" and nothing
       else will stop looking, and this is the case where they should not. */
    if ((flags & SURVEY_SUSPECT_DISPLACED) &&
        (flags & SURVEY_SUSPECT_REFERENCE))
        return "on the receiver's comb, but reads displaced: something real "
               "is here";
    if (flags & SURVEY_SUSPECT_DISPLACED)
        return "reads displaced by this receiver's error: not clocked here";
    if ((flags & SURVEY_SUSPECT_CLOCK_COHERENT) &&
        (flags & SURVEY_SUSPECT_REFERENCE))
        return "on the receiver's reference comb, and reads exact: clocked "
               "here";
    if (flags & SURVEY_SUSPECT_CLOCK_COHERENT)
        return "reads at its exact nominal: clocked with this receiver";
    if ((flags & SURVEY_SUSPECT_REFERENCE) &&
        (flags & SURVEY_SUSPECT_STEP_CENTRE))
        return "on the receiver's reference comb, and at a step centre";
    if (flags & SURVEY_SUSPECT_REFERENCE)
        return "on the receiver's 14.4 MHz reference comb";
    if (flags & SURVEY_SUSPECT_STEP_CENTRE)
        return "at a step centre, where the DC offset lands (filter is off)";
    if (flags & SURVEY_SUSPECT_UNEXPLAINED)
        return "a bare carrier on no modelled comb: unexplained";
    return NULL;
}

#endif
