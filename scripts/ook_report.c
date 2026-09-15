/*
 * On-off keying in a capture: where the transmissions are, and what they are.
 *
 * A tool because it kept being written. One session characterising a 434 MHz
 * SRD remote-control work wrote six variants of "load a capture, find the bursts, measure
 * them" -- a time-frequency scan, a fine carrier estimate, an envelope
 * histogram, an instantaneous-frequency histogram, a run-length histogram and
 * a run-sequence dump -- and every number landed in a transcript. The table
 * is the deliverable.
 *
 *   make probe-ook FILE_OOK=captures/x.bin AT_OOK=296800
 *
 * The shape is **find the window first, then hand it to the shipping code**,
 * and that is the finding rather than a convenience. `signal_find_carrier()`,
 * `signal_find_bursts()` and `signal_envelope_stats()` are perfectly capable
 * of describing a SRD remote control; they are simply never shown it, because each looks
 * at a fixed prefix of whatever buffer it is given:
 *
 *   SIGNAL_COARSE_PAIRS   65536   the first 32.8 ms, for the carrier search
 *   SIGNAL_BURST_SAMPLES  262144  the first 131 ms, for the burst verdict
 *
 * A transmitter that speaks once per press is not in either window. So this
 * walks the whole file, groups what it finds into transmissions, and then
 * calls those same three functions on a transmission -- and, for contrast, on
 * an equal-length window at the start of the capture, which is what
 * `probe-signal` is really reporting on.
 *
 * Not a check and not a decoder: it measures and prints, and every conclusion
 * is the reader's.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_profile.h"
#include "sdr_dsp.h"
#include "signal_probe.h"

/* One scan chunk. 1024 pairs is 512 us at 2 MS/s, which resolves the start of
   a transmission to well inside the shortest press worth finding, and gives
   the transform 1953 Hz bins at that rate. */
#define SCAN_SIZE 1024

/* How much of a transmission is loaded for the close look. Two seconds at
   2 MS/s, which is longer than any single press seen here and keeps this
   allocation-free like the rest of the probes. */
#define WINDOW_MAX_PAIRS 4000000

/* The decimated rate the envelope and run lengths are measured at. A 500 us
   chip is 100 samples at 200 kS/s, which is ample, and the box filter that
   comes with the decimation is the channel. */
#define WORK_RATE_HZ 200000.0

#define MAX_TRANSMISSIONS 256

static float window_i[WINDOW_MAX_PAIRS];
static float window_q[WINDOW_MAX_PAIRS];
static float scan_i[SCAN_SIZE];
static float scan_q[SCAN_SIZE];
static float scan_avg[SCAN_SIZE];
static float scan_max[SCAN_SIZE];
static double work_i[WINDOW_MAX_PAIRS / 4];
static double work_q[WINDOW_MAX_PAIRS / 4];
static double work_amp[WINDOW_MAX_PAIRS / 4];
static struct sdr_dsp dsp;

struct transmission {
    double start_seconds;
    double end_seconds;
    double peak_hz;       /* median of the busy chunks' peak bins */
    double refined_hz;    /* signal_find_carrier over the transmission */
    double over_floor_db;
    int chunks;
};

static struct transmission found[MAX_TRANSMISSIONS];
static int found_count;

/*
 * Samples, through the program's own byte-to-float seam.
 *
 * `sdr_dsp_convert_iq()` calls itself the one such seam and CLAUDE.md rests an
 * argument on there being exactly one: identical floats downstream means
 * identical answers by construction rather than by measurement. A diagnostic
 * that converts its own bytes is outside that argument, and the first
 * *absolute* statistic it grows -- a dBFS level, a clipping count -- reads
 * wrong with nothing on screen to say so, in the tool a reader reaches for to
 * find out why. This file did convert its own, having copied the pattern from
 * `signal_report.c`, which was already the exception (ticket 05).
 *
 * The profile supplies the container and the full scale, so the floats come
 * out in the device's own counts and are **not** normalised to +/-1. Nothing
 * here notices: every statistic this tool prints is a ratio -- a line over a
 * median, a percentile over a percentile, a coefficient of variation -- so a
 * constant scale cancels out of all of them. That was measured rather than
 * assumed, by diffing the whole report across the change.
 *
 * The DC offset is left in, because that is what the program's own sample
 * arrays carry; only the spectrum path removes it, and a tool that quietly
 * cleaned the samples would answer a question the program never asks.
 */
static struct device_profile g_device;

/*
 * The seam wants a magnitude array and refuses the call without one -- a NULL
 * there returns 0 pairs rather than skipping the work, which reads exactly
 * like an empty capture. This tool has no use for the magnitudes, so it keeps
 * a scratch buffer to satisfy the contract and throws the answer away.
 */
static float *g_magnitude_scratch;
static size_t g_magnitude_pairs;

static int convert(const unsigned char *raw, float *i_out, float *q_out,
                   size_t pairs) {
    if (pairs > g_magnitude_pairs) {
        float *grown = realloc(g_magnitude_scratch, pairs * sizeof *grown);
        if (!grown) {
            fprintf(stderr, "out of memory for %zu magnitudes\n", pairs);
            return -1;
        }
        g_magnitude_scratch = grown;
        g_magnitude_pairs = pairs;
    }
    if (sdr_dsp_convert_iq(&g_device, raw, pairs * g_device.bytes_per_pair,
                           i_out, q_out, g_magnitude_scratch, pairs) != pairs) {
        fprintf(stderr, "the sample seam refused %zu pairs\n", pairs);
        return -1;
    }
    return 0;
}

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/*
 * One chunk's peak, and how far it stands over the median bin.
 *
 * Over the *median* and not the mean, for the reason the survey's prominence
 * uses a median: one strong line drags a mean up and hides itself. On noise
 * this reads about 12 dB in a 1024-bin transform, which is what sets the
 * default bar well above it.
 */
static int chunk_peak(double sample_rate, double *peak_hz_out,
                      double *over_floor_db_out) {
    double sorted[SCAN_SIZE];
    double best = -1e9, median;
    int k, best_k = 0;

    /*
     * Full scale is the profile's, not 1.0: the samples come out of the seam
     * in the device's own counts. Passing 1.0 here while the samples are in
     * counts puts every bin about 42 dB too high, the spectrum clamps, peak
     * and median converge, and the tool reports "no transmission anywhere" on
     * a capture holding four. That is not hypothetical -- it is what this file
     * did for one build during ticket 05, and it is the absolute-scale failure
     * that ticket predicted, caught only because the whole report was diffed
     * across the change.
     */
    if (sdr_dsp_spectrum(&dsp, scan_i, scan_q, SCAN_SIZE, SCAN_SIZE,
                         (float)g_device.full_scale,
                         scan_avg, scan_max) < 0)
        return -1;
    for (k = 0; k < SCAN_SIZE; k++) {
        sorted[k] = scan_avg[k];
        if (scan_avg[k] > best) {
            best = scan_avg[k];
            best_k = k;
        }
    }
    qsort(sorted, SCAN_SIZE, sizeof(sorted[0]), compare_double);
    median = sorted[SCAN_SIZE / 2];

    /*
     * sdr_dsp_spectrum returns a shifted spectrum -- DC sits at size/2, the
     * negative frequencies below it. Reported as a signed offset from the
     * centre the samples were taken at, which is the only thing this tool is
     * ever told: the absolute frequency lives in the capture's sidecar.
     */
    *peak_hz_out = (double)(best_k - SCAN_SIZE / 2) * sample_rate / SCAN_SIZE;
    *over_floor_db_out = best - median;
    return 0;
}

/*
 * Walk the whole capture, marking the chunks that hold something, and group
 * runs of them into transmissions. `gap_seconds` is the silence that
 * separates two presses rather than dividing one -- a press here holds
 * preamble and data sections with brief gaps inside it, so a gap measured in
 * milliseconds keeps one press as one transmission.
 */
static int scan_capture(FILE *file, double sample_rate, double bar_db,
                        double gap_seconds) {
    unsigned char raw[SCAN_SIZE * 2];
    double chunk_seconds = (double)SCAN_SIZE / sample_rate;
    double peaks[MAX_TRANSMISSIONS];
    long chunk = 0, last_busy = -1;
    int in_run = 0, peak_count = 0;

    found_count = 0;
    while (fread(raw, 2, SCAN_SIZE, file) == (size_t)SCAN_SIZE) {
        double peak_hz, over_db;
        double at = (double)chunk * chunk_seconds;

        if (convert(raw, scan_i, scan_q, SCAN_SIZE) < 0)
            return -1;
        if (chunk_peak(sample_rate, &peak_hz, &over_db) < 0)
            return -1;

        if (over_db >= bar_db) {
            if (!in_run || at - (double)last_busy * chunk_seconds
                               > gap_seconds) {
                if (in_run && found_count < MAX_TRANSMISSIONS) {
                    /* Close the one before starting another. */
                    qsort(peaks, (size_t)peak_count, sizeof(peaks[0]),
                          compare_double);
                    found[found_count - 1].peak_hz =
                        peak_count ? peaks[peak_count / 2] : 0.0;
                }
                if (found_count >= MAX_TRANSMISSIONS)
                    break;
                found[found_count].start_seconds = at;
                found[found_count].chunks = 0;
                found_count++;
                peak_count = 0;
                in_run = 1;
            }
            found[found_count - 1].end_seconds = at + chunk_seconds;
            found[found_count - 1].chunks++;
            if (peak_count < MAX_TRANSMISSIONS)
                peaks[peak_count++] = peak_hz;
            last_busy = chunk;
        }
        chunk++;
    }
    if (in_run && found_count > 0) {
        qsort(peaks, (size_t)peak_count, sizeof(peaks[0]), compare_double);
        found[found_count - 1].peak_hz =
            peak_count ? peaks[peak_count / 2] : 0.0;
    }
    return 0;
}

/* Load `pairs` pairs starting at `at_seconds`. Returns how many it got. */
static size_t load_window(FILE *file, double sample_rate, double at_seconds,
                          size_t pairs) {
    unsigned char raw[65536 * 2];
    size_t got = 0;
    off_t start = (off_t)(at_seconds * sample_rate) * 2;

    if (pairs > WINDOW_MAX_PAIRS)
        pairs = WINDOW_MAX_PAIRS;
    if (fseeko(file, start, SEEK_SET) != 0)
        return 0;
    while (got < pairs) {
        size_t want = pairs - got;
        size_t n;
        if (want > 65536)
            want = 65536;
        n = fread(raw, 2, want, file);
        if (!n)
            break;
        if (convert(raw, window_i + got, window_q + got, n) < 0)
            break;
        got += n;
    }
    return got;
}

/*
 * What the shipping measurements say about a window. The whole point of this
 * tool is that these three are correct and were simply never pointed at the
 * signal, so they are called rather than reimplemented.
 */
static void report_shipping(const char *label, size_t pairs,
                            double sample_rate, double at_hz, double search_hz,
                            double guard_hz, double channel_hz) {
    struct signal_carrier carrier;
    struct signal_bursts bursts;
    struct signal_envelope envelope;

    printf("  %s\n", label);
    if (!signal_find_carrier(window_i, window_q, pairs, sample_rate,
                             at_hz - search_hz, at_hz + search_hz, guard_hz,
                             channel_hz, &carrier)) {
        printf("      carrier:  no line found in the window\n");
        return;
    }
    printf("      carrier:  %-20s %6.1f dB over its floor, standing %.3f\n",
           signal_verdict_name(signal_carrier_verdict(&carrier)),
           carrier.carrier_over_noise_db, carrier.carrier_power_fraction);
    printf("      line at:  %+.1f Hz (%+.1f from where it was asked for)\n",
           carrier.offset_hz, carrier.offset_hz - at_hz);

    if (signal_find_bursts(window_i, window_q, pairs, sample_rate,
                           SIGNAL_BURST_GAP_DEFAULT, &bursts)) {
        printf("      bursts:   %d of %.0f us, occupancy %.4f, contrast "
               "%.1f dB\n", bursts.count, bursts.median_seconds * 1e6,
               bursts.occupancy, bursts.contrast_db);
    } else {
        printf("      bursts:   %s, contrast %.1f dB\n",
               bursts.verdict == SIGNAL_BURST_BUSY ? "busy" : "level",
               bursts.contrast_db);
    }
    if (signal_envelope_stats(window_i, window_q, pairs, sample_rate,
                              carrier.offset_hz, channel_hz,
                              (double)g_device.full_scale, &envelope))
        printf("      envelope: variation %.3f (noise reads %.3f), peak/mean "
               "%.1f dB\n", envelope.variation, SIGNAL_ENVELOPE_RAYLEIGH,
               envelope.peak_over_mean_db);
    else
        printf("      envelope: refused, too far down the range to measure\n");
}

/*
 * Mix the window to `carrier_hz` and decimate to about WORK_RATE_HZ. The box
 * filter the decimation brings with it is the channel: everything after this
 * needs the signal isolated, for the reason `signal_envelope_stats` gives --
 * across a whole capture the envelope of a narrow signal is the envelope of
 * the noise beside it.
 */
static size_t to_baseband(size_t pairs, double sample_rate, double carrier_hz,
                          double *work_rate_out) {
    size_t k, m = 0;
    int decimate = (int)(sample_rate / WORK_RATE_HZ);
    double accumulate_i = 0.0, accumulate_q = 0.0;
    int in_bin = 0;

    if (decimate < 4)
        decimate = 4;
    for (k = 0; k < pairs; k++) {
        double phase = -2.0 * M_PI * carrier_hz * (double)k / sample_rate;
        double cosine = cos(phase), sine = sin(phase);
        accumulate_i += window_i[k] * cosine - window_q[k] * sine;
        accumulate_q += window_i[k] * sine + window_q[k] * cosine;
        if (++in_bin < decimate)
            continue;
        if (m >= WINDOW_MAX_PAIRS / 4)
            break;
        work_i[m] = accumulate_i / decimate;
        work_q[m] = accumulate_q / decimate;
        work_amp[m] = sqrt(work_i[m] * work_i[m] + work_q[m] * work_q[m]);
        m++;
        accumulate_i = accumulate_q = 0.0;
        in_bin = 0;
    }
    *work_rate_out = sample_rate / decimate;
    return m;
}

static void draw_bar(int count, int total, int width) {
    int j, bars = total > 0 ? count * width / total : 0;
    for (j = 0; j < bars; j++)
        putchar('#');
    for (; j < width; j++)
        putchar(' ');
}

/*
 * Is the carrier switched on and off, or is it always on?
 *
 * Two humps at the ends means on-off keying: the envelope is either at the
 * transmitter's level or at the noise floor, with little between. One hump
 * means a carrier that stays up, whatever is riding it.
 */
static double envelope_histogram(size_t count) {
    int histogram[30], bin;
    double maximum = 0.0;
    size_t k;

    memset(histogram, 0, sizeof(histogram));
    for (k = 0; k < count; k++) {
        if (work_amp[k] > maximum)
            maximum = work_amp[k];
    }
    if (!(maximum > 0.0) || !count)
        return 0.0;
    for (k = 0; k < count; k++) {
        bin = (int)(work_amp[k] / maximum * 30.0);
        if (bin >= 30)
            bin = 29;
        if (bin >= 0)
            histogram[bin]++;
    }
    /*
     * The peak is the one absolute number in this report, and it is in the
     * device's own counts -- full scale 127.5 for an 8-bit container -- because
     * that is what comes out of the sample seam and what the rest of the
     * program carries. Everything below it is a fraction of that peak, so the
     * histogram itself is unchanged by the container.
     */
    printf("\n  Envelope, as a fraction of the window's peak "
           "(%.4f counts):\n",
           maximum);
    for (bin = 0; bin < 30; bin++) {
        if (!histogram[bin])
            continue;
        printf("    %5.1f%% |", (bin + 0.5) / 30.0 * 100.0);
        draw_bar(histogram[bin], (int)count, 50);
        printf(" %d\n", histogram[bin]);
    }
    printf("    Two humps at the ends is on-off keying; one hump is a "
           "carrier that stays up.\n");
    return maximum;
}

/*
 * And if it is keyed, is the frequency doing anything while it is on?
 *
 * Measured over the ON samples only, because the instantaneous frequency of
 * an off carrier is the noise's and would fill the histogram with a floor
 * that means nothing. Two modes is frequency-shift keying; one is not.
 */
static void frequency_histogram(size_t count, double work_rate,
                                double threshold) {
    int histogram[41], bin, j;
    double low = -30000.0, high = 30000.0;
    size_t k;
    int on = 0;

    memset(histogram, 0, sizeof(histogram));
    for (k = 1; k < count; k++) {
        double real, imaginary, hz;
        if (work_amp[k] < threshold || work_amp[k - 1] < threshold)
            continue;
        real = work_i[k] * work_i[k - 1] + work_q[k] * work_q[k - 1];
        imaginary = work_q[k] * work_i[k - 1] - work_i[k] * work_q[k - 1];
        hz = atan2(imaginary, real) * work_rate / (2.0 * M_PI);
        bin = (int)((hz - low) / (high - low) * 41.0);
        if (bin >= 0 && bin < 41)
            histogram[bin]++;
        on++;
    }
    printf("\n  Instantaneous frequency while the carrier is on (%d of %zu "
           "samples):\n", on, count);
    for (bin = 0; bin < 41; bin++) {
        if (!histogram[bin])
            continue;
        printf("    %+8.0f Hz |", low + (high - low) * (bin + 0.5) / 41.0);
        for (j = 0; j < histogram[bin] * 50 / (on ? on : 1); j++)
            putchar('#');
        printf(" %d\n", histogram[bin]);
    }
    printf("    Two modes is frequency-shift keying; one is a single "
           "carrier.\n");
}

/*
 * How long is a chip?
 *
 * For keyed modulation the run lengths answer it directly and without a
 * search: the shortest run is one chip, and a line code shows as a small set
 * of multiples of it. A Manchester code gives runs of one and two chips and
 * nothing else, which is a stronger statement than any symbol-rate line, and
 * one this histogram either shows or does not.
 */
static void run_histogram(size_t count, double work_rate, double threshold,
                          double bucket_us) {
    int on_histogram[120], off_histogram[120], bin;
    double microseconds_per = 1e6 / work_rate;
    int state, run = 1, runs = 0;
    size_t k;

    if (!count)
        return;
    memset(on_histogram, 0, sizeof(on_histogram));
    memset(off_histogram, 0, sizeof(off_histogram));
    state = work_amp[0] >= threshold;
    for (k = 1; k < count; k++) {
        int now = work_amp[k] >= threshold;
        if (now == state) {
            run++;
            continue;
        }
        bin = (int)((double)run * microseconds_per / bucket_us);
        if (bin >= 120)
            bin = 119;
        if (bin >= 0) {
            if (state)
                on_histogram[bin]++;
            else
                off_histogram[bin]++;
            runs++;
        }
        state = now;
        run = 1;
    }
    printf("\n  Run lengths, %.0f us per bucket, %d runs:\n", bucket_us, runs);
    printf("         us          on                        off\n");
    for (bin = 0; bin < 120; bin++) {
        if (!on_histogram[bin] && !off_histogram[bin])
            continue;
        printf("    %5.0f-%5.0f  ", bin * bucket_us, (bin + 1) * bucket_us - 1);
        draw_bar(on_histogram[bin], runs, 26);
        printf("%4d  ", on_histogram[bin]);
        draw_bar(off_histogram[bin], runs, 26);
        printf("%4d\n", off_histogram[bin]);
    }
}

/* The runs themselves, which is what shows a preamble from a data section. */
static void run_sequence(size_t count, double work_rate, double threshold,
                         int limit) {
    double microseconds_per = 1e6 / work_rate;
    int state, run = 1, shown = 0;
    size_t k;

    if (!count || limit <= 0)
        return;
    printf("\n  The runs themselves (us), from the start of the window:\n    ");
    state = work_amp[0] >= threshold;
    for (k = 1; k < count && shown < limit; k++) {
        int now = work_amp[k] >= threshold;
        if (now == state) {
            run++;
            continue;
        }
        printf("%s%4.0f ", state ? "H" : "L", (double)run * microseconds_per);
        if (++shown % 12 == 0)
            printf("\n    ");
        state = now;
        run = 1;
    }
    printf("\n    An alternating run of one length is a preamble; a mix of "
           "one and two is data.\n");
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    double sample_rate = argc > 2 ? atof(argv[2]) : 2000000.0;
    double bar_db = argc > 3 ? atof(argv[3]) : 25.0;
    double gap_seconds = argc > 4 ? atof(argv[4]) : 0.050;
    double channel_hz = argc > 5 ? atof(argv[5]) : 50000.0;
    double guard_hz = argc > 6 ? atof(argv[6]) : 50000.0;
    double bucket_us = argc > 7 ? atof(argv[7]) : 10.0;
    int sequence_limit = argc > 8 ? atoi(argv[8]) : 48;
    FILE *file;
    size_t pairs, work_count;
    double work_rate, peak_hz, threshold, maximum;
    double longest = 0.0;
    int i, longest_at = -1;

    if (!path) {
        fprintf(stderr, "usage: ook_report <capture> [rate] [over_floor_db]"
                        " [gap_s] [channel_hz] [guard_hz] [bucket_us]"
                        " [runs]\n");
        return 2;
    }
    /* The receiver these captures were taken with: an 8-bit container at
       127.5 full scale. A function rather than an initialiser, so it is set
       here. */
    g_device = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);

    file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return 2;
    }
    sdr_dsp_init(&dsp);

    printf("%s\n  %.3f MS/s, chunks of %d pairs (%.0f us), a chunk counts as "
           "busy at %.0f dB over its own median bin\n\n", path,
           sample_rate / 1e6, SCAN_SIZE,
           (double)SCAN_SIZE / sample_rate * 1e6, bar_db);

    if (scan_capture(file, sample_rate, bar_db, gap_seconds) < 0) {
        fprintf(stderr, "%s: could not scan\n", path);
        fclose(file);
        return 2;
    }
    if (!found_count) {
        printf("  No transmission stands %.0f dB over the floor anywhere in "
               "this capture.\n"
               "  That is a real answer and the one a control frequency "
               "should give.\n", bar_db);
        fclose(file);
        return 0;
    }

    printf("  Transmissions found by walking the whole capture:\n");
    printf("      #    start       end     length    scan bin    refined"
           "   over floor\n");
    for (i = 0; i < found_count; i++) {
        double length = found[i].end_seconds - found[i].start_seconds;
        struct signal_carrier carrier;
        size_t got;

        /*
         * Refine each one rather than only the one examined closely. Two
         * captures of the same transmitter taken at different tunings should
         * agree on the absolute frequency, and that agreement is the only
         * corroboration available without a second receiver -- so it has to
         * be in the output rather than left for a reader to assemble.
         */
        found[i].refined_hz = found[i].peak_hz;
        found[i].over_floor_db = 0.0;
        got = load_window(file, sample_rate, found[i].start_seconds,
                          (size_t)(length * sample_rate));
        if (got >= 4096 &&
            signal_find_carrier(window_i, window_q, got, sample_rate,
                                found[i].peak_hz - channel_hz * 2.0,
                                found[i].peak_hz + channel_hz * 2.0, guard_hz,
                                channel_hz, &carrier)) {
            found[i].refined_hz = carrier.offset_hz;
            found[i].over_floor_db = carrier.carrier_over_noise_db;
        }
        printf("    %3d  %7.3f s  %7.3f s  %7.1f ms  %+9.0f  %+10.1f Hz"
               "  %6.1f dB\n", i + 1, found[i].start_seconds,
               found[i].end_seconds, length * 1000.0, found[i].peak_hz,
               found[i].refined_hz, found[i].over_floor_db);
        if (length > longest) {
            longest = length;
            longest_at = i;
        }
    }

    /*
     * The close look goes to the longest transmission rather than the first:
     * the first is as likely as not to be clipped by the start of the
     * recording, and a truncated press measures its own edge.
     */
    peak_hz = found[longest_at].refined_hz;
    printf("\n  The longest of them, %.3f s to %.3f s at %+.0f Hz:\n\n",
           found[longest_at].start_seconds, found[longest_at].end_seconds,
           peak_hz);

    pairs = load_window(file, sample_rate, found[longest_at].start_seconds,
                        (size_t)(longest * sample_rate));
    if (pairs < 4096) {
        fprintf(stderr, "%s: only %zu pairs in the transmission\n", path,
                pairs);
        fclose(file);
        return 2;
    }
    report_shipping("What signal_probe says about it:", pairs, sample_rate,
                    peak_hz, channel_hz * 2.0, guard_hz, channel_hz);

    /*
     * And the same measurements over an equal-length window at the start of
     * the capture. This is the control, and it is the whole argument for this
     * tool: it is what `probe-signal` reports, because that is the only part
     * of the file its fixed prefixes ever reach.
     */
    {
        size_t control = load_window(file, sample_rate, 0.0, pairs);
        if (control >= 4096) {
            printf("\n");
            report_shipping("The same, over an equal window at t = 0 -- which "
                            "is what probe-signal sees:", control, sample_rate,
                            peak_hz, channel_hz * 2.0, guard_hz, channel_hz);
        }
        /* Put the transmission back; the control overwrote it. */
        pairs = load_window(file, sample_rate,
                            found[longest_at].start_seconds, pairs);
    }

    work_count = to_baseband(pairs, sample_rate, peak_hz, &work_rate);
    printf("\n  Mixed to the carrier and filtered to the channel: %zu samples "
           "at %.1f kS/s\n", work_count, work_rate / 1000.0);

    maximum = envelope_histogram(work_count);
    threshold = maximum * 0.5;
    frequency_histogram(work_count, work_rate, threshold);
    run_histogram(work_count, work_rate, threshold, bucket_us);
    run_sequence(work_count, work_rate, threshold, sequence_limit);

    fclose(file);
    return 0;
}
