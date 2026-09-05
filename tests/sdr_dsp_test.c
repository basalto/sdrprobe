#include "check.h"
#include "sdr_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979323846f

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
               (size_t)sdr_dsp_spectrum(&dsp, i, q, count, SDR_DSP_FFT_SIZE,
                                        average, maximum), WINDOWS);

    int shifted_bin = SDR_DSP_FFT_SIZE / 2 + TONE_BIN;
    check_close("unit-tone peak dBFS", maximum[shifted_bin],
                0.0f, 0.002f);
    check_close("linear power average before dB", average[shifted_bin],
                10.0f * log10f(0.625f), 0.002f);

    check_size("short spectrum",
               (size_t)sdr_dsp_spectrum(&dsp, i, q,
                                        SDR_DSP_FFT_SIZE - 1,
                                        SDR_DSP_FFT_SIZE,
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

/*
 * The percentiles must be exactly what sorting the block would have produced,
 * not merely close: they are read off the HUD, they gate the calibration
 * colours, and the point of selecting instead of sorting is speed, not a
 * different answer. So compare against a sorted reference over the input
 * shapes that break a careless selection -- heavy duplicates above all, since
 * magnitudes come from 8-bit samples and repeat in their thousands.
 */
static int compare_float_test(const void *left, const void *right) {
    float a = *(const float *)left;
    float b = *(const float *)right;
    return (a > b) - (a < b);
}

static void check_percentiles_match_sorting(const char *shape,
                                            const float *magnitude,
                                            size_t count) {
    float *work = malloc(count * sizeof(*work));
    float *sorted = malloc(count * sizeof(*sorted));
    float *zeros = calloc(count, sizeof(*zeros));
    struct sdr_signal_stats stats;
    char name[96];

    if (!work || !sorted || !zeros) {
        fprintf(stderr, "percentile check allocation failed\n");
        exit(2);
    }
    memcpy(sorted, magnitude, count * sizeof(*sorted));
    qsort(sorted, count, sizeof(*sorted), compare_float_test);

    size_t noise_rank = (size_t)ceil(0.10 * (double)count);
    size_t signal_rank = (size_t)ceil(0.995 * (double)count);
    if (noise_rank < 1)
        noise_rank = 1;
    if (signal_rank > count)
        signal_rank = count;

    if (!sdr_dsp_signal_stats(magnitude, zeros, magnitude, count, work,
                              &stats)) {
        check_msg(0, "%s: signal stats refused %zu samples\n", shape, count);
    } else {
        snprintf(name, sizeof(name), "%s (%zu): p10 matches sorting", shape,
                 count);
        check_close(name, stats.noise_magnitude, sorted[noise_rank - 1], 0.0f);
        snprintf(name, sizeof(name), "%s (%zu): p99.5 matches sorting", shape,
                 count);
        check_close(name, stats.signal_magnitude, sorted[signal_rank - 1],
                    0.0f);
    }
    free(work);
    free(sorted);
    free(zeros);
}

static void test_percentiles_without_sorting(void) {
    const size_t big = 131072;   /* one block */
    float *values = malloc(big * sizeof(*values));
    size_t sizes[] = { 1, 2, 3, 7, 1000 };

    if (!values) {
        fprintf(stderr, "percentile allocation failed\n");
        exit(2);
    }

    /* The real shape: magnitudes of 8-bit samples, so a few hundred distinct
       values across a hundred thousand samples. */
    for (size_t i = 0; i < big; i++)
        values[i] = (float)(rand() % 180) + (float)(rand() % 4) * 0.25f;
    check_percentiles_match_sorting("block of duplicates", values, big);

    /* The shapes a median-of-three pivot exists for. */
    for (size_t i = 0; i < big; i++)
        values[i] = (float)i * 0.001f;
    check_percentiles_match_sorting("already sorted", values, big);
    for (size_t i = 0; i < big; i++)
        values[i] = (float)(big - i) * 0.001f;
    check_percentiles_match_sorting("reverse sorted", values, big);

    /* Every element identical: the case that makes a two-way split
       quadratic. */
    for (size_t i = 0; i < big; i++)
        values[i] = 42.0f;
    check_percentiles_match_sorting("all equal", values, big);

    /* Short blocks, where the rank clamps bite. */
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        for (size_t i = 0; i < sizes[s]; i++)
            values[i] = (float)((i * 37) % 91) + 0.5f;
        check_percentiles_match_sorting("short block", values, sizes[s]);
    }
    free(values);
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

    struct sdr_peak_gate gate = { 6.0f, 0.0f, 20.0f };
    int found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                                   workspace, peaks, 8);
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
        check_msg(wide > narrow,
                  "occupied width did not follow hump width: %d vs %d\n", wide,
                  narrow);
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

    struct sdr_peak_gate gate = { 6.0f, 0.0f, 20.0f };
    int found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                                   workspace, peaks, 8);
    check_int("both neighbours found", found, 2);
    if (found == 2) {
        check_int("the strong one leads", peaks[0].index, 200);
        check_int("the weak neighbour survives", peaks[1].index, 240);
    }
}

/*
 * A maximum below its own local floor is not a candidate.
 *
 * The shape is a notch inside a busy stretch: a run of carriers with one deep
 * gap in it, and a small maximum on the floor of that gap. It passes the
 * topographic gate, because there is a real descent either side of it before
 * the ground rises. Then the width walk stops inside the notch, so the hump
 * local_floor() leaves out is one bin wide, and the median it takes instead
 * comes off the carriers -- which are 12 dB above the peak.
 *
 * Reported, that is a peak standing -12.7 dB above the noise either side of
 * it, which is not a weak signal but an impossibility. A survey of
 * 24-1766 MHz produced one at 1603.219 MHz reading -3.0 dB, and a sweep two
 * days later put the same bin at -3.4 dB.
 */
static void test_a_maximum_below_its_own_floor(void) {
    static float power[SURVEY_BINS];
    static float workspace[SURVEY_BINS];
    struct sdr_peak peaks[8];
    int i, found;

    for (i = 0; i < SURVEY_BINS; i++)
        power[i] = -72.0f;
    for (i = 90; i <= 125; i++)
        power[i] = -35.0f;
    power[105] = -70.0f;
    power[106] = -47.7f;
    power[107] = -70.0f;

    {
        struct sdr_peak_gate gate = { 8.0f, 0.0f, 20.0f };
        found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                                   workspace, peaks, 8);
    }
    for (i = 0; i < found; i++) {
        check_msg(peaks[i].index != 106,
                  "the floor of a notch was reported as a peak, %.1f dB "
                  "above a floor measured on its neighbours\n",
                  peaks[i].prominence_db);
        check_msg(peaks[i].prominence_db > 0.0f,
                  "peak at bin %d stands %.1f dB above its floor\n",
                  peaks[i].index, peaks[i].prominence_db);
    }
    /* The carriers either side of the notch are still found: what was removed
       is the gap between them, not a level of signal. */
    check_int("the carriers either side survive", found, 2);
}

/*
 * A ripple on a multiplex is not a signal, and what removes it is that it has
 * no end.
 *
 * The shape is what a survey of a television band is full of: contiguous flat
 * multiplexes with ripple on top. Each ripple is a genuine local maximum with
 * a genuine descent either side, so it clears the topographic bar -- and there
 * is no point within reach in either direction where it falls 20 dB below its
 * own peak, because the multiplexes go on. A candidate whose extent has no end
 * is a feature inside something larger, not a signal in its own right.
 *
 * Measured on air: a 470-690 MHz sweep reported twenty such bumps at 19 to
 * 27 dB "above their floor", every one of them inside a DVB-T channel's 8 MHz.
 */
static void test_a_ripple_on_a_multiplex_is_not_a_signal(void) {
    static float power[SURVEY_BINS];
    static float workspace[SURVEY_BINS];
    struct sdr_peak peaks[16];
    struct sdr_peak_gate gate = { 8.0f, 8.0f, 20.0f };
    int i, found;

    for (i = 0; i < SURVEY_BINS; i++)
        power[i] = -50.0f;
    for (i = 20; i <= 580; i += 20) {
        power[i - 1] = -62.0f;
        power[i] = -49.0f;
        power[i + 1] = -62.0f;
    }

    found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                               workspace, peaks, 16);
    check_int("the ripple is not a list of signals", found, 0);

    /*
     * And the same bump does count when the multiplex ends. Quiet spectrum
     * either side gives it a -20 dB point, so its extent closes and it is a
     * carrier standing above a floor that is really its own.
     */
    for (i = 0; i < SURVEY_BINS; i++)
        power[i] = -95.0f;
    for (i = 280; i <= 320; i++)
        power[i] = -50.0f;
    power[299] = -62.0f;
    power[300] = -49.0f;
    power[301] = -62.0f;
    found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                               workspace, peaks, 16);
    check_true("a bump on a signal that ends is found", found >= 1);
}

/*
 * The cost of that rule, stated rather than discovered.
 *
 * A carrier standing less than `bandwidth_db` above its own noise has no
 * -bandwidth_db point either, so its extent does not close and it is not
 * reported -- even alone in quiet spectrum. ADR-0013 recorded this as the
 * accident's consequence and ADR-0017 keeps it deliberately, because the same
 * rule is what removes the ripples above and nothing measured tells the two
 * apart. It is a real loss: a bare tone at 102.4 MHz, 18 dB above its floor,
 * is found by an 88-108 MHz sweep whose walk happened to terminate and lost by
 * one whose walk did not.
 *
 * The way out is a narrower range, which gives finer bins and a deeper hold --
 * or the trough walk `.scratch/survey-extent/` is about.
 */
static void test_a_carrier_with_no_end_is_not_reported(void) {
    static float power[SURVEY_BINS];
    static float workspace[SURVEY_BINS];
    struct sdr_peak peaks[8];
    struct sdr_peak_gate gate = { 8.0f, 8.0f, 20.0f };
    int i, found;

    for (i = 0; i < SURVEY_BINS; i++)
        power[i] = -95.0f;
    /* Twelve decibels above the noise: over both bars, under the 20 dB the
       extent walk looks for. */
    place_hump(power, 300, 5, -83.0f, -95.0f);
    found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                               workspace, peaks, 8);
    check_int("twelve decibels of headroom is not enough", found, 0);

    /* Give it the headroom and it is found, at the level it actually stands. */
    place_hump(power, 300, 5, -70.0f, -95.0f);
    found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                               workspace, peaks, 8);
    check_int("twenty-five is", found, 1);
    if (found == 1) {
        check_int("at the right bin", peaks[0].index, 300);
        check_close("standing where it actually stands",
                    peaks[0].prominence_db, 25.0, 1.5);
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

    struct sdr_peak_gate gate = { 6.0f, 0.0f, 20.0f };
    int found = sdr_dsp_find_peaks(power, SURVEY_BINS, SURVEY_SENTINEL, &gate,
                                   workspace, peaks, 8);
    check_int("humps either side of a gap stay separate", found, 2);
}

/* Characterising one carrier: a bump of known centre and width in a spectrum
   whose bins map to real frequencies. */
/*
 * Measuring a carrier that stands close to its own noise.
 *
 * The survey measures whichever candidate is selected, and used to answer
 * "nothing measurable at that frequency now" for anything under about 20 dB
 * over a smooth floor -- on a carrier plainly visible in the chart above it.
 * The cause was its own first pass: with no -bandwidth_db point to walk to,
 * the width ran to the ends of the array and the floor window was left nothing
 * outside the carrier to measure, so the floor came back as not-a-number and
 * the measurement was refused.
 */
static void test_measuring_a_carrier_close_to_its_noise(void) {
    enum { BINS = 2048 };
    static float spectrum[BINS];
    static float workspace[BINS];
    struct sdr_carrier_report report;
    const float above[] = { 40.0f, 25.0f, 18.0f, 12.0f, 8.0f };

    for (size_t c = 0; c < sizeof(above) / sizeof(*above); c++) {
        char name[96];

        /* A dead flat floor, which is the hard case: nothing in the noise
           will stop a walk that has no threshold to stop it. */
        for (int i = 0; i < BINS; i++)
            spectrum[i] = -80.0f;
        for (int d = -3; d <= 3; d++)
            spectrum[BINS / 2 + d] =
                -80.0f + above[c] * (float)(0.5 * (1.0 + cos(PI_F * d / 4.0)));

        snprintf(name, sizeof(name), "%.0f dB over the floor is measurable",
                 (double)above[c]);
        if (!sdr_dsp_characterise_carrier(spectrum, BINS, 0.0, (double)BINS,
                                          0.5, 20.0, 20.0f, workspace,
                                          &report)) {
            check_msg(0, "%s\n", name);
            continue;
        }
        snprintf(name, sizeof(name), "%.0f dB over the floor: floor",
                 (double)above[c]);
        check_close(name, report.floor_dbfs, -80.0, 0.5);
        snprintf(name, sizeof(name), "%.0f dB over the floor: prominence",
                 (double)above[c]);
        check_close(name, report.prominence_db, above[c], 0.5);
        /* And the width is the carrier's, not the distance to wherever the
           noise first dipped: seven bins were painted. */
        snprintf(name, sizeof(name), "%.0f dB over the floor: width",
                 (double)above[c]);
        check_msg(report.bandwidth_hz <= 9.0,
                  "%s: %.0f bins\n", name, report.bandwidth_hz);
    }
}

/*
 * And nothing measured may claim the whole spectrum. The floor is taken
 * outside the carrier, so a carrier that spreads far enough leaves nothing to
 * take it from -- which is how the case above went wrong.
 */
static void test_a_measurement_cannot_swallow_the_spectrum(void) {
    enum { BINS = 2048 };
    static float spectrum[BINS];
    static float workspace[BINS];
    struct sdr_carrier_report report;

    /* A shelf across almost everything, with one bump on it: the bump is the
       carrier, the shelf is not its bandwidth. */
    for (int i = 0; i < BINS; i++)
        spectrum[i] = i > 40 && i < BINS - 40 ? -60.0f : -95.0f;
    for (int d = -3; d <= 3; d++)
        spectrum[BINS / 2 + d] =
            -60.0f + 25.0f * (float)(0.5 * (1.0 + cos(PI_F * d / 4.0)));

    check_msg(sdr_dsp_characterise_carrier(spectrum, BINS, 0.0, (double)BINS,
                                           0.5, 20.0, 20.0f, workspace,
                                           &report),
              "the bump on the shelf could not be measured\n");
    check_msg(report.bandwidth_hz <= (double)BINS / 8.0,
              "the measurement claims %.0f of %d bins\n", report.bandwidth_hz,
              BINS);
}

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
        check_msg(report.prominence_db >= 40.0f,
                  "carrier prominence only %.1f dB\n", report.prominence_db);
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


/*
 * The transform at every size it will be asked for.
 *
 * It has only ever run at 2048. It is a radix-2 loop and it ought to
 * generalise, and "ought to" is what .claude/skills/dsp-validation exists to
 * distrust: a bit-reversal or a butterfly that is right for one length and
 * wrong for another draws a spectrum that looks entirely plausible.
 *
 * So a tone of known amplitude at a known bin, through every size, asserting
 * it comes back in the right bin at the right level with its neighbours far
 * below -- and the awkward cases beside it, because an off-by-one shows at
 * bin zero and between two bins where it does not in the middle of one.
 */
static void test_the_transform_at_every_size(void) {
    static const int sizes[] = { 256, 512, 1024, 2048, 4096, 8192, 16384 };
    unsigned s;

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        int size = sizes[s];
        size_t count = (size_t)size * 4;   /* four windows of it */
        float *i = malloc(count * sizeof(float));
        float *q = malloc(count * sizeof(float));
        float *average = malloc((size_t)size * sizeof(float));
        float *maximum = malloc((size_t)size * sizeof(float));
        struct sdr_dsp dsp;
        int bin = size / 8;          /* somewhere unremarkable */
        int shifted = size / 2 + bin;
        int windows, k, loud = 0;

        if (!i || !q || !average || !maximum)
            exit(2);
        for (k = 0; k < (int)count; k++) {
            double angle = 2.0 * M_PI * (double)bin * (double)k /
                           (double)size;
            i[k] = (float)(127.5 * cos(angle));
            q[k] = (float)(127.5 * sin(angle));
        }
        sdr_dsp_init(&dsp);
        windows = sdr_dsp_spectrum(&dsp, i, q, count, size, average, maximum);

        check_msg(windows == 4, "size %d averaged %d windows, not 4\n", size,
                  windows);
        check_msg(fabsf(maximum[shifted]) < 0.05f,
                  "size %d: a full-scale tone reads %.3f dBFS, not 0\n", size,
                  maximum[shifted]);
        /* And nothing else does. A transform that scrambles its output puts
           energy where there is none, which a peak check alone misses. */
        for (k = 0; k < size; k++) {
            if (k >= shifted - 2 && k <= shifted + 2)
                continue;
            if (average[k] > -40.0f)
                loud++;
        }
        check_msg(loud == 0,
                  "size %d: %d bins away from the tone are above -40 dBFS\n",
                  size, loud);

        free(i); free(q); free(average); free(maximum);
    }
}

/*
 * The two places an off-by-one hides: a tone at bin zero, which after the
 * shift is the middle of the array, and one exactly between two bins, which
 * must split between them rather than land in one.
 */
static void test_the_awkward_bins(void) {
    static const int sizes[] = { 512, 2048, 8192 };
    unsigned s;

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        int size = sizes[s];
        size_t count = (size_t)size * 2;
        float *i = malloc(count * sizeof(float));
        float *q = malloc(count * sizeof(float));
        float *average = malloc((size_t)size * sizeof(float));
        float *maximum = malloc((size_t)size * sizeof(float));
        struct sdr_dsp dsp;
        int k;

        if (!i || !q || !average || !maximum)
            exit(2);

        /* Direct current: bin zero, which the shift puts in the middle. */
        for (k = 0; k < (int)count; k++) {
            i[k] = 127.5f;
            q[k] = 0.0f;
        }
        sdr_dsp_init(&dsp);
        sdr_dsp_spectrum(&dsp, i, q, count, size, average, maximum);
        check_msg(fabsf(maximum[size / 2]) < 0.05f,
                  "size %d: a constant reads %.3f dBFS at the middle bin\n",
                  size, maximum[size / 2]);

        /* Half a bin off: the energy must appear in both neighbours rather
           than all in one. */
        for (k = 0; k < (int)count; k++) {
            double angle = 2.0 * M_PI * ((double)(size / 8) + 0.5) *
                           (double)k / (double)size;
            i[k] = (float)(127.5 * cos(angle));
            q[k] = (float)(127.5 * sin(angle));
        }
        sdr_dsp_init(&dsp);
        sdr_dsp_spectrum(&dsp, i, q, count, size, average, maximum);
        {
            int lower = size / 2 + size / 8;
            check_msg(maximum[lower] > -6.0f && maximum[lower + 1] > -6.0f,
                      "size %d: a tone between bins gave %.1f and %.1f dBFS "
                      "either side\n", size, maximum[lower],
                      maximum[lower + 1]);
        }
        free(i); free(q); free(average); free(maximum);
    }
}

/* And a size the transform cannot do is refused rather than run. */
static void test_sizes_it_will_not_do(void) {
    check_true("a power of two inside the range",
               sdr_dsp_fft_size_valid(2048));
    check_true("the smallest", sdr_dsp_fft_size_valid(SDR_DSP_FFT_MIN));
    check_true("and the largest", sdr_dsp_fft_size_valid(SDR_DSP_FFT_MAX));
    check_true("not one that is not a power of two",
               !sdr_dsp_fft_size_valid(3000));
    check_true("nor one too large for the buffers",
               !sdr_dsp_fft_size_valid(SDR_DSP_FFT_MAX * 2));
    check_true("nor too small to mean anything",
               !sdr_dsp_fft_size_valid(64));
    check_true("nor zero", !sdr_dsp_fft_size_valid(0));
    check_true("nor negative", !sdr_dsp_fft_size_valid(-2048));
    {
        struct sdr_dsp dsp;
        static float i[4096], q[4096], average[4096], maximum[4096];
        sdr_dsp_init(&dsp);
        check_int("and a bad size returns nothing rather than nonsense",
                  sdr_dsp_spectrum(&dsp, i, q, 4096, 3000, average, maximum),
                  0);
    }
}


/*
 * The sizes a reader is offered, and that every one of them is a size the
 * transform will actually do -- a list offering a size that is refused is a
 * control that does nothing.
 */
static void test_the_sizes_offered(void) {
    int i, previous = 0;

    check_int("the smallest offered is the smallest allowed",
              sdr_dsp_fft_choice(0), SDR_DSP_FFT_MIN);
    check_int("and the largest is the largest",
              sdr_dsp_fft_choice(SDR_DSP_FFT_CHOICES - 1), SDR_DSP_FFT_MAX);
    check_int("the default is one of them",
              sdr_dsp_fft_choice_of(SDR_DSP_FFT_SIZE) >= 0, 1);

    for (i = 0; i < SDR_DSP_FFT_CHOICES; i++) {
        int size = sdr_dsp_fft_choice(i);
        check_msg(sdr_dsp_fft_size_valid(size),
                  "choice %d is %d, which the transform refuses\n", i, size);
        check_msg(size > previous, "choice %d is not larger than the one "
                  "before it\n", i);
        check_msg(sdr_dsp_fft_choice_of(size) == i,
                  "choice %d does not find itself again\n", i);
        previous = size;
    }
    check_int("past the end is nothing",
              sdr_dsp_fft_choice(SDR_DSP_FFT_CHOICES), 0);
    check_int("and before the start", sdr_dsp_fft_choice(-1), 0);
    check_int("a size not on the list is not on it",
              sdr_dsp_fft_choice_of(3000), -1);
}

int main(void) {
    test_conversion();
    test_standard_block();
    test_peak_bins();
    test_dc_removal();
    test_signal_stats();
    test_spectrum();
    test_channel_powers();
    test_percentiles_without_sorting();
    test_find_peaks();
    test_peak_beside_a_strong_neighbour();
    test_a_maximum_below_its_own_floor();
    test_a_ripple_on_a_multiplex_is_not_a_signal();
    test_a_carrier_with_no_end_is_not_reported();
    test_sentinel_splits_humps();
    test_measuring_a_carrier_close_to_its_noise();
    test_a_measurement_cannot_swallow_the_spectrum();
    test_characterise_carrier();

    test_the_transform_at_every_size();
    test_the_awkward_bins();
    test_sizes_it_will_not_do();
    test_the_sizes_offered();

    return check_report("generic DSP core");
}
