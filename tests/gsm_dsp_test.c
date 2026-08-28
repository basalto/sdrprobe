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

int main(void) {
    test_cellular_calibration();
    test_fcch_detection();

    if (failures) {
        fprintf(stderr, "%d gsm_dsp check(s) failed\n", failures);
        return 1;
    }
    puts("gsm_dsp checks passed");
    return 0;
}
