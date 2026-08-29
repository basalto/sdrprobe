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

/* Pack a known BSIC + reduced frame number into the 25 SCH information bits,
   matching the decoder's bit layout. */
static void sch_pack_info(int bsic, int t1, int t2, int t3p, uint8_t d[25]) {
    int idx = 0;
    for (int i = 5; i >= 0; i--)
        d[idx++] = (uint8_t)((bsic >> i) & 1);
    for (int i = 10; i >= 0; i--)
        d[idx++] = (uint8_t)((t1 >> i) & 1);
    for (int i = 4; i >= 0; i--)
        d[idx++] = (uint8_t)((t2 >> i) & 1);
    for (int i = 2; i >= 0; i--)
        d[idx++] = (uint8_t)((t3p >> i) & 1);
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
    sch_pack_info(bsic, t1, t2, t3p, info);
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
               (size_t)gsm_sch_decode(i, q, count, sample_rate, carrier_offset, &result, NULL),
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
               (size_t)gsm_sch_decode(i, q, count, sample_rate, carrier_offset, &result, NULL),
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
static void test_sch_real_capture(void) {
    FILE *file = fopen("testfiles/gsm_arfcn_69.bin", "rb");
    if (!file) {
        puts("  (skipping real-capture SCH test: testfiles/gsm_arfcn_69.bin "
             "absent)");
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

    int decoded = 0, bsic_ok = 0;
    size_t got;
    while ((got = fread(raw, 1, GSM_REAL_BLOCK, file)) == GSM_REAL_BLOCK) {
        size_t pairs = sdr_dsp_convert_iq(raw, GSM_REAL_BLOCK, i, q, mag,
                                          GSM_REAL_BLOCK / 2);
        struct gsm_sch_result result;
        if (gsm_sch_decode(i, q, pairs, 2000000.0, 400000.0, &result, NULL)) {
            decoded++;
            if (result.bsic == 45 && result.ncc == 5 && result.bcc == 5)
                bsic_ok++;
        }
    }
    fclose(file);
    free(raw);
    free(i);
    free(q);
    free(mag);

    if (decoded < 5) {
        fprintf(stderr, "real-capture SCH: only %d blocks decoded (expected >=5)\n",
                decoded);
        failures++;
    }
    if (bsic_ok < decoded) {
        fprintf(stderr, "real-capture SCH: %d/%d decodes had BSIC 45 (NCC 5, BCC 5)\n",
                bsic_ok, decoded);
        failures++;
    }
}

int main(void) {
    test_cellular_calibration();
    test_fcch_detection();
    test_sch_decode();
    test_sch_real_capture();

    if (failures) {
        fprintf(stderr, "%d gsm_dsp check(s) failed\n", failures);
        return 1;
    }
    puts("gsm_dsp checks passed");
    return 0;
}
