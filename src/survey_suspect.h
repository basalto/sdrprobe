#ifndef SURVEY_SUSPECT_H
#define SURVEY_SUSPECT_H

#include <math.h>

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

/* The RTL2832U's reference crystal, and the comb its harmonics land on. The
   spacing is half the crystal: a divider somewhere in the chain puts a tone
   every 14.4 MHz, which is what a disconnected sweep shows. */
#define RECEIVER_REFERENCE_HZ 28800000.0
#define RECEIVER_COMB_HZ (RECEIVER_REFERENCE_HZ / 2.0)

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
#define RECEIVER_FINE_COMB_HZ (RECEIVER_REFERENCE_HZ / 18.0)

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
    SURVEY_SUSPECT_UNRESOLVED = 1 << 2
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
static inline int survey_reference_harmonic(double hz, double tolerance_hz) {
    return survey_comb_harmonic(hz, RECEIVER_COMB_HZ, tolerance_hz);
}

/* And of the fine one, which needs a tighter tolerance to mean anything --
   survey_comb_harmonic() refuses rather than guessing when it does not have
   one. */
static inline int survey_fine_harmonic(double hz, double tolerance_hz) {
    return survey_comb_harmonic(hz, RECEIVER_FINE_COMB_HZ, tolerance_hz);
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
static inline unsigned survey_suspect(const struct survey_plan *plan, double hz,
                                      double bandwidth_hz, double sample_rate,
                                      int fft_size, int dc_filtered) {
    double tolerance = survey_suspect_tolerance(plan, sample_rate, fft_size);
    double comb = survey_comb_tolerance(plan, sample_rate, fft_size);
    int bare = survey_is_unresolved(bandwidth_hz, plan->bin_hz, sample_rate,
                                    fft_size);
    unsigned flags = SURVEY_SUSPECT_NONE;

    if (survey_reference_harmonic(hz, comb))
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
    if (bare && survey_fine_harmonic(hz, comb))
        flags |= SURVEY_SUSPECT_REFERENCE;
    if (!dc_filtered && survey_at_step_centre(plan, hz, tolerance))
        flags |= SURVEY_SUSPECT_STEP_CENTRE;
    if (bare)
        flags |= SURVEY_SUSPECT_UNRESOLVED;
    return flags;
}

/*
 * Whether the flags amount to a warning. Narrowness alone does not: a pager, a
 * telemetry link and a beacon are all legitimately narrow, and crying wolf on
 * them would teach the operator to ignore the line.
 */
static inline int survey_suspect_warns(unsigned flags) {
    return (flags & (SURVEY_SUSPECT_REFERENCE | SURVEY_SUSPECT_STEP_CENTRE)) !=
           0;
}

/*
 * How many of a sweep's candidates warn. Shown beside the candidate count, so
 * a sweep that is mostly the receiver talking to itself says so before anyone
 * clicks into it.
 */
static inline int survey_suspect_count(const struct survey_plan *plan,
                                       const struct sdr_peak *peaks, int count,
                                       double sample_rate, int fft_size,
                                       int dc_filtered) {
    int suspicious = 0;

    for (int i = 0; i < count; i++) {
        double hz = survey_plan_bin_centre(plan, peaks[i].index);

        if (survey_suspect_warns(survey_suspect(plan, hz, 0.0, sample_rate,
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
    if ((flags & SURVEY_SUSPECT_REFERENCE) &&
        (flags & SURVEY_SUSPECT_STEP_CENTRE))
        return "on the receiver's reference comb, and at a step centre";
    if (flags & SURVEY_SUSPECT_REFERENCE)
        return "on the receiver's 14.4 MHz reference comb";
    if (flags & SURVEY_SUSPECT_STEP_CENTRE)
        return "at a step centre, where the DC offset lands (filter is off)";
    return NULL;
}

#endif
