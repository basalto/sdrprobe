#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "view.h"
#include "survey_sweep.h"
#include "survey_suspect.h"
#include "survey_store.h"
#include "survey_carrier.h"
#include "site_history.h"
#include <time.h>

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

/*
 * Wait briefly for a block, and process it if one is there. Returns 1 when a
 * block was consumed and turned into a spectrum, 0 when none had arrived, and
 * -1 when the source has ended.
 *
 * What to *do* with it is the caller's, and that is the point of the split: a
 * block consumed before a retune's settle is over was in the pipeline while
 * the tuner was still moving, so it holds the previous tuning's samples.
 * Folding it writes that tuning's signal into these bins, at a frequency
 * nothing is transmitting on -- survey_sweep.h says so, the window has always
 * obeyed it, and this path did not: it folded every block it consumed, which
 * on a 0.10 s settle and a 0.10 s dwell was a third of everything it measured.
 * It is still consumed, because leaving it in the slot only means meeting it
 * again a moment later.
 */
static int next_block(struct app *app, struct slot_snapshot *snapshot) {
    struct timespec pause = { 0, 2 * 1000 * 1000L };

    if (consume_latest(&app->acq, snapshot))
        return process_block(app, 0.0) > 0 ? 1 : 0;
    if (snapshot->worker_failed || snapshot->worker_done)
        return -1;
    nanosleep(&pause, NULL);
    return 0;
}

/* A capture holds one tuning and never retunes, so nothing it delivers is
   stale and every block counts. Returns 0 when the source has ended. */
static int fold_next_block(struct app *app, const struct survey_plan *plan,
                           struct slot_snapshot *snapshot, int *folded) {
    int got = next_block(app, snapshot);

    if (got < 0)
        return 0;
    if (got > 0) {
        fold_spectrum(app, plan);
        (*folded)++;
    }
    return 1;
}


/* The sweep's maxima grouped into signals, decided once so the report, the
   confirmation pass and the saved file cannot disagree about how many there
   were. */
static int survey_carriers_now(const struct survey_plan *plan,
                               const struct sdr_peak *peaks, int count,
                               struct survey_carrier *out, int max) {
    return survey_carriers_from(survey_power, plan->bins,
                                SURVEY_SENTINEL_DBFS,
                                survey_plan_bin_centre(plan, 0), plan->bin_hz,
                                SURVEY_BANDWIDTH_DB, peaks, count, out, max);
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
    struct survey_candidate candidates[SURVEY_MAX_PEAKS];
    int suspicious = 0;
    int i, found;

    found = survey_candidates_from(app, plan, peaks, count, spectrum,
                                   candidates, SURVEY_MAX_PEAKS);
    printf("# candidate <frequency_hz> <level_dbfs> <prominence_db> "
           "<measured_hz|-> <bandwidth_hz|-> <flags|-> <allocation|->\n");
    for (i = 0; i < found; i++) {
        const struct survey_candidate *c = &candidates[i];
        char flags[64], centre[32], width[32];

        if (survey_suspect_warns(c->suspect))
            suspicious++;
        if (c->measured) {
            snprintf(centre, sizeof(centre), "%.0f", c->centre_hz);
            snprintf(width, sizeof(width), "%.0f", c->width_hz);
        } else {
            snprintf(centre, sizeof(centre), "-");
            snprintf(width, sizeof(width), "-");
        }
        printf("candidate %.0f %.1f %.1f %s %s %s %s\n", c->found_hz,
               (double)c->power_dbfs, (double)c->prominence_db, centre, width,
               survey_flag_text(c->suspect, flags, sizeof(flags)),
               c->allocation ? c->allocation : "-");
    }
    /*
     * And the same peaks grouped into signals. Printed as well as the
     * candidates rather than instead of them: the candidates are what was
     * measured and the carriers are what it was concluded to mean, and a
     * reader is entitled to both. It is also the difference between "26
     * candidates" and "6 stations", which is the number anybody actually
     * wanted.
     */
    {
        struct survey_carrier carriers[SURVEY_CARRIER_MAX];
        int carrier_count = survey_carriers_now(plan, peaks, count, carriers,
                                                SURVEY_CARRIER_MAX);
        int c;
        printf("# carrier <centre_hz> <power_centre_hz> <lower_hz> "
               "<upper_hz> <width_hz> <level_dbfs> <prominence_db> <maxima> "
               "<allocation|->\n");
        for (c = 0; c < carrier_count; c++) {
            const struct band_plan_entry *entry =
                band_plan_lookup(carriers[c].centre_hz);
            printf("carrier %.0f %.0f %.0f %.0f %.0f %.1f %.1f %d %s\n",
                   carriers[c].centre_hz, carriers[c].power_centre_hz,
                   carriers[c].lower_hz, carriers[c].upper_hz,
                   carriers[c].width_hz,
                   (double)carriers[c].peak_dbfs,
                   (double)carriers[c].prominence_db, carriers[c].peaks,
                   entry ? entry->name : "-");
        }
        printf("survey carriers %d\n", carrier_count);
    }
    /*
     * The old count of distinct measured centres is gone. It answered the same
     * question the `survey carriers` line above answers -- how many signals
     * these maxima are -- and answered it only when the whole survey came from
     * one tuning. Two lines calling different numbers "carriers" is how a
     * reader ends up trusting the wrong one, and it is how a real defect hid:
     * the aggregation split ARFCN 69's single carrier into three while this
     * line went on correctly saying one.
     */
    printf("survey candidates %d suspicious %d\n", found, suspicious);
    if (suspicious)
        printf("# suspicious candidates resemble the receiver rather than the "
               "band; nothing has been removed\n");
}

/*
 * Asking again, with no window.
 *
 * A sweep step is a tenth of a second, and a tenth of a second cannot tell a
 * transmitter from something that transmitted while it was listening. Above
 * 1.5 GHz that is not a corner case: five identical sweeps of 1550-1766 MHz
 * taken minutes apart found 0, 6, 6, 2 and 2 candidates at 30-odd dB above
 * their floors and *no frequency twice*, because the mobile-satellite bands
 * there are short bursts on channels that move. Reported as they were, each
 * sweep claimed a set of standing carriers that the next sweep contradicted.
 *
 * So every signal the sweep found is revisited: tune to it, look six times,
 * and say whether it is there. It is the same pass the window runs, through
 * the same two helpers, so the two cannot answer differently -- and it is the
 * only way the answer is reachable from a script at all (ADR-0012).
 *
 * A refuted candidate is still reported. What the pass adds is the verdict
 * beside it: this one held up, that one did not, and the third was never
 * asked. Removing it would be the silent editing ADR-0015 refuses, and would
 * also throw away the interesting half -- a signal that comes and goes is a
 * finding, not a mistake.
 */
static int survey_confirm_sweep(struct app *app, const struct survey_plan *plan,
                                const struct sdr_peak *peaks, int count,
                                struct survey_confirm_target *targets,
                                int max) {
    struct survey_carrier carriers[SURVEY_CARRIER_MAX];
    struct slot_snapshot snapshot;
    uint32_t home = app->applied_frequency;
    int carrier_count, i, asked, confirmed = 0, refuted = 0;

    carrier_count = survey_carriers_now(plan, peaks, count, carriers,
                                        SURVEY_CARRIER_MAX);
    asked = carrier_count < max ? carrier_count : max;
    for (i = 0; i < asked; i++) {
        targets[i].hz = carriers[i].centre_hz;
        targets[i].claim = SURVEY_CLAIM_NEW;
        targets[i].verdict = SURVEY_VERDICT_PENDING;
        targets[i].prominence_db = 0.0f;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    printf("# confirm <frequency_hz> <claim> <verdict> <prominence_db>\n");
    for (i = 0; i < asked && !stop_requested(); i++) {
        struct sdr_carrier_report report;
        double started;
        int looks = 0;
        int settled = 0;

        if (retune_receiver(app, (uint32_t)llround(targets[i].hz),
                            app->applied_ppm) < 0) {
            fprintf(stderr, "The receiver would not tune to %.3f MHz.\n",
                    targets[i].hz / 1e6);
            break;
        }
        survey_confirm_begin_target(app);
        started = monotonic_seconds();
        while (looks < SURVEY_CONFIRM_LOOKS && !stop_requested()) {
            double elapsed = monotonic_seconds() - started;

            int got = next_block(app, &snapshot);

            if (got < 0)
                break;
            if (!settled && elapsed < SURVEY_CONFIRM_SETTLE_SECONDS)
                continue;          /* still the previous target's spectrum */
            settled = 1;
            if (got > 0) {
                survey_confirm_look(app);
                looks++;
            } else if (elapsed > SURVEY_CONFIRM_SETTLE_SECONDS + 3.0) {
                break;             /* the blocks stopped coming */
            }
        }
        survey_confirm_decide(app, &targets[i], &report);
        if (targets[i].verdict == SURVEY_VERDICT_CONFIRMED)
            confirmed++;
        else
            refuted++;
        printf("confirm %.0f %s %s %.1f\n", targets[i].hz,
               targets[i].claim == SURVEY_CLAIM_MISSING ? "missing" : "new",
               targets[i].verdict == SURVEY_VERDICT_CONFIRMED ? "confirmed"
                                                              : "refuted",
               (double)targets[i].prominence_db);
    }
    printf("confirm-summary asked %d confirmed %d refuted %d\n", asked,
           confirmed, refuted);
    fflush(stdout);
    /* A sweep that found nothing still says so: "asked 0" is a pass that had
       nothing to ask about, and no summary line at all is a pass that did not
       run. A script reading this has to be able to tell them apart. */
    if (asked > 0 && home)
        retune_receiver(app, home, app->applied_ppm);
    return asked;
}

/*
 * Survey a capture. It holds one tuning, so there is one step, and the range
 * is whatever that tuning covers -- asking for another range would be asking
 * the capture for samples it does not contain.
 */
/*
 * Write the sweep down, as the window's Save button does.
 *
 * The same two things it does: the JSON under surveys/, which is the archive,
 * and the fold into what the site has heard, which is what makes the next
 * sweep able to say what changed. Doing only the first would leave a scripted
 * sweep unable to teach the history anything, and the history is the half that
 * answers questions.
 */
static void survey_save_run(struct app *app, const struct survey_plan *plan,
                            const struct sdr_peak *peaks, int count,
                            const float *spectrum,
                            const struct survey_confirm_target *targets,
                            int target_count) {
    struct survey_candidate candidates[SURVEY_MAX_PEAKS];
    struct survey_carrier carriers[SURVEY_CARRIER_MAX];
    char path[256];
    int found, carrier_count;

    if (!app->config.site[0]) {
        /* Refused, not saved as unknown: two sweeps with no site compare as
           the same place, which is the one way the archive misleads. */
        fprintf(stderr, "Not saving: no site is set. Use --site.\n");
        return;
    }
    found = survey_candidates_from(app, plan, peaks, count, spectrum,
                                   candidates, SURVEY_MAX_PEAKS);
    carrier_count = survey_carriers_now(plan, peaks, count, carriers,
                                        SURVEY_CARRIER_MAX);
    if (survey_store_write(app, plan, candidates, found, carriers,
                           carrier_count, targets, target_count, path,
                           sizeof(path)) < 0)
        return;
    printf("survey-saved %s candidates %d carriers %d\n", path, found,
           carrier_count);

    {
        struct site_history history;
        double hz[SURVEY_CARRIER_MAX];
        float level[SURVEY_CARRIER_MAX], prom[SURVEY_CARRIER_MAX];
        time_t now = time(NULL);
        struct tm local;
        int i, added;

        localtime_r(&now, &local);
        site_history_load(app->config.site, &history);
        for (i = 0; i < carrier_count; i++) {
            hz[i] = carriers[i].centre_hz;
            level[i] = carriers[i].peak_dbfs;
            prom[i] = carriers[i].prominence_db;
        }
        added = site_history_merge(&history, hz, level, prom, carrier_count,
                                   plan->bin_hz, local.tm_hour);
        site_history_save(&history);
        printf("survey-history site %s sweeps %d signals %d new %d quiet %d\n",
               app->config.site, history.sweeps, history.count, added,
               site_history_lost_now(&history));
    }
    fflush(stdout);
}

static int survey_capture(struct app *app) {
    struct survey_plan plan;
    struct slot_snapshot snapshot;
    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    struct sdr_peak_gate gate;
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
    /* The bar noise has to clear here, which depends on how deeply this
       capture's blocks were peak-held into each bin (ADR-0013). */
    gate.topographic_db = SURVEY_MIN_PROMINENCE_DB;
    gate.floor_db = SURVEY_FLOOR_THRESHOLD_DB;
    gate.bandwidth_db = SURVEY_BANDWIDTH_DB;
    printf("survey floor_bar %.1f\n", (double)gate.floor_db);
    count = sdr_dsp_find_peaks(survey_power, plan.bins, SURVEY_SENTINEL_DBFS,
                               &gate, app->magnitude_sorted, peaks,
                               SURVEY_MAX_PEAKS);
    report_candidates(app, &plan, peaks, count, held_spectrum);
    if (app->options.survey_save)
        survey_save_run(app, &plan, peaks, count, held_spectrum, NULL, 0);
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
    struct sdr_peak_gate gate;
    double dwell = options->survey_dwell_seconds > 0.0
                       ? options->survey_dwell_seconds
                       : SURVEY_DWELL_DEFAULT;
    struct survey_confirm_target confirmed[SURVEY_CONFIRM_MAX];
    int confirm_count = 0;
    int folded = 0;
    int discarded = 0;
    int step_folded = 0;
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
        step_folded = 0;
        do {
            int got;

            phase = survey_step_phase_at(monotonic_seconds() - started, dwell,
                                         step, plan.step_count);
            got = next_block(app, &snapshot);
            if (got < 0) {
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
            if (got > 0) {
                if (phase == SURVEY_STEP_SETTLING) {
                    discarded++;
                } else {
                    fold_spectrum(app, &plan);
                    step_folded++;
                }
            }
            phase = survey_step_phase_at(monotonic_seconds() - started, dwell,
                                         step, plan.step_count);
        } while (!survey_step_may_advance(phase, step_folded));
        folded += step_folded;
        if (ended)
            break;
    }

    printf("survey blocks %d settling %d%s\n", folded, discarded,
           ended ? " incomplete" : "");
    /* Chosen from the fold, not from a constant: a bin holding 218 transform
       bins over two blocks reaches further into the noise than one holding 27
       (ADR-0013). */
    gate.topographic_db = SURVEY_MIN_PROMINENCE_DB;
    gate.floor_db = SURVEY_FLOOR_THRESHOLD_DB;
    gate.bandwidth_db = SURVEY_BANDWIDTH_DB;
    printf("survey floor_bar %.1f\n", (double)gate.floor_db);
    count = sdr_dsp_find_peaks(survey_power, plan.bins, SURVEY_SENTINEL_DBFS,
                               &gate, app->magnitude_sorted, peaks,
                               SURVEY_MAX_PEAKS);
    /* No spectrum to measure from: it belongs to the last step only. Widths
       come back as "-" rather than as a number about the wrong signal. */
    report_candidates(app, &plan, peaks, count, NULL);
    /*
     * Ask again before saving, so what is written down carries the verdict
     * rather than the sweep's first impression. The sweep is minutes and this
     * is seconds, and it is the difference between an archive of claims and an
     * archive of findings.
     */
    if (options->survey_confirm)
        confirm_count = survey_confirm_sweep(app, &plan, peaks, count,
                                             confirmed, SURVEY_CONFIRM_MAX);
    if (app->options.survey_save)
        survey_save_run(app, &plan, peaks, count, NULL, confirmed,
                        confirm_count);
    return 0;
}

int survey_report_run(struct app *app) {
    held_valid = 0;
    if (app->receiver_mode)
        return survey_receiver(app);
    return survey_capture(app);
}
