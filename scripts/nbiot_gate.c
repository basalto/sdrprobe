/*
 * Is there an NB-IoT anchor carrier here?
 *
 * The gate `.scratch/lte-more-per-carrier/issues/04` demands before a line of
 * decoder is written, and the one the band 28 ticket skipped and had to be
 * withdrawn for. NB-IoT passes the receiver gate arithmetically -- one
 * resource block is 180 kHz against 1.92 MS/s -- and whether it is on air
 * here has never been measured.
 *
 * What is looked for is the narrowband primary synchronisation signal.
 * 36.211 clause 10.2.7.1.1: a length-11 Zadoff-Chu with root u = 5, the same
 * in every cell so no identity is needed, laid on subcarriers 0 to 10 of the
 * anchor's resource block across OFDM symbols 3 to 13 of **subframe 5**, each
 * symbol multiplied by a cover code. Subframe 5 of every radio frame, so it
 * repeats every 10 ms.
 *
 * The correlation is non-coherent across the eleven symbols: each is matched
 * on its own 128 samples and the magnitudes are summed. A coherent match over
 * the whole 785 us span would be sharper and would also need the tuning error
 * to be under a few hundred hertz, which no receiver here can promise.
 *
 * It demodulates nothing and decodes nothing. A peak far above its own floor,
 * repeating at 10 ms, is the answer; anything else closes the ticket.
 *
 *   make probe-nbiot FILE_NBIOT=captures/nbiot_936m8.bin
 */

#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lte_dsp.h"

#define NPSS_SUBCARRIERS 11
#define NPSS_SYMBOLS 11
#define NPSS_ROOT 5
/* Symbols 3 to 13 of the subframe, all with the short cyclic prefix. */
#define NPSS_SYMBOL_SAMPLES (LTE_FFT_SIZE + 9)
#define FRAME_SAMPLES LTE_FRAME_SAMPLES

/* 36.211 table 10.2.7.1.1-1. */
static const float cover[NPSS_SYMBOLS] = {
    1, 1, 1, 1, -1, -1, 1, 1, 1, -1, 1
};

/* The time-domain waveform of one NPSS symbol, with the anchor's resource
   block centred at zero: its twelve subcarriers span bins -6 to +5, and the
   sequence occupies the first eleven of them. */
static void npss_symbol(int l, float *re, float *im) {
    int n, i;

    for (n = 0; n < LTE_FFT_SIZE; n++) {
        double sr = 0.0, si = 0.0;
        for (i = 0; i < NPSS_SUBCARRIERS; i++) {
            /* d(n) = exp(-j*pi*u*n*(n+1)/11) */
            double phase = -M_PI * NPSS_ROOT * i * (i + 1) / 11.0;
            double dr = cos(phase) * cover[l];
            double di = sin(phase) * cover[l];
            int bin = i - 6;
            double turn = 2.0 * M_PI * bin * n / (double)LTE_FFT_SIZE;
            double c = cos(turn), s = sin(turn);
            sr += dr * c - di * s;
            si += dr * s + di * c;
        }
        re[n] = (float)(sr / NPSS_SUBCARRIERS);
        im[n] = (float)(si / NPSS_SUBCARRIERS);
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    static float ref_re[NPSS_SYMBOLS][LTE_FFT_SIZE];
    static float ref_im[NPSS_SYMBOLS][LTE_FFT_SIZE];
    static float i_samples[FRAME_SAMPLES * 4];
    static float q_samples[FRAME_SAMPLES * 4];
    unsigned char *raw;
    size_t pairs, want = FRAME_SAMPLES * 4, got;
    FILE *file;
    double best = -1.0, sum = 0.0, sumsq = 0.0;
    size_t best_at = 0, start, count = 0;
    int l;

    if (!path) {
        fprintf(stderr, "usage: %s <capture at 1.92 MS/s> | --self-test\n",
                argv[0]);
        return 2;
    }
    if (!strcmp(path, "--self-test")) {
        /*
         * A null result from a detector nobody has seen fire is worth
         * nothing, which is the mistake this repository keeps finding in its
         * own work. So: lay the signal this is looking for into noise at a
         * known place, and check it comes back.
         *
         * The synthetic carrier is deliberately weak -- the sequence at unit
         * amplitude against noise of the same -- because a gate that only
         * finds a strong one cannot be trusted to have looked for a weak one.
         */
        unsigned seed = 12345u;
        size_t n;
        pairs = FRAME_SAMPLES * 3;
        for (l = 0; l < NPSS_SYMBOLS; l++)
            npss_symbol(l, ref_re[l], ref_im[l]);
        for (n = 0; n < pairs; n++) {
            seed = seed * 1103515245u + 12345u;
            i_samples[n] = (float)((seed >> 16) & 0xffff) / 32768.0f - 1.0f;
            seed = seed * 1103515245u + 12345u;
            q_samples[n] = (float)((seed >> 16) & 0xffff) / 32768.0f - 1.0f;
        }
        for (start = 0; start + FRAME_SAMPLES < pairs; start += FRAME_SAMPLES) {
            /* Subframe 5, symbols 3 to 13: 1200 samples in, near enough for
               a detector that searches every alignment anyway. */
            size_t at = start + 1200;
            for (l = 0; l < NPSS_SYMBOLS; l++) {
                size_t base = at + (size_t)l * NPSS_SYMBOL_SAMPLES + 9;
                int k;
                for (k = 0; k < LTE_FFT_SIZE; k++) {
                    i_samples[base + (size_t)k] += ref_re[l][k];
                    q_samples[base + (size_t)k] += ref_im[l][k];
                }
            }
        }
        printf("NPSS gate, self-test: the sequence laid into noise at "
               "sample 1200 of every frame\n");
        printf("  %zu pairs, %.1f ms\n\n", pairs,
               (double)pairs / LTE_SAMPLE_RATE_HZ * 1000.0);
        goto correlate;
    }

    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    raw = malloc(want * 2);
    if (!raw) {
        fclose(file);
        return 1;
    }
    got = fread(raw, 1, want * 2, file);
    fclose(file);
    pairs = got / 2;
    if (pairs < FRAME_SAMPLES * 2) {
        fprintf(stderr, "need at least two frames, got %zu pairs\n", pairs);
        free(raw);
        return 1;
    }
    for (start = 0; start < pairs; start++) {
        i_samples[start] = (float)raw[2 * start] - 127.5f;
        q_samples[start] = (float)raw[2 * start + 1] - 127.5f;
    }
    free(raw);

    for (l = 0; l < NPSS_SYMBOLS; l++)
        npss_symbol(l, ref_re[l], ref_im[l]);

    printf("NPSS gate over %s\n", path);
    printf("  %zu pairs, %.1f ms, searching one frame of alignments\n\n",
           pairs, (double)pairs / LTE_SAMPLE_RATE_HZ * 1000.0);

correlate:

    /*
     * One frame of alignments is enough: the signal repeats every 10 ms, so
     * if it is here at all its start is somewhere in the first frame.
     */
    for (start = 0; start + (size_t)NPSS_SYMBOLS * NPSS_SYMBOL_SAMPLES <
                        pairs && start < FRAME_SAMPLES; start++) {
        double score = 0.0, energy = 0.0;
        for (l = 0; l < NPSS_SYMBOLS; l++) {
            size_t at = start + (size_t)l * NPSS_SYMBOL_SAMPLES + 9;
            double cr = 0.0, ci = 0.0;
            int n;
            for (n = 0; n < LTE_FFT_SIZE; n++) {
                float xr = i_samples[at + (size_t)n];
                float xi = q_samples[at + (size_t)n];
                cr += (double)xr * ref_re[l][n] + (double)xi * ref_im[l][n];
                ci += (double)xi * ref_re[l][n] - (double)xr * ref_im[l][n];
                energy += (double)xr * xr + (double)xi * xi;
            }
            score += sqrt(cr * cr + ci * ci);
        }
        if (energy <= 0.0)
            continue;
        score /= sqrt(energy);
        sum += score;
        sumsq += score * score;
        count++;
        if (score > best) {
            best = score;
            best_at = start;
        }
    }
    if (!count) {
        printf("  nothing to correlate\n");
        return 1;
    }
    {
        double mean = sum / (double)count;
        double var = sumsq / (double)count - mean * mean;
        double sd = var > 0.0 ? sqrt(var) : 0.0;
        printf("  best %.4f at sample %zu\n", best, best_at);
        printf("  floor: mean %.4f, deviation %.4f\n", mean, sd);
        printf("  the peak stands %.1f deviations above the floor\n",
               sd > 0.0 ? (best - mean) / sd : 0.0);
        /*
         * And whether it comes back a frame later, which is what separates a
         * synchronisation signal from a lucky alignment. Reported rather than
         * judged: this is a gate, and the number is the finding.
         */
        if (best_at + FRAME_SAMPLES + (size_t)NPSS_SYMBOLS *
                NPSS_SYMBOL_SAMPLES < pairs) {
            double repeat = 0.0, energy = 0.0;
            for (l = 0; l < NPSS_SYMBOLS; l++) {
                size_t at = best_at + FRAME_SAMPLES +
                            (size_t)l * NPSS_SYMBOL_SAMPLES + 9;
                double cr = 0.0, ci = 0.0;
                int n;
                for (n = 0; n < LTE_FFT_SIZE; n++) {
                    float xr = i_samples[at + (size_t)n];
                    float xi = q_samples[at + (size_t)n];
                    cr += (double)xr * ref_re[l][n] + (double)xi * ref_im[l][n];
                    ci += (double)xi * ref_re[l][n] - (double)xr * ref_im[l][n];
                    energy += (double)xr * xr + (double)xi * xi;
                }
                repeat += sqrt(cr * cr + ci * ci);
            }
            if (energy > 0.0) {
                repeat /= sqrt(energy);
                printf("  one frame later: %.4f (%.0f%% of the peak)\n",
                       repeat, 100.0 * repeat / best);
            }
        }
        printf("\n  A synchronisation signal stands well clear of its floor\n"
               "  and repeats at 10 ms. Noise does neither.\n");
    }
    return 0;
}
