#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "signal_probe.h"

/*
 * One frequency's magnitude, by rotating a phasor rather than calling cos and
 * sin per sample.
 *
 * The straightforward version is a transcendental pair per sample per
 * frequency, and a coarse scan over a 40 kHz window at 3 million samples does
 * not finish. The recurrence drifts, so it is renormalised every 65536 steps
 * -- often enough that the error stays far below the measurement, rarely
 * enough that the square root costs nothing.
 */
static double line_magnitude(const float *i_samples, const float *q_samples,
                             size_t count, double hz, double sample_rate) {
    double w = -2.0 * M_PI * hz / sample_rate;
    double step_re = cos(w), step_im = sin(w);
    double pr = 1.0, pi = 0.0, cr = 0.0, ci = 0.0;
    size_t n;

    if (!count)
        return 0.0;
    for (n = 0; n < count; n++) {
        double next = pr * step_re - pi * step_im;
        cr += (double)i_samples[n] * pr - (double)q_samples[n] * pi;
        ci += (double)i_samples[n] * pi + (double)q_samples[n] * pr;
        pi = pi * step_re + pr * step_im;
        pr = next;
        if ((n & 0xffff) == 0xffff) {
            double m = sqrt(pr * pr + pi * pi);
            if (m > 0.0) { pr /= m; pi /= m; }
        }
    }
    return sqrt(cr * cr + ci * ci) / (double)count;
}

/*
 * How much of the *channel* is a constant, once the carrier is at zero.
 *
 * Mixing the carrier to DC turns it into a constant, so the mean of what is
 * left is the carrier's own amplitude and everything else -- modulation,
 * noise inside the channel -- is what varies around it. The ratio of the two
 * powers is therefore "how much of this channel is a bare tone".
 *
 * The channel is isolated by averaging blocks of `decimate` samples, which is
 * a boxcar low-pass with its first null at the channel width. Crude, and
 * enough: the question is whether the energy beside the line is comparable to
 * the line, not what shape it has.
 *
 * The first version of this compared the line against the power of *all* the
 * samples handed in, which for a 2 MHz capture of a narrow carrier is almost
 * entirely broadband noise a long way outside the channel. It read 0.138 for
 * a carrier that stands 49 dB over its own floor, and the header claimed 0.93
 * -- a number that came from reasoning rather than from running it.
 */
static double constant_fraction(const float *i_samples, const float *q_samples,
                                size_t count, double carrier_hz,
                                double sample_rate, double channel_hz) {
    double w = -2.0 * M_PI * carrier_hz / sample_rate;
    double step_re = cos(w), step_im = sin(w);
    double pr = 1.0, pi = 0.0;
    double sum_re = 0.0, sum_im = 0.0, sum_sq = 0.0;
    double block_re = 0.0, block_im = 0.0;
    size_t n, decimate, in_block = 0, blocks = 0;

    if (!(channel_hz > 0.0) || !count)
        return 0.0;
    decimate = (size_t)(sample_rate / channel_hz);
    if (decimate < 1)
        decimate = 1;

    for (n = 0; n < count; n++) {
        double next = pr * step_re - pi * step_im;
        block_re += (double)i_samples[n] * pr - (double)q_samples[n] * pi;
        block_im += (double)i_samples[n] * pi + (double)q_samples[n] * pr;
        pi = pi * step_re + pr * step_im;
        pr = next;
        if ((n & 0xffff) == 0xffff) {
            double m = sqrt(pr * pr + pi * pi);
            if (m > 0.0) { pr /= m; pi /= m; }
        }
        if (++in_block == decimate) {
            block_re /= (double)decimate;
            block_im /= (double)decimate;
            sum_re += block_re;
            sum_im += block_im;
            sum_sq += block_re * block_re + block_im * block_im;
            block_re = block_im = 0.0;
            in_block = 0;
            blocks++;
        }
    }
    if (!blocks || sum_sq <= 0.0)
        return 0.0;
    sum_re /= (double)blocks;
    sum_im /= (double)blocks;
    sum_sq /= (double)blocks;
    return (sum_re * sum_re + sum_im * sum_im) / sum_sq;
}

int signal_find_carrier(const float *i_samples, const float *q_samples,
                        size_t pair_count, double sample_rate,
                        double low_hz, double high_hz, double guard_hz,
                        double channel_hz, struct signal_carrier *out) {
    /* Coarse over a prefix, then refined: a full-length scan of every
       candidate frequency is the thing that does not finish. */
    const size_t probe = pair_count < 300000 ? pair_count : 300000;
    double best = -1.0, carrier = 0.0, hz, step, floor_est;
    double line, spacing;
    int i;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!i_samples || !q_samples || pair_count < 1024 || !(sample_rate > 0.0))
        return 0;
    if (!(high_hz > low_hz))
        return 0;
    if (guard_hz < 0.0)
        guard_hz = 0.0;

    /* A bin of the coarse scan: fine enough that a line cannot hide between
       two of them, coarse enough to finish. */
    spacing = sample_rate / (double)probe * 4.0;
    if (spacing < 1.0)
        spacing = 1.0;

    for (hz = low_hz; hz <= high_hz; hz += spacing) {
        if (fabs(hz) < guard_hz)
            continue;
        line = line_magnitude(i_samples, q_samples, probe, hz, sample_rate);
        if (line > best) { best = line; carrier = hz; }
    }
    if (best <= 0.0)
        return 0;
    for (step = spacing / 4.0; step >= spacing / 256.0; step /= 4.0) {
        double centre = carrier;
        for (hz = centre - 4.0 * step; hz <= centre + 4.0 * step; hz += step) {
            if (fabs(hz) < guard_hz)
                continue;
            line = line_magnitude(i_samples, q_samples, probe, hz,
                                  sample_rate);
            if (line > best) { best = line; carrier = hz; }
        }
    }

    /* The line itself, at full length now that its frequency is known. */
    line = line_magnitude(i_samples, q_samples, pair_count, carrier,
                          sample_rate);

    /*
     * The floor, as a median of probes spread across the whole search window
     * rather than a mean of nine beside the carrier.
     *
     * Nine probes in one place is nine chances to land on another signal, and
     * that is not hypothetical: on a real empty frequency the mean-of-nine
     * version put the floor *above* the best line and reported -16.6 dB,
     * which reads as "less than nothing there" and is really "the probes hit
     * something". A median over the window ignores the few that land badly.
     */
    {
        double probes[SIGNAL_FLOOR_PROBES];
        int used = 0, j, k;
        /*
         * Spread over the whole captured span, not over the search window:
         * the window can be narrower than the channel, and then every probe
         * falls inside the carrier's own skirt, none survives, and the floor
         * comes back as zero. Which is what happened.
         */
        double edge = sample_rate * 0.45;
        for (i = 0; i < SIGNAL_FLOOR_PROBES; i++) {
            double at = -edge + 2.0 * edge * ((double)i + 0.5) /
                                (double)SIGNAL_FLOOR_PROBES;
            if (fabs(at) < guard_hz)
                continue;
            /* Not the carrier's own line, nor its immediate skirt. */
            if (fabs(at - carrier) < channel_hz)
                continue;
            probes[used++] = line_magnitude(i_samples, q_samples, probe, at,
                                            sample_rate);
        }
        for (j = 1; j < used; j++) {          /* small, so insertion sort */
            double v = probes[j];
            for (k = j - 1; k >= 0 && probes[k] > v; k--)
                probes[k + 1] = probes[k];
            probes[k + 1] = v;
        }
        floor_est = used ? probes[used / 2] : 0.0;
    }

    out->found = 1;
    out->offset_hz = carrier;
    out->magnitude = line;
    out->carrier_over_noise_db = floor_est > 0.0
                                  ? 20.0 * log10(line / floor_est) : 0.0;
    out->carrier_power_fraction = constant_fraction(i_samples, q_samples, pair_count,
                                     carrier, sample_rate, channel_hz);
    if (out->carrier_power_fraction > 1.0)
        out->carrier_power_fraction = 1.0;
    return 1;
}

/* ------------------------------------------------------------------ *
 * Does it have a symbol rate?
 * ------------------------------------------------------------------ */

double signal_symbol_line(const float *i_samples, const float *q_samples,
                          size_t pair_count, double sample_rate,
                          double symbol_rate_bd, double *strength) {
    /*
     * The same arithmetic tetra_dsp.c has carried, with the two rates passed
     * in rather than compiled in. Open loop, one estimate per buffer, so there
     * is nothing to lose lock -- and the caller keeps buffers short enough
     * that the clock cannot slide a symbol inside one, which at 35 ppm is
     * thousands of symbols.
     */
    double w, sr = 0.0, si = 0.0, mean = 0.0;
    double step_re, step_im, pr = 1.0, pim = 0.0;
    size_t n;

    if (strength)
        *strength = 0.0;
    if (!i_samples || !q_samples || pair_count < 64 || sample_rate <= 0.0 ||
        symbol_rate_bd <= 0.0 || symbol_rate_bd >= sample_rate / 2.0)
        return 0.0;

    w = 2.0 * M_PI * symbol_rate_bd / sample_rate;
    step_re = cos(w);
    step_im = sin(w);
    /* A phasor recurrence rather than a transcendental pair per sample, for
       the reason line_magnitude() gives: a scan over a thousand rates does not
       finish otherwise. Renormalised on the same schedule. */
    for (n = 0; n < pair_count; n++) {
        double a = (double)i_samples[n] * i_samples[n] +
                   (double)q_samples[n] * q_samples[n];
        double next = pr * step_re - pim * step_im;
        sr += a * pr;
        si -= a * pim;
        mean += a;
        pim = pim * step_re + pr * step_im;
        pr = next;
        if ((n & 0xffff) == 0xffff) {
            double m = sqrt(pr * pr + pim * pim);
            if (m > 0.0) { pr /= m; pim /= m; }
        }
    }
    mean /= (double)pair_count;
    if (strength && mean > 0.0)
        *strength = sqrt(sr * sr + si * si) / ((double)pair_count * mean);
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

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 * Does it repeat?
 * ------------------------------------------------------------------ */

static float period_match(const unsigned char *symbols, int count, int lag) {
    int k, same = 0, total = 0;

    for (k = lag; k < count; k++) {
        if (symbols[k] == symbols[k - lag])
            same++;
        total++;
    }
    return total > 0 ? (float)same / (float)total : 0.0f;
}

int signal_repeat_find(const unsigned char *symbols, int count,
                       int low, int high, struct signal_repeat *out) {
    int lag, best = 0, k;
    float best_match = 0.0f, runner_up = 0.0f;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    /* Four periods at the longest lag asked for, so every lag in the range is
       measured over the same amount of evidence. Zeroed first, so a caller
       that hands in too little can tell a refusal from a finding. */
    if (!symbols || count < 4 * high || low < 2 || high <= low)
        return 0;
    for (lag = low; lag <= high; lag++) {
        float m = period_match(symbols, count, lag);

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
     * frame to frame breaks them: a TETRA capture gives 0.744 at 255 and 0.547
     * at 1020. Relying on that would be relying on the signal being
     * interesting.
     */
    for (lag = low; lag <= high; lag++) {
        if (period_match(symbols, count, lag) >= best_match - 0.02f) {
            best = lag;
            break;
        }
    }
    best_match = period_match(symbols, count, best);
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
        m = period_match(symbols, count, lag);
        if (m > runner_up)
            runner_up = m;
    }
    /*
     * Standing clear, not merely highest. Everything correlates a little with
     * everything, so "the best of a thousand lags" is a number a stream of
     * noise also produces.
     *
     * Decided *before* anything is written out. tetra_burst_find's header has
     * always said a refusal "reads as a period of 0 rather than as leftovers",
     * and the code it described set the period, the repeat and the runner-up
     * and only then returned 0 -- so a caller that trusted the sentence read
     * whichever lag happened to win a search that had just been rejected. The
     * check that found it asserted the sentence.
     */
    if (best_match < 0.4f || best_match < runner_up * 1.4f)
        return 0;
    out->period = best;
    out->repeat = best_match;
    out->runner_up = runner_up;

    if (best <= SIGNAL_PROFILE_MAX) {
        int counted[SIGNAL_PROFILE_MAX];
        int hit[SIGNAL_PROFILE_MAX];

        for (k = 0; k < best; k++)
            counted[k] = hit[k] = 0;
        for (k = best; k < count; k++) {
            int slot = k % best;
            counted[slot]++;
            if (symbols[k] == symbols[k - best])
                hit[slot]++;
        }
        out->profile_len = best;
        for (k = 0; k < best; k++) {
            out->profile[k] = counted[k]
                                  ? (float)hit[k] / (float)counted[k] : 0.0f;
            if (out->profile[k] > 0.9f)
                out->fixed++;
            else if (out->profile[k] < 0.4f)
                out->varying++;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ *
 * Does it repeat at a period, in the samples themselves?
 * ------------------------------------------------------------------ */

double signal_lag_correlation(const float *i_samples, const float *q_samples,
                              size_t at, size_t lag, size_t window) {
    double sr = 0.0, si = 0.0, e1 = 0.0, e2 = 0.0;
    size_t n;

    if (!i_samples || !q_samples || !window)
        return 0.0;
    for (n = 0; n < window; n++) {
        float ar = i_samples[at + n], ai = q_samples[at + n];
        float br = i_samples[at + lag + n], bi = q_samples[at + lag + n];
        sr += (double)ar * br + (double)ai * bi;    /* a times conj(b) */
        si += (double)ai * br - (double)ar * bi;
        e1 += (double)ar * ar + (double)ai * ai;
        e2 += (double)br * br + (double)bi * bi;
    }
    if (e1 <= 0.0 || e2 <= 0.0)
        return 0.0;
    return sqrt(sr * sr + si * si) / sqrt(e1 * e2);
}

int signal_fold_at(const float *i_samples, const float *q_samples,
                   size_t pair_count, size_t lag, size_t window, size_t step,
                   struct signal_fold *out) {
    double acc[SIGNAL_FOLD_SLOTS];
    double sorted[SIGNAL_FOLD_SLOTS];
    size_t hits[SIGNAL_FOLD_SLOTS];
    size_t slots, p, best_slot = 0;
    double best = 0.0;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!i_samples || !q_samples || !lag || !window || !step)
        return 0;
    if (lag + window > pair_count)
        return 0;

    /*
     * How many slots the period is divided into, capped so this stays
     * allocation-free. The step is left alone: it sets how many correlations
     * are computed, and lowering the slot count only averages more of them
     * together.
     */
    slots = lag / step;
    if (slots > SIGNAL_FOLD_SLOTS)
        slots = SIGNAL_FOLD_SLOTS;
    if (slots < 4)
        return 0;

    for (p = 0; p < slots; p++) {
        acc[p] = 0.0;
        hits[p] = 0;
    }
    /*
     * The slot is where the position falls *inside the period*, and it has to
     * be computed that way rather than as (p / step) % slots.
     *
     * The two agree only when the step divides the lag. When it does not, the
     * fold's own period is slots*step rather than lag, so it drifts against
     * the signal by the remainder every cycle and smears the peak away. The
     * first version of this had that fault and it was visible: an LTE capture
     * whose 10 ms burst stood at 3.4 times its floor read 1.2, while the
     * 5 ms row -- whose lag the step happens to divide -- did not move at all.
     */
    for (p = 0; p + lag + window <= pair_count; p += step) {
        size_t slot = (p % lag) * slots / lag;
        acc[slot] += signal_lag_correlation(i_samples, q_samples, p, lag,
                                            window);
        hits[slot]++;
    }
    for (p = 0; p < slots; p++) {
        acc[p] = hits[p] ? acc[p] / (double)hits[p] : 0.0;
        sorted[p] = acc[p];
        if (acc[p] > best) {
            best = acc[p];
            best_slot = p;
        }
    }
    qsort(sorted, slots, sizeof(*sorted), cmp_double);

    out->peak = best;
    out->floor = sorted[slots / 2];
    out->ratio = out->floor > 0.0 ? out->peak / out->floor : 0.0;
    out->phase = best_slot * lag / slots;
    out->slots = (int)slots;
    out->step = step;
    return 1;
}
