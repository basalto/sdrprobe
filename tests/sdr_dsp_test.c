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

static void check_int(const char *name, long actual, long expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %ld, expected %ld\n", name, actual, expected);
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

/* A survey array with three humps of known place, width and height over a
   -95 dBFS floor. */
#define SURVEY_BINS 600
#define SURVEY_SENTINEL (-300.0f)

static void place_hump(float *power, int centre, int half_width, float peak,
                       float floor_level) {
    for (int i = centre - half_width; i <= centre + half_width; i++) {
        if (i < 0 || i >= SURVEY_BINS)
            continue;
        double away = fabs((double)(i - centre)) / (double)(half_width + 1);
        float level = (float)(peak - (peak - floor_level) * away * away);
        if (level > power[i])
            power[i] = level;
    }
}

static void test_find_peaks(void) {
    static float power[SURVEY_BINS];
    static float workspace[SURVEY_BINS];
    struct sdr_peak peaks[8];

    for (int i = 0; i < SURVEY_BINS; i++)
        power[i] = -95.0f;
    place_hump(power, 100, 4, -40.0f, -95.0f);
    place_hump(power, 300, 8, -55.0f, -95.0f);
    place_hump(power, 450, 2, -70.0f, -95.0f);

    int found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, 6.0f,
                                   20.0f, workspace, peaks, 8);
    check_int("peaks found", found, 3);
    if (found == 3) {
        check_int("strongest peak bin", peaks[0].index, 100);
        check_int("second peak bin", peaks[1].index, 300);
        check_int("third peak bin", peaks[2].index, 450);
        check_close("strongest peak level", peaks[0].power_dbfs, -40.0, 0.01);
        check_close("strongest prominence", peaks[0].prominence_db, 55.0, 1.0);
        /* The -20 dB width of the widest hump is broader than the narrowest. */
        int wide = peaks[1].upper_index - peaks[1].lower_index;
        int narrow = peaks[2].upper_index - peaks[2].lower_index;
        if (wide <= narrow) {
            fprintf(stderr, "occupied width did not follow hump width: %d vs %d\n",
                    wide, narrow);
            failures++;
        }
    }
}

/* The case a mean floor gets wrong: a weak carrier sitting beside a strong
   one, which is exactly what a survey has to show. */
static void test_peak_beside_a_strong_neighbour(void) {
    static float power[SURVEY_BINS];
    static float workspace[SURVEY_BINS];
    struct sdr_peak peaks[8];

    for (int i = 0; i < SURVEY_BINS; i++)
        power[i] = -95.0f;
    place_hump(power, 200, 10, -35.0f, -95.0f);
    place_hump(power, 240, 3, -72.0f, -95.0f);

    int found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, 6.0f,
                                   20.0f, workspace, peaks, 8);
    check_int("both neighbours found", found, 2);
    if (found == 2) {
        check_int("the strong one leads", peaks[0].index, 200);
        check_int("the weak neighbour survives", peaks[1].index, 240);
    }
}

/* Unswept bins bound a hump instead of joining it, so a gap in the sweep
   cannot fuse two candidates into one. */
static void test_sentinel_splits_humps(void) {
    static float power[SURVEY_BINS];
    static float workspace[SURVEY_BINS];
    struct sdr_peak peaks[8];

    for (int i = 0; i < SURVEY_BINS; i++)
        power[i] = -95.0f;
    place_hump(power, 100, 6, -50.0f, -95.0f);
    place_hump(power, 140, 6, -50.0f, -95.0f);
    for (int i = 118; i <= 122; i++)
        power[i] = SURVEY_SENTINEL;

    int found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, 6.0f,
                                   20.0f, workspace, peaks, 8);
    check_int("humps either side of a gap stay separate", found, 2);
}

/* Characterising one carrier: a bump of known centre and width in a spectrum
   whose bins map to real frequencies. */
static void test_characterise_carrier(void) {
    const size_t bins = 2048;
    const double sample_rate = 2000000.0;
    const double centre_hz = 1000000000.0;
    const double bin_hz = sample_rate / (double)bins;
    const double carrier_hz = centre_hz + 400000.0;
    float *spectrum = malloc(bins * sizeof(*spectrum));
    float *workspace = malloc(bins * sizeof(*workspace));
    struct sdr_carrier_report report;

    if (!spectrum || !workspace) {
        fprintf(stderr, "characterise allocation failed\n");
        exit(2);
    }
    for (size_t i = 0; i < bins; i++)
        spectrum[i] = -100.0f;
    /* 100 kHz wide, centred where we said. */
    int centre_bin = (int)((carrier_hz - (centre_hz - sample_rate / 2.0)) / bin_hz);
    int half = (int)(50000.0 / bin_hz);
    for (int i = centre_bin - half; i <= centre_bin + half; i++) {
        double away = fabs((double)(i - centre_bin)) / (double)(half + 1);
        spectrum[i] = (float)(-45.0 - 25.0 * away * away);
    }

    int ok = sdr_dsp_characterise_carrier(spectrum, bins, centre_hz,
                                          sample_rate, carrier_hz, 200000.0,
                                          20.0f, workspace, &report);
    check_int("carrier characterised", ok, 1);
    if (ok) {
        check_close("carrier centre", report.centre_hz / 1e6,
                    carrier_hz / 1e6, 0.002);       /* within 2 kHz */
        check_close("carrier offset", report.offset_hz / 1e3, 400.0, 2.0);
        check_close("carrier peak", report.peak_dbfs, -45.0, 0.5);
        check_close("carrier bandwidth", report.bandwidth_hz / 1e3, 100.0,
                    10.0);                           /* within 10% */
        if (report.prominence_db < 40.0f) {
            fprintf(stderr, "carrier prominence only %.1f dB\n",
                    report.prominence_db);
            failures++;
        }
    }
    /* Nothing there: an empty window must not invent a carrier. */
    for (size_t i = 0; i < bins; i++)
        spectrum[i] = -100.0f;
    check_int("flat spectrum yields no carrier",
              sdr_dsp_characterise_carrier(spectrum, bins, centre_hz,
                                           sample_rate, carrier_hz, 200000.0,
                                           20.0f, workspace, &report), 0);
    free(spectrum);
    free(workspace);
}

int main(void) {
    test_conversion();
    test_standard_block();
    test_peak_bins();
    test_dc_removal();
    test_signal_stats();
    test_spectrum();
    test_channel_powers();
    test_find_peaks();
    test_peak_beside_a_strong_neighbour();
    test_sentinel_splits_humps();
    test_characterise_carrier();

    if (failures) {
        fprintf(stderr, "%d sdr_dsp check(s) failed\n", failures);
        return 1;
    }
    puts("sdr_dsp checks passed");
    return 0;
}
