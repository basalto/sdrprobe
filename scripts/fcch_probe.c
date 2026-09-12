/*
 * Every coherent tone in a GSM channel, and which of them is the FCCH.
 *
 * A tool because the question came up twice in one session and
 * `signal_probe` cannot answer it. Inside an occupied 200 kHz GSM carrier
 * everything is modulated energy, so `signal_find_carrier()` returns
 * whichever bin happens to be loudest and calls it "a modulated carrier"
 * wherever it is pointed -- which is true and useless. The FCCH is not the
 * loudest thing in the channel; it is the *coherent* thing, one burst in ten,
 * and `gsm_fcch_detect()` is the estimator that knows the difference.
 *
 *   make probe-fcch FILE_FCCH=captures/x.bin RATE_FCCH=2000000 \
 *       CARRIER_FCCH=400000 [SPAN_FCCH=] [STEP_FCCH=] [HALF_FCCH=]
 *
 * `CARRIER_FCCH` is where the channel's carrier sits in the capture, as an
 * offset from its centre -- +400000 for anything recorded with `--arfcn`,
 * which tunes 400 kHz below the channel.
 *
 * It sweeps `gsm_fcch_detect()` across the channel with a **narrow** search at
 * each step, so each answer is local: the shipping detector uses
 * GSM_FCCH_SEARCH_HALF_HZ (50 kHz) and reports one winner over that whole
 * span, which is exactly what hides a second tone from it.
 *
 * The question it exists to settle, `.scratch/startup-installation/issues/08-*`:
 * when a channel's FCCH-derived correction disagrees with another channel's by
 * tens of ppm, is there **one** tone in the wrong place -- a transmitter that
 * really is off, or a channel that is not where it is thought to be -- or
 * **two**, with the detector preferring the wrong one? Those are different
 * faults and nothing else here tells them apart.
 *
 * Not a check and not a decoder: it measures and prints, and every conclusion
 * is the reader's.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gsm_dsp.h"

#define MAX_PAIRS 4000000

static float samples_i[MAX_PAIRS];
static float samples_q[MAX_PAIRS];

/* The house format: unsigned 8-bit interleaved I/Q with 127.5 = zero, DC left
   in, exactly as `app->i_samples` carries it. */
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

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : NULL;
    double rate = argc > 2 && *argv[2] ? atof(argv[2]) : 2000000.0;
    double carrier = argc > 3 && *argv[3] ? atof(argv[3]) : 400000.0;
    /* How far either side of the nominal tone to look, and how finely. The
       default span covers the whole channel and then some; the step is well
       under the detector's own resolution so a tone cannot fall between two
       probes. */
    double span = argc > 4 && *argv[4] ? atof(argv[4]) : 60000.0;
    double step = argc > 5 && *argv[5] ? atof(argv[5]) : 2000.0;
    /*
     * How wide each individual probe searches, separately from how finely the
     * sweep steps.
     *
     * These were one knob and that was a flaw worth recording: with the
     * search width tied to the step, resolving a few-kHz effect meant
     * stepping in a few-kHz stride, so the answer was quantised to exactly
     * the size of the thing being measured. A first run of this read a 3.6 kHz
     * spread across four tunings with a 2.5 kHz step, which is not a
     * measurement of anything. Sweep finely, search widely enough for the
     * detector to work.
     */
    double half = argc > 6 && *argv[6] ? atof(argv[6]) : 0.0;
    double nominal = carrier + GSM_FCCH_TONE_HZ;
    size_t pairs;
    double at;
    double best_conf = 0.0;  /* the strongest amplitude seen */
    double best_tone = 0.0;
    int hits = 0;

    if (!path) {
        fprintf(stderr,
                "usage: fcch_probe FILE [rate] [carrier_hz] [span] [step]\n");
        return 2;
    }
    pairs = load(path);
    if (!pairs) {
        fprintf(stderr, "%s: no samples\n", path);
        return 2;
    }

    printf("%s\n", path);
    printf("  %.2f s at %.3f MS/s, %zu pairs\n", (double)pairs / rate,
           rate / 1e6, pairs);
    printf("  carrier at %+.0f Hz, so the FCCH belongs at %+.1f Hz\n", carrier,
           nominal);
    if (half <= 0.0)
        half = step;
    printf("  sweeping +/-%.0f Hz around it in %.0f Hz steps, each probe "
           "searching +/-%.0f\n\n", span, step, half);

    /*
     * **Amplitude is the discriminator, not confidence**, and that was worth
     * finding out the hard way: `gsm_fcch_detect()`'s confidence is a lag-1
     * autocorrelation coherence, and GMSK is constant-envelope and
     * continuous-phase, so *every* narrow slice of an occupied GSM carrier
     * reads 0.96 to 0.997. Swept across a channel the confidence is flat and
     * says nothing about where the tone is. The shipping detector works
     * because it searches +/-50 kHz and takes the **strongest** line, so
     * strength is the quantity that decides, and it is the one to print.
     */
    printf("  %12s  %12s  %10s  %10s  %s\n", "asked at", "tone found",
           "amplitude", "coherence", "from nominal");
    for (at = nominal - span; at <= nominal + span + 1.0; at += step) {
        struct gsm_fcch_result fcch;

        /* A search half-width of one step, so each probe answers about its own
           neighbourhood rather than reporting the channel's one best tone over
           and over. */
        if (!gsm_fcch_detect(samples_i, samples_q, pairs, rate, at, half,
                             &fcch))
            continue;
        if (!fcch.detected)
            continue;
        hits++;
        printf("  %+12.0f  %+12.1f  %10.4f  %10.3f  %+.1f Hz (%+.2f ppm at "
               "947 MHz)\n", at, fcch.tone_frequency_hz,
               (double)fcch.amplitude, (double)fcch.confidence,
               fcch.tone_frequency_hz - nominal,
               (fcch.tone_frequency_hz - nominal) / 947.6e6 * 1e6);
        if (fcch.amplitude > best_conf) {
            best_conf = fcch.amplitude;
            best_tone = fcch.tone_frequency_hz;
        }
    }

    printf("\n  %d coherent tone%s in the channel\n", hits,
           hits == 1 ? "" : "s");
    if (hits) {
        printf("  strongest at %+.1f Hz, amplitude %.4f, %+.1f Hz from "
               "nominal (%+.2f ppm)\n", best_tone, best_conf,
               best_tone - nominal,
               (best_tone - nominal) / 947.6e6 * 1e6);
        /*
         * The reading is the reader's, and the two answers are different
         * faults: one tone in the wrong place is a carrier that is not where
         * it was thought to be, and two is a detector that preferred the
         * wrong one over its 50 kHz search.
         */
        printf("  one tone: the carrier is not where it was assumed.\n");
        printf("  several:  the shipping detector picks one over +/-%.0f Hz "
               "and may prefer the wrong one.\n", GSM_FCCH_SEARCH_HALF_HZ);
    }
    return 0;
}
