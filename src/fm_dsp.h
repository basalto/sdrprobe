#ifndef FM_DSP_H
#define FM_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * FM broadcast: the multiplex, and the front end of the RDS subcarrier.
 *
 * Probe side of the context map. Everything here says what the signal looks
 * like -- how strong the pilot is, whether it is locked, what the symbols
 * measured to -- and nothing here claims to have read a message. Soft symbols
 * are where it stops; blocks, groups and a station's name belong to the
 * decoder that follows (CONTEXT.md, and the same seam gsm_dsp.c stops at).
 *
 * It links libm and nothing else: no raylib, no librtlsdr (ADR-0012).
 *
 * The arithmetic that makes this tractable, and it is worth stating before
 * any of the code: every rate in an FM multiplex is derived from the 19 kHz
 * pilot by an integer.
 *
 *     pilot            19000 Hz
 *     stereo           38000 Hz   = 2 x pilot
 *     RDS subcarrier   57000 Hz   = 3 x pilot
 *     RDS symbols      1187.5 Hz  = 57000 / 48 = pilot / 16
 *
 * The station transmits the pilot precisely so a receiver can have all of
 * them. So there is no blind carrier loop to converge or fail to converge,
 * and no independent symbol-timing recovery: lock the pilot, and the
 * subcarrier phase is three times that phase and the symbol clock ticks every
 * sixteen pilot cycles. What remains unknown is one phase offset within the
 * symbol and the sense of the differential encoding -- both small searches
 * with a right answer, rather than loops with a convergence question.
 */

#define FM_PILOT_HZ 19000.0
#define FM_RDS_SUBCARRIER_HZ 57000.0
#define FM_RDS_SYMBOL_RATE_HZ 1187.5
/* 19000 / 1187.5. An integer, and the reason the symbol clock is free. */
#define FM_RDS_PILOT_CYCLES_PER_SYMBOL 16
/* The subcarrier is suppressed and the data is +-2.4 kHz around it. */
#define FM_RDS_HALF_BANDWIDTH_HZ 2400.0

/*
 * The lowest rate this will accept.
 *
 * A broadcast multiplex runs to 57 kHz plus its sidebands, and the carrier it
 * rides on deviates +-75 kHz, so the I/Q has to carry about 240 kHz before the
 * discriminator sees an undistorted multiplex. The house rate and the FM
 * captures are far above it; the check exists so a capture recorded at a rate
 * that cannot hold the subcarrier is refused rather than quietly decoded into
 * nonsense.
 */
#define FM_MIN_SAMPLE_RATE 240000.0

/*
 * Stage one: the discriminator.
 *
 * The angle each sample turns from the one before, which for an FM carrier is
 * proportional to the instantaneous frequency and so is the multiplex. Output
 * is radians per sample, one value per input pair after the first: `pairs`
 * pairs in, `pairs - 1` samples out.
 *
 * Interleaved unsigned 8-bit I/Q with 127.5 as zero, the house convention.
 */
size_t fm_discriminate(const uint8_t *iq, size_t pairs, float *out,
                       size_t capacity);
/* The same from centred floats, which is what a check builds. */
size_t fm_discriminate_f(const float *i, const float *q, size_t pairs,
                         float *out, size_t capacity);

/*
 * Stage two: the pilot, recovered coherently.
 *
 * A phase-locked loop on 19 kHz. Coherent because the phase is the payload:
 * an incoherent measurement would say how strong the pilot is, and what is
 * needed is *where it is*, so that three times it can be handed to the
 * subcarrier.
 *
 * `phase` is the loop's current phase in radians, unwrapped modulo 2*pi.
 * `amplitude` and `floor_estimate` are what the lock decision is made from,
 * and are exposed because a lock that cannot be inspected cannot be
 * diagnosed.
 */
struct fm_pilot {
    double sample_rate;
    double phase;               /* radians, [0, 2*pi) */
    double frequency;           /* radians per sample, tracked */
    double frequency_average;   /* the same, slowed down enough to report */
    double i_average, q_average;  /* the correlator arms, smoothed */
    double nominal;             /* radians per sample at exactly 19 kHz */
    double alpha, beta;         /* loop gains, from FM_PILOT_LOOP_BW_HZ */
    double amplitude;           /* |smoothed correlation| -- the coherent part */
    double lock_i, lock_q;      /* the phasor, smoothed again and slowly */
    double incoherent;          /* its length, smoothed the same way */
    double coherence;           /* the ratio, 0 to 1; the lock decision */
    double floor_estimate;      /* smoothed |input|, for diagnostics */
    double error;               /* the last phase error, radians */
    long settled;               /* samples fed since the last reset */
};

/*
 * How wide the loop is, and how sure it has to be.
 *
 * A broadcast pilot is a stable tone from a transmitter with a far better
 * reference than this receiver, so the loop only has to absorb the receiver's
 * own error -- a few tens of ppm at 19 kHz is under a hertz. 10 Hz is
 * generous for that and narrow enough to reject the audio either side of it.
 */
#define FM_PILOT_LOOP_BW_HZ 10.0
#define FM_PILOT_DAMPING 0.707
/* Lock takes both: the pilot standing over the multiplex around it, and long
   enough fed that the loop's own transient has passed. A ratio alone calls a
   lock on the first sample of a signal it has not begun to track. */
/*
 * Measured, on three captures taken with the antenna this program is used
 * with, after the loop's own half second of settling:
 *
 *   89.6 MHz, a strong station   min 0.927   mean 0.984   max 0.998
 *   87.7 MHz, a weak one         min 0.830   mean 0.965   max 0.997
 *   91.9 MHz, nothing at all     min 0.052   mean 0.246   max 0.544
 *
 * Both stations stand above 0.70 for every sample of their captures and the
 * empty channel for none of them, so 0.70 is where this goes -- with 0.13
 * clear beneath the worse station and 0.16 clear above the noise.
 */
#define FM_PILOT_MIN_COHERENCE 0.70
/*
 * How long before a lock may be declared at all.
 *
 * Not politeness: the coherence is a ratio of two smoothers, and for the
 * first few tens of milliseconds both are near zero and the ratio is whatever
 * the noise decides. On the empty channel that transient carries it over the
 * threshold for 53 ms and never again -- 2.5% of the capture, all of it at
 * the start. A quarter second is five times that, and still a quarter second.
 */
#define FM_PILOT_SETTLE_SECONDS 0.25
/* How long the correlator arms are averaged before their magnitude is taken.
   Longer buys rejection of everything that is not the pilot, at the price of
   how quickly a lock can be declared; 10 ms is 190 pilot cycles. */
#define FM_PILOT_AVERAGE_SECONDS 0.010
/* And how long the reported frequency is averaged over, which is a different
   question with a different answer -- see fm_dsp.c. */
#define FM_PILOT_REPORT_SECONDS 0.100
/* The second, slower average the lock decision is made over. Long enough that
   a wandering phasor has had time to wander. */
#define FM_PILOT_LOCK_SECONDS 0.200

void fm_pilot_init(struct fm_pilot *pilot, double sample_rate);
/* One sample of multiplex in; the loop advances. Returns the phase it used
   for this sample, which is what a subcarrier mixer wants. */
double fm_pilot_feed(struct fm_pilot *pilot, float sample);
/* Whether the pilot is being tracked rather than guessed at. */
int fm_pilot_locked(const struct fm_pilot *pilot);
/* What the loop settled on, in hertz -- 19 kHz times the receiver's own error,
   which is why this is worth reading even when the RDS decode fails. */
double fm_pilot_hz(const struct fm_pilot *pilot);
/* The parts-per-million the pilot says this receiver is out by, or 0 when it
   has not locked. A broadcast pilot is a better reference than the GSM FCCH
   in one respect and worse in another -- see the note in fm_dsp.c. */
double fm_pilot_ppm(const struct fm_pilot *pilot);

/*
 * Stage three and four: the subcarrier, brought down to baseband at exactly
 * sixteen samples a symbol.
 *
 * Sixteen because that is the pilot rate: a symbol is sixteen pilot cycles,
 * so emitting one sample per pilot cycle needs no resampler, no fractional
 * accumulator and no rate that has to divide anything. The samples are the
 * average of the mixed input since the last emission -- an integrate-and-dump,
 * which is both the decimator and its own anti-alias filter, and cheap enough
 * that the alternative was not worth the arithmetic.
 *
 * Complex, because a suppressed-carrier subcarrier lands on an axis nobody
 * has told us yet. Which axis is worked out from the symbols themselves in
 * the stage after this.
 */
#define FM_RDS_SAMPLES_PER_SYMBOL 16
/* Where the anti-alias filter turns over: above the data's 2.4 kHz so the
   passband is flat, and far enough below the 9.5 kHz fold edge that three
   one-pole sections have room to work. */
#define FM_RDS_LOWPASS_HZ 4000.0

struct fm_rds_front {
    struct fm_pilot pilot;
    double sample_rate;
    double previous_phase;     /* to spot the wrap */
    double turned;             /* pilot radians since the start, unwrapped */
    double next_emit;          /* the value of `turned` the next sample is due */
    double lp_i[3], lp_q[3];   /* the anti-alias sections */
    double lowpass_k;
    double acc_i, acc_q;
    long acc_n;
};

int fm_rds_front_init(struct fm_rds_front *front, double sample_rate);
/*
 * Multiplex in, complex baseband out at FM_RDS_SAMPLES_PER_SYMBOL a symbol.
 * Returns how many came out, which is roughly n * 19000 / sample_rate.
 * Nothing comes out before the pilot locks: without the pilot there is no
 * carrier to mix by and no clock to emit on, and inventing either would
 * produce a confident stream of noise.
 */
size_t fm_rds_front_feed(struct fm_rds_front *front, const float *mpx,
                         size_t n, float *out_i, float *out_q,
                         size_t capacity);

/*
 * Stage five: baseband to soft bits.
 *
 * Three things happen here and each is a decision with a right answer rather
 * than a loop with a convergence question.
 *
 * **Timing.** The pilot gives the symbol *rate* but not which of the sixteen
 * samples begins a symbol. A biphase symbol is half a cycle positive and half
 * negative, so its matched filter is eight samples of +1 followed by eight of
 * -1; the offset whose filter output has the largest mean square is the one
 * the symbols are actually on. Sixteen candidates, and the answer is whichever
 * is biggest.
 *
 * **The axis.** The subcarrier is suppressed, so the data comes back on
 * whatever phase the channel left it at. Squaring a BPSK constellation
 * collapses its two points onto one, so half the angle of the sum of the
 * squares is the axis -- with a 180 degree ambiguity that squaring cannot
 * resolve and does not need to, for the reason below.
 *
 * **The differential.** RDS encodes each data bit as whether the channel bit
 * changed, so the decoder multiplies consecutive symbols. Two conventions
 * fall out of that multiplication and are worth naming, because one of them
 * is a trap this repository has been caught by twice and the other is not:
 *
 *   - Biphase polarity -- whether positive-then-negative means one or zero --
 *     flips the sign of *every* symbol, and a product of two symbols is
 *     unchanged by that. It cannot be got wrong here. That is not luck; it is
 *     what differential encoding is for.
 *   - Whether "no change" means zero is a real convention with a real answer
 *     (it does, IEC 62106) and nothing in a round trip can check it, because
 *     an encoder and a decoder that both had it backwards would agree
 *     perfectly. Only a real capture reading a programme identification that
 *     matches its station can, and that belongs to the ticket after this one.
 */
size_t fm_rds_soft_bits(const float *bb_i, const float *bb_q, size_t samples,
                        float *soft, size_t capacity, int *timing_offset,
                        double *axis_radians);

#endif
