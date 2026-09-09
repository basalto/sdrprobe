#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "view.h"
#include "survey_session.h"
#include "survey_sweep.h"
#include "survey_suspect.h"
#include "survey_store.h"
#include "survey_carrier.h"
#include "site_history.h"

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
 * **It decides nothing.** `survey_session.h` owns the sweep, the fold, the
 * confirmation pass and the watch, and this is the printing adapter over it --
 * the window in `view_survey.c` is the other one
 * (`.scratch/deepening/issues/04-survey-session.md`). Before that split this
 * file re-sequenced the same steps, and the two copies had already drifted:
 * this one held its own peak-held spectrum and its own step loop, and its
 * `# confirm` header promised five fields where its rows carried seven.
 *
 * The output is meant to be read by a program. One record per line, a keyword
 * first so a grep is enough, integer hertz so comparisons are exact, and the
 * allocation last because it is the only field that can contain a space.
 */

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

/* One block, as the session sees it. */
static struct survey_block survey_block_now(struct app *app) {
    struct survey_block block;

    memset(&block, 0, sizeof(block));
    block.i_samples = app->i_samples;
    block.q_samples = app->q_samples;
    block.pair_count = app->pair_count;
    block.spectrum = app->spectrum_average;
    block.scratch = app->magnitude_sorted;
    block.centre_hz = (double)app->applied_frequency;
    block.sample_rate = (double)app->applied_sample_rate;
    block.reference_clock_hz = app->device.reference_clock_hz;
    block.remove_dc = app->remove_dc;
    return block;
}

/*
 * Wait briefly for a block, and process it if one is there. Returns 1 when a
 * block was consumed and turned into a spectrum, 0 when none had arrived, and
 * -1 when the source has ended.
 *
 * What to *do* with it is the session's, and that is the point of the split: a
 * block consumed before a retune's settle is over was in the pipeline while
 * the tuner was still moving, so it holds the previous tuning's samples.
 * Folding it writes that tuning's signal into these bins, at a frequency
 * nothing is transmitting on -- survey_sweep.h says so, the window has always
 * obeyed it, and this path did not until the two shared a machine: it folded
 * every block it consumed, which on a 0.10 s settle and a 0.10 s dwell was a
 * third of everything it measured. It is still consumed, because leaving it in
 * the slot only means meeting it again a moment later.
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

/*
 * Do what the session asked for: this side holds the receiver with a straight
 * retune, having no lease to nest and nothing on screen to put back.
 */
static void survey_obey_headless(struct app *app,
                                 const struct survey_session_event *event,
                                 int printing) {
    struct survey_session *ss = &app->survey.session;

    if (printing && event->target_finished > 0)
        survey_print_confirm_target(
            &ss->confirm.target[event->target_finished - 1]);
    if (event->history_dirty && ss->history_loaded)
        installation_history_save(&app->installation, &ss->history);
    if (event->watch_swept) {
        printf("watch sweep %d carriers %d appeared %d quiet %d\n",
               ss->watch_sweeps, ss->watch_carriers, ss->watch_appeared,
               ss->watch_lost);
        fflush(stdout);
    }
    if (event->watch_finished) {
        printf("watch-summary sweeps %d appeared %d quiet %d\n",
               ss->watch_sweeps, ss->watch_total_appeared,
               ss->watch_total_lost);
        fflush(stdout);
    }
    if (event->sweep_stopped)
        fprintf(stderr, "%s\n", ss->status);
    if (event->retune_hz) {
        if (retune_receiver(app, event->retune_hz, app->applied_ppm) < 0) {
            struct survey_session_event refusal;

            fprintf(stderr, "The receiver would not tune to %.3f MHz.\n",
                    (double)event->retune_hz / 1e6);
            survey_session_retune_failed(ss, (double)event->retune_hz,
                                         &refusal);
        } else {
            /* The settle starts when the tuner moved, not when it was asked
               to: a retune flushes the pipeline and costs about a tenth of a
               second, which is the whole of the settle. */
            survey_session_retuned(ss, monotonic_seconds());
        }
    }
}

/*
 * Drive the session until it stops. Returns 0 when the source ended under it.
 *
 * One loop for the sweep, the confirmation pass and the measurement, because
 * the session makes them one machine: feed it whatever arrives, obey what it
 * asks for. The two shapes this replaced -- a `do/while` over
 * `survey_step_may_advance` for the sweep and a nested `while` over the looks
 * for the pass -- were the same loop written twice with different bugs.
 */
static int survey_drive(struct app *app, struct slot_snapshot *snapshot,
                        int printing) {
    struct survey_session *ss = &app->survey.session;

    while (ss->state != SURVEY_SESSION_IDLE && !stop_requested()) {
        struct survey_session_event event;
        struct survey_block block;
        int got = next_block(app, snapshot);

        if (got < 0) {
            survey_session_source_ended(ss,
                                        snapshot->worker_error[0]
                                            ? snapshot->worker_error : NULL,
                                        &event);
            survey_obey_headless(app, &event, printing);
            return 0;
        }
        block = survey_block_now(app);
        survey_session_tick(ss, &block, got > 0, monotonic_seconds(), &event);
        survey_obey_headless(app, &event, printing);
    }
    return 1;
}

/*
 * Print the candidates. The spectrum a candidate may be measured out of is the
 * session's to hand over, and it refuses across a swept range: there the held
 * spectrum belongs to whichever step was last, and a bandwidth read from it
 * would be a number about the wrong signal.
 */
static void report_candidates(struct app *app) {
    const struct survey_session *ss = &app->survey.session;
    struct survey_candidate candidates[SURVEY_MAX_PEAKS];
    int suspicious = 0;
    int i, found;

    found = survey_candidates_from(app, &ss->plan, ss->peaks, ss->peak_count,
                                   survey_session_spectrum(ss), candidates,
                                   SURVEY_MAX_PEAKS);
    /*
     * `extent_hz` is the width in the sweep's own bins, and `resolved` says
     * whether that width means anything. A full-tuner sweep puts 212 kHz in a
     * bin, so a 25 kHz carrier and a 3 kHz spur both come back as one or two
     * bins and the number is a floor rather than a measurement. It was
     * computed for every candidate and never reported, which left a reader
     * unable to tell narrow from unresolvable.
     */
    printf("# candidate <frequency_hz> <level_dbfs> <prominence_db> "
           "<measured_hz|-> <bandwidth_hz|-> <extent_hz> <resolved|floor> "
           "<flags|-> <allocation|->\n");
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
        printf("candidate %.0f %.1f %.1f %s %s %.0f %s %s %s\n",
               c->found_hz, (double)c->power_dbfs, (double)c->prominence_db,
               centre, width, c->extent_hz,
               survey_extent_is_floor(c->extent_hz, ss->plan.bin_hz)
                   ? "floor" : "resolved",
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
    printf("# carrier <centre_hz> <power_centre_hz> <lower_hz> "
           "<upper_hz> <width_hz> <level_dbfs> <prominence_db> <maxima> "
           "<allocation|->\n");
    for (i = 0; i < ss->carrier_count; i++) {
        const struct band_plan_entry *entry =
            band_plan_lookup(ss->carriers[i].centre_hz);
        printf("carrier %.0f %.0f %.0f %.0f %.0f %.1f %.1f %d %s\n",
               ss->carriers[i].centre_hz, ss->carriers[i].power_centre_hz,
               ss->carriers[i].lower_hz, ss->carriers[i].upper_hz,
               ss->carriers[i].width_hz,
               (double)ss->carriers[i].peak_dbfs,
               (double)ss->carriers[i].prominence_db,
               ss->carriers[i].peaks,
               entry ? entry->name : "-");
    }
    printf("survey carriers %d\n", ss->carrier_count);
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
 * the same machine, so the two cannot answer differently -- and it is the only
 * way the answer is reachable from a script at all (ADR-0012).
 *
 * A refuted candidate is still reported. What the pass adds is the verdict
 * beside it: this one held up, that one did not, and the third was never
 * asked. Removing it would be the silent editing ADR-0015 refuses, and would
 * also throw away the interesting half -- a signal that comes and goes is a
 * finding, not a mistake.
 */
static int survey_confirm_sweep(struct app *app,
                                struct slot_snapshot *snapshot) {
    struct survey_session *ss = &app->survey.session;
    struct survey_session_event event;
    uint32_t home = app->applied_frequency;
    int asked;

    asked = survey_session_confirm_all(ss, monotonic_seconds(), &event);
    survey_print_confirm_header();
    if (asked > 0) {
        survey_obey_headless(app, &event, 1);
        survey_drive(app, snapshot, 1);
    }
    /* A sweep that found nothing still says so: "asked 0" is a pass that had
       nothing to ask about, and no summary line at all is a pass that did not
       run. A script reading this has to be able to tell them apart. */
    survey_print_confirm_summary(ss);
    if (asked > 0 && home)
        retune_receiver(app, home, app->applied_ppm);
    return ss->confirm.count;
}

/*
 * Write the sweep down, as the window's Save button does.
 *
 * The same two things it does: the JSON under surveys/, which is the archive,
 * and the fold into what the site has heard, which is what makes the next
 * sweep able to say what changed. Doing only the first would leave a scripted
 * sweep unable to teach the history anything, and the history is the half that
 * answers questions.
 */
static void survey_save_run(struct app *app) {
    const struct survey_session *ss = &app->survey.session;
    struct survey_candidate candidates[SURVEY_MAX_PEAKS];
    char path[256];
    int found;

    if (!app->config.site[0]) {
        /* Refused, not saved as unknown: two sweeps with no site compare as
           the same place, which is the one way the archive misleads. */
        fprintf(stderr, "Not saving: no site is set. Use --site.\n");
        return;
    }
    found = survey_candidates_from(app, &ss->plan, ss->peaks, ss->peak_count,
                                   survey_session_spectrum(ss), candidates,
                                   SURVEY_MAX_PEAKS);
    if (survey_store_write(app, &ss->plan, candidates, found, ss->carriers,
                           ss->carrier_count, ss->confirm.target,
                           ss->confirm.count, path, sizeof(path)) < 0)
        return;
    printf("survey-saved %s candidates %d carriers %d\n", path, found,
           ss->carrier_count);

    {
        struct site_history history;
        double hz[SURVEY_CARRIER_MAX];
        float level[SURVEY_CARRIER_MAX], prom[SURVEY_CARRIER_MAX];
        time_t now = time(NULL);
        struct tm local;
        int i, added;

        localtime_r(&now, &local);
        installation_history_load(&app->installation, &history);
        for (i = 0; i < ss->carrier_count; i++) {
            hz[i] = ss->carriers[i].centre_hz;
            level[i] = ss->carriers[i].peak_dbfs;
            prom[i] = ss->carriers[i].prominence_db;
        }
        added = site_history_merge(&history, hz, level, prom,
                                   ss->carrier_count, ss->plan.bin_hz,
                                   local.tm_hour);
        if (installation_history_save(&app->installation, &history) == 0) {
            printf("survey-history site %s sweeps %d signals %d new %d "
                   "quiet %d\n", app->installation.site, history.sweeps,
                   history.count, added,
                   site_history_lost_now(&history));
        } else {
            /*
             * ADR-0022 keys a baseline by receiver, site and antenna, and
             * refuses to write one that cannot say what produced it -- a
             * capture reports no receiver, so this is the ordinary answer for
             * a replayed file rather than a fault. Saying so beats a line
             * that reads as though something was recorded.
             */
            printf("survey-history not kept: a baseline needs a receiver, a "
                   "site and an antenna\n");
        }
    }
    fflush(stdout);
}

/* What the sweep cost, in blocks: how many were folded and how many arrived
   while the tuner was still moving. */
static void survey_print_blocks(const struct survey_session *ss) {
    printf("survey blocks %d settling %d%s\n", ss->blocks_folded,
           ss->blocks_discarded,
           ss->source_ended && ss->step + 1 < ss->step_count
               ? " incomplete" : "");
}

static void survey_print_floor_bar(void) {
    /* Chosen from the fold, not from a constant: a bin holding 218 transform
       bins over two blocks reaches further into the noise than one holding 27
       (ADR-0013). */
    printf("survey floor_bar %.1f\n", (double)SURVEY_FLOOR_THRESHOLD_DB);
}

/*
 * Survey a capture. It holds one tuning, so there is one step, and the range
 * is whatever that tuning covers -- asking for another range would be asking
 * the capture for samples it does not contain.
 */
static int survey_capture(struct app *app) {
    struct survey_session *ss = &app->survey.session;
    struct slot_snapshot snapshot;
    struct survey_session_event event;
    double rate = (double)app->applied_sample_rate;
    double centre = (double)app->applied_frequency;

    if (survey_session_one_tuning(ss, centre, rate, &event) !=
        SURVEY_PLAN_OK) {
        fprintf(stderr, "Cannot plan a survey at %.3f MHz.\n", centre / 1e6);
        return -1;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    survey_drive(app, &snapshot, 0);
    if (snapshot.worker_failed) {
        fprintf(stderr, "Acquisition failed: %s\n", snapshot.worker_error);
        return -1;
    }
    if (ss->blocks_folded == 0) {
        fprintf(stderr, "No spectrum was produced from the capture.\n");
        return -1;
    }

    survey_print_installation(app);
    printf("survey range %.0f %.0f\n", ss->plan.lower_hz, ss->plan.upper_hz);
    printf("survey steps %d bins %d bin_hz %.1f blocks %d\n",
           ss->plan.step_count, ss->plan.bins, ss->plan.bin_hz,
           ss->blocks_folded);
    survey_print_floor_bar();
    report_candidates(app);
    if (app->options.survey_save)
        survey_save_run(app);
    return 0;
}

/* Sweep a receiver across the range asked for, folding whatever arrives in
   each step's dwell. The same machine the window runs, without the frame loop
   around it. */
static int survey_receiver(struct app *app) {
    const struct options *options = &app->options;
    struct survey_session *ss = &app->survey.session;
    struct slot_snapshot snapshot;
    struct survey_session_event event;
    double dwell = options->survey_dwell_seconds > 0.0
                       ? options->survey_dwell_seconds
                       : SURVEY_DWELL_DEFAULT;

    switch (survey_session_sweep(ss, (double)options->survey_from_hz,
                                 (double)options->survey_to_hz,
                                 (double)app->applied_sample_rate, dwell,
                                 monotonic_seconds(), &event)) {
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

    survey_print_installation(app);
    printf("survey range %.0f %.0f\n", ss->plan.lower_hz, ss->plan.upper_hz);
    printf("survey steps %d bins %d bin_hz %.1f dwell %.2f estimate_s %.0f\n",
           ss->plan.step_count, ss->plan.bins, ss->plan.bin_hz, dwell,
           ss->plan.seconds);
    fflush(stdout);

    memset(&snapshot, 0, sizeof(snapshot));
    survey_obey_headless(app, &event, 0);
    survey_drive(app, &snapshot, 0);

    survey_print_blocks(ss);
    survey_print_floor_bar();
    report_candidates(app);
    /*
     * Ask again before saving, so what is written down carries the verdict
     * rather than the sweep's first impression. The sweep is minutes and this
     * is seconds, and it is the difference between an archive of claims and an
     * archive of findings.
     */
    if (options->survey_confirm)
        survey_confirm_sweep(app, &snapshot);
    if (app->options.survey_save)
        survey_save_run(app);
    return 0;
}

int survey_report_run(struct app *app) {
    struct survey_session *ss = &app->survey.session;

    survey_session_reset(ss);
    /*
     * What this site has heard, so a scripted sweep's marks mean something and
     * a confirmation pass can teach it what it settled. Loaded here for the
     * reason the window loads it on the way in: the session holds a history
     * and never reaches a file (ADR-0022).
     */
    if (app->config.site[0]) {
        struct site_history history;

        if (installation_history_load(&app->installation, &history) == 0)
            survey_session_set_history(ss, &history, 1);
    }
    if (app->receiver_mode)
        return survey_receiver(app);
    return survey_capture(app);
}
