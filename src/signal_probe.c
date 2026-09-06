#include <math.h>
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
    out->line_over_floor_db = floor_est > 0.0
                                  ? 20.0 * log10(line / floor_est) : 0.0;
    out->in_line = constant_fraction(i_samples, q_samples, pair_count,
                                     carrier, sample_rate, channel_hz);
    if (out->in_line > 1.0)
        out->in_line = 1.0;
    return 1;
}
