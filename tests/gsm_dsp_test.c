#include "gsm_dsp.h"
#include "sdr_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI_F 3.14159265358979323846f

static int failures;

static void check_close(const char *name, float actual, float expected,
                        float tolerance) {
    if (!isfinite(actual) || fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "%s: got %.6f, expected %.6f (+/- %.6f)\n",
                name, actual, expected, tolerance);
        failures++;
    }
}

static void check_size(const char *name, size_t actual, size_t expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %zu, expected %zu\n",
                name, actual, expected);
        failures++;
    }
}

/* GSM calibration exercises the plugin's channel map plus the generic centroid
   estimate and PPM correction it reuses from sdr_dsp. */
static void test_cellular_calibration(void) {
    uint32_t frequency = 0;
    check_size("GSM 900 ARFCN 113",
               (size_t)gsm_downlink_hz(113, &frequency), 1);
    check_size("GSM 900 ARFCN 113 frequency", frequency, 957600000U);
    check_size("GSM 900 ARFCN 117",
               (size_t)gsm_downlink_hz(117, &frequency), 1);
    check_size("GSM 900 ARFCN 117 frequency", frequency, 958400000U);
    check_size("GSM 900 ARFCN 120",
               (size_t)gsm_downlink_hz(120, &frequency), 1);
    check_size("GSM 900 ARFCN 120 frequency", frequency, 959000000U);
    check_size("invalid GSM 900 ARFCN",
               (size_t)gsm_downlink_hz(125, &frequency), 0);

    float spectrum[101];
    float calibration_workspace[101];
    const double lower = 957000000.0;
    const double upper = 958000000.0;
    const double actual = 957620000.0;
    for (size_t n = 0; n < 101; n++) {
        double bin_frequency = lower + (upper - lower) * n / 101.0;
        double distance = (bin_frequency - actual) / 45000.0;
        spectrum[n] = -90.0f + (float)(45.0 * exp(-0.5 * distance * distance));
    }
    struct sdr_channel_estimate estimate;
    check_size("cellular channel estimate",
               (size_t)sdr_dsp_estimate_channel_center(
                   spectrum, 101, lower, upper, 957600000.0, 100000.0,
                   50000.0, calibration_workspace, &estimate), 1);
    check_close("cellular measured center",
                (float)estimate.measured_frequency_hz,
                (float)actual, 2500.0f);
    check_close("cellular peak", estimate.peak_dbfs, -45.0f, 0.25f);
    check_close("cellular floor", estimate.floor_dbfs, -90.0f, 1.0f);
    check_close("cellular prominence", estimate.prominence_db, 45.0f, 1.0f);
    check_size("positive residual correction",
               (size_t)(sdr_dsp_corrected_ppm(5, 957619152.0,
                                              957600000.0) + 1000),
               (size_t)(-15 + 1000));
    check_size("negative residual correction",
               (size_t)(sdr_dsp_corrected_ppm(-4, 957590424.0,
                                              957600000.0) + 1000),
               (size_t)(6 + 1000));

    for (size_t n = 0; n < 101; n++)
        spectrum[n] = -80.0f;
    check_size("flat spectrum rejected",
               (size_t)sdr_dsp_estimate_channel_center(
                   spectrum, 101, lower, upper, 957600000.0, 100000.0,
                   20000.0, calibration_workspace, &estimate), 0);
}

static void test_fcch_detection(void) {
    const double sample_rate = 2000000.0;
    const double target = 400000.0 + GSM_FCCH_TONE_HZ; /* ~467708 Hz */
    const double tone = 460000.0;                      /* within window */
    const size_t count = 8192;
    const size_t tone_start = 1000;
    const size_t tone_len = 1600;
    float *i = malloc(count * sizeof(*i));
    float *q = malloc(count * sizeof(*q));
    struct gsm_fcch_result result;

    if (!i || !q) {
        fprintf(stderr, "fcch allocation failed\n");
        exit(2);
    }

    srand(1);
    for (size_t n = 0; n < count; n++) {
        i[n] = 40.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
        q[n] = 40.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
    }
    for (size_t n = 0; n < tone_len; n++) {
        double phase = 2.0 * PI_F * tone * (double)n / sample_rate;
        i[tone_start + n] = 60.0f * (float)cos(phase);
        q[tone_start + n] = 60.0f * (float)sin(phase);
    }

    check_size("FCCH detected",
               (size_t)gsm_fcch_detect(i, q, count, sample_rate,
                                       target, 30000.0, &result), 1);
    check_close("FCCH tone frequency", (float)result.tone_frequency_hz,
                (float)tone, 200.0f);

    /* Noise only: no coherent tone, so no detection. */
    srand(2);
    for (size_t n = 0; n < count; n++) {
        i[n] = 40.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
        q[n] = 40.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
    }
    check_size("FCCH rejects noise",
               (size_t)gsm_fcch_detect(i, q, count, sample_rate,
                                       target, 30000.0, &result), 0);

    /* Tone present but outside the search window. */
    for (size_t n = 0; n < tone_len; n++) {
        double phase = 2.0 * PI_F * 100000.0 * (double)n / sample_rate;
        i[tone_start + n] = 60.0f * (float)cos(phase);
        q[tone_start + n] = 60.0f * (float)sin(phase);
    }
    check_size("FCCH rejects out-of-window tone",
               (size_t)gsm_fcch_detect(i, q, count, sample_rate,
                                       target, 30000.0, &result), 0);

    free(i);
    free(q);
}

/* Round-trip: encode + MSK-modulate a full SCH burst into noisy I/Q, then
   recover the BSIC and frame number through the demod + Viterbi + parity chain.
   This proves the sync + coding path; real-signal accuracy still needs a live
   capture. */
static void test_sch_decode(void) {
    const double sample_rate = 2000000.0;
    const double carrier_offset = 400000.0;
    const int bsic = 42; /* NCC 5, BCC 2 */
    const int t1 = 100, t2 = 13, t3p = 2;
    const int t3 = 10 * t3p + 1;
    const int expected_fn = 51 * (((t3 + 26) - t2) % 26) + t3 + 51 * 26 * t1;

    uint8_t info[25];
    uint8_t coded[78];
    gsm_sch_pack_info(bsic, t1, t2, t3p, info);
    gsm_sch_encode(info, coded);

    const size_t count = 4000;
    const size_t start = 500;
    float *i = malloc(count * sizeof(*i));
    float *q = malloc(count * sizeof(*q));
    if (!i || !q) {
        fprintf(stderr, "sch allocation failed\n");
        exit(2);
    }

    srand(7);
    for (size_t n = 0; n < count; n++) {
        i[n] = 3.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
        q[n] = 3.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
    }
    size_t written = gsm_sch_modulate(coded, sample_rate, carrier_offset, start,
                                      i, q, count);
    if (written == 0) {
        fprintf(stderr, "sch modulate wrote no samples\n");
        failures++;
    }
    /* Add mild noise on top of the burst. */
    for (size_t n = start; n < start + written; n++) {
        i[n] += 3.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
        q[n] += 3.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
    }

    struct gsm_sch_result result;
    check_size("SCH decoded",
               (size_t)gsm_sch_decode(i, q, count, sample_rate, carrier_offset, GSM_OPT_FILTER|GSM_OPT_FINECFO|GSM_OPT_TRELLIS, &result, NULL),
               1);
    check_size("SCH BSIC", (size_t)result.bsic, (size_t)bsic);
    check_size("SCH NCC", (size_t)result.ncc, 5);
    check_size("SCH BCC", (size_t)result.bcc, 2);
    check_size("SCH T1", (size_t)result.t1, (size_t)t1);
    check_size("SCH T2", (size_t)result.t2, (size_t)t2);
    check_size("SCH T3", (size_t)result.t3, (size_t)t3);
    check_size("SCH frame number", (size_t)result.frame_number,
               (size_t)expected_fn);

    /* Noise only: no burst, so no decode. */
    srand(9);
    for (size_t n = 0; n < count; n++) {
        i[n] = 40.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
        q[n] = 40.0f * ((float)rand() / (float)RAND_MAX - 0.5f);
    }
    check_size("SCH rejects noise",
               (size_t)gsm_sch_decode(i, q, count, sample_rate, carrier_offset, GSM_OPT_FILTER|GSM_OPT_FINECFO|GSM_OPT_TRELLIS, &result, NULL),
               0);

    free(i);
    free(q);
}

/* Real-capture regression: a 2 s recording of GSM 900 ARFCN 69 (948.8 MHz),
   tuned to expected - 400 kHz, must decode a stable BSIC. The frame number is
   not asserted (plain differential detection leaves residual data-field errors
   that a soft-decision trellis would remove); BSIC/NCC/BCC are reliable. Skips
   gracefully when the capture is absent. */
#define GSM_REAL_BLOCK (16 * 16384)

/* Deterministic PRNG so the synthetic channel is byte-identical every run. */
static unsigned long long sch_rng;
static double sch_urand(void) {
    sch_rng = sch_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)((sch_rng >> 11) & 0x1FFFFFFFFFFFFFULL) / 9007199254740992.0;
}
static double sch_gauss(void) {
    double u1 = sch_urand(), u2 = sch_urand();
    if (u1 < 1e-12) u1 = 1e-12;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI_F * u2);
}

/* Known fields -> encode -> MSK modulate -> a symbol-spaced 3-tap ISI channel
   -> AWGN -> decode, over many random bursts. `gsm_sch_modulate` stays an
   idealised modulator; the channel lives here in the test, so the taps can be
   the ones actually measured on testfiles/gsm_arfcn_69.bin and the noise can
   be referenced to the burst's own RMS (a per-sample SNR that means
   something). Also compares the soft-decision trellis against the hard
   fallback on the very same bursts. */
#define SCH_SYN_PAIRS 20000
#define SCH_SYN_START 4000
#define SCH_SYN_TRIALS 60
#define SCH_SYN_SNR_DB 3.0

static void test_sch_synthetic_channel(void) {
    float *ci = malloc(SCH_SYN_PAIRS * sizeof(*ci));
    float *cq = malloc(SCH_SYN_PAIRS * sizeof(*cq));
    float *ni = malloc(SCH_SYN_PAIRS * sizeof(*ni));
    float *nq = malloc(SCH_SYN_PAIRS * sizeof(*nq));
    if (!ci || !cq || !ni || !nq) {
        fprintf(stderr, "synthetic-channel allocation failed\n");
        exit(2);
    }
    const double fs = 2000000.0, offset = 400000.0;
    const double taps[3] = { 0.197, 0.928, 0.316 }; /* measured on ARFCN 69 */
    const int sps = (int)(fs / GSM_SYMBOL_RATE_HZ + 0.5);
    int soft_ok = 0, hard_ok = 0;

    sch_rng = 20260830ULL;
    for (int t = 0; t < SCH_SYN_TRIALS; t++) {
        int bsic = (int)(sch_urand() * 64);
        int t1 = (int)(sch_urand() * 2048);
        int t2 = (int)(sch_urand() * 26);
        int t3p = (int)(sch_urand() * 5);
        int t3 = 10 * t3p + 1;
        int fn = 51 * (((t3 + 26) - t2) % 26) + t3 + 51 * 26 * t1;

        uint8_t info[GSM_SCH_INFO_BITS], coded[GSM_SCH_CODED_BITS];
        gsm_sch_pack_info(bsic, t1, t2, t3p, info);
        gsm_sch_encode(info, coded);

        for (int n = 0; n < SCH_SYN_PAIRS; n++) { ci[n] = 0.0f; cq[n] = 0.0f; }
        size_t wrote = gsm_sch_modulate(coded, fs, offset, SCH_SYN_START,
                                        ci, cq, SCH_SYN_PAIRS);
        if (!wrote) {
            fprintf(stderr, "synthetic-channel: modulation produced nothing\n");
            failures++;
            break;
        }
        double energy = 0.0;
        for (size_t n = SCH_SYN_START; n < SCH_SYN_START + wrote &&
                                       n < SCH_SYN_PAIRS; n++)
            energy += ci[n] * ci[n] + cq[n] * cq[n];
        double rms = sqrt(energy / (double)wrote / 2.0);
        double sigma = rms / pow(10.0, SCH_SYN_SNR_DB / 20.0);

        for (int n = 0; n < SCH_SYN_PAIRS; n++) {
            double ai = 0.0, aq = 0.0;
            for (int l = 0; l < 3; l++) {
                int idx = n - (l - 1) * sps;
                if (idx < 0 || idx >= SCH_SYN_PAIRS)
                    continue;
                ai += taps[l] * ci[idx];
                aq += taps[l] * cq[idx];
            }
            ni[n] = (float)(ai + sigma * sch_gauss());
            nq[n] = (float)(aq + sigma * sch_gauss());
        }

        struct gsm_sch_result r;
        uint32_t soft = GSM_OPT_FILTER | GSM_OPT_FINECFO | GSM_OPT_TRELLIS;
        if (gsm_sch_decode(ni, nq, SCH_SYN_PAIRS, fs, offset, soft, &r, NULL) &&
            r.bsic == bsic && r.t1 == t1 && r.t2 == t2 && r.frame_number == fn)
            soft_ok++;
        if (gsm_sch_decode(ni, nq, SCH_SYN_PAIRS, fs, offset,
                           GSM_OPT_FILTER | GSM_OPT_FINECFO, &r, NULL) &&
            r.bsic == bsic && r.t1 == t1 && r.t2 == t2 && r.frame_number == fn)
            hard_ok++;
    }
    free(ci); free(cq); free(ni); free(nq);

    /* Every field, not just the BSIC: through ISI and noise the decoder must
       recover the frame number too. Measured 60/60 soft against 45/60 hard at
       3 dB, so the thresholds leave room without being vacuous. */
    if (soft_ok * 100 < SCH_SYN_TRIALS * 95) {
        fprintf(stderr,
                "synthetic-channel: soft decode recovered every field on "
                "%d/%d bursts (expected >=95%%)\n", soft_ok, SCH_SYN_TRIALS);
        failures++;
    }
    /* The soft trellis exists to beat the hard-decision fallback; if it ever
       stops doing so on the same bursts, it is not earning its complexity. */
    if (soft_ok <= hard_ok) {
        fprintf(stderr,
                "synthetic-channel: soft decode %d/%d did not beat the hard "
                "path %d/%d\n", soft_ok, SCH_SYN_TRIALS, hard_ok,
                SCH_SYN_TRIALS);
        failures++;
    }
}

/* Decode every block of a real capture and check the result against what the
   signal itself must satisfy: one cell's BSIC throughout, and a frame number
   that tracks real time. A wrong field layout in sch_parse still round-trips
   against an encoder that shares it -- that is how the pre-2026-08-30 layout
   survived -- so these real-signal invariants are what actually pin the bit
   positions. */
static void check_real_capture(const char *path, int bsic, int ncc, int bcc,
                               int min_decoded) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        printf("  (skipping real-capture SCH test: %s absent)\n", path);
        return;
    }
    unsigned char *raw = malloc(GSM_REAL_BLOCK);
    float *i = malloc((GSM_REAL_BLOCK / 2) * sizeof(*i));
    float *q = malloc((GSM_REAL_BLOCK / 2) * sizeof(*q));
    float *mag = malloc((GSM_REAL_BLOCK / 2) * sizeof(*mag));
    if (!raw || !i || !q || !mag) {
        fprintf(stderr, "real-capture allocation failed\n");
        exit(2);
    }

    int decoded = 0, bsic_ok = 0, ordered = 1;
    int first_fn = 0, last_fn = 0, min_t1 = 0, max_t1 = 0;
    size_t got;
    while ((got = fread(raw, 1, GSM_REAL_BLOCK, file)) == GSM_REAL_BLOCK) {
        size_t pairs = sdr_dsp_convert_iq(raw, GSM_REAL_BLOCK, i, q, mag,
                                          GSM_REAL_BLOCK / 2);
        struct gsm_sch_result result;
        if (gsm_sch_decode(i, q, pairs, 2000000.0, 400000.0, GSM_OPT_FILTER|GSM_OPT_FINECFO|GSM_OPT_TRELLIS, &result, NULL)) {
            if (decoded == 0) {
                first_fn = result.frame_number;
                min_t1 = max_t1 = result.t1;
            } else {
                if (result.frame_number <= last_fn)
                    ordered = 0;
                if (result.t1 < min_t1) min_t1 = result.t1;
                if (result.t1 > max_t1) max_t1 = result.t1;
            }
            last_fn = result.frame_number;
            decoded++;
            if (result.bsic == bsic && result.ncc == ncc &&
                result.bcc == bcc)
                bsic_ok++;
        }
    }
    fclose(file);
    free(raw);
    free(i);
    free(q);
    free(mag);

    if (decoded < min_decoded) {
        fprintf(stderr, "%s: only %d blocks decoded (expected >=%d)\n",
                path, decoded, min_decoded);
        failures++;
    }
    if (bsic_ok < decoded) {
        fprintf(stderr, "%s: %d/%d decodes had BSIC %d (NCC %d, BCC %d)\n",
                path, bsic_ok, decoded, bsic, ncc, bcc);
        failures++;
    }

    /* The frame number must track real time. A wrong field layout in
       sch_parse still round-trips against an encoder that shares it -- that
       is how the old layout survived -- so this is the check that actually
       pins the bit positions. The capture is ~2 s = ~433 frames, and the SCH
       burst advances monotonically, so the decoded frame numbers must climb
       and must not span more than the capture itself. */
    if (decoded > 1) {
        if (!ordered) {
            fprintf(stderr, "%s: frame numbers are not increasing\n", path);
            failures++;
        }
        int span = last_fn - first_fn;
        if (span < 0 || span > 450) {
            fprintf(stderr,
                    "%s: frame numbers span %d frames across a ~433-frame "
                    "capture\n", path, span);
            failures++;
        }
        /* T1 steps once per 1326 frames (~6.1 s), so a 2 s capture sees one
           value or two adjacent ones. */
        if (max_t1 - min_t1 > 1) {
            fprintf(stderr, "%s: T1 spans %d..%d over ~2 s\n", path,
                    min_t1, max_t1);
            failures++;
        }
    }
}

int main(void) {
    test_cellular_calibration();
    test_fcch_detection();
    test_sch_decode();
    test_sch_synthetic_channel();
    /* Two independent cells: the layout fix was derived from ARFCN 69, so
       ARFCN 73 is the capture that checks it generalises. */
    /* The decode-count floors are deliberately high: both captures decode
       every block, and a slack floor would hide a sensitivity regression.
       Widening the SCH carrier search back to +-100 kHz drops ARFCN 73 from
       30 to 16, which these floors catch. */
    check_real_capture("testfiles/gsm_arfcn_69.bin", 59, 7, 3, 25);
    check_real_capture("testfiles/gsm_arfcn_73.bin", 56, 7, 0, 25);

    if (failures) {
        fprintf(stderr, "%d gsm_dsp check(s) failed\n", failures);
        return 1;
    }
    puts("gsm_dsp checks passed");
    return 0;
}
