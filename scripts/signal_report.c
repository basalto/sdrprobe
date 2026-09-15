/*
 * What signal_probe says about a capture, at a signal and at its controls.
 *
 * A tool because it kept being written. One session wrote six variants of
 * "load a capture, measure this statistic at these offsets, print a table" --
 * for the symbol-rate line, the burst structure, the envelope, the spectral
 * shape, the AIS channel powers and the ILS sidebands -- and every one was
 * thrown away with the answer left in a transcript. The table is the
 * deliverable: it is what a ticket needs and what settles whether something
 * is on air.
 *
 * The shape is signal-against-controls, because that is what every one of
 * those six was doing. A measurement at one frequency is a number; the same
 * measurement at a frequency where nothing should be is what makes it
 * evidence. The AIS null was worth nothing until the same code put a known
 * TETRA carrier 16 dB clear of its own controls.
 *
 * A control wider than its separation from the signal is not a control, and
 * the report will not say so. The channel is a boxcar decimation, whose
 * far-out rejection is poor, so a strong signal leaks into a wide control
 * channel. Keep CHANNEL_SIGNAL well under the offset between a control and
 * the signal it is a control for.
 *
 *   make probe-signal FILE_SIGNAL=captures/x.bin AT_SIGNAL=300000 \
 *       CONTROLS_SIGNAL=-200000,200000 CHANNEL_SIGNAL=20000
 *
 * `AT` and `CONTROLS` are offsets from the capture's own centre in hertz. A
 * capture recorded on top of its signal has it at 0, which is the one place
 * signal_find_carrier() will not look -- see the guard in signal_probe.h --
 * so record off-centre or pass a guard of 0 and mean it.
 *
 * Prints, per offset: whether a standing carrier is there and how much of the
 * channel stands still, **where in the window the line actually was**, the
 * burst structure, and the envelope against Rayleigh. Not a check and not a
 * decoder: it measures and prints, and every conclusion is the reader's.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_profile.h"
#include "sdr_dsp.h"
#include "signal_probe.h"

/*
 * A ceiling, not a default. It used to be 4000000 -- 2.00 s at 2 MS/s -- and
 * a 5 s capture was silently read as its first two seconds, which on a
 * transmitter that speaks once per press is the difference between an answer
 * and a confident absence. The whole file is read now and the cap only says
 * how much memory this is allowed to want; reaching it prints a line.
 *
 * 64M pairs is 32 s at 2 MS/s and costs 768 MB across three arrays, so it is
 * allocated from the file's own size rather than reserved.
 */
#define MAX_PAIRS 64000000
#define MAX_OFFSETS 16

static float *samples_i;
static float *samples_q;
static float *samples_magnitude;

/*
 * Through the program's own byte-to-float seam, not a private copy of it.
 *
 * There is exactly one such seam -- sdr_dsp_convert_iq() -- and the whole
 * argument that a 12-bit container decodes identically to an 8-bit one rests
 * on everything downstream taking floats from it. A diagnostic that spells
 * the conversion out again is a second seam that can disagree with the first,
 * silently and only about captures nobody has looked at: this one hardcoded
 * the house 8-bit format, so a wider container would have been read as noise
 * with a confident report printed over it.
 *
 * The samples arrive in the device's own counts rather than normalised to
 * unity. Nothing here is affected, because every statistic this tool prints
 * is a ratio -- dB over a floor, a fraction, a coefficient of variation --
 * and that was verified by diffing the whole report across the change rather
 * than by reasoning about it.
 *
 * The magnitudes are a by-product this tool never reads, but the seam refuses
 * a NULL there and returns zero pairs, which reads exactly like an empty
 * capture.
 *
 * The DC offset is left in, because that is what `app->i_samples` carries --
 * only the spectrum path removes it. A tool that quietly cleaned the samples
 * would answer a question the program never asks.
 */
static size_t load(const char *path, const struct device_profile *device) {
    FILE *file = fopen(path, "rb");
    static unsigned char raw[65536];
    size_t count = 0, capacity;
    long size;

    if (!file) {
        perror(path);
        exit(2);
    }
    /* Sized from the file, so the whole of it is read. */
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0) {
        perror(path);
        exit(2);
    }
    rewind(file);
    capacity = (size_t)size / device->bytes_per_pair;
    if (capacity > MAX_PAIRS) {
        fprintf(stderr,
                "  note: %s holds %zu pairs; reading the first %d, which is "
                "this tool's ceiling\n",
                path, capacity, MAX_PAIRS);
        capacity = MAX_PAIRS;
    }
    if (!capacity)
        return 0;
    samples_i = malloc(capacity * sizeof *samples_i);
    samples_q = malloc(capacity * sizeof *samples_q);
    samples_magnitude = malloc(capacity * sizeof *samples_magnitude);
    if (!samples_i || !samples_q || !samples_magnitude) {
        fprintf(stderr, "out of memory for %zu pairs\n", capacity);
        exit(2);
    }
    while (count < capacity) {
        size_t want = capacity - count;
        size_t got;

        if (want > sizeof raw / device->bytes_per_pair)
            want = sizeof raw / device->bytes_per_pair;
        got = fread(raw, device->bytes_per_pair, want, file);
        if (!got)
            break;
        if (sdr_dsp_convert_iq(device, raw, got * device->bytes_per_pair,
                               samples_i + count, samples_q + count,
                               samples_magnitude + count, got) != got) {
            fprintf(stderr, "the sample seam refused %zu pairs\n", got);
            exit(2);
        }
        count += got;
    }
    fclose(file);
    return count;
}

static void report_one(const char *label, double at_hz, size_t pairs,
                       double rate, double channel_hz, double search_hz,
                       double guard_hz, double full_scale) {
    struct signal_carrier carrier;
    struct signal_bursts bursts;
    struct signal_envelope envelope;
    struct signal_activity activity;
    const float *win_i = samples_i, *win_q = samples_q;
    size_t win_pairs = pairs;
    int found, nothing;

    /*
     * Where to look, before what is there.
     *
     * Every measurement below reads a prefix of what it is handed -- 32.8 ms
     * for the coarse carrier search, 131 ms for the burst verdict -- so on a
     * transmitter that speaks once per button press they all read the silence
     * before the first press and report a confident absence. Seeking first is
     * the whole fix, and `probe-ook` is the evidence it is enough: the same
     * three functions read 53.7 dB over the floor on the right 0.8 s of a
     * capture and -4.3 dB on an equal window at t = 0.
     *
     * The band watched is the whole search window and not the channel,
     * because the carrier's offset is exactly what is not yet known.
     */
    if (signal_find_activity(samples_i, samples_q, pairs, rate, at_hz,
                             2.0 * search_hz + channel_hz, &activity)
        && !activity.uniform) {
        win_i = samples_i + activity.offset_pairs;
        win_q = samples_q + activity.offset_pairs;
        win_pairs = activity.pair_count;
    }

    found = signal_find_carrier(win_i, win_q, win_pairs, rate,
                                at_hz - search_hz, at_hz + search_hz,
                                guard_hz, channel_hz, &carrier);
    printf("  %-14s %+10.0f Hz  ", label, at_hz);
    if (!found) {
        printf("no line found in the window\n");
        return;
    }
    printf("%-20s %6.1f dB  standing %.3f\n",
           signal_verdict_name(signal_carrier_verdict(&carrier)),
           carrier.carrier_over_noise_db, carrier.carrier_power_fraction);
    /*
     * Where the line actually is, which used to be the one thing this tool
     * measured and did not print: the row was labelled with the offset it was
     * *asked* for, so `carrier.offset_hz` never left the function.
     *
     * Both numbers, because they answer different questions. The delta says
     * whether the caller aimed at the right place -- `.scratch/am-airband/`
     * spent a run aiming 24 kHz off, at a carrier group's centre rather than
     * its peak, and read two controls' worth of nothing with no hint why. The
     * offset itself is what says whose signal it is: a tone clocked from the
     * receiver's own reference reads at its exact nominal frequency however
     * far out the crystal is, where an external one is displaced by the
     * tuning times the ppm error (`.scratch/device-model/issues/11-*`).
     *
     * Printed as an offset and not an absolute, because this tool is given a
     * rate and never a centre frequency -- that lives in the capture's
     * sidecar, and adding the sum here would invent one for a caller who
     * passed neither.
     *
     * The caption changes on a `no carrier` verdict, and it has to. Below
     * SIGNAL_CARRIER_PRESENT_DB there is no line, and what the search
     * returned is the largest of thousands of noise samples -- the very thing
     * signal_probe.h measured that threshold against. Calling that "the line"
     * at a tenth of a hertz gives a noise peak the authority of a
     * measurement. Where it landed is still worth printing: both controls
     * here wander to the edge of the search window, 36 and 32 kHz from where
     * they were asked for, which is what an empty frequency looks like and
     * the same evidence tetra_burst_find's best lag gives when it wanders.
     */
    nothing = signal_carrier_verdict(&carrier) == SIGNAL_NOTHING;
    printf("  %-14s %10s      %-18s%+.1f Hz  (%+.1f from where it was asked"
           " for)\n", "", "",
           nothing ? "strongest bin at:" : "line found at:",
           carrier.offset_hz, carrier.offset_hz - at_hz);

    signal_find_bursts(win_i, win_q, win_pairs, rate,
                       SIGNAL_BURST_GAP_DEFAULT, &bursts);
    printf("  %-14s %10s      envelope in time: %-10s contrast %5.1f dB",
           "", "",
           bursts.verdict == SIGNAL_BURST_SEPARABLE ? "bursts"
               : bursts.verdict == SIGNAL_BURST_BUSY ? "busy" : "level",
           bursts.contrast_db);
    if (bursts.verdict == SIGNAL_BURST_SEPARABLE)
        printf("  %d of %.0f us, occupancy %.4f", bursts.count,
               bursts.median_seconds * 1e6, bursts.occupancy);
    printf("\n");

    if (signal_envelope_stats(win_i, win_q, win_pairs, rate,
                              carrier.offset_hz, channel_hz, full_scale, &envelope))
        printf("  %-14s %10s      envelope shape:   variation %.3f "
               "(noise reads %.3f)  peak/mean %.1f dB  residual %+.0f Hz\n",
               "", "", envelope.variation, SIGNAL_ENVELOPE_RAYLEIGH,
               envelope.peak_over_mean_db, envelope.mean_frequency_hz);
    else
        printf("  %-14s %10s      envelope shape:   refused, too far down "
               "the range to measure\n", "", "");

    /*
     * Which window the three answers above came from, always -- including
     * when it was the whole buffer.
     *
     * This is the half of the fix that matters. A reader who gets "no
     * carrier" cannot otherwise tell "I measured, and there is nothing" from
     * "I did not look where it is", and the second is what this tool spent a
     * session doing while sounding exactly like the first.
     */
    if (!activity.found)
        printf("  %-14s %10s      looked at:        the whole buffer "
               "(too short to scan)\n", "", "");
    else if (activity.uniform)
        printf("  %-14s %10s      looked at:        the whole buffer, level "
               "throughout (%.1f dB between busiest and quietest)\n",
               "", "", activity.over_floor_db);
    else
        printf("  %-14s %10s      looked at:        %.3f s to %.3f s of "
               "%.2f s, %.1f dB up, %d busy run%s, duty %.3f\n",
               "", "",
               (double)activity.offset_pairs / rate,
               (double)(activity.offset_pairs + activity.pair_count) / rate,
               (double)pairs / rate, activity.over_floor_db,
               activity.run_count, activity.run_count == 1 ? "" : "s",
               activity.duty);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    double rate = argc > 2 ? atof(argv[2]) : 2000000.0;
    double at = argc > 3 ? atof(argv[3]) : 0.0;
    const char *controls = argc > 4 ? argv[4] : "";
    double channel = argc > 5 ? atof(argv[5]) : 20000.0;
    double search = argc > 6 ? atof(argv[6]) : 0.0;
    double guard = argc > 7 ? atof(argv[7]) : 0.0;
    size_t limit = argc > 8 ? (size_t)atof(argv[8]) : 0;
    size_t pairs;
    struct device_profile device;
    double offsets[MAX_OFFSETS];
    int count = 0, i;

    if (!path) {
        fprintf(stderr,
                "usage: signal_report <capture> [rate] [at_hz]"
                " [control_hz,...] [channel_hz] [search_hz] [guard_hz]"
                " [pairs]\n");
        return 2;
    }
    /* Wide enough to find a carrier the caller placed approximately, and no
       wider: survey_sweep.h has the argument for deriving this from the
       width, and a tool has no width to derive it from. */
    if (!(search > 0.0))
        search = channel > 40000.0 ? channel : 40000.0;
    /* A guard of zero means the caller wants zero looked at. Defaulting it
       from the offset instead would silently refuse to measure a capture
       recorded on top of its signal, which is the common mistake and worth
       an explicit answer rather than a quiet one. */
    /*
     * The house receiver's profile, because a capture is 8-bit interleaved
     * I/Q unless its sidecar says otherwise. The tuner is named only so the
     * profile is a real one; nothing here reads its reach.
     */
    device = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);
    pairs = load(path, &device);
    /*
     * How much of the capture to use, because the answer depends on it and
     * that is worth being able to show. `carrier_power_fraction` mixes at one
     * fixed frequency, so a carrier drifting even a fraction of a hertz walks
     * out of phase over a long observation and the mean cancels: the
     * 75.0005 MHz harmonic reads 0.921 over 400000 pairs and 0.779 over four
     * million, which is a bare carrier and a modulated one by
     * SIGNAL_BARE_FRACTION.
     */
    if (limit > 0 && limit < pairs)
        pairs = limit;
    if (pairs < 1024) {
        fprintf(stderr, "%s: only %zu pairs\n", path, pairs);
        return 2;
    }

    offsets[count++] = at;
    if (*controls) {
        char buffer[256];
        char *token;
        snprintf(buffer, sizeof(buffer), "%s", controls);
        for (token = strtok(buffer, ","); token && count < MAX_OFFSETS;
             token = strtok(NULL, ","))
            offsets[count++] = atof(token);
    }

    printf("%s\n  %.2f s at %.3f MS/s, %zu pairs\n"
           "  channel %.0f Hz, search +/-%.0f Hz, guard %.0f Hz\n\n",
           path, (double)pairs / rate, rate / 1e6, pairs, channel, search,
           guard);
    for (i = 0; i < count; i++) {
        report_one(i == 0 ? "the signal" : "a control", offsets[i], pairs,
                   rate, channel, search, guard, (double)device.full_scale);
    }
    if (count < 2)
        printf("  No control offset was given. A measurement at one\n"
               "  frequency is a number; the same measurement where nothing\n"
               "  should be is what makes it evidence.\n");
    return 0;
}
