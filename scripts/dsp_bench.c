/*
 * dsp_bench.c - what the DSP actually costs, per sample block.
 *
 * The question this answers is whether any of it is near the budget. One
 * block is 256 KB, 131072 I/Q pairs, 65.5 ms of signal at 2 MS/s, and blocks
 * arrive at about 15 per second. Everything the program does to a block --
 * conversion, statistics, spectrum, decode -- has to fit inside that 65.5 ms,
 * or the acquisition slot starts overwriting blocks before they are looked at,
 * which the HUD counts.
 *
 * Anything reported here at a few per cent of the budget is not worth
 * optimising, with SIMD or otherwise: the receiver is the bottleneck. Anything
 * approaching it is.
 *
 * Build/run:
 *     make bench-dsp                       # as the program is built, -O2
 *     make bench-dsp BENCH_ARCH=-march=native   # with this machine's SIMD
 */
#include "sdr_dsp.h"
#include "adsb_dsp.h"
#include "gsm_dsp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
#define SAMPLE_RATE 2000000.0
#define BLOCK_MS (BLOCK_PAIRS / SAMPLE_RATE * 1000.0)
#define SURVEY_BINS 8192

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

static void report(const char *stage, double total_ms, int runs) {
    double per = total_ms / (double)runs;
    printf("  %-34s %8.3f ms/block   %6.2f%% of the budget\n", stage, per,
           100.0 * per / BLOCK_MS);
}

/* Fill a block from a capture, looping it if the file is short. */
static void load_block(const char *path, unsigned char *raw) {
    FILE *f = fopen(path, "rb");
    size_t filled = 0;

    if (!f) {
        fprintf(stderr, "Cannot open %s; using noise instead.\n", path);
        for (size_t i = 0; i < BLOCK_BYTES; i++)
            raw[i] = (unsigned char)(127 + (rand() % 9) - 4);
        return;
    }
    while (filled < BLOCK_BYTES) {
        size_t got = fread(raw + filled, 1, BLOCK_BYTES - filled, f);
        if (got == 0) {
            if (filled == 0 || fseek(f, 0, SEEK_SET) != 0)
                break;
            continue;
        }
        filled += got;
    }
    fclose(f);
}

int main(int argc, char **argv) {
    static unsigned char raw[BLOCK_BYTES];
    static float i_samples[BLOCK_PAIRS];
    static float q_samples[BLOCK_PAIRS];
    static float magnitudes[BLOCK_PAIRS];
    static float workspace[BLOCK_PAIRS];
    static float peaks[BLOCK_PAIRS];
    static float average[SDR_DSP_FFT_SIZE];
    static float maximum[SDR_DSP_FFT_SIZE];
    static float survey[SURVEY_BINS];
    static struct sdr_peak found[512];
    static struct sdr_dsp dsp;
    const char *adsb_path = argc > 1 ? argv[1] : "testfiles/adsb_cpr_pair.bin";
    const char *gsm_path = argc > 2 ? argv[2] : "testfiles/gsm_arfcn_73.bin";
    const int runs = 40;
    double start;
    size_t pairs;

    sdr_dsp_init(&dsp);
    load_block(adsb_path, raw);

    printf("One block is %zu pairs, %.1f ms of signal at %.0f S/s.\n",
           (size_t)BLOCK_PAIRS, BLOCK_MS, SAMPLE_RATE);
    printf("Every stage below runs once per block in the frame loop.\n\n");
    printf("ADS-B path (%s)\n", adsb_path);

    start = now_ms();
    for (int r = 0; r < runs; r++)
        pairs = sdr_dsp_convert_iq(raw, BLOCK_BYTES, i_samples, q_samples,
                                   magnitudes, BLOCK_PAIRS);
    report("byte -> I/Q + magnitude", now_ms() - start, runs);

    struct sdr_signal_stats stats;
    start = now_ms();
    for (int r = 0; r < runs; r++)
        sdr_dsp_signal_stats(i_samples, q_samples, magnitudes, pairs,
                             workspace, &stats);
    report("signal statistics (two percentiles)", now_ms() - start, runs);

    start = now_ms();
    for (int r = 0; r < runs; r++)
        sdr_dsp_peak_bins(magnitudes, pairs, peaks, 1600);
    report("magnitude peak bins", now_ms() - start, runs);

    start = now_ms();
    for (int r = 0; r < runs; r++)
        sdr_dsp_remove_dc(i_samples, q_samples, pairs);
    report("DC removal", now_ms() - start, runs);

    start = now_ms();
    for (int r = 0; r < runs; r++)
        sdr_dsp_spectrum(&dsp, i_samples, q_samples, pairs, average, maximum);
    report("spectrum: 64 x 2048-point FFT", now_ms() - start, runs);

    struct adsb_decoder decoder;
    struct adsb_message messages[64];
    adsb_decoder_init(&decoder);
    start = now_ms();
    for (int r = 0; r < runs; r++)
        adsb_demod(&decoder, magnitudes, pairs, 0.0, messages, 64, NULL, NULL);
    report("Mode S demodulation", now_ms() - start, runs);

    /* The survey's peak search, over a full array of measured bins. */
    for (int i = 0; i < SURVEY_BINS; i++)
        survey[i] = -95.0f + (float)(rand() % 200) / 100.0f;
    for (int i = 0; i < SURVEY_BINS; i += 97)
        survey[i] = -40.0f;
    start = now_ms();
    for (int r = 0; r < runs; r++)
        sdr_dsp_find_peaks(survey, SURVEY_BINS, -300.0f, 8.0f, 20.0f,
                           workspace, found, 512);
    report("survey peak search (8192 bins)", now_ms() - start, runs);

    /* GSM is a different capture and only runs in its own view. */
    load_block(gsm_path, raw);
    pairs = sdr_dsp_convert_iq(raw, BLOCK_BYTES, i_samples, q_samples,
                               magnitudes, BLOCK_PAIRS);
    struct gsm_sch_result sch;
    struct gsm_sch_symbols symbols;
    start = now_ms();
    for (int r = 0; r < runs; r++)
        gsm_sch_decode(i_samples, q_samples, pairs, SAMPLE_RATE, 400000.0,
                       GSM_OPT_FILTER | GSM_OPT_FINECFO | GSM_OPT_TRELLIS,
                       &sch, &symbols);
    printf("\nGSM path (%s)\n", gsm_path);
    report("SCH decode, all refinements", now_ms() - start, runs);

    start = now_ms();
    for (int r = 0; r < runs; r++)
        gsm_sch_decode(i_samples, q_samples, pairs, SAMPLE_RATE, 400000.0, 0,
                       &sch, &symbols);
    report("SCH decode, no refinements", now_ms() - start, runs);

    struct gsm_fcch_result fcch;
    start = now_ms();
    for (int r = 0; r < runs; r++)
        gsm_fcch_detect(i_samples, q_samples, pairs, SAMPLE_RATE, 67708.0,
                        GSM_FCCH_SEARCH_HALF_HZ, &fcch);
    report("FCCH tone detection", now_ms() - start, runs);

    puts("\nA stage at a few per cent of the budget is not where the time");
    puts("goes; the receiver delivers one block every 65.5 ms whatever the");
    puts("program does with it.");
    return 0;
}
