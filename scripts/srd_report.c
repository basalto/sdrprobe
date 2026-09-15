/*
 * One SRD capture, through the shipping chain, with every intermediate shown.
 *
 *   make probe-srd FILE_SRD=testfiles/srd_remote_control_fsk.bin
 *
 * `probe-ook` answers "where are the transmissions and what modulation are
 * they", which is a Probe-context question and stops at the envelope. This
 * one carries on into the Decoder context: it slices, recovers a chip period,
 * discretises to chips and Manchester-decodes, printing what each stage made
 * of the last. It is a tool rather than a scratch script because the question
 * it answers kept coming back -- `srd_dsp.h` records that the 2-FSK SRD remote control capture's
 * data bursts "remain only partially understood", with a 10-13% bit error
 * rate established in a transcript and reproducible nowhere.
 *
 * It calls the shipping functions and adds none of its own arithmetic, so a
 * number here is the number the program acts on. The two things it adds are
 * *sweeps* rather than measurements, and they exist because a single answer
 * from a mode-finding heuristic cannot be told from a wrong one:
 *
 *   - a chip-period sweep, so `srd_chip_period()`'s pick can be read against
 *     the violation rate every other candidate would have given, and
 *   - the longest unbroken Manchester run against the total, which is what
 *     `srd_extract_frames()`'s generic path actually keeps.
 *
 * Not a check: it measures and prints, and every conclusion is the reader's.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_profile.h"
#include "sdr_dsp.h"
#include "srd_dsp.h"
#include "srd_frame.h"

#define MAX_RUNS 8192
#define MAX_CHIPS 16384

static struct device_profile g_device;

/* The seam refuses a NULL magnitude array rather than skipping the work, and
   an empty report is what that looks like from outside (ticket 05). Nothing
   here wants the magnitudes; the buffer exists to satisfy the contract. */
static float *g_magnitude;

static float *g_i, *g_q, *g_sig;
static struct srd_run g_runs[MAX_RUNS];
static uint8_t g_chips[MAX_CHIPS];

static size_t load_capture(const char *path, double *seconds_out) {
    FILE *f = fopen(path, "rb");
    unsigned char *raw;
    long bytes;
    size_t pairs;

    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    bytes = ftell(f);
    fseek(f, 0, SEEK_SET);

    pairs = (size_t)bytes / g_device.bytes_per_pair;
    raw = malloc((size_t)bytes);
    g_i = malloc(pairs * sizeof(float));
    g_q = malloc(pairs * sizeof(float));
    g_magnitude = malloc(pairs * sizeof(float));
    if (!raw || !g_i || !g_q || !g_magnitude) {
        fprintf(stderr, "out of memory for %zu pairs\n", pairs);
        fclose(f);
        return 0;
    }
    if (fread(raw, 1, (size_t)bytes, f) != (size_t)bytes) {
        fprintf(stderr, "short read on %s\n", path);
        fclose(f);
        return 0;
    }
    fclose(f);

    if (sdr_dsp_convert_iq(&g_device, raw, (size_t)bytes, g_i, g_q,
                           g_magnitude, pairs) != pairs) {
        fprintf(stderr, "the sample seam refused %zu pairs\n", pairs);
        free(raw);
        return 0;
    }
    free(raw);
    *seconds_out = 0.0;
    return pairs;
}

/*
 * A run-length histogram in the same 10 us buckets srd_chip_period() bins
 * into, so what it picked can be read against what it was choosing from.
 */
static void print_run_histogram(const struct srd_run *runs, size_t count) {
    int hist[200];
    size_t i;
    int b, top = 0;

    memset(hist, 0, sizeof(hist));
    for (i = 0; i < count; i++) {
        double us = runs[i].duration_seconds * 1e6;
        b = (int)(us / 10.0);
        if (b >= 0 && b < 200)
            hist[b]++;
    }
    for (b = 0; b < 200; b++)
        if (hist[b] > top)
            top = hist[b];
    if (top == 0)
        return;

    printf("    run lengths, 10 us buckets (count, bar):\n");
    for (b = 0; b < 200; b++) {
        int bar;
        if (hist[b] == 0)
            continue;
        printf("      %4d-%4d us  %4d  ", b * 10, b * 10 + 9, hist[b]);
        for (bar = 0; bar < (hist[b] * 40) / top; bar++)
            putchar('#');
        putchar('\n');
    }
}

/*
 * What a chip period makes of these runs: how many chips, how many Manchester
 * violations, and the longest unbroken stretch -- which is the quantity
 * srd_extract_frames()'s generic path keeps and every other one discards.
 */
static void score_chip_period(const struct srd_run *runs, size_t run_count,
                              double chip_s, enum srd_manchester_polarity pol,
                              size_t *chips_out, size_t *viol_out,
                              size_t *longest_out) {
    size_t n_chips, c, best = 0, viol = 0;
    int phase;
    size_t best_phase_viol = (size_t)-1, best_phase_longest = 0;

    *chips_out = *viol_out = *longest_out = 0;
    if (!(chip_s > 0.0))
        return;

    n_chips = srd_runs_to_chips(runs, run_count, chip_s, g_chips, MAX_CHIPS);
    *chips_out = n_chips;
    if (n_chips < 2)
        return;

    for (phase = 0; phase < 2; phase++) {
        size_t run = 0;
        viol = 0;
        best = 0;
        for (c = (size_t)phase; c + 1 < n_chips; c += 2) {
            if (g_chips[c] == g_chips[c + 1]) {
                viol++;
                if (run > best)
                    best = run;
                run = 0;
            } else {
                run++;
            }
        }
        if (run > best)
            best = run;
        if (viol < best_phase_viol) {
            best_phase_viol = viol;
            best_phase_longest = best;
        }
    }
    (void)pol;
    *viol_out = best_phase_viol;
    *longest_out = best_phase_longest;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "testfiles/srd_remote_control_fsk.bin";
    double sample_rate = argc > 2 ? atof(argv[2]) : 2000000.0;
    struct srd_transmission txs[SRD_MAX_TRANSMISSIONS];
    size_t pairs;
    double unused;
    int count, t;

    g_device = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);

    pairs = load_capture(path, &unused);
    if (pairs == 0)
        return 1;

    printf("%s\n", path);
    printf("  %zu pairs, %.3f s at %.0f S/s, full scale %.1f\n\n",
           pairs, (double)pairs / sample_rate, sample_rate,
           (double)g_device.full_scale);

    count = srd_find_transmissions(g_i, g_q, pairs, sample_rate,
                                   (float)g_device.full_scale,
                                   SRD_BUSY_BAR_DB_DEFAULT,
                                   SRD_GAP_SECONDS_DEFAULT,
                                   txs, SRD_MAX_TRANSMISSIONS);
    if (count <= 0) {
        printf("  no transmission found anywhere in this capture\n");
        return 0;
    }
    printf("  %d transmissions\n\n", count);

    g_sig = malloc((pairs / 8 + 4096) * sizeof(float));
    if (!g_sig)
        return 1;

    for (t = 0; t < count; t++) {
        struct srd_transmission *x = &txs[t];
        double work_rate = 0.0, thresh, chip_s;
        size_t sig_n, run_count, chips, viol, longest;
        int k;

        printf("  tx %-2d  %.4f s  %6.1f ms  %+9.1f kHz  %5.1f dB over floor  %s\n",
               t, x->start_seconds, x->duration_seconds * 1e3,
               x->carrier_hz / 1e3, x->carrier_over_noise_db,
               x->modulation == SRD_MOD_FSK2 ? "2FSK" : "OOK");

        if (x->modulation == SRD_MOD_FSK2) {
            sig_n = srd_demodulate_fsk(g_i + x->offset_pairs,
                                       g_q + x->offset_pairs, x->pair_count,
                                       sample_rate, x->carrier_hz, g_sig,
                                       pairs / 8 + 4096, &work_rate);
            thresh = srd_discriminator_threshold(g_sig, sig_n);
            printf("    discriminator: %zu samples at %.0f S/s, "
                   "slice at %+.0f Hz\n", sig_n, work_rate, thresh);
        } else {
            sig_n = srd_demodulate_envelope(g_i + x->offset_pairs,
                                            g_q + x->offset_pairs,
                                            x->pair_count, sample_rate,
                                            x->carrier_hz, g_sig,
                                            pairs / 8 + 4096, &work_rate);
            thresh = srd_envelope_threshold(g_sig, sig_n);
            printf("    envelope: %zu samples at %.0f S/s, slice at %.1f\n",
                   sig_n, work_rate, thresh);
        }

        run_count = srd_extract_runs(g_sig, sig_n, work_rate, thresh,
                                     g_runs, MAX_RUNS);
        printf("    %zu runs%s\n", run_count,
               run_count >= MAX_RUNS ? "   <- the cap; the stream is cut short"
                                     : "");
        print_run_histogram(g_runs, run_count);

        chip_s = srd_chip_period(g_runs, run_count);
        if (chip_s > 0.0)
            printf("    srd_chip_period() -> %.2f us, explaining %.1f%% of "
                   "the time\n", chip_s * 1e6,
                   100.0 * srd_chip_coverage(g_runs, run_count, chip_s));
        else
            printf("    srd_chip_period() -> refused: no candidate reached "
                   "%.0f%% of the time in one- or two-chip runs\n",
                   100.0 * SRD_CHIP_COVERAGE_MIN);

        /*
         * The sweep. A mode-finding heuristic returns one number whether or
         * not it is right, so the neighbouring candidates are what say
         * whether the pick is a minimum or merely the first local maximum in
         * a histogram.
         */
        printf("    chip period sweep (us, chips, violations, longest run, "
               "time explained):\n");
        for (k = 1; k <= 110; k++) {
            double cand = k <= 12 ? (double)k * 10.0e-6
                                  : (double)(k + 1) * 10.0e-6;
            score_chip_period(g_runs, run_count, cand,
                              SRD_MANCHESTER_THOMAS, &chips, &viol, &longest);
            if (chips < 2)
                continue;
            printf("      %6.1f  %6zu  %5zu (%5.1f%%)  %5zu bits  %5.1f%%%s\n",
                   cand * 1e6, chips, viol,
                   chips ? 100.0 * (double)viol / ((double)chips / 2.0) : 0.0,
                   longest,
                   100.0 * srd_chip_coverage(g_runs, run_count, cand),
                   fabs(cand - chip_s) < 5e-6 ? "   <- picked" : "");
        }

        score_chip_period(g_runs, run_count, chip_s, SRD_MANCHESTER_THOMAS,
                          &chips, &viol, &longest);
        printf("    at the picked period: %zu chips, %zu violations, "
               "longest unbroken %zu bits of %zu possible\n",
               chips, viol, longest, chips / 2);

        if (chip_s <= 0.0)
            printf("    srd_extract_frames() not attempted: no chip period\n");

        if (chip_s > 0.0) {
            struct srd_frame frames[16];
            size_t n = srd_extract_frames(g_runs, run_count, chip_s,
                                          SRD_MANCHESTER_THOMAS, frames, 16);
            printf("    srd_extract_frames() -> %zu frames\n", n);
            for (size_t fi = 0; fi < n; fi++) {
                size_t b;
                printf("      kind %d, %zu bits, %zu errors: ",
                       (int)frames[fi].kind, frames[fi].bit_count,
                       frames[fi].error_count);
                for (b = 0; b < frames[fi].byte_count; b++)
                    printf("%02X ", frames[fi].bytes[b]);
                putchar('\n');
            }
        }
        putchar('\n');
    }
    return 0;
}
