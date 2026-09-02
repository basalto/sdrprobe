/*
 * What technology is this, and on what grid?
 *
 * A diagnostic, not a check: it prints a walk through two measurements and
 * ends in a conclusion, the way probe-gsm-chain does. It exists because a
 * survey can prove a band is occupied and say nothing about what occupies it,
 * and until this there was no measurement in the repository that could tell
 * an LTE carrier from a 5G NR one.
 *
 * Neither measurement has a sequence model in it, which is the point. Both
 * work on a signal nothing here can demodulate, and both are immune to the
 * receiver's tuning error -- a frequency offset contributes the same constant
 * phase to every term of a lag correlation, and the magnitude discards it.
 *
 *   1. Periodicity. Correlate the signal with a delayed copy of itself and
 *      fold the result over the delay, so a burst that really sits at one
 *      phase averages up while everything else averages down. LTE puts a
 *      primary synchronisation signal every 5 ms; NR puts a synchronisation
 *      block every 20 ms by default and nothing at 5.
 *
 *   2. Subcarrier spacing. Every OFDM symbol begins with a copy of its own
 *      tail, so the signal correlates with itself at a lag of exactly one
 *      useful symbol and at no other lag. At 1.92 MS/s that lag is 128
 *      samples for 15 kHz and 64 for 30 kHz.
 *
 * Read the ratio, never the peak. A correlation always returns a best window;
 * only its height above the floor says whether it found anything -- see
 * .claude/skills/dsp-validation/SKILL.md.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/* Normalised correlation of a window against the same window `lag` later. */
static double lag_correlation(const float *re, const float *im, size_t at,
                              size_t lag, size_t window) {
    double sr = 0.0, si = 0.0, e1 = 0.0, e2 = 0.0;
    size_t n;
    for (n = 0; n < window; n++) {
        float ar = re[at + n], ai = im[at + n];
        float br = re[at + lag + n], bi = im[at + lag + n];
        sr += (double)ar * br + (double)ai * bi;    /* a times conj(b) */
        si += (double)ai * br - (double)ar * bi;
        e1 += (double)ar * ar + (double)ai * ai;
        e2 += (double)br * br + (double)bi * bi;
    }
    if (e1 <= 0.0 || e2 <= 0.0)
        return 0.0;
    return sqrt(sr * sr + si * si) / sqrt(e1 * e2);
}

/* Is there a burst at a fixed phase of this period? Returns the peak of the
   folded profile and, through `floor_out`, its median. */
static double folded_peak(const float *re, const float *im, size_t pairs,
                          size_t lag, size_t window, size_t step,
                          double *floor_out, size_t *phase_out) {
    size_t slots = lag / step, p, best_slot = 0;
    double *acc = calloc(slots, sizeof(*acc)), *sorted, best = 0.0;
    size_t *hits = calloc(slots, sizeof(*hits));
    if (!acc || !hits)
        exit(2);
    for (p = 0; p + lag + window <= pairs; p += step) {
        size_t slot = (p / step) % slots;
        acc[slot] += lag_correlation(re, im, p, lag, window);
        hits[slot]++;
    }
    sorted = malloc(slots * sizeof(*sorted));
    if (!sorted)
        exit(2);
    for (p = 0; p < slots; p++) {
        acc[p] = hits[p] ? acc[p] / (double)hits[p] : 0.0;
        sorted[p] = acc[p];
        if (acc[p] > best) { best = acc[p]; best_slot = p; }
    }
    qsort(sorted, slots, sizeof(*sorted), cmp);
    *floor_out = sorted[slots / 2];
    *phase_out = best_slot * step;
    free(acc); free(hits); free(sorted);
    return best;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    double rate = argc > 2 ? atof(argv[2]) : 1920000.0;
    double periods_ms[] = { 5.0, 10.0, 20.0, 40.0 };
    size_t n_periods = sizeof(periods_ms) / sizeof(periods_ms[0]);
    unsigned char *raw; float *re, *im; FILE *f;
    size_t bytes, pairs, i, k, lag, best_lag = 0;
    double dc_r = 0.0, dc_i = 0.0;
    double best_period = 0.0, best_ratio = 0.0, five_ratio = 0.0;
    double best_cp = 0.0, cp_floor = 0.0;

    if (!path) {
        fprintf(stderr, "usage: signal_periodicity <capture> [sample_rate]\n");
        return 2;
    }
    f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END); bytes = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    pairs = bytes / 2;
    raw = malloc(bytes);
    re = malloc(pairs * sizeof(*re));
    im = malloc(pairs * sizeof(*im));
    if (!raw || !re || !im || fread(raw, 1, bytes, f) != bytes) {
        fprintf(stderr, "could not read %s\n", path);
        return 2;
    }
    fclose(f);
    for (i = 0; i < pairs; i++) {
        re[i] = ((float)raw[2 * i] - 127.5f) / 127.5f;
        im[i] = ((float)raw[2 * i + 1] - 127.5f) / 127.5f;
        dc_r += re[i]; dc_i += im[i];
    }
    dc_r /= (double)pairs; dc_i /= (double)pairs;
    for (i = 0; i < pairs; i++) {
        re[i] -= (float)dc_r; im[i] -= (float)dc_i;
    }

    printf("%s\n  %.2f s at %.3f MS/s\n\n", path, (double)pairs / rate,
           rate / 1e6);

    printf("  PERIODICITY -- a burst at a fixed phase\n");
    printf("  %-10s %8s %8s %8s   %s\n", "period", "peak", "floor", "ratio",
           "phase");
    for (k = 0; k < n_periods; k++) {
        double floor_v = 0.0, peak, ratio;
        size_t phase = 0;
        lag = (size_t)(periods_ms[k] / 1000.0 * rate);
        if (lag + 128 >= pairs) {
            printf("  %-10.1f (capture too short)\n", periods_ms[k]);
            continue;
        }
        peak = folded_peak(re, im, pairs, lag, 128, 32, &floor_v, &phase);
        ratio = floor_v > 0.0 ? peak / floor_v : 0.0;
        printf("  %-7.1f ms %8.3f %8.3f %8.1f   %.3f ms\n", periods_ms[k],
               peak, floor_v, ratio, (double)phase / rate * 1000.0);
        if (periods_ms[k] == 5.0)
            five_ratio = ratio;
        if (ratio > best_ratio) { best_ratio = ratio; best_period = periods_ms[k]; }
    }

    printf("\n  SUBCARRIER SPACING -- the cyclic prefix's own lag\n");
    printf("  %-8s %-10s %8s %8s\n", "lag", "implies", "p99", "floor");
    {
        size_t scan = pairs > 1500000 ? 1500000 : pairs;
        for (lag = 48; lag <= 144; lag += 8) {
            size_t t, count = 0;
            double *s = malloc(((scan - lag - 24) / 8 + 2) * sizeof(*s));
            if (!s) exit(2);
            for (t = 0; t + lag + 24 <= scan; t += 8)
                s[count++] = lag_correlation(re, im, t, lag, 24);
            qsort(s, count, sizeof(*s), cmp);
            printf("  %-8zu %6.1f kHz %8.3f %8.3f%s\n", lag,
                   rate / 1000.0 / (double)lag, s[(size_t)(count * 0.99)],
                   s[count / 2],
                   (lag == 64 || lag == 128) ? "   <-- 30 / 15 kHz" : "");
            if (s[(size_t)(count * 0.99)] > best_cp) {
                best_cp = s[(size_t)(count * 0.99)];
                cp_floor = s[count / 2];
                best_lag = lag;
            }
            free(s);
        }
    }

    printf("\n  CONCLUSION\n  ----------\n");
    if (best_ratio < 3.0) {
        printf("  * Nothing repeats strongly enough to name. The strongest was\n"
               "    %.1f ms at %.1f times its floor, which is not a burst.\n",
               best_period, best_ratio);
    } else if (five_ratio >= 3.0 && best_period <= 10.0) {
        printf("  * A burst every %.0f ms at %.1f times its floor, and 5 ms is\n"
               "    part of it. That is LTE: its primary synchronisation signal\n"
               "    repeats twice a frame and nothing else does.\n",
               best_period, best_ratio);
    } else {
        printf("  * A burst every %.0f ms at %.1f times its floor, with 5 ms at\n"
               "    only %.1f. LTE cannot look like this -- its primary signal is\n"
               "    every 5 ms -- and 20 ms is the default period of a 5G NR\n"
               "    synchronisation block.\n",
               best_period, best_ratio, five_ratio);
    }
    printf("  * The prefix correlates best at a lag of %zu samples, %.1f kHz\n"
           "    spacing, %.2f against a floor of %.2f.\n",
           best_lag, rate / 1000.0 / (double)best_lag, best_cp, cp_floor);
    if (best_lag == 64)
        printf("  * At 30 kHz a synchronisation block's 127 subcarriers span\n"
               "    3.81 MHz, which no rate an RTL-SDR reaches can capture.\n");
    else if (best_lag == 128)
        printf("  * At 15 kHz a synchronisation block's 127 subcarriers span\n"
               "    1.905 MHz, which fits the 1.92 MS/s grid.\n");

    free(raw); free(re); free(im);
    return 0;
}
