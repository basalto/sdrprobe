#include "tetra_dsp.h"

#include "signal_probe.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * The four phase steps, in quarter-turns of pi/4, indexed by the dibit
 * B(2k-1),B(2k) read as a two-bit number.
 *
 * ETSI EN 300 392-2 V3.8.1 (2016-08) table 5.1:
 *
 *     B(2k-1)  B(2k)   Dphi(k)
 *        0       0      +pi/4
 *        0       1     +3pi/4
 *        1       0      -pi/4
 *        1       1     -3pi/4
 *
 * This was guessed before the document was to hand, and the guess had the last
 * two the other way round -- dibit 2 as -3pi/4 and dibit 3 as -pi/4. The
 * synthetic round trip passed anyway, every time, because the encoder and the
 * decoder shared the mistake. That is the whole of why the header used to say
 * this table was not to be believed until a parity check passed over real
 * symbols, and it is the same failure as a conjugated primary sequence and a
 * scattered SCH field layout. The fix is a transcription from the standard,
 * and the check that catches a transcription error is the training sequence
 * below: known bits, on air, in a known place.
 */
static const int step_quarters[TETRA_PHASE_STEPS] = { 1, 3, -1, -3 };

double tetra_step_for_dibit(int dibit) {
    if (dibit < 0 || dibit >= TETRA_PHASE_STEPS)
        return 0.0;
    return (double)step_quarters[dibit] * M_PI / 4.0;
}

/* Wrap to (-pi, pi]. */
static double wrap(double radians) {
    while (radians > M_PI)
        radians -= 2.0 * M_PI;
    while (radians <= -M_PI)
        radians += 2.0 * M_PI;
    return radians;
}

int tetra_dibit_for_step(double radians) {
    int best = 0, k;
    double closest = 4.0;

    for (k = 0; k < TETRA_PHASE_STEPS; k++) {
        double away = fabs(wrap(radians - tetra_step_for_dibit(k)));
        if (away < closest) {
            closest = away;
            best = k;
        }
    }
    return best;
}

/*
 * A root-raised cosine, sampled at `rate` with symbol period 1/symbol_rate.
 *
 * Written out rather than taken from a library, like every other filter here
 * (ADR-0003). The two removable singularities are the whole difficulty: at
 * t = 0 and at t = T/(4a) the closed form is 0/0, and a filter that returns a
 * not-a-number at its centre tap is one that silently produces nothing.
 */
static double rrc(double t, double symbol_rate, double rolloff) {
    double period = 1.0 / symbol_rate;
    double x = t / period;
    double a = rolloff;
    double numerator, denominator;

    if (fabs(x) < 1e-9)
        return 1.0 - a + 4.0 * a / M_PI;
    if (a > 0.0 && fabs(fabs(x) - 1.0 / (4.0 * a)) < 1e-9) {
        double first = (1.0 + 2.0 / M_PI) * sin(M_PI / (4.0 * a));
        double second = (1.0 - 2.0 / M_PI) * cos(M_PI / (4.0 * a));
        return a / sqrt(2.0) * (first + second);
    }
    numerator = sin(M_PI * x * (1.0 - a)) +
                4.0 * a * x * cos(M_PI * x * (1.0 + a));
    denominator = M_PI * x * (1.0 - (4.0 * a * x) * (4.0 * a * x));
    return numerator / denominator;
}

double tetra_coarse_offset_hz(const float *i_samples, const float *q_samples,
                              size_t pairs, double sample_rate,
                              double search_half_width_hz) {
    /*
     * The power-weighted centre of the channel, found by sweeping a narrow
     * window rather than by transforming: the offset wanted is a single number
     * and a whole spectrum is a lot of arithmetic to get it.
     *
     * Coarse deliberately. It has to land inside the fine estimator's 4500 Hz
     * ambiguity and no closer.
     */
    double best_hz = 0.0, best_power = -1.0;
    double hz;

    if (!i_samples || !q_samples || pairs < 1024 || !(sample_rate > 0.0))
        return 0.0;
    for (hz = -search_half_width_hz; hz <= search_half_width_hz;
         hz += 500.0) {
        double w = -2.0 * M_PI * hz / sample_rate;
        double sr = 0.0, si = 0.0, power = 0.0;
        size_t n, window = 0;

        /* Integrate the channel's energy after mixing this candidate to zero:
           a boxcar the width of a symbol is a crude but adequate low pass. */
        for (n = 0; n < pairs; n++) {
            double c = cos(w * (double)n), s = sin(w * (double)n);
            sr += (double)i_samples[n] * c - (double)q_samples[n] * s;
            si += (double)i_samples[n] * s + (double)q_samples[n] * c;
            if (++window >= (size_t)(sample_rate / TETRA_SYMBOL_RATE_HZ)) {
                power += sr * sr + si * si;
                sr = si = 0.0;
                window = 0;
            }
        }
        if (power > best_power) {
            best_power = power;
            best_hz = hz;
        }
    }
    return best_hz;
}

size_t tetra_channel(const float *i_samples, const float *q_samples,
                     size_t pairs, double sample_rate, double offset_hz,
                     float *out_i, float *out_q, size_t capacity) {
    static float mixed_i[TETRA_MAX_WORK + 512];
    static float mixed_q[TETRA_MAX_WORK + 512];
    static double taps[2 * TETRA_RRC_SPAN * 16 + 1];
    int decimation, half, taps_count, k;
    size_t n, written = 0, kept = 0;
    double w;

    if (!i_samples || !q_samples || !out_i || !out_q || pairs == 0)
        return 0;
    decimation = (int)(sample_rate / TETRA_WORK_RATE_HZ + 0.5);
    if (decimation < 1 ||
        fabs(sample_rate - decimation * TETRA_WORK_RATE_HZ) > 1.0)
        return 0;              /* refused, not resampled */

    /* Mix to zero and decimate, averaging each group rather than picking one
       of it: the average is a low pass, and without one the decimation folds
       everything above 50 kHz onto the channel. */
    w = -2.0 * M_PI * offset_hz / sample_rate;
    {
        double ar = 0.0, ai = 0.0;
        int in_group = 0;
        for (n = 0; n < pairs && kept < TETRA_MAX_WORK; n++) {
            double c = cos(w * (double)n), s = sin(w * (double)n);
            ar += (double)i_samples[n] * c - (double)q_samples[n] * s;
            ai += (double)i_samples[n] * s + (double)q_samples[n] * c;
            if (++in_group == decimation) {
                mixed_i[kept] = (float)(ar / decimation);
                mixed_q[kept] = (float)(ai / decimation);
                kept++;
                ar = ai = 0.0;
                in_group = 0;
            }
        }
    }

    /* The matched filter, at the working rate. */
    half = (int)(TETRA_RRC_SPAN * TETRA_SAMPLES_PER_SYMBOL + 0.5);
    if (half > TETRA_RRC_SPAN * 16)
        half = TETRA_RRC_SPAN * 16;
    taps_count = 2 * half + 1;
    {
        double sum = 0.0;
        for (k = 0; k < taps_count; k++) {
            double t = (double)(k - half) / TETRA_WORK_RATE_HZ;
            taps[k] = rrc(t, TETRA_SYMBOL_RATE_HZ, TETRA_RRC_ROLLOFF);
            sum += taps[k] * taps[k];
        }
        /* Unit energy, so a level measured through it means the same thing at
           any span. */
        sum = sum > 0.0 ? sqrt(sum) : 1.0;
        for (k = 0; k < taps_count; k++)
            taps[k] /= sum;
    }
    for (n = (size_t)half; n + (size_t)half < kept && written < capacity;
         n++) {
        double ar = 0.0, ai = 0.0;
        for (k = 0; k < taps_count; k++) {
            ar += taps[k] * (double)mixed_i[n + (size_t)half - (size_t)k];
            ai += taps[k] * (double)mixed_q[n + (size_t)half - (size_t)k];
        }
        out_i[written] = (float)ar;
        out_q[written] = (float)ai;
        written++;
    }
    return written;
}

double tetra_symbol_timing(const float *i_samples, const float *q_samples,
                           size_t pairs, double *strength) {
    /*
     * One rate, and the arithmetic is signal_symbol_line()'s.
     *
     * It moved down because it needs nothing transcribed from ETSI: a line at
     * the symbol rate in the squared magnitude is a property of any linearly
     * modulated signal with excess bandwidth, so the same measurement answers
     * "does this have a symbol rate, and what is it" for a carrier nobody has
     * identified. What stays here is the pair of constants that say this one
     * is TETRA.
     */
    return signal_symbol_line(i_samples, q_samples, pairs, TETRA_WORK_RATE_HZ,
                              TETRA_SYMBOL_RATE_HZ, strength);
}

/*
 * Four-point cubic interpolation, because the symbol instants land between
 * samples and at 5.56 samples per symbol linear is not good enough.
 *
 * Measured: linear left 0.067 radians of error on a *noiseless* signal --
 * nearly four degrees of a constellation whose points are 90 apart -- and,
 * tellingly, it was worst when the timing was on a sample and best a third of
 * a symbol off, which is the signature of an interpolator rather than of a
 * timing estimate. Cubic takes the same case to well under a degree.
 */
static void interpolate(const float *i_samples, const float *q_samples,
                        size_t pairs, double at, double *re, double *im) {
    size_t n = (size_t)at;
    double mu = at - (double)n;
    double c0, c1, c2, c3;

    if (n < 1 || n + 2 >= pairs) {
        /* No room for the four points: fall back rather than read outside. */
        if (n + 1 >= pairs) {
            *re = *im = 0.0;
            return;
        }
        *re = (1.0 - mu) * i_samples[n] + mu * i_samples[n + 1];
        *im = (1.0 - mu) * q_samples[n] + mu * q_samples[n + 1];
        return;
    }
    c0 = -mu * (mu - 1.0) * (mu - 2.0) / 6.0;
    c1 = (mu + 1.0) * (mu - 1.0) * (mu - 2.0) / 2.0;
    c2 = -(mu + 1.0) * mu * (mu - 2.0) / 2.0;
    c3 = (mu + 1.0) * mu * (mu - 1.0) / 6.0;
    *re = c0 * i_samples[n - 1] + c1 * i_samples[n] +
          c2 * i_samples[n + 1] + c3 * i_samples[n + 2];
    *im = c0 * q_samples[n - 1] + c1 * q_samples[n] +
          c2 * q_samples[n + 1] + c3 * q_samples[n + 2];
}

int tetra_demodulate(const float *i_samples, const float *q_samples,
                     size_t pairs, double coarse_offset_hz,
                     struct tetra_symbols *out) {
    double timing, strength, previous_re = 0.0, previous_im = 0.0;
    double sum_re = 0.0, sum_im = 0.0, rotation;
    double at;
    int count = 0, k;

    if (!i_samples || !q_samples || !out || pairs < 64)
        return 0;
    memset(out, 0, sizeof(*out));
    out->coarse_offset_hz = coarse_offset_hz;

    timing = tetra_symbol_timing(i_samples, q_samples, pairs, &strength);
    out->timing_phase = timing;

    /* Every symbol's phase step, before the fine offset is known. */
    for (at = timing * TETRA_SAMPLES_PER_SYMBOL;
         at + 1.0 < (double)pairs && count < TETRA_MAX_SYMBOLS;
         at += TETRA_SAMPLES_PER_SYMBOL) {
        double re, im;

        interpolate(i_samples, q_samples, pairs, at, &re, &im);
        if (count > 0 || (previous_re != 0.0 || previous_im != 0.0)) {
            /* The step is this symbol against the last: a product with the
               conjugate, which is what makes absolute phase irrelevant. */
            double dr = re * previous_re + im * previous_im;
            double di = im * previous_re - re * previous_im;
            if (count > 0 || dr != 0.0 || di != 0.0)
                out->step[count++] = (float)atan2(di, dr);
        }
        previous_re = re;
        previous_im = im;
    }
    if (count < 2)
        return 0;
    out->count = count;

    /*
     * The fine offset, from the steps themselves.
     *
     * Every legal step is an odd multiple of pi/4, so four times any of them
     * is an odd multiple of pi -- which is to say e^(j4*step) is -1 for all
     * four, whatever was sent. The data cancels itself and what is left is
     * four times the rotation the offset added. That is the same trick as
     * squaring a BPSK signal to find its carrier, one power further up.
     *
     * Ambiguous every quarter turn, which is a quarter of the symbol rate:
     * 4500 Hz. The coarse stage exists to land inside it.
     */
    for (k = 0; k < count; k++) {
        sum_re += cos(4.0 * (double)out->step[k]);
        sum_im += sin(4.0 * (double)out->step[k]);
    }
    rotation = wrap(atan2(-sum_im, -sum_re)) / 4.0;
    out->fine_offset_hz = rotation * TETRA_SYMBOL_RATE_HZ / (2.0 * M_PI);

    /* Correct, slice, and measure how well the result fits. */
    {
        double error = 0.0;
        for (k = 0; k < count; k++) {
            double corrected = wrap((double)out->step[k] - rotation);
            int dibit = tetra_dibit_for_step(corrected);
            double away = fabs(wrap(corrected - tetra_step_for_dibit(dibit)));

            out->step[k] = (float)corrected;
            out->dibit[k] = (unsigned char)dibit;
            error += away * away;
        }
        out->rms_error_rad = (float)sqrt(error / (double)count);
        /*
         * Steps spread uniformly sit pi/sqrt(48) from the nearest of four,
         * which is what noise, a constant-envelope modulation, or the wrong
         * symbol rate all produce. One means every step landed exactly.
         */
        {
            double random_error = M_PI / sqrt(48.0);
            double lock = 1.0 - out->rms_error_rad / random_error;
            out->lock = (float)(lock < 0.0 ? 0.0 : (lock > 1.0 ? 1.0 : lock));
        }
    }
    (void)strength;
    return count;
}

int tetra_burst_find(const unsigned char *dibits, int count, int low, int high,
                     struct tetra_burst_sync *out) {
    /*
     * The search moved down to signal_repeat_find() intact -- it needs nothing
     * transcribed, which its own comment here always said, and a burst grid
     * measured from the symbols is as useful before anybody knows the
     * technology as after.
     *
     * What stays here is TETRA's shape: the profile is only carried over when
     * the period found is a timeslot, because that is the only length
     * `struct tetra_burst_sync` is sized for and the only one a TETRA caller
     * can read.
     */
    struct signal_repeat found;
    int k;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!signal_repeat_find(dibits, count, low, high, &found))
        return 0;
    out->period = found.period;
    out->repeat = found.repeat;
    out->runner_up = found.runner_up;
    if (found.period == TETRA_SLOT_SYMBOLS &&
        found.profile_len == TETRA_SLOT_SYMBOLS) {
        for (k = 0; k < TETRA_SLOT_SYMBOLS; k++)
            out->profile[k] = found.profile[k];
        out->fixed = found.fixed;
        out->varying = found.varying;
    }
    return 1;
}

/*
 * The synchronization training sequence, EN 300 392-2 V3.8.1 equation (9.11):
 *
 *   (y1..y38) = 1,1, 0,0, 0,0, 0,1, 1,0, 0,1, 1,1, 0,0, 1,1,
 *               1,0, 1,0, 0,1, 1,1, 0,0, 0,0, 0,1, 1,0, 0,1, 1,1
 *
 * as the nineteen dibits it modulates to. It sits at bits 215 to 252 of the
 * synchronization continuous downlink burst (table 9.9), which is symbols 108
 * to 126 of the 255 -- bit 2n-1 and bit 2n are symbol n.
 */
static const unsigned char sync_word[TETRA_SYNC_SYMBOLS] = {
    3, 0, 0, 1, 2, 1, 3, 0, 3, 2, 2, 1, 3, 0, 0, 1, 2, 1, 3
};

int tetra_sync_dibits(const unsigned char **dibits) {
    if (dibits)
        *dibits = sync_word;
    return TETRA_SYNC_SYMBOLS;
}

int tetra_find_sync(const unsigned char *dibits, int count, int *matched) {
    int at, best = -1, best_hits = 0;

    if (matched)
        *matched = 0;
    if (!dibits || count < TETRA_SYNC_SYMBOLS)
        return -1;
    for (at = 0; at + TETRA_SYNC_SYMBOLS <= count; at++) {
        int hits = 0, k;
        for (k = 0; k < TETRA_SYNC_SYMBOLS; k++)
            if (dibits[at + k] == sync_word[k])
                hits++;
        if (hits > best_hits) {
            best_hits = hits;
            best = at;
        }
    }
    if (matched)
        *matched = best_hits;
    /*
     * Sixteen of nineteen, and the number is measured rather than picked.
     *
     * Chance is a quarter, so thirteen comes up about once in a hundred
     * thousand positions -- which sounds ample and is not: a block holds a few
     * thousand positions and a capture holds several blocks, and at thirteen
     * an empty channel produced a fourteen-of-nineteen "match" on the second
     * chunk tried. Sixteen is another two orders of magnitude down, and costs
     * nothing: the real signal matches nineteen of nineteen in every chunk.
     */
    return best_hits >= 16 ? best : -1;
}
