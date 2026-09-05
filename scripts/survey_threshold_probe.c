/*
 * Where a survey's noise floor actually reaches, so the candidate threshold
 * can be chosen instead of inherited.
 *
 * ADR-0013 left the survey filtering candidates by accident: the occupied
 * width walk ran to the ends of the array when a peak had no -20 dB point, the
 * floor window was then left nothing to measure, and the candidate was dropped
 * for having no floor. The effective bar came out near 20 dB, moved with how
 * ragged the noise was, and nobody chose it. The ADR says doing it properly
 * means "choosing a threshold from the dwell, and re-checking the candidate
 * counts against a live sweep", starting from measurements.
 *
 * These are the measurements. Pure complex noise goes through the real
 * transform (sdr_dsp_spectrum, Hann-windowed, averaged over every window in a
 * block) and the real fold (survey_fold_hold, peak-holding each transform bin
 * into the survey bin it lands in, over every block of the dwell). Then the
 * real peak finder looks at the result. Nothing here is a model of the survey;
 * it is the survey, with the antenna replaced by a random number generator.
 *
 * What comes out is a table: at each fold depth, how far above its own local
 * floor the loudest thing in a band of nothing reaches, and how many such
 * things a whole sweep produces. A threshold below that number is a survey
 * reporting noise; far above it is one refusing signals.
 *
 * Deterministic: the generator is seeded, so two runs of the same
 * configuration produce the same table and a change can be compared against
 * it. Six draws per point, because one draw of noise can move a count further
 * than the effect being looked for.
 *
 *   make probe-survey-threshold
 *   make probe-survey-threshold DRAWS=12
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr_dsp.h"
#include "survey_sweep.h"

#define PAIRS 131072
#define DRAWS_DEFAULT 6

static float noise_i[PAIRS];
static float noise_q[PAIRS];
static float spectrum[SDR_DSP_FFT_MAX];
static float maximum[SDR_DSP_FFT_MAX];
static float survey[SURVEY_BINS];
static float workspace[SURVEY_BINS];
static struct sdr_dsp dsp;

/* A plain 64-bit generator, so the tables do not depend on the platform's
   rand(). Box-Muller for the pair, which is what a receiver's noise is. */
static uint64_t rng_state;

static double uniform(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)((rng_state >> 11) & 0x1fffffffffffffULL) /
           9007199254740992.0;
}

static void gaussian_pair(double *a, double *b) {
    double u = uniform();
    double v = uniform();
    double r;

    if (u < 1e-12)
        u = 1e-12;
    r = sqrt(-2.0 * log(u));
    *a = r * cos(2.0 * 3.14159265358979323846 * v);
    *b = r * sin(2.0 * 3.14159265358979323846 * v);
}

static void fill_noise(void) {
    for (int n = 0; n < PAIRS; n++) {
        double a, b;
        gaussian_pair(&a, &b);
        /* Around a tenth of full scale: well clear of clipping, and the level
           itself does not matter -- every number here is a difference. */
        noise_i[n] = (float)(a * 0.1);
        noise_q[n] = (float)(b * 0.1);
    }
}

/*
 * One sweep's worth of noise, folded exactly as survey_report.c folds a
 * receiver's blocks: each step's transform bins peak-held into the survey bins
 * they fall in, every block of the dwell held on top of the last.
 */
static void fold_a_sweep(const struct survey_plan *plan, int blocks) {
    double rate = 2000000.0;
    double fft_bin_hz = rate / (double)SDR_DSP_FFT_SIZE;

    for (int i = 0; i < plan->bins; i++)
        survey[i] = SURVEY_SENTINEL_DBFS;

    for (int step = 0; step < plan->step_count; step++) {
        double centre = survey_plan_step_centre(plan, step);
        double lower = centre - rate / 2.0;

        for (int block = 0; block < blocks; block++) {
            fill_noise();
            if (sdr_dsp_spectrum(&dsp, noise_i, noise_q, PAIRS,
                                 SDR_DSP_FFT_SIZE, spectrum, maximum) <= 0)
                continue;
            for (int k = 0; k < SDR_DSP_FFT_SIZE; k++) {
                double hz = lower + ((double)k + 0.5) * fft_bin_hz;
                int bin;

                if (!survey_fold_keeps(hz, centre, rate))
                    continue;
                bin = survey_plan_bin_at(plan, hz);
                if (bin < 0)
                    continue;
                survey[bin] = survey_fold_hold(survey[bin], spectrum[k]);
            }
        }
    }
    (void)fft_bin_hz;
}

/* How ragged the folded array is, which is the whole question: the bar only
   has to clear what noise reaches. Reported because a table of zeroes is
   equally what a probe that folded nothing would print. */
static void spread(const struct survey_plan *plan, double *sd,
                   double *worst_step) {
    double sum = 0.0, sumsq = 0.0;
    double biggest = 0.0;
    int n = 0, i;

    for (i = 0; i < plan->bins; i++) {
        if (survey[i] <= SURVEY_SENTINEL_DBFS)
            continue;
        sum += survey[i];
        sumsq += (double)survey[i] * survey[i];
        n++;
        if (i > 0 && survey[i - 1] > SURVEY_SENTINEL_DBFS) {
            double step = fabs(survey[i] - survey[i - 1]);
            if (step > biggest)
                biggest = step;
        }
    }
    *sd = n > 1 ? sqrt(sumsq / n - (sum / n) * (sum / n)) : 0.0;
    *worst_step = biggest;
}

struct outcome {
    int candidates;     /* what a sweep of nothing reported */
    float strongest;    /* how far above its floor the worst of them stood */
};

static struct outcome look(const struct survey_plan *plan, float floor_db) {
    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    struct sdr_peak_gate gate;
    struct outcome out;
    int found, i;

    gate.topographic_db = SURVEY_MIN_PROMINENCE_DB;
    gate.floor_db = floor_db;
    gate.bandwidth_db = SURVEY_BANDWIDTH_DB;
    found = sdr_dsp_find_peaks(survey, plan->bins, SURVEY_SENTINEL_DBFS,
                               &gate, workspace, peaks, SURVEY_MAX_PEAKS);
    out.candidates = found;
    out.strongest = 0.0f;
    for (i = 0; i < found; i++)
        if (peaks[i].prominence_db > out.strongest)
            out.strongest = peaks[i].prominence_db;
    return out;
}

/* A range narrow enough to keep the probe quick while leaving the fold depth
   as it is on a real sweep of that width: what matters is bin_hz and the
   blocks, not how many steps are walked to fill the array. */
static void measure(const char *label, double from_hz, double to_hz,
                    double dwell, int draws) {
    struct survey_plan plan;
    double fft_bin_hz = 2000000.0 / (double)SDR_DSP_FFT_SIZE;
    int blocks = survey_blocks_in(dwell);
    double depth;
    const float bars[] = { 0.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f };
    int worst[sizeof(bars) / sizeof(*bars)];
    float peak_seen = 0.0f;
    size_t b;

    if (survey_plan_make(from_hz, to_hz, 2000000.0, SDR_DSP_FFT_SIZE, dwell,
                         &plan) != SURVEY_PLAN_OK) {
        printf("  %-22s cannot be planned\n", label);
        return;
    }
    depth = survey_fold_depth(plan.bin_hz, fft_bin_hz, blocks);
    for (b = 0; b < sizeof(bars) / sizeof(*bars); b++)
        worst[b] = 0;

    double sd = 0.0, worst_step = 0.0;
    for (int draw = 0; draw < draws; draw++) {
        rng_state = 0x9e3779b97f4a7c15ULL + (uint64_t)draw * 0x1000193ULL;
        fold_a_sweep(&plan, blocks);
        {
            double this_sd, this_step;
            spread(&plan, &this_sd, &this_step);
            if (this_sd > sd)
                sd = this_sd;
            if (this_step > worst_step)
                worst_step = this_step;
        }
        for (b = 0; b < sizeof(bars) / sizeof(*bars); b++) {
            struct outcome out = look(&plan, bars[b]);
            if (out.candidates > worst[b])
                worst[b] = out.candidates;
            if (b == 0 && out.strongest > peak_seen)
                peak_seen = out.strongest;
        }
    }

    printf("  %-22s %5.0f kHz bins, %d block%s, fold %6.0f\n",
           label, plan.bin_hz / 1e3, blocks, blocks == 1 ? " " : "s", depth);
    printf("    the folded array: sd %.2f dB, biggest bin-to-bin step %.2f dB,"
           " worst noise peak %.1f dB\n", sd, worst_step, (double)peak_seen);
    printf("    false candidates by bar: ");
    for (b = 0; b < sizeof(bars) / sizeof(*bars); b++)
        printf("%.0f dB:%-4d ", (double)bars[b], worst[b]);
    printf("\n    chosen bar %.1f dB -> %d\n",
           (double)SURVEY_FLOOR_THRESHOLD_DB,
           look(&plan, SURVEY_FLOOR_THRESHOLD_DB).candidates);
}

int main(int argc, char **argv) {
    int draws = argc > 1 ? atoi(argv[1]) : DRAWS_DEFAULT;

    if (draws < 1)
        draws = DRAWS_DEFAULT;
    sdr_dsp_init(&dsp);

    printf("Pure noise through the real transform and fold, %d draws each.\n",
           draws);
    printf("A survey of nothing should report nothing. The bar that first\n"
           "achieves that, with margin, is the threshold worth having.\n\n");

    /* The shapes a survey actually takes, from the finest band to the whole
       tuner. The fold deepens with the bin width, which is why one constant
       cannot serve both ends. */
    measure("2 MHz band, 0.1 s", 240e6, 242e6, 0.10, draws);
    measure("2 MHz band, 0.3 s", 240e6, 242e6, 0.30, draws);
    measure("20 MHz band, 0.1 s", 88e6, 108e6, 0.10, draws);
    measure("20 MHz band, 0.3 s", 88e6, 108e6, 0.30, draws);
    measure("216 MHz band, 0.1 s", 1550e6, 1766e6, 0.10, draws);
    measure("220 MHz band, 0.2 s", 470e6, 690e6, 0.20, draws);
    measure("whole tuner, 0.1 s", 24e6, 1766e6, 0.10, draws);
    return 0;
}
