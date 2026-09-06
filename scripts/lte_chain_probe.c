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

/* How far either way the frame start is nudged when nothing fits. A symbol at
   1.92 MS/s is 137 samples and a normal cyclic prefix is 9, so 30 covers every
   error that is a timing error rather than a symbol slip. */
#define LTE_TIMING_NUDGE 30

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

/*
 * Whether the equalised broadcast-channel elements are QPSK at all.
 *
 * This is the measurement that tells "read wrong" from "too weak", and the
 * chain had no way to say which. Raising each element to the fourth power
 * maps all four QPSK points onto the same place, so the data cancels itself
 * and what is left is coherent if the points were really there -- the same
 * trick tetra_dsp.c uses on pi/4-DQPSK, where every legal phase step is an
 * odd multiple of pi/4 and four times any of them is -1.
 *
 * Near 1 means four clean clusters. Near 0 means the phases are spread, which
 * is what noise with no signal under it looks like.
 *
 * An earlier version of this measured distance from the ideal points instead,
 * and it was worthless: the trace normalises by the *mean* magnitude, so the
 * spread it reported was the soft bits' dynamic range and it read 58-66% on
 * the cell that decodes and 49-64% on the one that does not. A number that
 * cannot separate the control from the fault is not evidence.
 */
static double pbch_qpsk_coherence(const struct lte_trace *trace) {
    double sr = 0.0, si = 0.0, power = 0.0;
    int k;

    if (!trace || trace->element_count <= 0)
        return 0.0;
    for (k = 0; k < trace->element_count; k++) {
        double i = (double)trace->element_i[k];
        double q = (double)trace->element_q[k];
        /* z^2 then (z^2)^2, which is z^4 without a pow() call. */
        double ar = i * i - q * q, ai = 2.0 * i * q;
        double br = ar * ar - ai * ai, bi = 2.0 * ar * ai;
        sr += br;
        si += bi;
        power += sqrt(br * br + bi * bi);
    }
    if (power <= 0.0)
        return 0.0;
    return sqrt(sr * sr + si * si) / power;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    FILE *f;
    int block = 0, cells = 0, messages = 0;
    int swept = 0;

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
                            &cell, NULL) != 1) {
            /* Say which gate refused it, since that is the whole question. */
            struct lte_pss_result pss;
            lte_pss_detect(i_samples, q_samples, pairs, LTE_SAMPLE_RATE_HZ,
                           &pss, NULL);
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
        /* The two decisions the chain used to report as facts. "normal CP"
           beside the score the extended prefix actually reached, and the
           frame's timing beside the best competitor far enough away to be a
           different symbol boundary rather than this one's shoulder. */
        printf("          CP normal ");
        if (cell.cp_measured[0])
            printf("%.3f", (double)cell.cp_score[0]);
        else
            printf("not tried");
        printf("  extended ");
        if (cell.cp_measured[1])
            printf("%.3f", (double)cell.cp_score[1]);
        else
            printf("not tried");
        printf("   timing shift %+d  sidelobe %.3f (peak %.3f)\n",
               cell.timing_shift, (double)cell.timing_sidelobe,
               (double)cell.pss_correlation);

        {
            static const int ports[3] = { 1, 2, 4 };
            int h, read_any = 0;
            for (h = 0; h < 3; h++) {
                float soft[LTE_PBCH_SOFT_BITS];
                struct lte_mib mib;
                struct lte_trace pbch;
                int written = lte_pbch_soft_bits(i_samples, q_samples, pairs,
                                                 LTE_SAMPLE_RATE_HZ, &cell,
                                                 cell.subframe0_start,
                                                 ports[h], soft, &pbch);
                double magnitude = 0.0, evm;
                int k;
                if (written != LTE_PBCH_SOFT_BITS) {
                    printf("          %d port(s): no soft bits\n", ports[h]);
                    continue;
                }
                for (k = 0; k < LTE_PBCH_SOFT_BITS; k++)
                    magnitude += fabs((double)soft[k]);
                magnitude /= LTE_PBCH_SOFT_BITS;
                evm = pbch_qpsk_coherence(&pbch);
                if (!lte_mib_decode(soft, cell.pci, &mib)) {
                    printf("          %d port(s): no parity fits "
                           "(mean |soft| %.3f, QPSK coherence %.3f)\n", ports[h],
                           magnitude, evm);
                    continue;
                }
                printf("          MIB via %d-port combining (coherence %.3f): "
                       "%d blocks (%.2f MHz)  PHICH %s %s  "
                       "SFN %d  %d port(s)\n", ports[h], evm,
                       mib.bandwidth_prb,
                       lte_mib_occupied_hz(mib.bandwidth_prb) / 1e6,
                       mib.phich_extended ? "extended" : "normal",
                       lte_phich_resource_name(mib.phich_resource_sixths),
                       mib.system_frame_number, mib.antenna_ports);
                read_any = 1;
                break;
            }
            if (read_any)
                messages++;
            else if (!swept) {
                /*
                 * Nothing fitted. Before blaming a stage further down, ask
                 * the one question that needs no theory: would *any* frame
                 * start have worked?
                 *
                 * The frame boundary comes from a correlation peak, and a
                 * timing error of more than the cyclic prefix costs the
                 * broadcast channel everything while costing the primary and
                 * secondary sequences almost nothing -- so the chain can look
                 * healthy at exactly the point it has gone wrong. If some
                 * nudge decodes, the timing is the fault and its size says
                 * how far out. If none does across a whole symbol either way,
                 * the timing is exonerated and the fault is downstream.
                 *
                 * Once per run, on the first block that finds a cell and no
                 * message: 61 starts times three port hypotheses is a couple
                 * of seconds, which is worth paying once and not per block.
                 */
                int nudge, fits = 0;
                swept = 1;
                printf("          sweeping the frame start +-%d samples, "
                       "since none of the three port hypotheses fitted:\n",
                       LTE_TIMING_NUDGE);
                for (nudge = -LTE_TIMING_NUDGE; nudge <= LTE_TIMING_NUDGE;
                     nudge++) {
                    long at = (long)cell.subframe0_start + nudge;
                    if (at < 0)
                        continue;
                    for (h = 0; h < 3; h++) {
                        float soft[LTE_PBCH_SOFT_BITS];
                        struct lte_mib mib;
                        if (lte_pbch_soft_bits(i_samples, q_samples, pairs,
                                               LTE_SAMPLE_RATE_HZ, &cell,
                                               (size_t)at, ports[h], soft,
                                               NULL) != LTE_PBCH_SOFT_BITS)
                            continue;
                        if (!lte_mib_decode(soft, cell.pci, &mib))
                            continue;
                        printf("            %+d samples, %d port(s): "
                               "MIB fits -- %d blocks, SFN %d\n", nudge,
                               ports[h], mib.bandwidth_prb,
                               mib.system_frame_number);
                        fits++;
                    }
                }
                if (!fits)
                    printf("            no frame start in that range fits "
                           "under any port count\n");
            }
        }
        block++;
    }
    fclose(f);
    printf("\n%d blocks, %d with a cell, %d with a message\n", block, cells,
           messages);
    return 0;
}
