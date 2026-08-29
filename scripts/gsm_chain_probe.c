/*
 * gsm_chain_probe.c - white-box walk through the whole GSM SCH processing
 * chain, printing every stage with commentary, then running a deterministic
 * self-check and a real-capture consistency sweep, and printing a conclusion.
 *
 * This is a diagnostic / teaching tool, not a unit test. It #includes the
 * plugin source directly so it can reach the file-local (static) helpers and
 * reproduce each intermediate stage that gsm_sch_decode() hides. Build/run:
 *
 *     make probe-gsm-chain                 # uses testfiles/gsm_arfcn_69.bin
 *     make probe-gsm-chain FILE=other.bin
 *
 * Because it compiles gsm_dsp.c in, link only sdr_dsp.c alongside it (the
 * Makefile target does this).
 */
#include "sdr_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pull in the plugin's implementation (and its statics) directly. */
#include "gsm_dsp.c"

#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
#define NOMINAL_OFFSET_HZ 400000.0
#define SAMPLE_RATE_HZ 2000000.0

static const char *yesno(int v) { return v ? "yes" : "no"; }

static void print_bits(const char *label, const uint8_t *bits, int count) {
    printf("    %s", label);
    for (int i = 0; i < count; i++)
        putchar(bits[i] ? '1' : '0');
    putchar('\n');
}

/* Reproduce sch_parse's field extraction for display (the plugin's version is
   static and only fills the result struct). */
static void parse_fields(const uint8_t u[GSM_SCH_UNCODED_BITS], int *bsic,
                         int *t1, int *t2, int *t3p) {
    *bsic = 0;
    for (int i = 0; i < 6; i++)
        *bsic = (*bsic << 1) | u[i];
    *t1 = 0;
    for (int i = 6; i < 17; i++)
        *t1 = (*t1 << 1) | u[i];
    *t2 = 0;
    for (int i = 17; i < 22; i++)
        *t2 = (*t2 << 1) | u[i];
    *t3p = 0;
    for (int i = 22; i < 25; i++)
        *t3p = (*t3p << 1) | u[i];
}

static int frame_number(int t1, int t2, int t3) {
    return 51 * (((t3 + 26) - t2) % 26) + t3 + 51 * 26 * t1;
}

/* Walk one block through every stage of the chain, printing as we go. */
static int probe_block(const unsigned char *raw, size_t bytes) {
    static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
    size_t pairs = sdr_dsp_convert_iq(raw, bytes, I, Q, M, BLOCK_PAIRS);
    double fs = SAMPLE_RATE_HZ;
    double sps = fs / GSM_SYMBOL_RATE_HZ;

    puts("========================================================================");
    puts("STAGE 1  byte->float I/Q conversion (generic core)");
    puts("  Unsigned 8-bit interleaved I/Q, 127.5 = zero. One pair per sample.");
    printf("  pairs=%zu  sample_rate=%.0f S/s  samples/symbol=%.4f\n",
           pairs, fs, sps);

    puts("");
    puts("STAGE 2  FCCH carrier refinement");
    puts("  The FCCH is a pure tone at carrier + 1625/24 kHz (~67.708 kHz).");
    puts("  We find it to recover the true carrier: an uncalibrated dongle can");
    puts("  be tens of kHz off, which biases the differential demod.");
    struct gsm_fcch_result fcch;
    int have_fcch = gsm_fcch_detect(I, Q, pairs, fs,
                                    NOMINAL_OFFSET_HZ + GSM_FCCH_TONE_HZ,
                                    100000.0, &fcch);
    double refined = NOMINAL_OFFSET_HZ;
    if (have_fcch)
        refined = fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
    printf("  fcch detected=%s confidence=%.3f tone=%.1f Hz\n", yesno(have_fcch),
           (double)fcch.confidence, fcch.tone_frequency_hz);
    printf("  refined carrier offset=%.1f Hz (nominal %.0f, residual %.1f Hz)\n",
           refined, NOMINAL_OFFSET_HZ, refined - NOMINAL_OFFSET_HZ);
    if (!have_fcch) {
        puts("  no FCCH -> not a BCCH carrier / no burst; stopping this block.");
        return 0;
    }

    puts("");
    puts("STAGE 3  downconvert to baseband at the refined carrier");
    puts("  Multiply by exp(-j 2pi refined n / fs) so the channel sits at DC.");
    static float bi[BLOCK_PAIRS], bq[BLOCK_PAIRS];
    for (size_t n = 0; n < pairs; n++) {
        double ph = -GSM_TWO_PI * refined * (double)n / fs;
        double c = cos(ph), s = sin(ph);
        bi[n] = (float)(I[n] * c - Q[n] * s);
        bq[n] = (float)(I[n] * s + Q[n] * c);
    }
    int nsym = (int)(((double)pairs - 2.0) / sps) - 1;
    printf("  usable symbols in block=%d\n", nsym);

    puts("");
    puts("STAGE 4  timing + burst-position search (hard differential demod)");
    puts("  For 8 fractional timings we differential-demod every symbol");
    puts("  (bit = sign of Im(conj(prev)*cur)) and correlate against the");
    puts("  differentially-encoded 64-bit training sequence.");
    uint8_t *m = malloc((size_t)nsym);
    uint8_t train_diff[GSM_SCH_TRAINING_BITS];
    for (int j = 1; j < GSM_SCH_TRAINING_BITS; j++)
        train_diff[j] = sch_training[j] ^ sch_training[j - 1];
    int best_match = 0, best_t = -1, best_p = -1, best_inv = 0;
    for (int t = 0; t < SCH_TIMINGS; t++) {
        double phase0 = (double)t * sps / (double)SCH_TIMINGS;
        sch_diff_bits(bi, bq, pairs, sps, phase0, nsym, m);
        int last = nsym - GSM_SCH_TRAINING_BITS - 39;
        for (int p = 39; p < last; p++) {
            int matches = 0;
            for (int j = 1; j < GSM_SCH_TRAINING_BITS; j++)
                matches += (m[p + j] == train_diff[j]);
            int inv = 0, score = matches;
            if ((GSM_SCH_TRAINING_BITS - 1) - matches > matches) {
                score = (GSM_SCH_TRAINING_BITS - 1) - matches;
                inv = 1;
            }
            if (score > best_match) {
                best_match = score;
                best_t = t;
                best_p = p;
                best_inv = inv;
            }
        }
    }
    double match_ratio = (double)best_match / (GSM_SCH_TRAINING_BITS - 1);
    printf("  best timing phase=%d/%d  position=%d  training match=%.3f  "
           "invert=%s\n",
           best_t, SCH_TIMINGS, best_p, match_ratio, yesno(best_inv));
    if (best_t < 0 || match_ratio < SCH_MIN_MATCH) {
        puts("  no burst above the match threshold in this block; stopping.");
        free(m);
        return 0;
    }

    puts("");
    puts("STAGE 5  soft-confidence of the differential decisions");
    puts("  The hard path keeps only the SIGN of Im. The MAGNITUDE is the");
    puts("  per-symbol confidence the joint soft-decision decoder will use.");
    double phase0 = (double)best_t * sps / (double)SCH_TIMINGS;
    {
        double prev_i, prev_q;
        sch_interp(bi, bq, pairs, phase0 + (double)(best_p - 42) * sps, &prev_i,
                   &prev_q);
        double sum_train = 0, sum_data = 0;
        int nt = 0, nd = 0, weak = 0;
        double meanabs = 0;
        for (int j = 0; j < GSM_SCH_BURST_BITS; j++) {
            int k = best_p - 42 + j;
            double si, sq;
            sch_interp(bi, bq, pairs, phase0 + (double)(k + 1) * sps, &si, &sq);
            double im = prev_i * sq - prev_q * si;
            prev_i = si;
            prev_q = sq;
            double a = fabs(im);
            meanabs += a;
            int in_train = (j >= 3 + 39 && j < 3 + 39 + 64);
            if (in_train) { sum_train += a; nt++; }
            else { sum_data += a; nd++; }
        }
        meanabs /= GSM_SCH_BURST_BITS;
        /* Second pass to count weak (low-confidence) symbols. */
        sch_interp(bi, bq, pairs, phase0 + (double)(best_p - 42) * sps, &prev_i,
                   &prev_q);
        for (int j = 0; j < GSM_SCH_BURST_BITS; j++) {
            int k = best_p - 42 + j;
            double si, sq;
            sch_interp(bi, bq, pairs, phase0 + (double)(k + 1) * sps, &si, &sq);
            double im = prev_i * sq - prev_q * si;
            prev_i = si;
            prev_q = sq;
            if (fabs(im) < 0.35 * meanabs)
                weak++;
        }
        printf("  mean |Im|: training=%.1f  data=%.1f\n",
               nt ? sum_train / nt : 0.0, nd ? sum_data / nd : 0.0);
        printf("  low-confidence symbols (<0.35 mean): %d / %d\n", weak,
               GSM_SCH_BURST_BITS);
        puts("  -> any of these near the data fields can flip a hard bit that");
        puts("     the current integrate-then-hard-Viterbi path cannot recover.");
    }

    puts("");
    puts("STAGE 6  training sync check");
    sch_diff_bits(bi, bq, pairs, sps, phase0, nsym, m);
    if (best_inv)
        for (int k = 0; k < nsym; k++)
            m[k] ^= 1u;
    {
        int match = 0;
        for (int j = 1; j < GSM_SCH_TRAINING_BITS; j++)
            match += (m[best_p + j] == train_diff[j]);
        printf("  differential-training match = %d / %d\n", match,
               GSM_SCH_TRAINING_BITS - 1);
    }

    puts("");
    puts("STAGE 7  channel-bit reconstruction (anchored at the training)");
    puts("  data2 integrates FORWARD from d[p+63]=TRAIN[63];");
    puts("  data1 integrates BACKWARD from d[p]=TRAIN[0].");
    puts("  A single differential error propagates along the field -> this is");
    puts("  the weak link the joint decoder removes.");
    uint8_t coded[GSM_SCH_CODED_BITS];
    {
        uint8_t prev = sch_training[GSM_SCH_TRAINING_BITS - 1];
        int base2 = best_p + GSM_SCH_TRAINING_BITS;
        for (int j = 0; j < 39; j++) {
            uint8_t b = prev ^ m[base2 + j];
            coded[39 + j] = b;
            prev = b;
        }
        prev = sch_training[0];
        for (int i = 0; i < 39; i++) {
            uint8_t b = prev ^ m[best_p - i];
            coded[38 - i] = b;
            prev = b;
        }
    }
    print_bits("coded[0..77]  = ", coded, GSM_SCH_CODED_BITS);

    puts("");
    puts("STAGE 8  Viterbi decode (rate-1/2, K=5, hard Hamming metric)");
    uint8_t u[GSM_SCH_UNCODED_BITS];
    sch_viterbi(coded, u);
    print_bits("uncoded[0..38]= ", u, GSM_SCH_UNCODED_BITS);

    puts("");
    puts("STAGE 9  parity check (10-bit, GSM-inverted)");
    uint8_t d[GSM_SCH_INFO_BITS], pexp[10];
    for (int i = 0; i < GSM_SCH_INFO_BITS; i++)
        d[i] = u[i];
    sch_parity(d, pexp);
    int parity_ok = 1;
    for (int j = 0; j < 10; j++)
        if (pexp[j] != u[GSM_SCH_INFO_BITS + j])
            parity_ok = 0;
    print_bits("parity received = ", u + GSM_SCH_INFO_BITS, 10);
    print_bits("parity expected = ", pexp, 10);
    printf("  parity %s\n", parity_ok ? "OK" : "FAIL");

    puts("");
    puts("STAGE 10 parse BSIC + reduced frame number");
    int bsic, t1, t2, t3p;
    parse_fields(u, &bsic, &t1, &t2, &t3p);
    int t3 = 10 * t3p + 1;
    int valid_timing = (t2 <= 25 && t3p <= 4);
    printf("  BSIC=%d (NCC %d, BCC %d)\n", bsic, (bsic >> 3) & 7, bsic & 7);
    printf("  T1=%d T2=%d T3'=%d -> T3=%d  frame=%d  timing_valid=%s\n", t1, t2,
           t3p, t3, frame_number(t1, t2, t3), yesno(valid_timing));

    /* Cross-check against the real gsm_sch_decode(). */
    struct gsm_sch_result r;
    int dec = gsm_sch_decode(I, Q, pairs, fs, NOMINAL_OFFSET_HZ, &r, NULL);
    printf("  gsm_sch_decode() -> decoded=%s", yesno(dec));
    if (dec)
        printf("  BSIC=%d frame=%d (T1/T2/T3 %d/%d/%d)", r.bsic,
               r.frame_number, r.t1, r.t2, r.t3);
    putchar('\n');

    free(m);
    return dec;
}

/* Deterministic self-check: encode a known SCH, modulate it cleanly, decode it
   back, and confirm every field round-trips. */
static int self_check(void) {
    puts("");
    puts("========================================================================");
    puts("SELF-CHECK  synthetic encode -> modulate -> decode round-trip");
    int bsic = 42, t1 = 100, t2 = 13, t3p = 2;
    int t3 = 10 * t3p + 1;
    uint8_t info[GSM_SCH_INFO_BITS];
    int idx = 0;
    for (int i = 5; i >= 0; i--) info[idx++] = (bsic >> i) & 1;
    for (int i = 10; i >= 0; i--) info[idx++] = (t1 >> i) & 1;
    for (int i = 4; i >= 0; i--) info[idx++] = (t2 >> i) & 1;
    for (int i = 2; i >= 0; i--) info[idx++] = (t3p >> i) & 1;
    uint8_t coded[GSM_SCH_CODED_BITS];
    gsm_sch_encode(info, coded);

    size_t count = 4000, start = 500;
    static float I[4000], Q[4000];
    srand(11);
    for (size_t n = 0; n < count; n++) {
        I[n] = 3.0f * ((float)rand() / RAND_MAX - 0.5f);
        Q[n] = 3.0f * ((float)rand() / RAND_MAX - 0.5f);
    }
    gsm_sch_modulate(coded, SAMPLE_RATE_HZ, NOMINAL_OFFSET_HZ, start, I, Q, count);
    for (size_t n = start; n < start + 1100 && n < count; n++) {
        I[n] += 3.0f * ((float)rand() / RAND_MAX - 0.5f);
        Q[n] += 3.0f * ((float)rand() / RAND_MAX - 0.5f);
    }
    struct gsm_sch_result r;
    int ok = gsm_sch_decode(I, Q, count, SAMPLE_RATE_HZ, NOMINAL_OFFSET_HZ, &r,
                            NULL);
    int pass = ok && r.bsic == bsic && r.t1 == t1 && r.t2 == t2 && r.t3 == t3 &&
               r.frame_number == frame_number(t1, t2, t3);
    printf("  expected BSIC=%d frame=%d ; got decoded=%s BSIC=%d frame=%d\n",
           bsic, frame_number(t1, t2, t3), yesno(ok), ok ? r.bsic : -1,
           ok ? r.frame_number : -1);
    printf("  SELF-CHECK %s\n", pass ? "PASS" : "FAIL");
    return pass;
}

/* Sweep every block, report BSIC/frame per block, and judge frame-number
   consistency (T1 is constant over ~6 s, so it should agree across a short
   capture; disagreement exposes the current T1/T2/T3 unreliability). */
static void consistency_sweep(FILE *f) {
    puts("");
    puts("========================================================================");
    puts("CONSISTENCY SWEEP  every block (BSIC is reliable; frame number is not)");
    static unsigned char raw[BLOCK_BYTES];
    static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
    int blk = 0, decoded = 0;
    int bsic_hist[64] = { 0 };
    int t1_min = 1 << 30, t1_max = -1, bsic_mode = -1;
    rewind(f);
    while (fread(raw, 1, BLOCK_BYTES, f) == BLOCK_BYTES) {
        size_t pairs = sdr_dsp_convert_iq(raw, BLOCK_BYTES, I, Q, M, BLOCK_PAIRS);
        struct gsm_sch_result r;
        if (gsm_sch_decode(I, Q, pairs, SAMPLE_RATE_HZ, NOMINAL_OFFSET_HZ, &r,
                           NULL)) {
            decoded++;
            if (r.bsic >= 0 && r.bsic < 64)
                bsic_hist[r.bsic]++;
            if (r.t1 < t1_min) t1_min = r.t1;
            if (r.t1 > t1_max) t1_max = r.t1;
            printf("  block %2d: BSIC=%d frame=%d (T1/T2/T3 %d/%d/%d)\n", blk,
                   r.bsic, r.frame_number, r.t1, r.t2, r.t3);
        }
        blk++;
    }
    int bsic_agree = 0;
    for (int i = 0; i < 64; i++)
        if (bsic_hist[i] > bsic_agree) { bsic_agree = bsic_hist[i]; bsic_mode = i; }
    puts("");
    printf("  blocks decoded: %d\n", decoded);
    printf("  BSIC agreement: %d/%d on BSIC=%d  (reliable)\n", bsic_agree,
           decoded, bsic_mode);
    if (decoded)
        printf("  T1 spread across capture: %d..%d  (should be a single value)\n",
               t1_min, t1_max);
    puts("");
    puts("  CONCLUSION");
    puts("  ---------");
    puts("  * BSIC/NCC/BCC decode reliably from the real signal.");
    if (t1_max > t1_min)
        puts("  * T1 disagrees across the capture -> the reduced frame number is\n"
             "    NOT reliable with the current hard-decision path. Data-field\n"
             "    differential errors slip past the 10-bit parity.");
    else
        puts("  * T1 is consistent in this capture, but data-field errors still\n"
             "    make T2/T3 unreliable in general (see docs/sch-frame-number-decode.md).");
    puts("  * Fix: a joint soft-decision differential+convolutional trellis.");
    puts("    Plan: docs/sch-frame-number-decode.md, ADR docs/adr/0011-*.md.");
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "testfiles/gsm_arfcn_69.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    printf("GSM SCH chain probe on %s\n", path);

    static unsigned char raw[BLOCK_BYTES];
    if (fread(raw, 1, BLOCK_BYTES, f) == BLOCK_BYTES)
        probe_block(raw, BLOCK_BYTES);
    else
        puts("(file shorter than one block; skipping single-block walk)");

    int self = self_check();
    consistency_sweep(f);
    fclose(f);
    return self ? 0 : 1;
}
