#ifndef SURVEY_SWEEP_H
#define SURVEY_SWEEP_H

#include <math.h>

/*
 * What a sweep is, step by step, and what a measurement of one candidate adds
 * up to. Plain doubles: no raylib, no receiver, no clock.
 *
 * The window arithmetic next door in freq_window.h was extracted after two
 * of its decisions shipped wrong. This is the other half of the same view, and
 * the same argument applies with more force, because none of it is visible
 * even when it is wrong: a step plan that leaves a gap between steps hides
 * whatever transmits in the gap, and the chart looks exactly as it should. The
 * only way to see that is to check the arithmetic (ADR-0012).
 *
 * Constants live here rather than in app.h so the checks can reach them; app.h
 * includes this header.
 */

/* How many bins the survey array holds. A narrow range gets finer bins than a
   wide one, because the alternative is either an unusable resolution across
   1.7 GHz or an array nobody can draw -- but never finer than the FFT filling
   it, since asking for more resolution than the spectrum has only interpolates
   noise. The resolution actually in use is reported on screen rather than left
   to be inferred. */
#define SURVEY_BINS 8192

/* Room for every candidate a wide sweep turns up, not just the loudest few.
   At 64 a full-tuner sweep filled the list with FM and DAB and marked nothing
   above 1 GHz, because the cap keeps the strongest and the strongest are all
   in the broadcast bands. A candidate costs 24 bytes; being stingy here buys
   nothing and hides whole allocations. */
#define SURVEY_MAX_PEAKS 512

#define SURVEY_SETTLE_SECONDS 0.10

/* How long to sit on each step. One block catches whatever is transmitting at
   that instant, which is the wrong tool for anything bursty: a channel that
   keys up for 200 ms every few seconds is simply absent from most steps.
   Dwelling longer peak-holds across the blocks that arrive, so a burst
   anywhere in the dwell leaves its mark. The cost is linear -- doubling the
   dwell doubles the sweep. */
#define SURVEY_DWELL_DEFAULT 0.10
#define SURVEY_DWELL_MIN 0.02
#define SURVEY_DWELL_MAX 10.0
#define SURVEY_MEASURE_SECONDS 2.0
/* The descent a maximum must make before it can reach higher ground. This is
   what rejects the shoulder of a strong carrier, and it is not a noise
   threshold -- see SURVEY_FLOOR_THRESHOLD_DB below for that one. */
#define SURVEY_MIN_PROMINENCE_DB 8.0f

/*
 * How far above the level around it a candidate has to stand: the same bar it
 * had to clear to be a candidate at all.
 *
 * ADR-0013's complaint was that the gate and the reported figure are different
 * measurements, so a candidate could clear the first and report much less of
 * the second -- and that the filtering which should have been the second was
 * being done by accident, by an unbounded width walk, at an effective 20 dB
 * nobody chose. Holding both to one number is what makes the *above floor*
 * column mean what the help text says it means.
 *
 * The ADR expected this to need a threshold derived from the dwell, on the
 * grounds that 8 dB is below what peak-held noise reaches -- 16 dB at one
 * block, falling to 8.3 dB at sixteen. That is not what this program's noise
 * does, and `make probe-survey-threshold` is the measurement: pure noise
 * through the real transform and the real fold gives a survey array with a
 * standard deviation of 0.2 to 0.5 dB and a biggest bin-to-bin step of 2.7 dB,
 * at every fold depth from 1 to 218, and **not one candidate at any bar down
 * to zero**. The reason is the averaging inside sdr_dsp_spectrum(): each
 * transform bin is the mean of every Hann window in the block, 64 of them, and
 * the peak hold over a few hundred such means barely moves. The ADR's table
 * has to have been measuring an unaveraged periodogram, where an exponential
 * per bin does reach 16 dB.
 *
 * So there is nothing for a dwell-dependent bar to track. One number, equal to
 * the gate, and the probe is what says so.
 */
#define SURVEY_FLOOR_THRESHOLD_DB SURVEY_MIN_PROMINENCE_DB

/* How deeply a sweep peak-held into one of its bins: transform bins per survey
   bin, times blocks folded. Not a threshold any more -- the probe above found
   nothing for it to predict -- but it is what that probe sweeps over, and what
   a future claim about noise would have to be a function of. */
static inline double survey_fold_depth(double bin_hz, double fft_bin_hz,
                                       int blocks) {
    double per_bin = fft_bin_hz > 0.0 ? bin_hz / fft_bin_hz : 1.0;

    if (per_bin < 1.0)
        per_bin = 1.0;
    if (blocks < 1)
        blocks = 1;
    return per_bin * (double)blocks;
}

/* How many blocks a dwell is worth. At least one: a step always folds
   something before it moves on. */
#define SURVEY_BLOCK_SECONDS 0.0655
static inline int survey_blocks_in(double dwell_seconds) {
    int blocks = (int)(dwell_seconds / SURVEY_BLOCK_SECONDS);
    return blocks < 1 ? 1 : blocks;
}

#define SURVEY_BANDWIDTH_DB 20.0f
#define SURVEY_SENTINEL_DBFS (-300.0f)
#define SURVEY_OFFSET_HZ 300000.0 /* keep a candidate off the DC spike */

/* Of each step's span, the middle that is kept. The tuner's response rolls off
   at the edges, so a signal there reads low; the steps overlap by the rest. */
#define SURVEY_USABLE_SPAN 0.8

/* The fewest bins worth charting, and the widest a range may be before the
   array runs out -- both bounds of the same arithmetic. */
#define SURVEY_MIN_BINS 16

enum survey_plan_status {
    SURVEY_PLAN_OK,
    SURVEY_PLAN_BAD_RANGE, /* the high edge is not above the low one */
    SURVEY_PLAN_BAD_DWELL, /* outside SURVEY_DWELL_MIN..MAX */
    SURVEY_PLAN_BAD_RATE   /* a sample rate too low to step across anything */
};

struct survey_plan {
    double lower_hz;
    double upper_hz;
    double step_span_hz; /* what one tuning covers, after the roll-off */
    int step_count;
    int bins;
    double bin_hz;
    double dwell_seconds;
    double seconds; /* what the whole sweep will cost */
};

/*
 * Work out the sweep before starting it: how many tunings it takes, how many
 * bins the range gets, and what it will cost in time.
 *
 * The step count must *cover* the range -- ceil, not round -- or the top of
 * the range is never tuned to and whatever is up there is silently missing.
 */
static inline enum survey_plan_status
survey_plan_make(double from_hz, double to_hz, double sample_rate,
                 int fft_size, double dwell_seconds, struct survey_plan *plan) {
    double span;
    double finest_hz;
    double bin_hz;

    if (!(to_hz > from_hz))
        return SURVEY_PLAN_BAD_RANGE;
    if (!(dwell_seconds >= SURVEY_DWELL_MIN) ||
        !(dwell_seconds <= SURVEY_DWELL_MAX))
        return SURVEY_PLAN_BAD_DWELL;

    span = to_hz - from_hz;
    plan->step_span_hz = sample_rate * SURVEY_USABLE_SPAN;
    if (plan->step_span_hz < 1.0)
        return SURVEY_PLAN_BAD_RATE;

    plan->lower_hz = from_hz;
    plan->upper_hz = to_hz;
    plan->dwell_seconds = dwell_seconds;

    /* As many bins as the array holds, unless that would be finer than the
       FFT feeding them: past that point the extra bins carry no more
       information than the ones beside them. */
    finest_hz = fft_size > 0 ? sample_rate / (double)fft_size : 0.0;
    bin_hz = span / (double)SURVEY_BINS;
    if (bin_hz < finest_hz)
        bin_hz = finest_hz;
    plan->bins = bin_hz > 0.0 ? (int)(span / bin_hz) : SURVEY_BINS;
    if (plan->bins > SURVEY_BINS)
        plan->bins = SURVEY_BINS;
    if (plan->bins < SURVEY_MIN_BINS)
        plan->bins = SURVEY_MIN_BINS;
    plan->bin_hz = span / (double)plan->bins;

    plan->step_count = (int)ceil(span / plan->step_span_hz);
    if (plan->step_count < 1)
        plan->step_count = 1;
    plan->seconds = (double)plan->step_count *
                    (SURVEY_SETTLE_SECONDS + dwell_seconds);
    return SURVEY_PLAN_OK;
}

/* Where the receiver is tuned for a step: the middle of the span that step
   covers. */
static inline double survey_plan_step_centre(const struct survey_plan *plan,
                                             int step) {
    return plan->lower_hz + ((double)step + 0.5) * plan->step_span_hz;
}

/* Which survey bin a frequency falls in, or -1 when it falls outside the
   swept range. */
static inline int survey_plan_bin_at(const struct survey_plan *plan,
                                     double hz) {
    int bin;

    if (hz < plan->lower_hz || hz >= plan->upper_hz)
        return -1;
    bin = (int)((hz - plan->lower_hz) / (plan->upper_hz - plan->lower_hz) *
                (double)plan->bins);
    if (bin < 0 || bin >= plan->bins)
        return -1;
    return bin;
}

/* The middle of survey bin `bin` -- the inverse of the mapping above, and the
   frequency a candidate found in that bin is reported at. It is a bin centre,
   so it is only ever within half a bin of the truth; measuring the candidate
   is what sharpens it. */
static inline double survey_plan_bin_centre(const struct survey_plan *plan,
                                            int bin) {
    return plan->lower_hz + ((double)bin + 0.5) * plan->bin_hz;
}

/* Whether an FFT bin at `hz` is inside the part of this tuning that is kept.
   Outside it the tuner rolls off, and the next step covers it properly. */
static inline int survey_fold_keeps(double hz, double centre_hz,
                                    double sample_rate) {
    return fabs(hz - centre_hz) <= sample_rate * SURVEY_USABLE_SPAN / 2.0;
}

/*
 * Peak-hold within a survey bin. A survey bin is usually wider than an FFT
 * bin, and a narrow carrier averaged with the noise beside it is a carrier the
 * survey would miss -- so the strongest wins, and the sentinel loses to
 * anything.
 */
static inline float survey_fold_hold(float existing, float power) {
    if (existing <= SURVEY_SENTINEL_DBFS || power > existing)
        return power;
    return existing;
}

enum survey_step_phase {
    SURVEY_STEP_SETTLING, /* the tuner has not caught up; nothing to fold */
    SURVEY_STEP_DWELLING, /* fold this block in and stay here */
    SURVEY_STEP_NEXT,     /* dwell is over; tune to the next step */
    SURVEY_STEP_FINISHED  /* that was the last step */
};

/*
 * What to do with the block that just arrived, `elapsed` seconds after tuning
 * to `step` of `step_count`.
 *
 * A block arriving during the settle is discarded rather than folded: it holds
 * samples from before the tuner moved, so folding it writes the *previous*
 * step's signal into this step's bins, which reads as a real carrier at a
 * frequency nothing is transmitting on.
 */
static inline enum survey_step_phase
survey_step_phase_at(double elapsed, double dwell_seconds, int step,
                     int step_count) {
    if (elapsed < SURVEY_SETTLE_SECONDS)
        return SURVEY_STEP_SETTLING;
    if (elapsed < SURVEY_SETTLE_SECONDS + dwell_seconds)
        return SURVEY_STEP_DWELLING;
    if (step + 1 >= step_count)
        return SURVEY_STEP_FINISHED;
    return SURVEY_STEP_NEXT;
}

/*
 * Whether the sweep may leave this step yet.
 *
 * The dwell being over is not enough: it is a floor on how long to listen, not
 * a promise that anything was heard. Blocks arrive every 65.5 ms at the house
 * rate and the settle takes the first of them, so a step whose dwell is short
 * can pass through both phases having folded nothing -- and the bins it was
 * responsible for are then left unmeasured, which draws as a gap in the sweep
 * and reads as a band with nothing in it. Staying until one block has been
 * folded costs at most a block and removes that silence.
 *
 * The caller still has to stop when the source ends, or a receiver that has
 * died would hold the sweep on its first step for ever.
 */
static inline int survey_step_may_advance(enum survey_step_phase phase,
                                          int folded_this_step) {
    if (phase == SURVEY_STEP_SETTLING || phase == SURVEY_STEP_DWELLING)
        return 0;
    return folded_this_step > 0;
}

/*
 * What repeatedly measuring one candidate adds up to: how often it was there,
 * and how much its centre moved while it was.
 */
struct survey_measurement {
    int blocks; /* blocks looked at, whether or not anything was found */
    int hits;   /* blocks the candidate was actually up in */
    double centre_sum;
    double centre_square_sum;
    float first_prominence;
};

static inline void survey_measure_reset(struct survey_measurement *m) {
    m->blocks = 0;
    m->hits = 0;
    m->centre_sum = 0.0;
    m->centre_square_sum = 0.0;
    m->first_prominence = 0.0f;
}

/*
 * Fold one block's characterisation in. `found` says whether the carrier was
 * characterised at all; returns 1 when the block counted as the candidate
 * being up, which is what the duty below is a fraction of.
 *
 * "Up" means at least half the prominence it was first seen with. An absolute
 * threshold cannot work here: candidates are found at every level from a
 * pager's carrier to a broadcast transmitter, and the question is whether
 * *this* one is transmitting now, not whether it is strong.
 */
static inline int survey_measure_observe(struct survey_measurement *m,
                                         int found, float prominence_db,
                                         double centre_hz) {
    m->blocks++;
    if (!found)
        return 0;
    if (m->first_prominence <= 0.0f)
        m->first_prominence = prominence_db;
    if (prominence_db < m->first_prominence / 2.0f)
        return 0;
    m->hits++;
    m->centre_sum += centre_hz;
    m->centre_square_sum += centre_hz * centre_hz;
    return 1;
}

/* The fraction of looked-at blocks the candidate was up in. */
static inline double survey_measure_duty(const struct survey_measurement *m) {
    if (m->blocks <= 0)
        return 0.0;
    return (double)m->hits / (double)m->blocks;
}

static inline const char *survey_measure_duty_label(double duty) {
    if (duty > 0.9)
        return "continuous";
    if (duty > 0.3)
        return "intermittent";
    return "bursty";
}

/*
 * How far the measured centre wandered, in Hz. Computed from the running sums
 * rather than a buffer of samples, so rounding can carry the variance a little
 * below zero on a carrier that did not move at all; that is a rock-steady
 * signal, not a negative spread.
 */
static inline double survey_measure_spread_hz(
    const struct survey_measurement *m) {
    double mean;
    double variance;

    if (m->hits < 2)
        return 0.0;
    mean = m->centre_sum / (double)m->hits;
    variance = m->centre_square_sum / (double)m->hits - mean * mean;
    if (variance < 0.0)
        variance = 0.0;
    return sqrt(variance);
}

#endif
