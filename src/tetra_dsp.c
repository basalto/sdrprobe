#include "tetra_dsp.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * The four phase steps, as quarter-turns of pi/4: dibit 0 sends +pi/4, 1 sends
 * +3pi/4, 2 sends -3pi/4, 3 sends -pi/4.
 *
 * Read the note in the header before trusting the *assignment*. What is safe
 * to rely on here is the shape -- four distinct odd multiples of pi/4 -- which
 * is what makes the constellation four-cornered and is all ticket 01 needs.
 */
static const int step_quarters[TETRA_PHASE_STEPS] = { 1, 3, -3, -1 };

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
     * Oerder and Meyr: the squared magnitude of a linearly modulated signal
     * with excess bandwidth carries a line at the symbol rate, and the phase
     * of that line is where the symbols are.
     *
     * No loop, so there is nothing to lose lock; one estimate per chunk, so
     * the caller has to keep chunks short enough that the clock cannot slide a
     * symbol inside one. At 35 ppm that is thousands of symbols.
     */
    double w = 2.0 * M_PI * TETRA_SYMBOL_RATE_HZ / TETRA_WORK_RATE_HZ;
    double sr = 0.0, si = 0.0, mean = 0.0;
    size_t n;

    if (strength)
        *strength = 0.0;
    if (!i_samples || !q_samples || pairs < 64)
        return 0.0;
    for (n = 0; n < pairs; n++) {
        double a = (double)i_samples[n] * i_samples[n] +
                   (double)q_samples[n] * q_samples[n];
        sr += a * cos(w * (double)n);
        si -= a * sin(w * (double)n);
        mean += a;
    }
    mean /= (double)pairs;
    if (strength && mean > 0.0)
        *strength = sqrt(sr * sr + si * si) / ((double)pairs * mean);
    /* The line's phase is the timing, in symbol periods: negated because a
       later symbol instant is a lagging phase. */
    {
        double phase = atan2(si, sr);
        double fraction = -phase / (2.0 * M_PI);
        while (fraction < 0.0)
            fraction += 1.0;
        while (fraction >= 1.0)
            fraction -= 1.0;
        return fraction;
    }
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

static float period_match(const unsigned char *dibits, int count, int lag) {
    int k, same = 0, total = 0;

    for (k = lag; k < count; k++) {
        if (dibits[k] == dibits[k - lag])
            same++;
        total++;
    }
    return total > 0 ? (float)same / (float)total : 0.0f;
}

int tetra_burst_find(const unsigned char *dibits, int count, int low, int high,
                     struct tetra_burst_sync *out) {
    int lag, best = 0, k;
    float best_match = 0.0f, runner_up = 0.0f;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    /* Four periods at the longest lag asked for, so every lag in the range is
       measured over the same amount of evidence. Zeroed first, so a caller
       that hands in too little can tell a refusal from a finding. */
    if (!dibits || count < 4 * high || low < 2 || high <= low)
        return 0;
    for (lag = low; lag <= high; lag++) {
        float m = period_match(dibits, count, lag);

        if (m > best_match) {
            runner_up = best_match;
            best_match = m;
            best = lag;
        } else if (m > runner_up) {
            runner_up = m;
        }
    }
    /*
     * The *fundamental*, not the strongest.
     *
     * Anything with a period of 255 repeats just as well at 510, 765 and 1020,
     * and on a perfectly periodic stream those tie to within a rounding error
     * -- the first version of this picked 1020 over 255 by a thousandth and
     * the synthetic check caught it. So the period is the smallest lag that
     * matches as well as the best does, within a margin.
     *
     * On real air the harmonics are weaker, because content that varies from
     * frame to frame breaks them: this capture gives 0.744 at 255 and 0.547 at
     * 1020. Relying on that would be relying on the signal being interesting.
     */
    for (lag = low; lag <= high; lag++) {
        if (period_match(dibits, count, lag) >= best_match - 0.02f) {
            best = lag;
            break;
        }
    }
    best_match = period_match(dibits, count, best);
    /*
     * And the runner-up is the best lag that is *not* a multiple of the period,
     * since a multiple is the same finding rather than a competing one. Without
     * that, a clean periodic signal always looks ambiguous.
     */
    runner_up = 0.0f;
    for (lag = low; lag <= high; lag++) {
        float m;
        if (lag % best == 0)
            continue;
        m = period_match(dibits, count, lag);
        if (m > runner_up)
            runner_up = m;
    }
    out->period = best;
    out->repeat = best_match;
    out->runner_up = runner_up;
    /*
     * Standing clear, not merely highest. Everything correlates a little with
     * everything at a quarter, so "the best of a thousand lags" is a number a
     * stream of noise also produces.
     */
    if (best_match < 0.4f || best_match < runner_up * 1.4f)
        return 0;

    if (best == TETRA_SLOT_SYMBOLS) {
        int counted[TETRA_SLOT_SYMBOLS];
        int hit[TETRA_SLOT_SYMBOLS];

        for (k = 0; k < TETRA_SLOT_SYMBOLS; k++)
            counted[k] = hit[k] = 0;
        for (k = best; k < count; k++) {
            int phase = k % best;
            if (dibits[k] == dibits[k - best])
                hit[phase]++;
            counted[phase]++;
        }
        for (k = 0; k < TETRA_SLOT_SYMBOLS; k++) {
            out->profile[k] = counted[k] ? (float)hit[k] / (float)counted[k]
                                         : 0.0f;
            if (out->profile[k] > 0.9f)
                out->fixed++;
            else if (out->profile[k] < 0.4f)
                out->varying++;
        }
    }
    return 1;
}
