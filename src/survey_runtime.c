/*
 * The survey's per-block step, out of the view that draws it.
 *
 * `survey_session.{c,h}` is the machine and already touches neither the
 * receiver nor a file; this is the adapter around it -- turning its events
 * into retunes, history saves and the lines a scripted watch prints -- which
 * `frame_advance()` calls on every path.
 *
 * Out of `view_survey.c` by `.scratch/layer-boundaries/issues/02-*`. The
 * view keeps the chart, the candidate list, the band picker and the clicks,
 * and calls back into these: selecting a candidate and obeying an event are
 * things a click does too, so they live with the step rather than beside the
 * drawing.
 *
 * `update_survey()` is called *every frame* with `spectrum_updated` as a
 * parameter rather than gated on it, and that is load-bearing: a look counts
 * blocks, because counting frames gave the confirmation pass six looks in a
 * tenth of a second, while a step counts time.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "debug_log.h"
#include "installation.h"
#include "runtime.h"
#include "survey_session.h"
#include "survey_window.h"

/*
 * What one block looks like to the session: samples, a spectrum, and the
 * facts about the container they came out of. No `struct app` past this
 * point, which is what lets a check drive the same machine (ADR-0012).
 */
/*
 * The four facts a candidate needs, read out of `struct app` once here rather
 * than by every caller that builds a `struct survey_record` or a view model
 * of its own -- `survey_report.c` and this file are the two.
 *
 * Declared in view.h rather than here: both callers already include it, and a
 * survey record's tuning is not only this view model's business. What moved
 * is which file defines the bodies, so that this one compiles with no
 * raylib call in it and check-survey-view-model can link `-lm` alone --
 * they used to live in view_survey.c, which draws.
 */
struct reading_clock survey_reading_clock(const struct app *app) {
    struct reading_clock clock = { 0.0, 0.0 };
    int calibrated = 0;

    if (!app)
        return clock;
    if (installation_ppm(&app->installation, &calibrated))
        clock.crystal_ppm = (double)calibrated;
    clock.applied_ppm = (double)app->applied.ppm;
    return clock;
}

void survey_tuning_from(struct survey_record_tuning *out,
                        const struct app *app) {
    memset(out, 0, sizeof(*out));
    out->centre_hz = (double)app->applied.frequency_hz;
    out->sample_rate_hz = (double)app->applied.sample_rate_hz;
    out->reference_clock_hz = app->device.reference_clock_hz;
    out->remove_dc = app->remove_dc;
    out->clock = survey_reading_clock(app);
}

struct survey_block survey_block_of(struct app *app) {
    struct survey_block b;

    memset(&b, 0, sizeof(b));
    b.i_samples = app->frame.i_samples;
    b.q_samples = app->frame.q_samples;
    b.pair_count = app->frame.pair_count;
    b.spectrum = app->frame.spectrum_average;
    b.scratch = app->frame.magnitude_sorted;
    b.centre_hz = (double)app->applied.frequency_hz;
    b.sample_rate = (double)app->applied.sample_rate_hz;
    b.reference_clock_hz = app->device.reference_clock_hz;
    b.clock = survey_reading_clock(app);
    b.remove_dc = app->remove_dc;
    b.full_scale = app->device.full_scale;
    return b;
}

/*
 * `now` is the caller's clock (`frame_advance.h`'s own rule): this used to
 * call raylib's `GetTime()` itself, which is exactly `0.0` before
 * `InitWindow()` -- confirmed, not assumed -- and every function in this
 * file that reached it is one step from being called from the headless
 * Viewer session, which never calls `InitWindow()` at all. Threaded through
 * from here down to `survey_obey()`, `survey_select()`,
 * `survey_confirm_if_asked()` and `survey_start()`.
 */
void survey_obey(struct app *app,
                        const struct survey_session_event *event,
                        double now) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;

    if (event->target_finished > 0) {
        struct survey_confirm_target *tgt = &ss->confirm.target[event->target_finished - 1];
        debug_log_write("survey-confirm", "target %d/%d %.6f MHz: hits %d/%d, verdict %d",
                        event->target_finished, ss->confirm.count,
                        tgt->hz / 1e6, tgt->hits, tgt->looks, tgt->verdict);
        if (s->confirm_printed)
            survey_print_confirm_target(tgt);
    }
    if (event->history_dirty && ss->history_loaded)
        installation_history_save(&app->installation, &ss->history);
    if (event->watch_swept && app->options.survey_watch > 0) {
        /* A watch started from the command line has nobody reading the
           status line. */
        printf("watch sweep %d carriers %d appeared %d quiet %d\n",
               ss->watch_sweeps, ss->watch_carriers, ss->watch_appeared,
               ss->watch_lost);
        fflush(stdout);
    }
    if (event->watch_finished && app->options.survey_watch > 0) {
        printf("watch-summary sweeps %d appeared %d quiet %d\n",
               ss->watch_sweeps, ss->watch_total_appeared,
               ss->watch_total_lost);
        fflush(stdout);
    }
    if (event->confirm_finished) {
        if (s->confirm_printed) {
            survey_print_confirm_summary(ss);
            s->confirm_printed = 0;
        }
        /* Inside out: returning the pass's claim puts the receiver back on the
           sweep's tuning, which is where it belongs -- not on whatever was on
           screen before the survey opened. The view keeps its own claim. */
        receiver_return(app, &s->confirm_lease_token);
    }
    if (event->marks_stale)
        survey_history_refresh(app);
    if (event->retune_hz) {
        /* Borrowed on the way in when nothing holds it yet: a confirmation
           pass nests inside the survey's own claim, and the sweep's claim is
           the one the view keeps for as long as it is up. */
        int failed;

        if (survey_session_confirming(ss)) {
            failed = receiver_lease_token_active(&s->confirm_lease_token)
                         ? retune_receiver(app, event->retune_hz,
                                           app->applied.ppm) < 0
                         : receiver_borrow_at(app, &s->confirm_lease_token,
                                              event->retune_hz, 0) < 0;
        } else {
            failed = retune_receiver(app, event->retune_hz,
                                     app->applied.ppm) < 0;
        }
        if (!failed) {
            /* The settle starts when the tuner moved, not when it was asked
               to: a retune costs about a tenth of a second, which is the
               whole of the settle. */
            survey_session_retuned(ss, now);
        } else {
            struct survey_session_event refusal;

            survey_session_retune_failed(ss, (double)event->retune_hz,
                                         &refusal);
            if (refusal.confirm_finished) {
                if (s->confirm_printed) {
                    survey_print_confirm_summary(ss);
                    s->confirm_printed = 0;
                }
                receiver_return(app, &s->confirm_lease_token);
            } else if (refusal.release_receiver) {
                receiver_restore_held(app, &s->lease_token);
            }
        }
    } else if (event->release_receiver && !event->confirm_finished &&
               !survey_session_confirming(ss)) {
        /* Back where the operator was, until they pick a candidate -- and
           still holding the receiver, because this view keeps the right to
           sweep again. */
        receiver_restore_held(app, &s->lease_token);
    }
}

/*
 * A sweep asked for on the command line asks again by itself, which is the
 * only way the pass is reachable without somebody to click it (ADR-0012).
 *
 * At the end of the sweep, and this used to be inside the peak finder where it
 * looked equivalent and was not: that runs on every block of every dwell, so
 * the trigger fired on the first block of the first step, with seven bins of
 * eight thousand measured and nothing yet to call new. It found no targets,
 * cleared the flag -- once, deliberately, so a second sweep is not a repeat --
 * and the pass never ran again. `--survey-confirm` was accepted and did
 * nothing for the rest of the run, which is what a transcript in
 * .scratch/phantom-candidates/ shows and nobody could read from it.
 */
void survey_confirm_if_asked(struct app *app, double now) {
    struct survey_view *s = &app->survey;
    struct survey_session_event event;

    if (!app->options.survey_confirm ||
        survey_session_confirming(&s->session))
        return;
    app->options.survey_confirm = 0;
    if (survey_session_confirm_changes(&s->session, now, &event) > 0) {
        s->confirm_printed = 1;
        survey_print_confirm_header();
        survey_obey(app, &event, now);
    } else {
        fprintf(stderr, "Nothing to ask again about: the sweep found nothing "
                        "this site has not heard before.\n");
    }
}

/*
 * The nth strongest candidate on screen, counting from one, or -1.
 *
 * By power rather than by frequency: a script asking for "the strongest" and
 * getting whatever happens to be lowest in the band is asking a different
 * question. Kept next to the walk it shares an ordering idea with.
 */
int survey_strongest_visible(const struct survey_view *s, int rank) {
    int taken[SURVEY_MAX_PEAKS];
    int i, r, best = -1;

    if (rank < 1 || s->session.peak_count <= 0)
        return -1;
    for (i = 0; i < s->session.peak_count; i++)
        taken[i] = 0;
    for (r = 0; r < rank; r++) {
        best = -1;
        for (i = 0; i < s->session.peak_count; i++) {
            if (taken[i] || !survey_peak_visible(s, i))
                continue;
            if (best < 0 ||
                s->session.peaks[i].power_dbfs >
                    s->session.peaks[best].power_dbfs)
                best = i;
        }
        if (best < 0)
            return -1;
        taken[best] = 1;
    }
    return best;
}

void survey_select(struct app *app, int index, double now) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;
    struct survey_session_event event;
    double hz;

    if (index < 0 || index >= ss->peak_count)
        return;
    s->selected = index;
    hz = survey_bin_hz(s, ss->peaks[index].index);
    /* Stepping the list with Up/Down can land on a candidate the window is
       zoomed past; follow it rather than selecting something invisible. */
    if (hz < s->view_lower_hz || hz > s->view_upper_hz) {
        double span = s->view_upper_hz - s->view_lower_hz;
        s->view_lower_hz = hz - span / 2.0;
        s->view_upper_hz = s->view_lower_hz + span;
        survey_clamp_view(s);
    }

    if (!app->receiver_mode) {
        /* The measurement goes, the sweep stays: the candidates are what the
           sweep found, and a click cannot empty the list it was a click in. */
        survey_session_forget_measurement(ss);
        snprintf(ss->status, sizeof(ss->status),
                 "%.4f MHz selected; measuring needs a live receiver.",
                 hz / 1e6);
        return;
    }
    survey_session_measure(ss, hz, now, &event);
    survey_obey(app, &event, now);
}

void update_survey(struct app *app, double now, int spectrum_updated) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;
    struct survey_block block = survey_block_of(app);
    struct survey_session_event event;
    int was_sweeping = survey_session_sweeping(ss);

    survey_session_tick(ss, &block, spectrum_updated, now, &event);
    if (was_sweeping && event.sweep_finished) {
        debug_log_write("survey", "sweep done, %d peaks", ss->peak_count);
        survey_confirm_if_asked(app, now);
    }
    survey_obey(app, &event, now);
    /*
     * And a script may ask for a candidate to be selected and measured.
     *
     * The detail panel and its Inspect button only exist once one has been,
     * so without this there is no way to photograph that screen -- and it is
     * a screen this program has twice shipped broken (CLAUDE.md). Not while a
     * confirmation pass is running: it has the receiver, and selecting would
     * retune it out from under the pass.
     */
    if (event.sweep_finished && app->options.survey_select > 0 &&
        !survey_session_confirming(ss)) {
        int rank = app->options.survey_select;
        int best = survey_strongest_visible(s, rank);
        app->options.survey_select = 0;
        if (best >= 0)
            survey_select(app, best, now);
    }
}

/*
 * The seven below came out of `view_survey.c` by ticket 04 of
 * `.scratch/layer-boundaries/`, and the link is what found them: every one
 * was already called from this file, so a server built without the window
 * could not link. Three are a *headless* pass's own `printf`s, which had
 * been living in the file named for the screen that pass does not have.
 *
 * The other four are `freq_window` arithmetic over `struct survey_view`,
 * `static` here only because the two adapters they stand on were -- those
 * are `survey_window.h` now.
 */
/*
 * Print what a confirmation pass settled, for a pass nobody is watching.
 *
 * A pass started from the command line has no status line and no panel, and a
 * verdict nobody can read is a verdict that may as well not have been reached
 * (ADR-0012). The same two records the headless sweep writes, through the same
 * spellings -- docs/band-surveys.md is the format.
 */
void survey_print_confirm_target(const struct survey_confirm_target *target) {
    char flags[SURVEY_FLAG_TEXT_MAX];

    /*
     * **Both frequencies**: the one the sweep asked about, which keys the row
     * and matches the `candidate` rows above it, and the one the pass
     * measured, which is what the flags are about. They can be tens of
     * kilohertz apart and used to be indistinguishable
     * (`.scratch/reading-origin/issues/03-*`).
     *
     * Appended rather than inserted, so every existing field keeps its
     * position and a reader of field three still finds the verdict -- this is
     * not a format change under ADR-0016, it is a wider row.
     */
    printf("confirm %.0f %s %s %.1f %d/%d %.0f %s %.0f\n", target->hz,
           target->claim == SURVEY_CLAIM_MISSING ? "missing" : "new",
           survey_verdict_name(target->verdict),
           (double)target->prominence_db, target->hits, target->looks,
           target->bandwidth_hz,
           survey_flag_text(target->suspicion, flags, sizeof(flags)),
           target->measured_hz);
    if (!target->kind_measured)
        return;
    printf("kind %.0f %s %.1f %.3f %.3f %s %.4f\n", target->hz,
           signal_verdict_name(signal_carrier_verdict(&target->carrier)),
           target->carrier.carrier_over_noise_db,
           target->carrier.carrier_power_fraction,
           target->envelope.found ? target->envelope.variation : -1.0,
           survey_burst_name(target->bursts.verdict),
           target->bursts.occupancy);
}

void survey_print_confirm_header(void) {
    printf("# confirm <frequency_hz> <claim> <verdict> <prominence_db> "
           "<hits>/<looks> <bandwidth_hz> <flags|-> <measured_hz|0>\n");
    printf("# kind <frequency_hz> <carrier> <over_noise_db> <standing_share> "
           "<envelope> <bursts> <occupancy>\n");
}

void survey_print_confirm_summary(const struct survey_session *ss) {
    printf("confirm-summary asked %d confirmed %d intermittent %d refuted "
           "%d\n", ss->confirm.count, ss->confirm.confirmed,
           ss->confirm.intermittent, ss->confirm.refuted);
    fflush(stdout);
}

/* The frequency at the middle of a survey bin, through the window the chart
   is drawn against rather than through the session's own range: they differ
   only before the first sweep, where the fields are the honest answer. */
double survey_bin_hz(const struct survey_view *s, int bin) {
    struct freq_window w = survey_freq_window_of(s);

    /* Bins span what was swept, so before a sweep there is nothing to index
       into and the range's low edge is the honest answer. */
    if (s->session.bins <= 0)
        return s->session.lower_hz;
    return freq_window_bin_hz(&w, s->session.bins, bin);
}

void survey_clamp_view(struct survey_view *s) {
    struct freq_window w = survey_freq_window_of(s);

    freq_window_clamp(&w, SURVEY_MIN_SPAN_HZ);
    survey_freq_window_put(s, &w);
}

/* Candidates inside the window on screen. Zooming into a band and still
   being shown a list of what is loudest elsewhere is no use, so the list, the
   count and the Up/Down walk all follow the window. */
int survey_peak_visible(const struct survey_view *s, int index) {
    struct freq_window w = survey_freq_window_of(s);

    if (index < 0 || index >= s->session.peak_count)
        return 0;
    return freq_window_bin_visible(&w, s->session.bins,
                                   s->session.peaks[index].index);
}

/*
 * Reload what this site has heard, and re-mark this sweep against it.
 *
 * The one place the history reaches a file on this side: the session holds it
 * and marks against it, and only the program knows which installation the
 * baseline belongs to (ADR-0022). Done when the site changes or a sweep ends
 * rather than per frame -- it reads a file, and the answer does not change
 * between frames. It used to be inside the peak finder, which runs on every
 * folded block.
 */
void survey_history_refresh(struct app *app) {
    struct survey_session *ss = &app->survey.session;
    struct site_history history;

    if (!app->config.site[0]) {
        survey_session_set_history(ss, NULL, 0);
        return;
    }
    if (installation_history_load(&app->installation, &history) < 0) {
        survey_session_set_history(ss, NULL, 0);
        return;
    }
    survey_session_set_history(ss, &history, 1);
}
