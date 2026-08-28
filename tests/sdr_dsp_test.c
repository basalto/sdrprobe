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

static void test_conversion(void) {
    const uint8_t bytes[] = {127, 128, 255};
    float i[2] = {99.0f, 99.0f};
    float q[2] = {99.0f, 99.0f};
    float magnitude[2] = {99.0f, 99.0f};
    size_t pairs = sdr_dsp_convert_iq(bytes, sizeof(bytes), i, q,
                                      magnitude, 2);

    check_size("odd byte count", pairs, 1);
    check_close("centered I", i[0], -0.5f, 0.0001f);
    check_close("centered Q", q[0], 0.5f, 0.0001f);
    check_close("I/Q magnitude", magnitude[0], sqrtf(0.5f), 0.0001f);
    check_close("unmatched byte untouched", i[1], 99.0f, 0.0001f);
    check_size("empty conversion",
               sdr_dsp_convert_iq(bytes, 0, i, q, magnitude, 2), 0);
    check_size("capacity bound",
               sdr_dsp_convert_iq(bytes, sizeof(bytes), i, q,
                                  magnitude, 0), 0);
}

static void test_standard_block(void) {
    const size_t byte_count = 16 * 16384;
    const size_t pair_count = byte_count / 2;
    uint8_t *bytes = malloc(byte_count);
    float *i = malloc(pair_count * sizeof(*i));
    float *q = malloc(pair_count * sizeof(*q));
    float *magnitude = malloc(pair_count * sizeof(*magnitude));

    if (!bytes || !i || !q || !magnitude) {
        fprintf(stderr, "standard-block allocation failed\n");
        exit(2);
    }
    for (size_t n = 0; n < byte_count; n += 2) {
        bytes[n] = 127;
        bytes[n + 1] = 128;
    }
    check_size("standard-block conversion",
               sdr_dsp_convert_iq(bytes, byte_count, i, q, magnitude,
                                  pair_count),
               pair_count);
    check_close("standard-block final I", i[pair_count - 1], -0.5f,
                0.0001f);
    check_close("standard-block final Q", q[pair_count - 1], 0.5f,
                0.0001f);

    free(bytes);
    free(i);
    free(q);
    free(magnitude);
}

static void test_peak_bins(void) {
    const float magnitudes[] = {1, 5, 2, 3, 9, 4, 8};
    float peaks[3] = {0};
    size_t bins = sdr_dsp_peak_bins(magnitudes, 7, peaks, 3);

    check_size("peak bin count", bins, 3);
    check_close("peak bin 0", peaks[0], 5.0f, 0.0001f);
    check_close("peak bin 1", peaks[1], 9.0f, 0.0001f);
    check_close("peak bin 2", peaks[2], 8.0f, 0.0001f);
}

static void test_dc_removal(void) {
    float i[] = {3.0f, 5.0f, 7.0f, 9.0f};
    float q[] = {-4.0f, -2.0f, 0.0f, 2.0f};

    sdr_dsp_remove_dc(i, q, 4);
    check_close("DC-removed I mean",
                (i[0] + i[1] + i[2] + i[3]) / 4.0f, 0.0f, 0.0001f);
    check_close("DC-removed Q mean",
                (q[0] + q[1] + q[2] + q[3]) / 4.0f, 0.0f, 0.0001f);
    check_close("DC-removed I shape", i[0], -3.0f, 0.0001f);
    check_close("DC-removed Q shape", q[3], 3.0f, 0.0001f);
    sdr_dsp_remove_dc(i, q, 0);
}

static void test_signal_stats(void) {
    float i[] = {1.0f, 2.0f, 3.0f, 4.0f, 127.5f};
    float q[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float magnitude[] = {1.0f, 2.0f, 3.0f, 4.0f, 127.5f};
    float workspace[5];
    struct sdr_signal_stats stats;

    check_size("signal stats available",
               (size_t)sdr_dsp_signal_stats(i, q, magnitude, 5,
                                            workspace, &stats), 1);
    check_close("noise p10", stats.noise_magnitude, 1.0f, 0.0001f);
    check_close("signal p99.5", stats.signal_magnitude, 127.5f, 0.0001f);
    check_close("estimated SNR", stats.snr_db,
                20.0f * log10f(127.5f), 0.0001f);
    check_close("clipping pairs", stats.clipping_percent, 20.0f, 0.0001f);
    check_close("full-scale headroom", stats.headroom_db, 0.0f, 0.0001f);
    check_size("empty signal stats",
               (size_t)sdr_dsp_signal_stats(i, q, magnitude, 0,
                                            workspace, &stats), 0);
}

static void fill_tone(float *i, float *q, size_t offset, float amplitude,
                      int bin) {
    for (int n = 0; n < SDR_DSP_FFT_SIZE; n++) {
        float phase = 2.0f * PI_F * (float)bin * (float)n /
                      (float)SDR_DSP_FFT_SIZE;
        i[offset + (size_t)n] = 127.5f * amplitude * cosf(phase);
        q[offset + (size_t)n] = 127.5f * amplitude * sinf(phase);
    }
}

static void test_spectrum(void) {
    enum { WINDOWS = 2, TONE_BIN = 37 };
    const size_t count = WINDOWS * SDR_DSP_FFT_SIZE;
    float *i = calloc(count, sizeof(*i));
    float *q = calloc(count, sizeof(*q));
    float average[SDR_DSP_FFT_SIZE];
    float maximum[SDR_DSP_FFT_SIZE];
    struct sdr_dsp dsp;

    if (!i || !q) {
        fprintf(stderr, "allocation failed\n");
        exit(2);
    }

    sdr_dsp_init(&dsp);
    fill_tone(i, q, 0, 1.0f, TONE_BIN);
    fill_tone(i, q, SDR_DSP_FFT_SIZE, 0.5f, TONE_BIN);

    check_size("spectrum window count",
               (size_t)sdr_dsp_spectrum(&dsp, i, q, count,
                                        average, maximum), WINDOWS);

    int shifted_bin = SDR_DSP_FFT_SIZE / 2 + TONE_BIN;
    check_close("unit-tone peak dBFS", maximum[shifted_bin],
                0.0f, 0.002f);
    check_close("linear power average before dB", average[shifted_bin],
                10.0f * log10f(0.625f), 0.002f);

    check_size("short spectrum",
               (size_t)sdr_dsp_spectrum(&dsp, i, q,
                                        SDR_DSP_FFT_SIZE - 1,
                                        average, maximum), 0);

    free(i);
    free(q);
}

static void test_channel_powers(void) {
    const size_t bins = SDR_DSP_FFT_SIZE;
    const double lower = 957000000.0;
    const double upper = 959000000.0;
    const double base = 935000000.0;
    const double spacing = 200000.0;
    float *spectrum = malloc(bins * sizeof(*spectrum));
    float powers[125];

    if (!spectrum) {
        fprintf(stderr, "channel-powers allocation failed\n");
        exit(2);
    }
    for (int a = 0; a < 125; a++)
        powers[a] = -300.0f;

    double bin_width = (upper - lower) / (double)bins;
    for (size_t n = 0; n < bins; n++) {
        double frequency = lower + bin_width * (double)n;
        /* Strong energy only inside grid index 117 (958.4 MHz +/- 100 kHz). */
        spectrum[n] = (fabs(frequency - 958400000.0) < 100000.0)
                          ? -40.0f
                          : -90.0f;
    }

    int written = sdr_dsp_channel_powers(spectrum, bins, lower, upper,
                                         lower, upper, base, spacing,
                                         1, 124, powers);
    /* Indices 111..119 have their full 200 kHz inside the span. */
    check_size("channel-powers written", (size_t)written, 9);
    check_close("channel-powers index 117", powers[117], -40.0f, 0.1f);
    check_close("channel-powers index 115", powers[115], -90.0f, 0.1f);
    check_close("channel-powers untouched edge", powers[110], -300.0f, 0.1f);

    /* Accept window can restrict which channels are filled. */
    for (int a = 0; a < 125; a++)
        powers[a] = -300.0f;
    sdr_dsp_channel_powers(spectrum, bins, lower, upper,
                           958300000.0, 958500000.0, base, spacing,
                           1, 124, powers);
    check_close("accept-window index 117", powers[117], -40.0f, 0.1f);
    check_close("accept-window index 116 excluded", powers[116], -300.0f,
                0.1f);

    free(spectrum);
}

int main(void) {
    test_conversion();
    test_standard_block();
    test_peak_bins();
    test_dc_removal();
    test_signal_stats();
    test_spectrum();
    test_channel_powers();

    if (failures) {
        fprintf(stderr, "%d sdr_dsp check(s) failed\n", failures);
        return 1;
    }
    puts("sdr_dsp checks passed");
    return 0;
}
