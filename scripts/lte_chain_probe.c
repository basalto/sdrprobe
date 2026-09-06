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

/* How many radio frames of lag the broadcast-repetition test looks over. The
   answer it is after is at four; one, two and three are there so that a
   carrier repeating at every lag can be told from one repeating at the right
   one. */
#define PBCH_LAGS 5

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

/*
 * Normalised correlation between two sets of soft bits, zero-mean.
 */
static double soft_correlation(const float *a, const float *b, int count) {
    double ma = 0.0, mb = 0.0, num = 0.0, da = 0.0, db = 0.0;
    int k;

    for (k = 0; k < count; k++) {
        ma += (double)a[k];
        mb += (double)b[k];
    }
    ma /= (double)count;
    mb /= (double)count;
    for (k = 0; k < count; k++) {
        double x = (double)a[k] - ma, y = (double)b[k] - mb;
        num += x * y;
        da += x * x;
        db += y * y;
    }
    if (da <= 0.0 || db <= 0.0)
        return 0.0;
    return num / sqrt(da * db);
}

/*
 * Does this carrier have a broadcast channel at all?
 *
 * The question the decode funnel cannot answer: a cell whose parity never fits
 * looks the same whether the message is being read wrongly or was never
 * transmitted. Synchronisation signals with nothing behind them is what a
 * repeater or a sync-only transmitter looks like, and no work on the decoder
 * would ever produce a message from one.
 *
 * The test needs no decoder and no descrambling. A PBCH carries the same coded
 * block over 40 ms, one self-decodable quarter per radio frame, and which
 * quarter is used is picked by the two low bits of the frame number -- so
 * subframe 0 of frame n and of frame n+4 carry **the same coded bits under the
 * same scrambling**, while frames one, two and three apart carry different
 * ones. Nothing else in LTE has a 40 ms period.
 *
 * So a real broadcast channel correlates high at a lag of four frames and low
 * at one, two and three; noise is low at every lag; and something stuck or
 * repeating is high at every lag, which is why the near lags are printed
 * rather than only the one being tested.
 *
 * The offset control is what makes the number mean anything. The same profile
 * is taken over subframe 7, which carries no broadcast channel -- so a peak at
 * +4 appearing there too would belong to the measurement rather than to the
 * signal.
 *
 * Note what the null does *not* say. It correlates high and flat (0.84-0.91 on
 * the band 8 cell, 0.23-0.28 on the band 20 one), because a lightly loaded
 * subframe is nearly identical frame to frame -- the reference signals alone
 * repeat. The test is therefore the *peak*, never the level: a region with no
 * broadcast channel can correlate far more strongly overall and still have no
 * maximum at four frames, which is exactly what both captures show.
 *
 * Subframe 5 was tried first and is a poor null. It gave an intermediate,
 * partly structured profile, and the whole point of a control is that it be
 * unambiguous.
 */
struct repetition {
    double sum[PBCH_LAGS + 1];
    int count[PBCH_LAGS + 1];
};

static void repetition_add(struct repetition *acc, const float *i_samples,
                           const float *q_samples, size_t pairs,
                           const struct lte_cell *cell, int ports,
                           size_t from) {
    float soft[PBCH_LAGS + 1][LTE_PBCH_SOFT_BITS];
    int have[PBCH_LAGS + 1];
    int lag;

    for (lag = 0; lag <= PBCH_LAGS; lag++) {
        size_t at = from + (size_t)lag * (size_t)LTE_FRAME_SAMPLES;
        have[lag] = lte_pbch_soft_bits(i_samples, q_samples, pairs,
                                       LTE_SAMPLE_RATE_HZ, cell, at, ports,
                                       soft[lag], NULL) == LTE_PBCH_SOFT_BITS;
    }
    if (!have[0])
        return;
    for (lag = 1; lag <= PBCH_LAGS; lag++) {
        if (!have[lag])
            continue;
        acc->sum[lag] += soft_correlation(soft[0], soft[lag],
                                          LTE_PBCH_SOFT_BITS);
        acc->count[lag]++;
    }
}

static void repetition_report(const struct repetition *acc, const char *label) {
    int lag;

    printf("  %-28s", label);
    for (lag = 1; lag <= PBCH_LAGS; lag++) {
        if (!acc->count[lag]) {
            printf("  +%d   n/a", lag);
            continue;
        }
        printf("  +%d %+.3f", lag, acc->sum[lag] / (double)acc->count[lag]);
    }
    printf("\n");
}

/*
 * Are the reference signals where we think, carrying the sequence we think?
 *
 * Everything measured so far has been of the broadcast elements *after* the
 * channel estimate was applied, so a wrong estimate and a wrong extraction
 * look identical from there. This looks at the estimate itself.
 *
 * A reference symbol has unit magnitude, so dividing the received value by the
 * expected one keeps the magnitude whatever sequence is used -- which is why
 * trace->channel_db cannot answer this and why it has to be the phase.
 *
 * Taken against the right sequence, the per-reference estimates differ from
 * their neighbours by one consistent rotation: the channel's delay, a phase
 * ramp across the band. Taken against the wrong one they differ randomly. So
 * the statistic is the coherence of the *difference* between neighbours,
 * exactly as the secondary sequence is read differentially and for the same
 * reason -- it survives any channel while a wrong sequence cannot fake it.
 *
 * Near 1: the references are being read correctly. Near 0: they are not, and
 * every soft bit downstream was equalised with noise.
 */
static double crs_coherence(const float *i_samples, const float *q_samples,
                            size_t pairs, const struct lte_cell *cell,
                            int symbol, int port) {
    float row_re[LTE_PBCH_SUBCARRIERS], row_im[LTE_PBCH_SUBCARRIERS];
    float ref_re[LTE_PBCH_SUBCARRIERS / 6], ref_im[LTE_PBCH_SUBCARRIERS / 6];
    int positions[LTE_PBCH_SUBCARRIERS / 6];
    double tr[LTE_PBCH_SUBCARRIERS / 6], ti[LTE_PBCH_SUBCARRIERS / 6];
    double sr = 0.0, si = 0.0, mag = 0.0;
    int count, m;

    if (cell->subframe0_start + LTE_SUBFRAME_SAMPLES > pairs)
        return 0.0;
    read_slot1_symbol(i_samples, q_samples, cell->subframe0_start, symbol,
                      cell->frequency_offset_hz, LTE_SAMPLE_RATE_HZ,
                      row_re, row_im);
    count = lte_crs_subcarriers(cell->pci, 1, symbol, port, positions);
    if (count <= 0)
        return 0.0;
    if (lte_crs_sequence(cell->pci, 1, symbol, port, 0, ref_re, ref_im) <= 0)
        return 0.0;
    for (m = 0; m < count; m++) {
        int k = positions[m];
        tr[m] = (double)row_re[k] * ref_re[m] + (double)row_im[k] * ref_im[m];
        ti[m] = (double)row_im[k] * ref_re[m] - (double)row_re[k] * ref_im[m];
    }
    for (m = 0; m + 1 < count; m++) {
        /* t[m+1] * conj(t[m]) */
        double dr = tr[m + 1] * tr[m] + ti[m + 1] * ti[m];
        double di = ti[m + 1] * tr[m] - tr[m + 1] * ti[m];
        sr += dr;
        si += di;
        mag += sqrt(dr * dr + di * di);
    }
    if (mag <= 0.0)
        return 0.0;
    return sqrt(sr * sr + si * si) / mag;
}

/*
 * How close the decode gets, rather than whether it arrives.
 *
 * "No parity fits" is one bit of information and it is the same bit whether
 * the elements are noise or whether one convention is inverted. Rebuilding
 * the chain here and counting *how many* of the sixteen parity bits disagree
 * turns it into a measurement: eight is what a random block gives, so a best
 * of eight says the soft bits carry nothing, and a best of one or two says
 * everything is nearly right and something small is inverted.
 *
 * All three port masks are tried against the same decoded block, because the
 * mask is what the parity is exclusive-ored with and the count is the thing
 * being read out of it.
 */
static int crc_distance(const float soft[LTE_MIB_QUARTER_BITS], int pci,
                        int *best_quarter, int *best_ports) {
    int quarter, best = LTE_MIB_CRC_BITS + 1;
    static const int candidates[3] = { 1, 2, 4 };

    for (quarter = 0; quarter < LTE_MIB_QUARTERS; quarter++) {
        float attempt[LTE_MIB_QUARTER_BITS];
        float whole[LTE_MIB_RATE_MATCHED_BITS];
        float coded[LTE_MIB_CODED_BITS];
        uint8_t block[LTE_MIB_BLOCK_BITS];
        int n, c, from = quarter * LTE_MIB_QUARTER_BITS;

        memcpy(attempt, soft, sizeof(attempt));
        lte_mib_descramble(pci, quarter, attempt);
        for (n = 0; n < LTE_MIB_RATE_MATCHED_BITS; n++)
            whole[n] = 0.0f;
        for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
            whole[from + n] = attempt[n];
        lte_mib_rate_dematch(whole, coded);
        lte_mib_convolutional_decode(coded, block);

        for (c = 0; c < 3; c++) {
            uint8_t parity[LTE_MIB_CRC_BITS];
            int wrong = 0;
            lte_mib_parity(block, candidates[c], parity);
            for (n = 0; n < LTE_MIB_CRC_BITS; n++)
                if (parity[n] != block[LTE_MIB_BITS + n])
                    wrong++;
            if (wrong < best) {
                best = wrong;
                if (best_quarter)
                    *best_quarter = quarter;
                if (best_ports)
                    *best_ports = candidates[c];
            }
        }
    }
    return best;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    FILE *f;
    int block = 0, cells = 0, messages = 0;
    int swept = 0;
    int dist_sum[3] = { 0, 0, 0 }, dist_min[3] = { 0, 0, 0 };
    int dist_n[3] = { 0, 0, 0 };
    double crs_sum[4] = { 0.0, 0.0, 0.0, 0.0 };
    int crs_n[4] = { 0, 0, 0, 0 };
    struct repetition on_pbch[3], off_pbch[3];

    memset(on_pbch, 0, sizeof(on_pbch));
    memset(off_pbch, 0, sizeof(off_pbch));

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
            int pt;
            for (pt = 0; pt < 4; pt++) {
                crs_sum[pt] += crs_coherence(i_samples, q_samples, pairs,
                                             &cell, pt < 2 ? 0 : 1, pt);
                crs_n[pt]++;
            }
        }

        /* Accumulated over every block and reported once at the end: one
           block's correlation is a single draw and these are small numbers. */
        {
            static const int pp[3] = { 1, 2, 4 };
            int pi;
            for (pi = 0; pi < 3; pi++) {
                float soft[LTE_PBCH_SOFT_BITS];
                int q = -1, pt = 0, d;
                if (lte_pbch_soft_bits(i_samples, q_samples, pairs,
                                       LTE_SAMPLE_RATE_HZ, &cell,
                                       cell.subframe0_start, pp[pi], soft,
                                       NULL) != LTE_PBCH_SOFT_BITS)
                    continue;
                d = crc_distance(soft, cell.pci, &q, &pt);
                dist_sum[pi] += d;
                if (!dist_n[pi] || d < dist_min[pi])
                    dist_min[pi] = d;
                dist_n[pi]++;
            }
        }

        {
            /* Once per port hypothesis. The 40 ms repetition is a grade for
               the *extraction*, needing no decode: read the elements the way
               the cell actually transmits them and the same coded block comes
               back four frames later; read them the wrong way and it does
               not. It is the only handle on which hypothesis is right for a
               cell that never decodes under any of them. */
            static const int ports[3] = { 1, 2, 4 };
            int pi;
            for (pi = 0; pi < 3; pi++) {
                repetition_add(&on_pbch[pi], i_samples, q_samples, pairs,
                               &cell, ports[pi], cell.subframe0_start);
                repetition_add(&off_pbch[pi], i_samples, q_samples, pairs,
                               &cell, ports[pi], cell.subframe0_start +
                                   (size_t)(7 * LTE_SUBFRAME_SAMPLES));
            }
        }

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

                /*
                 * And the error a sample sweep cannot reach.
                 *
                 * The frame boundary is not only a correlation peak, it is a
                 * peak plus the half-frame the secondary sequence reported: a
                 * primary sequence from subframe 5 puts subframe 0 five
                 * subframes earlier. Get that wrong and the broadcast channel
                 * is read out of a subframe that has none -- while the
                 * primary and secondary sequences, which live in both
                 * subframe 0 and subframe 5, look perfect.
                 *
                 * That is this exact failure shape, and +-30 samples is
                 * nowhere near it: a subframe is 1920 samples.
                 */
                printf("          and every subframe of the frame, in case "
                       "the boundary is a whole subframe out:\n");
                fits = 0;
                for (nudge = 0; nudge < 10; nudge++) {
                    size_t at = cell.subframe0_start +
                                (size_t)nudge * (size_t)LTE_SUBFRAME_SAMPLES;
                    for (h = 0; h < 3; h++) {
                        float soft[LTE_PBCH_SOFT_BITS];
                        struct lte_mib mib;
                        if (lte_pbch_soft_bits(i_samples, q_samples, pairs,
                                               LTE_SAMPLE_RATE_HZ, &cell, at,
                                               ports[h], soft,
                                               NULL) != LTE_PBCH_SOFT_BITS)
                            continue;
                        if (!lte_mib_decode(soft, cell.pci, &mib))
                            continue;
                        printf("            subframe %d, %d port(s): MIB "
                               "fits -- %d blocks, SFN %d\n", nudge,
                               ports[h], mib.bandwidth_prb,
                               mib.system_frame_number);
                        fits++;
                    }
                }
                if (!fits)
                    printf("            no subframe of the frame fits "
                           "either\n");

                /*
                 * And finally: is the key wrong, or are the elements?
                 *
                 * The broadcast channel is scrambled with the cell identity,
                 * and the identity comes from a secondary sequence that can
                 * be confidently wrong -- a whole-subcarrier frequency error
                 * moves every subcarrier it reads, and lte_cell_search sweeps
                 * for exactly that reason. If some *other* identity
                 * descrambles into a message, the elements were right all
                 * along and only the key was wrong, which is a completely
                 * different fault from the extraction being wrong.
                 *
                 * Extraction does not depend on the descrambling identity, so
                 * the soft bits are read once per port count and only the key
                 * varies -- 504 identities against three reads rather than
                 * 1512 reads.
                 */
                printf("          and every descrambling identity, in case "
                       "the one found is confidently wrong:\n");
                fits = 0;
                for (h = 0; h < 3; h++) {
                    float soft[LTE_PBCH_SOFT_BITS];
                    int p;
                    if (lte_pbch_soft_bits(i_samples, q_samples, pairs,
                                           LTE_SAMPLE_RATE_HZ, &cell,
                                           cell.subframe0_start, ports[h],
                                           soft, NULL) != LTE_PBCH_SOFT_BITS)
                        continue;
                    for (p = 0; p < LTE_PCI_COUNT; p++) {
                        struct lte_mib mib;
                        if (!lte_mib_decode(soft, p, &mib))
                            continue;
                        printf("            PCI %d, %d port(s): MIB fits -- "
                               "%d blocks, SFN %d\n", p, ports[h],
                               mib.bandwidth_prb, mib.system_frame_number);
                        fits++;
                    }
                }
                if (!fits)
                    printf("            no identity in 0..%d descrambles "
                           "these elements into a message\n",
                           LTE_PCI_COUNT - 1);
            }
        }
        block++;
    }
    fclose(f);
    printf("\n%d blocks, %d with a cell, %d with a message\n", block, cells,
           messages);
    if (cells > 0) {
        static const int pp[3] = { 1, 2, 4 };
        int pt;
        printf("\nparity bits wrong, over the blocks with a cell "
               "(8 is a random block):\n");
        for (pt = 0; pt < 3; pt++)
            printf("  %d-port combining:  best %d   mean %.1f\n", pp[pt],
                   dist_n[pt] ? dist_min[pt] : -1,
                   dist_n[pt] ? (double)dist_sum[pt] / dist_n[pt] : 0.0);
        printf("\nreference coherence, mean over the blocks with a cell:\n ");
        for (pt = 0; pt < 4; pt++)
            printf("  port %d %.3f", pt,
                   crs_n[pt] ? crs_sum[pt] / (double)crs_n[pt] : 0.0);
        printf("\n  chance is about 0.30 -- eleven phase differences per "
               "port -- so a port near it is carrying no references\n");
        printf("\nbroadcast repetition, mean over the blocks with a cell:\n");
        repetition_report(&on_pbch[0], "subframe 0, 1 port");
        repetition_report(&on_pbch[1], "subframe 0, 2 port");
        repetition_report(&on_pbch[2], "subframe 0, 4 port");
        repetition_report(&off_pbch[0], "subframe 7, 1 port (no PBCH)");
        printf("  a broadcast channel repeats at +4 and nowhere else, and "
               "only in subframe 0\n");
    }
    return 0;
}
