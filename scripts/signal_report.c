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
 *   make probe-signal FILE_SIGNAL=captures/x.bin AT_SIGNAL=300000 \
 *       CONTROLS_SIGNAL=-200000,200000 CHANNEL_SIGNAL=20000
 *
 * `AT` and `CONTROLS` are offsets from the capture's own centre in hertz. A
 * capture recorded on top of its signal has it at 0, which is the one place
 * signal_find_carrier() will not look -- see the guard in signal_probe.h --
 * so record off-centre or pass a guard of 0 and mean it.
 *
 * Prints, per offset: whether a standing carrier is there and how much of the
 * channel stands still, the burst structure, and the envelope against
 * Rayleigh. Not a check and not a decoder: it measures and prints, and every
 * conclusion is the reader's.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "signal_probe.h"

#define MAX_PAIRS 4000000
#define MAX_OFFSETS 16

static float samples_i[MAX_PAIRS];
static float samples_q[MAX_PAIRS];

/*
 * The house format: unsigned 8-bit interleaved I/Q with 127.5 = zero.
 *
 * The DC offset is left in, because that is what `app->i_samples` carries --
 * only the spectrum path removes it. A tool that quietly cleaned the samples
 * would answer a question the program never asks.
 */
static size_t load(const char *path) {
    FILE *file = fopen(path, "rb");
    unsigned char pair[2];
    size_t count = 0;

    if (!file) {
        perror(path);
        exit(2);
    }
    while (count < MAX_PAIRS && fread(pair, 1, 2, file) == 2) {
        samples_i[count] = ((float)pair[0] - 127.5f) / 127.5f;
        samples_q[count] = ((float)pair[1] - 127.5f) / 127.5f;
        count++;
    }
    fclose(file);
    return count;
}

static void report_one(const char *label, double at_hz, size_t pairs,
                       double rate, double channel_hz, double search_hz,
                       double guard_hz) {
    struct signal_carrier carrier;
    struct signal_bursts bursts;
    struct signal_envelope envelope;
    int found;

    found = signal_find_carrier(samples_i, samples_q, pairs, rate,
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

    signal_find_bursts(samples_i, samples_q, pairs, rate,
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

    if (signal_envelope_stats(samples_i, samples_q, pairs, rate,
                              carrier.offset_hz, channel_hz, &envelope))
        printf("  %-14s %10s      envelope shape:   variation %.3f "
               "(noise reads %.3f)  peak/mean %.1f dB  residual %+.0f Hz\n",
               "", "", envelope.variation, SIGNAL_ENVELOPE_RAYLEIGH,
               envelope.peak_over_mean_db, envelope.mean_frequency_hz);
    else
        printf("  %-14s %10s      envelope shape:   refused, too far down "
               "the range to measure\n", "", "");
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
    pairs = load(path);
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
                   rate, channel, search, guard);
        printf("\n");
    }
    if (count < 2)
        printf("  No control offset was given. A measurement at one\n"
               "  frequency is a number; the same measurement where nothing\n"
               "  should be is what makes it evidence.\n");
    return 0;
}
