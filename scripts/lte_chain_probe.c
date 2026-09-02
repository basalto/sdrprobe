/*
 * A walk through the LTE decode chain, block by block, for one capture.
 *
 * Not a test: it prints what each stage saw rather than asserting anything.
 * The point is the stages that succeed quietly on their way to a failure --
 * a strong PSS with no secondary sequence behind it says the correlator found
 * something that is not a cell; a cell with no message says the carrier is
 * there and too weak to read; and a silent PSS says the tuning is wrong.
 * `make check-lte-dsp` proves the arithmetic; this says what the air did.
 *
 *   make probe-lte-chain FILE_LTE=captures/lte_earfcn6300_...bin
 *
 * It compiles lte_dsp.c in so it can reach the file's statics.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lte_dsp.c"
#include "lte_mib.h"

#define BLOCK_PAIRS (16 * 16384)

static float i_samples[BLOCK_PAIRS];
static float q_samples[BLOCK_PAIRS];
static uint8_t raw[2 * BLOCK_PAIRS];

/* The best each Zadoff-Chu root manages anywhere in the search window, which
   the public result only summarises. */
static void root_scores(const float *si, const float *sq, size_t pairs,
                        float best[LTE_N_ID_2_COUNT],
                        size_t where[LTE_N_ID_2_COUNT]) {
    struct pss_reference reference[LTE_N_ID_2_COUNT];
    size_t span = pairs - LTE_FFT_SIZE + 1;
    size_t start;
    int r;

    if (span > LTE_HALF_FRAME_SAMPLES)
        span = LTE_HALF_FRAME_SAMPLES;
    for (r = 0; r < LTE_N_ID_2_COUNT; r++) {
        pss_reference_build(r, &reference[r]);
        best[r] = 0.0f;
        where[r] = 0;
    }

    for (start = 0; start < span; start++) {
        double energy[2] = { 0.0, 0.0 };
        int n;
        for (n = 0; n < LTE_FFT_SIZE; n++) {
            double e = (double)si[start + (size_t)n] * si[start + (size_t)n] +
                       (double)sq[start + (size_t)n] * sq[start + (size_t)n];
            energy[n < LTE_FFT_SIZE / 2 ? 0 : 1] += e;
        }
        if (energy[0] <= 0.0 || energy[1] <= 0.0)
            continue;
        for (r = 0; r < LTE_N_ID_2_COUNT; r++) {
            double magnitude[2];
            float score;
            int half;
            for (half = 0; half < 2; half++) {
                double cr = 0.0, ci = 0.0;
                int from = half * (LTE_FFT_SIZE / 2);
                int to = from + LTE_FFT_SIZE / 2;
                for (n = from; n < to; n++) {
                    float xr = si[start + (size_t)n];
                    float xi = sq[start + (size_t)n];
                    cr += (double)xr * reference[r].re[n] +
                          (double)xi * reference[r].im[n];
                    ci += (double)xi * reference[r].re[n] -
                          (double)xr * reference[r].im[n];
                }
                magnitude[half] = sqrt(cr * cr + ci * ci) /
                                  sqrt(energy[half] *
                                       reference[r].half_energy[half]);
            }
            score = (float)(0.5 * (magnitude[0] + magnitude[1]));
            if (score > best[r]) {
                best[r] = score;
                where[r] = start;
            }
        }
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    FILE *f;
    int block = 0, cells = 0, messages = 0;

    if (!path) {
        fprintf(stderr, "usage: %s <capture.bin>\n", argv[0]);
        return 1;
    }
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    printf("LTE chain over %s, 1.92 MS/s assumed\n\n", path);

    while (block < 12) {
        size_t read = fread(raw, 1, sizeof(raw), f);
        size_t pairs = read / 2, n;
        double mean_i = 0.0, mean_q = 0.0, power = 0.0;
        float best[LTE_N_ID_2_COUNT];
        size_t where[LTE_N_ID_2_COUNT];
        struct lte_cell cell;
        int r;

        if (pairs < LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE)
            break;
        for (n = 0; n < pairs; n++) {
            i_samples[n] = ((float)raw[2 * n] - 127.5f) / 127.5f;
            q_samples[n] = ((float)raw[2 * n + 1] - 127.5f) / 127.5f;
            mean_i += i_samples[n];
            mean_q += q_samples[n];
        }
        mean_i /= (double)pairs;
        mean_q /= (double)pairs;
        for (n = 0; n < pairs; n++) {
            i_samples[n] -= (float)mean_i;
            q_samples[n] -= (float)mean_q;
            power += (double)i_samples[n] * i_samples[n] +
                     (double)q_samples[n] * q_samples[n];
        }
        power /= (double)pairs;

        printf("block %-2d  %zu pairs  rms %.4f  dc (%+.4f, %+.4f)\n", block,
               pairs, sqrt(power), mean_i, mean_q);

        root_scores(i_samples, q_samples, pairs, best, where);
        printf("          PSS roots:");
        for (r = 0; r < LTE_N_ID_2_COUNT; r++)
            printf("  N_ID_2 %d %.3f @%zu", r, (double)best[r], where[r]);
        printf("\n");

        if (lte_cell_search(i_samples, q_samples, pairs, LTE_SAMPLE_RATE_HZ,
                            &cell) != 1) {
            /* Say which gate refused it, since that is the whole question. */
            struct lte_pss_result pss;
            lte_pss_detect(i_samples, q_samples, pairs, LTE_SAMPLE_RATE_HZ,
                           &pss);
            if (!pss.detected)
                printf("          no cell: best PSS %.3f is below the %.2f "
                       "candidate floor\n", (double)pss.peak,
                       (double)LTE_PSS_MIN_CORRELATION);
            else
                printf("          no cell: PSS %.3f passed, the secondary "
                       "sequence reached only %.3f against a runner-up of "
                       "%.3f\n", (double)pss.peak,
                       (double)cell.sss_correlation,
                       (double)cell.sss_runner_up);
            block++;
            continue;
        }

        cells++;
        printf("          cell %d  (N_ID_1 %d, N_ID_2 %d)  %s CP  "
               "half-frame %d\n", cell.pci, cell.n_id_1, cell.n_id_2,
               cell.extended_cp ? "extended" : "normal", cell.half_frame);
        printf("          subframe 0 at %zu  offset %+.0f Hz  "
               "PSS %.3f/%.3f  SSS %.3f/%.3f\n", cell.subframe0_start,
               cell.frequency_offset_hz, (double)cell.pss_correlation,
               (double)cell.pss_runner_up, (double)cell.sss_correlation,
               (double)cell.sss_runner_up);

        {
            static const int ports[3] = { 1, 2, 4 };
            int h, read_any = 0;
            for (h = 0; h < 3; h++) {
                float soft[LTE_PBCH_SOFT_BITS];
                struct lte_mib mib;
                int written = lte_pbch_soft_bits(i_samples, q_samples, pairs,
                                                 LTE_SAMPLE_RATE_HZ, &cell,
                                                 cell.subframe0_start,
                                                 ports[h], soft);
                double magnitude = 0.0;
                int k;
                if (written != LTE_PBCH_SOFT_BITS) {
                    printf("          %d port(s): no soft bits\n", ports[h]);
                    continue;
                }
                for (k = 0; k < LTE_PBCH_SOFT_BITS; k++)
                    magnitude += fabs((double)soft[k]);
                magnitude /= LTE_PBCH_SOFT_BITS;
                if (!lte_mib_decode(soft, cell.pci, &mib)) {
                    printf("          %d port(s): no parity fits "
                           "(mean |soft| %.3f)\n", ports[h], magnitude);
                    continue;
                }
                if (mib.antenna_ports != ports[h]) {
                    printf("          %d port(s): parity says %d, so the "
                           "combining was wrong\n", ports[h],
                           mib.antenna_ports);
                    continue;
                }
                printf("          MIB: %d blocks (%.2f MHz)  PHICH %s %s  "
                       "SFN %d  %d port(s)\n", mib.bandwidth_prb,
                       lte_mib_occupied_hz(mib.bandwidth_prb) / 1e6,
                       mib.phich_extended ? "extended" : "normal",
                       lte_phich_resource_name(mib.phich_resource_sixths),
                       mib.system_frame_number, mib.antenna_ports);
                read_any = 1;
                break;
            }
            if (read_any)
                messages++;
        }
        block++;
    }
    fclose(f);
    printf("\n%d blocks, %d with a cell, %d with a message\n", block, cells,
           messages);
    return 0;
}
