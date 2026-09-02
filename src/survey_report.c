#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "view.h"
#include "survey_sweep.h"
#include "survey_suspect.h"

/*
 * The band survey with no window and nobody watching: sweep, then print what
 * was found, one candidate to a line.
 *
 * Everything else the survey knows how to do could only be reached by a person
 * clicking on it. That makes it the one part of the program an agent cannot
 * check at all -- not the arithmetic, which has its own checks, but the
 * question that actually matters: *given this signal, does the survey report
 * the right candidates?* A capture surveyed here answers that in a second,
 * repeatably, and `make check` can ask it (ADR-0012, layer 2).
 *
 * It also does something for the operator that clicking cannot: an agent can
 * sweep the same range twice an hour apart and diff the two.
 *
 * The output is meant to be read by a program. One record per line, a keyword
 * first so a grep is enough, integer hertz so comparisons are exact, and the
 * allocation last because it is the only field that can contain a space.
 */

/* The array the sweep folds into, and the spectrum it was folded from. Static
   rather than on the stack: SURVEY_BINS floats is 32 KB, and this runs on the
   main thread beside everything else. */
/*
 * What the sweep was taken with, printed before anything it found.
 *
 * Levels only compare between sweeps taken the same way, so a survey that does
 * not say what it was taken with cannot be a baseline for the next one. The
 * site has no default and is omitted when unset: two sweeps both labelled with
 * a guess would compare as the same place, which is the one error this is
 * meant to prevent.
 */
static void survey_print_installation(const struct app *app) {
    if (app->config.antenna[0])
        printf("survey antenna %s\n", app->config.antenna);
    if (app->config.site[0])
        printf("survey site %s\n", app->config.site);
    if (app->applied_gain_tenths > 0)
        printf("survey gain %.1f\n", (double)app->applied_gain_tenths / 10.0);
}

static float survey_power[SURVEY_BINS];
static float held_spectrum[SDR_DSP_FFT_SIZE];
static int held_valid;

static void fold_spectrum(struct app *app, const struct survey_plan *plan) {
    double rate = (double)app->applied_sample_rate;
    double centre = (double)app->applied_frequency;
    double bin_hz = rate / (double)SDR_DSP_FFT_SIZE;
    double lower = centre - rate / 2.0;

    for (int i = 0; i < SDR_DSP_FFT_SIZE; i++) {
        double hz = lower + ((double)i + 0.5) * bin_hz;
        int bin;

        if (!survey_fold_keeps(hz, centre, rate))
            continue;
        bin = survey_plan_bin_at(plan, hz);
        if (bin < 0)
            continue;
        survey_power[bin] = survey_fold_hold(survey_power[bin],
                                             app->spectrum_average[i]);
    }
    /* Keep the spectrum itself too, peak-held the same way. A single-tuning
       survey can measure its own candidates out of this without retuning,
       which is what makes a capture's report complete. */
    for (int i = 0; i < SDR_DSP_FFT_SIZE; i++)
        held_spectrum[i] = held_valid
                               ? survey_fold_hold(held_spectrum[i],
                                                  app->spectrum_average[i])
                               : app->spectrum_average[i];
    held_valid = 1;
}

/* Wait for one block and fold it in. Returns 0 when the source has ended. */
static int fold_next_block(struct app *app, const struct survey_plan *plan,
                           struct slot_snapshot *snapshot, int *folded) {
    struct timespec pause = { 0, 2 * 1000 * 1000L };

    if (consume_latest(&app->acq, snapshot)) {
        if (process_block(app, 0.0) > 0) {
            fold_spectrum(app, plan);
            (*folded)++;
        }
        return 1;
    }
    if (snapshot->worker_failed || snapshot->worker_done)
        return 0;
    nanosleep(&pause, NULL);
    return 1;
}

static const char *flag_text(unsigned flags, char *buffer, size_t size) {
    size_t used = 0;

    buffer[0] = '\0';
    if (flags & SURVEY_SUSPECT_REFERENCE)
        used += (size_t)snprintf(buffer + used, size - used, "reference");
    if (flags & SURVEY_SUSPECT_STEP_CENTRE)
        used += (size_t)snprintf(buffer + used, size - used, "%sstep-centre",
                                 used ? "," : "");
    if (flags & SURVEY_SUSPECT_UNRESOLVED)
        used += (size_t)snprintf(buffer + used, size - used, "%sunresolved",
                                 used ? "," : "");
    return used ? buffer : "-";
}

/*
 * Print the candidates. `spectrum` is non-NULL only when the whole survey came
 * from one tuning, in which case every candidate can be measured out of it;
 * across a swept range the spectrum belongs to whichever step happened to be
 * last, and a bandwidth read from it would be a number about the wrong signal.
 */
static void report_candidates(struct app *app, const struct survey_plan *plan,
                              const struct sdr_peak *peaks, int count,
                              const float *spectrum) {
    double centres[SURVEY_MAX_PEAKS];
    int centre_count = 0;
    int suspicious = 0;

    printf("# candidate <frequency_hz> <level_dbfs> <prominence_db> "
           "<measured_hz|-> <bandwidth_hz|-> <flags|-> <allocation|->\n");
    for (int i = 0; i < count; i++) {
        double found_hz = survey_plan_bin_centre(plan, peaks[i].index);
        double judged_hz = found_hz;
        struct sdr_carrier_report report;
        int measured = 0;
        double bandwidth = 0.0;
        char flags[64];
        char centre[32];
        char width[32];
        const struct band_plan_entry *entry;
        unsigned suspect;

        if (spectrum) {
            measured = sdr_dsp_characterise_carrier(
                spectrum, SDR_DSP_FFT_SIZE, (double)app->applied_frequency,
                (double)app->applied_sample_rate, found_hz, 200000.0,
                SURVEY_BANDWIDTH_DB, app->magnitude_sorted, &report);
            if (measured) {
                bandwidth = report.bandwidth_hz;
                /* The measurement is the better frequency, so the comb test
                   is applied to it -- but the candidate is still reported
                   where the survey found it. Several peaks inside one wide
                   carrier all measure to the same centre, and printing that
                   centre in place of each would hide the fact that the peak
                   finder returned several. */
                judged_hz = report.centre_hz;
            }
        }
        suspect = survey_suspect(plan, judged_hz, bandwidth,
                                 (double)app->applied_sample_rate,
                                 SDR_DSP_FFT_SIZE, app->remove_dc);
        if (survey_suspect_warns(suspect))
            suspicious++;
        if (measured) {
            int seen = 0;

            snprintf(centre, sizeof(centre), "%.0f", judged_hz);
            snprintf(width, sizeof(width), "%.0f", bandwidth);
            for (int k = 0; k < centre_count; k++)
                if (fabs(centres[k] - judged_hz) <=
                    (double)app->applied_sample_rate / SDR_DSP_FFT_SIZE)
                    seen = 1;
            if (!seen && centre_count < SURVEY_MAX_PEAKS)
                centres[centre_count++] = judged_hz;
        } else {
            snprintf(centre, sizeof(centre), "-");
            snprintf(width, sizeof(width), "-");
        }
        entry = band_plan_lookup(judged_hz);
        printf("candidate %.0f %.1f %.1f %s %s %s %s\n", found_hz,
               (double)peaks[i].power_dbfs, (double)peaks[i].prominence_db,
               centre, width, flag_text(suspect, flags, sizeof(flags)),
               entry ? entry->name : "-");
    }
    printf("survey candidates %d suspicious %d", count, suspicious);
    if (spectrum)
        printf(" carriers %d", centre_count);
    printf("\n");
    if (spectrum && centre_count < count)
        printf("# several candidates measured to the same centre: a wide "
               "carrier has more than one local maximum\n");
    if (suspicious)
        printf("# suspicious candidates resemble the receiver rather than the "
               "band; nothing has been removed\n");
}

/*
 * Survey a capture. It holds one tuning, so there is one step, and the range
 * is whatever that tuning covers -- asking for another range would be asking
 * the capture for samples it does not contain.
 */
static int survey_capture(struct app *app) {
    struct survey_plan plan;
    struct slot_snapshot snapshot;
    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    double rate = (double)app->applied_sample_rate;
    double centre = (double)app->applied_frequency;
    double usable = rate * SURVEY_USABLE_SPAN;
    int folded = 0;
    int count;

    if (survey_plan_make(centre - usable / 2.0, centre + usable / 2.0, rate,
                         SDR_DSP_FFT_SIZE, SURVEY_DWELL_MIN,
                         &plan) != SURVEY_PLAN_OK) {
        fprintf(stderr, "Cannot plan a survey at %.3f MHz.\n", centre / 1e6);
        return -1;
    }
    for (int i = 0; i < plan.bins; i++)
        survey_power[i] = SURVEY_SENTINEL_DBFS;

    memset(&snapshot, 0, sizeof(snapshot));
    while (!stop_requested() &&
           fold_next_block(app, &plan, &snapshot, &folded))
        ;
    if (snapshot.worker_failed) {
        fprintf(stderr, "Acquisition failed: %s\n", snapshot.worker_error);
        return -1;
    }
    if (folded == 0) {
        fprintf(stderr, "No spectrum was produced from the capture.\n");
        return -1;
    }

    survey_print_installation(app);
    printf("survey range %.0f %.0f\n", plan.lower_hz, plan.upper_hz);
    printf("survey steps %d bins %d bin_hz %.1f blocks %d\n", plan.step_count,
           plan.bins, plan.bin_hz, folded);
    count = sdr_dsp_find_peaks(survey_power, plan.bins, SURVEY_SENTINEL_DBFS,
                               SURVEY_MIN_PROMINENCE_DB, SURVEY_BANDWIDTH_DB,
                               app->magnitude_sorted, peaks,
                               SURVEY_MAX_PEAKS);
    report_candidates(app, &plan, peaks, count, held_spectrum);
    return 0;
}

/* Sweep a receiver across the range asked for, folding whatever arrives in
   each step's dwell. The same machine update_survey() runs, without the
   frame loop around it. */
static int survey_receiver(struct app *app) {
    const struct options *options = &app->options;
    struct survey_plan plan;
    struct slot_snapshot snapshot;
    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    double dwell = options->survey_dwell_seconds > 0.0
                       ? options->survey_dwell_seconds
                       : SURVEY_DWELL_DEFAULT;
    int folded = 0;
    int ended = 0;
    int count;

    switch (survey_plan_make((double)options->survey_from_hz,
                             (double)options->survey_to_hz,
                             (double)app->applied_sample_rate,
                             SDR_DSP_FFT_SIZE, dwell, &plan)) {
    case SURVEY_PLAN_BAD_RANGE:
        fprintf(stderr, "The high edge of the range must be above the low "
                        "one.\n");
        return -1;
    case SURVEY_PLAN_BAD_DWELL:
        fprintf(stderr, "Dwell must be between %.2f and %.0f seconds.\n",
                SURVEY_DWELL_MIN, SURVEY_DWELL_MAX);
        return -1;
    case SURVEY_PLAN_BAD_RATE:
        fprintf(stderr, "Sample rate is too low to sweep.\n");
        return -1;
    case SURVEY_PLAN_OK:
        break;
    }
    for (int i = 0; i < plan.bins; i++)
        survey_power[i] = SURVEY_SENTINEL_DBFS;

    survey_print_installation(app);
    printf("survey range %.0f %.0f\n", plan.lower_hz, plan.upper_hz);
    printf("survey steps %d bins %d bin_hz %.1f dwell %.2f estimate_s %.0f\n",
           plan.step_count, plan.bins, plan.bin_hz, dwell, plan.seconds);
    fflush(stdout);

    memset(&snapshot, 0, sizeof(snapshot));
    for (int step = 0; step < plan.step_count && !stop_requested();
         step++) {
        double centre = survey_plan_step_centre(&plan, step);
        double started;
        enum survey_step_phase phase;

        if (retune_receiver(app, (uint32_t)llround(centre),
                            app->applied_ppm) < 0) {
            fprintf(stderr, "The receiver would not tune to %.3f MHz.\n",
                    centre / 1e6);
            return -1;
        }
        started = monotonic_seconds();
        do {
            if (!fold_next_block(app, &plan, &snapshot, &folded)) {
                /* The receiver stopped delivering. Retuning to the next step
                   would sweep a dead worker in silence, so say so and keep
                   whatever was folded up to here. */
                fprintf(stderr,
                        "Acquisition ended during step %d of %d: %s\n",
                        step + 1, plan.step_count,
                        snapshot.worker_error[0] ? snapshot.worker_error
                                                 : "no more blocks");
                ended = 1;
                break;
            }
            phase = survey_step_phase_at(monotonic_seconds() - started, dwell,
                                         step, plan.step_count);
        } while (phase == SURVEY_STEP_SETTLING || phase == SURVEY_STEP_DWELLING);
        if (ended)
            break;
    }

    printf("survey blocks %d%s\n", folded, ended ? " incomplete" : "");
    count = sdr_dsp_find_peaks(survey_power, plan.bins, SURVEY_SENTINEL_DBFS,
                               SURVEY_MIN_PROMINENCE_DB, SURVEY_BANDWIDTH_DB,
                               app->magnitude_sorted, peaks,
                               SURVEY_MAX_PEAKS);
    /* No spectrum to measure from: it belongs to the last step only. Widths
       come back as "-" rather than as a number about the wrong signal. */
    report_candidates(app, &plan, peaks, count, NULL);
    return 0;
}

int survey_report_run(struct app *app) {
    held_valid = 0;
    if (app->receiver_mode)
        return survey_receiver(app);
    return survey_capture(app);
}
