#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "view.h"
#include "survey_layout.h"
#include "row_list.h"
#include "freq_window.h"
#include "survey_suspect.h"
#include "survey_store.h"
#include "band_plan_view.h"
#include "survey_bands.h"
#include "lte_dsp.h"
#include "sdrgui.h"

/*
 * The band survey: sweep a range, chart what is on it, and measure whichever
 * candidate the operator picks.
 *
 * This is the Probe context, and the boundary is load-bearing here in a way it
 * is nowhere else in the program. The survey measures — power, occupied
 * bandwidth, prominence, offset, duty, stability — and it looks a frequency up
 * in a band plan. It never says what a signal is: nothing has been
 * demodulated, and a carrier inside the GSM downlink allocation is a carrier
 * inside an allocation. The words on screen carry that distinction, because
 * the code alone cannot (ADR-0015).
 *
 * **The sweep is not here.** `survey_session.h` owns the machine -- when a
 * step is over, when the fold is applied, what a confirmation pass asks about,
 * what a watch reports -- and this file is one of its two adapters
 * (`.scratch/deepening/issues/04-survey-session.md`, ADR-0012). What is left
 * is the window: the text fields, the frequency window, the chart, the
 * candidate list, and the receiver, which the session asks for and never
 * touches.
 */

int survey_editing(const struct app *app) {
    return app->tab == TAB_SURVEY &&
           app->survey.focus >= 0;
}

static int survey_start(struct app *app);
static void survey_keep_current(struct survey_view *s);
static void survey_sweep_span(struct app *app, double from, double to);
static int survey_sweep_target(const struct survey_view *s, double *from,
                               double *to);
static void survey_history_refresh(struct app *app);
static void survey_select(struct app *app, int index);

/* Back into the spelling the field takes, so a range given on the command
   line reads the way someone would have typed it. */
static void survey_format_hz(char *text, size_t size, uint32_t hz) {
    if (hz % 1000000U == 0)
        snprintf(text, size, "%uM", hz / 1000000U);
    else if (hz % 1000U == 0)
        snprintf(text, size, "%uK", hz / 1000U);
    else
        snprintf(text, size, "%u", hz);
}

static struct survey_layout survey_layout_now(void) {
    return survey_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

/* The hour of the day, for the clock the history keeps. */
static int survey_hour_of_day(void) {
    time_t now = time(NULL);
    struct tm local;

    localtime_r(&now, &local);
    return local.tm_hour;
}

/*
 * What one block looks like to the session: samples, a spectrum, and the
 * facts about the container they came out of. No `struct app` past this
 * point, which is what lets a check drive the same machine (ADR-0012).
 */
static struct survey_block survey_block_of(struct app *app) {
    struct survey_block b;

    memset(&b, 0, sizeof(b));
    b.i_samples = app->i_samples;
    b.q_samples = app->q_samples;
    b.pair_count = app->pair_count;
    b.spectrum = app->spectrum_average;
    b.scratch = app->magnitude_sorted;
    b.centre_hz = (double)app->applied_frequency;
    b.sample_rate = (double)app->applied_sample_rate;
    b.reference_clock_hz = app->device.reference_clock_hz;
    b.remove_dc = app->remove_dc;
    return b;
}

/*
 * Print what a confirmation pass settled, for a pass nobody is watching.
 *
 * A pass started from the command line has no status line and no panel, and a
 * verdict nobody can read is a verdict that may as well not have been reached
 * (ADR-0012). The same two records the headless sweep writes, through the same
 * spellings -- docs/band-surveys.md is the format.
 */
void survey_print_confirm_target(const struct survey_confirm_target *target) {
    char flags[64];

    printf("confirm %.0f %s %s %.1f %d/%d %.0f %s\n", target->hz,
           target->claim == SURVEY_CLAIM_MISSING ? "missing" : "new",
           survey_verdict_name(target->verdict),
           (double)target->prominence_db, target->hits, target->looks,
           target->bandwidth_hz,
           survey_flag_text(target->suspicion, flags, sizeof(flags)));
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
           "<hits>/<looks> <bandwidth_hz> <flags|->\n");
    printf("# kind <frequency_hz> <carrier> <over_noise_db> <standing_share> "
           "<envelope> <bursts> <occupancy>\n");
}

void survey_print_confirm_summary(const struct survey_session *ss) {
    printf("confirm-summary asked %d confirmed %d intermittent %d refuted "
           "%d\n", ss->confirm.count, ss->confirm.confirmed,
           ss->confirm.intermittent, ss->confirm.refuted);
    fflush(stdout);
}

/*
 * Do what the session asked for.
 *
 * The session says where it wants the tuning and never touches the receiver,
 * so this is where a lease is borrowed, a retune is attempted, and a refusal
 * is handed back. It is also where the history reaches a file: the session
 * holds one and says when it changed, and only the program knows which
 * installation it belongs to (ADR-0022).
 */
static void survey_obey(struct app *app,
                        const struct survey_session_event *event) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;

    if (event->target_finished > 0 && s->confirm_printed)
        survey_print_confirm_target(
            &ss->confirm.target[event->target_finished - 1]);
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
                                           app->applied_ppm) < 0
                         : receiver_borrow_at(app, &s->confirm_lease_token,
                                              event->retune_hz, 0) < 0;
        } else {
            failed = retune_receiver(app, event->retune_hz,
                                     app->applied_ppm) < 0;
        }
        if (!failed) {
            /* The settle starts when the tuner moved, not when it was asked
               to: a retune costs about a tenth of a second, which is the
               whole of the settle. */
            survey_session_retuned(ss, GetTime());
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
 * The window arithmetic lives in freq_window.h, as plain doubles that
 * tests/freq_window_test.c can exercise without a window or a receiver.
 * These are the adapters between it and the view's own state: the range that
 * exists is what was swept once there is a sweep, and what the fields say
 * before that.
 */
static struct freq_window freq_window_of(const struct survey_view *s) {
    const struct survey_session *ss = &s->session;
    struct freq_window w;

    w.data_lower_hz = ss->bins > 0 ? ss->lower_hz : s->field_lower_hz;
    w.data_upper_hz = ss->bins > 0 ? ss->upper_hz : s->field_upper_hz;
    w.view_lower_hz = s->view_lower_hz;
    w.view_upper_hz = s->view_upper_hz;
    return w;
}

static void freq_window_put(struct survey_view *s,
                              const struct freq_window *w) {
    s->view_lower_hz = w->view_lower_hz;
    s->view_upper_hz = w->view_upper_hz;
}

/*
 * What is suspicious about a frequency this survey found. The bandwidth is
 * only known once the candidate has been measured, so it is passed as 0 until
 * then and the two frequency tests carry the warning on their own.
 */
static unsigned survey_suspect_at(const struct app *app, double hz,
                                  double bandwidth_hz) {
    return survey_suspect(&app->survey.session.plan,
                          app->device.reference_clock_hz, hz, bandwidth_hz,
                          (double)app->applied_sample_rate, SDR_DSP_FFT_SIZE,
                          app->remove_dc);
}

/* How many of this sweep's candidates resemble the receiver. Recomputed each
   frame rather than stored: it is a few hundred multiplications, and a stored
   count is one more thing that can disagree with the list beside it. */
static int survey_suspicious_now(const struct app *app) {
    return survey_suspect_count(&app->survey.session.plan,
                                app->device.reference_clock_hz,
                                app->survey.session.peaks,
                                app->survey.session.peak_count,
                                (double)app->applied_sample_rate,
                                SDR_DSP_FFT_SIZE, app->remove_dc);
}

/* The frequency at the middle of a survey bin, through the window the chart
   is drawn against rather than through the session's own range: they differ
   only before the first sweep, where the fields are the honest answer. */
static double survey_bin_hz(const struct survey_view *s, int bin) {
    struct freq_window w = freq_window_of(s);

    /* Bins span what was swept, so before a sweep there is nothing to index
       into and the range's low edge is the honest answer. */
    if (s->session.bins <= 0)
        return s->session.lower_hz;
    return freq_window_bin_hz(&w, s->session.bins, bin);
}

static double survey_bin_width_hz(const struct survey_view *s) {
    return survey_session_bin_width_hz(&s->session);
}

/*
 * The range everything is measured against: what was swept once a sweep has
 * run, and what is typed in the fields before that. Without the second half
 * the view has no extent until the first sweep, and zooming, panning and
 * dragging a rectangle all quietly did nothing on a freshly opened survey --
 * they were dividing by a span of zero.
 */
static double survey_data_lower(const struct survey_view *s) {
    return freq_window_of(s).data_lower_hz;
}

static double survey_data_upper(const struct survey_view *s) {
    return freq_window_of(s).data_upper_hz;
}

static void survey_clamp_view(struct survey_view *s) {
    struct freq_window w = freq_window_of(s);

    freq_window_clamp(&w, SURVEY_MIN_SPAN_HZ);
    freq_window_put(s, &w);
}

static void survey_reset_view(struct survey_view *s) {
    struct freq_window w = freq_window_of(s);

    freq_window_reset(&w);
    freq_window_put(s, &w);
}

/*
 * Which field a focus index names, and what it will accept.
 *
 * The range and dwell take a frequency's spelling; the site and the antenna
 * are names a person chose, so they take anything printable. Written as a
 * lookup because the alternative -- a chain of ternaries per keystroke -- had
 * already reached three fields and would not survive five.
 */
struct survey_field {
    char *text;
    int *length;
    int capacity;
    int numeric;
};

static struct survey_field survey_field_at(struct survey_view *s, int focus) {
    struct survey_field f;
    memset(&f, 0, sizeof(f));
    switch (focus) {
    case 0: f.text = s->from;    f.length = &s->from_length;
            f.capacity = (int)sizeof(s->from);    f.numeric = 1; break;
    case 1: f.text = s->to;      f.length = &s->to_length;
            f.capacity = (int)sizeof(s->to);      f.numeric = 1; break;
    case 2: f.text = s->dwell;   f.length = &s->dwell_length;
            f.capacity = (int)sizeof(s->dwell);   f.numeric = 1; break;
    case 3: f.text = s->site;    f.length = &s->site_length;
            f.capacity = (int)sizeof(s->site);    break;
    case 4: f.text = s->antenna; f.length = &s->antenna_length;
            f.capacity = (int)sizeof(s->antenna); break;
    default: break;
    }
    return f;
}

/*
 * Write the sweep down, so the next one has something to be compared with.
 *
 * The spectrum is only handed over when the whole survey came from one tuning;
 * across a swept range it belongs to whichever step happened to be last, and a
 * bandwidth measured out of it would be a number about the wrong signal --
 * which is survey_session_spectrum()'s own refusal, and the reason both
 * adapters go through survey_candidates_from().
 */
static void survey_save_sweep(struct app *app) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;
    struct survey_candidate candidates[SURVEY_MAX_PEAKS];
    char path[256];
    int count;

    if (ss->peak_count <= 0) {
        snprintf(ss->status, sizeof(ss->status),
                 "Nothing to save yet: sweep first.");
        return;
    }
    if (!s->site[0]) {
        /* Refused rather than saved as unknown. Two sweeps with no site
           compare as the same place, which is the one way this archive can
           mislead instead of merely disappoint. */
        snprintf(ss->status, sizeof(ss->status),
                 "Name the site first -- a sweep without one cannot be "
                 "compared with another.");
        s->focus = 3;
        return;
    }
    count = survey_candidates_from(app, &ss->plan, ss->peaks, ss->peak_count,
                                   survey_session_spectrum(ss), candidates,
                                   SURVEY_MAX_PEAKS);
    /*
     * The archive and the memory are written together. If they drift apart --
     * a sweep in surveys/ that the history never saw -- the window starts
     * calling known signals new, which is worse than having no memory at all.
     */
    {
        double hz[SURVEY_CARRIER_MAX];
        float level[SURVEY_CARRIER_MAX], prom[SURVEY_CARRIER_MAX];
        struct site_history history;
        int i;
        /* Carriers, not candidates. The site remembers signals; a candidate
           is one maximum of one, and a station with four would arrive as four
           things to be surprised by next time. */
        for (i = 0; i < ss->carrier_count; i++) {
            hz[i] = ss->carriers[i].centre_hz;
            level[i] = ss->carriers[i].peak_dbfs;
            prom[i] = ss->carriers[i].prominence_db;
        }
        site_history_init(&history, app->config.site);
        installation_history_load(&app->installation, &history);
        site_history_merge(&history, hz, level, prom, ss->carrier_count,
                           ss->plan.bin_hz, survey_hour_of_day());
        installation_history_save(&app->installation, &history);
    }
    /* Whatever "Ask again" has already settled about this sweep. Cleared when
       a sweep starts, so a save can never carry the previous sweep's
       verdicts under this one's frequencies. */
    if (survey_store_write(app, &ss->plan, candidates, count, ss->carriers,
                           ss->carrier_count, ss->confirm.target,
                           ss->confirm.count, path, sizeof(path)) < 0) {
        snprintf(ss->status, sizeof(ss->status),
                 "Could not write the survey; see the terminal.");
        return;
    }
    {
        /* The name, not the path: the status line is one line and the
           directory is always the same one. */
        const char *slash = strrchr(path, '/');
        snprintf(ss->status, sizeof(ss->status),
                 "Saved %d candidates to surveys/%.64s -- compare sweeps "
                 "with scripts/survey_tool.py diff", count,
                 slash ? slash + 1 : path);
    }
    survey_history_refresh(app);
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
static void survey_history_refresh(struct app *app) {
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

/* Which signal a maximum belongs to, or NULL. The list and the popup both ask,
   because a reader points at a bump and wants to know about the carrier. */
static const struct survey_carrier *survey_carrier_at(const struct survey_view *s,
                                                      double hz) {
    return survey_session_carrier_at(&s->session, hz);
}

/* What the popup says about one remembered signal. */
static void survey_history_line(const struct site_history *history,
                                const struct site_entry *entry, char *out,
                                size_t size) {
    if (!entry) {
        snprintf(out, size, "new here -- this site has not heard it before");
        return;
    }
    if (entry->last_sweep >= history->sweeps)
        snprintf(out, size, "heard in %d of %d sweeps here", entry->sweeps,
                 history->sweeps);
    else
        snprintf(out, size,
                 "heard in %d of %d sweeps, last %d sweep%s ago at %.1f dBFS",
                 entry->sweeps, history->sweeps,
                 history->sweeps - entry->last_sweep,
                 history->sweeps - entry->last_sweep == 1 ? "" : "s",
                 (double)entry->dbfs);
}

/* The fields start from whatever the last session left. */
static void survey_load_installation(struct app *app) {
    struct survey_view *s = &app->survey;
    snprintf(s->site, sizeof(s->site), "%s", app->config.site);
    s->site_length = (int)strlen(s->site);
    snprintf(s->antenna, sizeof(s->antenna), "%s", app->config.antenna);
    s->antenna_length = (int)strlen(s->antenna);
}

/*
 * And an edit goes straight back to the file.
 *
 * Saved when the field loses focus rather than on every keystroke: a
 * half-typed site is a wrong site, and writing it would leave the wrong one
 * behind if the operator then walked away.
 */
static void survey_commit_installation(struct app *app) {
    struct survey_view *s = &app->survey;
    int changed = 0;

    if (strcmp(app->config.site, s->site)) {
        snprintf(app->config.site, sizeof(app->config.site), "%s", s->site);
        changed = 1;
    }
    if (s->antenna[0] && strcmp(app->config.antenna, s->antenna)) {
        snprintf(app->config.antenna, sizeof(app->config.antenna), "%s",
                 s->antenna);
        changed = 1;
    }
    if (changed) {
        config_remember_site(&app->config, app->config.site);
        config_remember_antenna(&app->config, app->config.antenna);
        config_save(&app->config);
        survey_history_refresh(app);
        /*
         * Moving to a site the receiver has been calibrated at restores that
         * calibration. Levels and frequencies from two sites are only
         * comparable if the receiver was corrected the same way at both, and
         * the operator should not have to remember which number went with
         * which room.
         */
        if (app->receiver_mode && app->config.site[0]) {
            int ppm = config_site_ppm(&app->config, app->config.site);
            if (ppm != app->applied_ppm &&
                retune_receiver(app, app->applied_frequency, ppm) == 0) {
                app->options.ppm = ppm;
                snprintf(s->session.status, sizeof(s->session.status),
                         "Site %s: applied its %+d PPM correction.",
                         app->config.site, ppm);
            }
        }
    }
}

static void survey_refresh_fields(struct survey_view *s) {
    uint32_t from_hz;
    uint32_t to_hz;
    double lower;
    double upper;

    if (parse_frequency(s->from, &from_hz) < 0 ||
        parse_frequency(s->to, &to_hz) < 0 || to_hz <= from_hz)
        return;
    lower = (double)from_hz;
    upper = (double)to_hz;
    if (lower == s->field_lower_hz && upper == s->field_upper_hz)
        return;
    s->field_lower_hz = lower;
    s->field_upper_hz = upper;
    /* Typing a range means wanting that range, so the window follows the
       fields. Without this a zoomed window and an edited field would disagree
       about what the one Sweep button is going to sweep. */
    s->view_lower_hz = lower;
    s->view_upper_hz = upper;
    if (s->session.bins > 0)
        survey_clamp_view(s);
}

/* Zoom about the selected candidate when there is one, so zooming in keeps
   what you picked in sight; otherwise about the middle. */
static void survey_zoom(struct survey_view *s, double factor) {
    struct freq_window w = freq_window_of(s);
    int has_anchor = s->selected >= 0 && s->selected < s->session.peak_count;
    double anchor = has_anchor
                        ? survey_bin_hz(s, s->session.peaks[s->selected].index)
                        : 0.0;

    freq_window_zoom(&w, factor, anchor, has_anchor, SURVEY_MIN_SPAN_HZ);
    freq_window_put(s, &w);
}

/* Panning a window that already spans the whole sweep cannot move it, and a
   key that silently does nothing reads as a broken key. Say which it is. */
static void survey_pan(struct app *app, double fraction) {
    struct survey_view *s = &app->survey;
    struct freq_window w = freq_window_of(s);
    double span = w.view_upper_hz - w.view_lower_hz;

    if (!freq_window_pan(&w, fraction, SURVEY_MIN_SPAN_HZ))
        snprintf(s->session.status, sizeof(s->session.status),
                 "Already showing %s of the range; zoom in first (+ or the "
                 "wheel over the chart).",
                 span >= (w.data_upper_hz - w.data_lower_hz) - 1.0
                     ? "all"
                     : "the end");
    freq_window_put(s, &w);
}

/* Candidates inside the window on screen. Zooming into a band and still
   being shown a list of what is loudest elsewhere is no use, so the list, the
   count and the Up/Down walk all follow the window. */
static int survey_peak_visible(const struct survey_view *s, int index) {
    struct freq_window w = freq_window_of(s);

    if (index < 0 || index >= s->session.peak_count)
        return 0;
    return freq_window_bin_visible(&w, s->session.bins,
                                   s->session.peaks[index].index);
}

static int survey_visible_count(const struct survey_view *s) {
    int count = 0;

    for (int i = 0; i < s->session.peak_count; i++)
        if (survey_peak_visible(s, i))
            count++;
    return count;
}

/* The nth visible candidate, as an index into peaks; -1 when there is none. */
static int survey_nth_visible(const struct survey_view *s, int n) {
    int seen = 0;

    for (int i = 0; i < s->session.peak_count; i++) {
        if (!survey_peak_visible(s, i))
            continue;
        if (seen == n)
            return i;
        seen++;
    }
    return -1;
}

static int survey_visible_rank(const struct survey_view *s, int index) {
    int seen = 0;

    for (int i = 0; i < s->session.peak_count; i++) {
        if (i == index)
            return seen;
        if (survey_peak_visible(s, i))
            seen++;
    }
    return -1;
}

/* The allocations overlapping what is on screen, for the chart to shade. */
static int survey_visible_bands(struct sdrgui_survey_band *bands, int capacity,
                                double lower_hz, double upper_hz) {
    int count = 0;

    for (int i = 0; i < band_plan_entry_count() && count < capacity; i++) {
        const struct band_plan_entry *entry = band_plan_entry_at(i);
        if (entry->upper_hz <= lower_hz || entry->lower_hz >= upper_hz)
            continue;
        bands[count].lower_hz = entry->lower_hz;
        bands[count].upper_hz = entry->upper_hz;
        bands[count].name = entry->name;
        count++;
    }
    return count;
}

void view_survey_defaults(struct app *app) {
    struct survey_view *s = &app->survey;

    survey_session_reset(&s->session);
    /* The tuner's full span, which is what an operator asking "what is out
       there" means. R820T limits; another tuner simply refuses to tune part of
       it, and the sweep reports the steps it could not take. */
    snprintf(s->from, sizeof(s->from), "24M");
    s->from_length = (int)strlen(s->from);
    snprintf(s->to, sizeof(s->to), "1766M");
    s->to_length = (int)strlen(s->to);
    snprintf(s->dwell, sizeof(s->dwell), "%.2f", SURVEY_DWELL_DEFAULT);
    s->dwell_length = (int)strlen(s->dwell);
    survey_load_installation(app);
    s->list_scroll = 0;
    s->selected = -1;
    s->hover = -1;
    /* No field is focused until one is clicked, so the number keys keep
       switching views the way they do in every other Scope view. */
    s->focus = -1;
    snprintf(s->session.status, sizeof(s->session.status),
             "Set a range and press Sweep. The whole tuner takes a few minutes;"
             " a band takes seconds.");
}

/* Remember the tuning to come back to: a sweep walks the receiver away from
   wherever the operator had it, and leaving the view should not strand them
   at 1766 MHz. */
void view_survey_enter(struct app *app) {
    struct survey_view *s = &app->survey;

    survey_load_installation(app);
    survey_history_refresh(app);

    /* Re-entering a view that never gave the receiver back keeps the claim it
       already has: where the operator had it has not changed. */
    if (!receiver_lease_token_active(&s->lease_token))
        receiver_borrow(app, &s->lease_token);
    /* A range given on the command line arrives here, and sweeps without
       being asked twice: someone who typed it has already asked. */
    if (app->options.survey_seen && !survey_session_sweeping(&s->session)) {
        survey_format_hz(s->from, sizeof(s->from), app->options.survey_from_hz);
        s->from_length = (int)strlen(s->from);
        survey_format_hz(s->to, sizeof(s->to), app->options.survey_to_hz);
        s->to_length = (int)strlen(s->to);
        if (app->options.survey_dwell_seconds > 0.0) {
            snprintf(s->dwell, sizeof(s->dwell), "%.2f",
                     app->options.survey_dwell_seconds);
            s->dwell_length = (int)strlen(s->dwell);
        }
        app->options.survey_seen = 0;   /* only the first entry */
        survey_start(app);
    }
}

void view_survey_leave(struct app *app) {
    struct survey_view *s = &app->survey;

    survey_session_stop(&s->session, NULL);
    /* Inside out: a confirmation pass borrows from this view, and the lease
       refuses to let this view return while the pass still holds it. */
    receiver_return(app, &s->confirm_lease_token);
    receiver_return(app, &s->lease_token);
}

/* What the drawing forgets when the measurements under it are replaced. */
static void survey_clear(struct survey_view *s) {
    survey_session_clear(&s->session);
    s->list_scroll = 0;
    s->selected = -1;
    s->hover = -1;
}

static int survey_start(struct app *app) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;
    struct survey_session_event event;
    uint32_t from_hz;
    uint32_t to_hz;
    double dwell;

    if (!app->receiver_mode) {
        snprintf(ss->status, sizeof(ss->status),
                 "A sweep needs a live receiver: a capture holds one tuning.");
        return -1;
    }
    if (parse_frequency(s->from, &from_hz) < 0 ||
        parse_frequency(s->to, &to_hz) < 0) {
        snprintf(ss->status, sizeof(ss->status),
                 "Use Hz or a K/M/G value, for example 88M");
        return -1;
    }
    if (parse_seconds(s->dwell, &dwell) < 0) {
        snprintf(ss->status, sizeof(ss->status),
                 "Dwell must be between %.2f and %.0f seconds.",
                 SURVEY_DWELL_MIN, SURVEY_DWELL_MAX);
        return -1;
    }

    if (survey_session_sweep(ss, (double)from_hz, (double)to_hz,
                             (double)app->applied_sample_rate, dwell,
                             GetTime(), &event) != SURVEY_PLAN_OK)
        return -1;
    /*
     * A watch asked for on the command line arms itself, which is the only
     * way it is reachable without somebody to click it (ADR-0012). Armed
     * before the sweep runs rather than after: the sweep's last block is what
     * folds it into the history, and it only does that while watching.
     */
    if (app->options.survey_watch > 0 && ss->watch_sweeps == 0 &&
        app->config.site[0]) {
        struct survey_session_event armed;

        survey_session_watch(ss, 1, app->options.survey_watch, GetTime(),
                             &armed);
    }
    /* The drawing follows the range that is about to be swept. */
    survey_clear(s);
    survey_reset_view(s);
    view_survey_enter(app);
    survey_obey(app, &event);
    return survey_session_sweeping(ss) ? 0 : -1;
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
static void survey_confirm_if_asked(struct app *app) {
    struct survey_view *s = &app->survey;
    struct survey_session_event event;

    if (!app->options.survey_confirm ||
        survey_session_confirming(&s->session))
        return;
    app->options.survey_confirm = 0;
    if (survey_session_confirm_changes(&s->session, GetTime(), &event) > 0) {
        s->confirm_printed = 1;
        survey_print_confirm_header();
        survey_obey(app, &event);
    } else {
        fprintf(stderr, "Nothing to ask again about: the sweep found nothing "
                        "this site has not heard before.\n");
    }
}

/*
 * Point the range fields at a band, and the chart with them.
 *
 * Shared by the click and by --survey-band, which is how this is reachable
 * from a script at all -- and how the chart's failure to follow was found.
 */
int survey_choose_band(struct app *app, int nth) {
    struct survey_view *s = &app->survey;
    const struct band_plan_entry *entry =
        survey_band_at(nth - 1, app->device.tune_lower_hz, app->device.tune_upper_hz);
    double from = 0.0, to = 0.0;

    if (!entry || survey_band_range(entry, app->device.tune_lower_hz,
                                    app->device.tune_upper_hz, &from, &to) != 0)
        return -1;
    survey_format_hz(s->from, sizeof(s->from), (uint32_t)llround(from));
    s->from_length = (int)strlen(s->from);
    survey_format_hz(s->to, sizeof(s->to), (uint32_t)llround(to));
    s->to_length = (int)strlen(s->to);
    /* And the dwell, because one value does not suit a band of two megahertz
       and one of two hundred. */
    snprintf(s->dwell, sizeof(s->dwell), "%.2f",
             survey_band_dwell(from, to, (double)app->applied_sample_rate));
    s->dwell_length = (int)strlen(s->dwell);

    /*
     * And the chart, now, rather than when a sweep next runs.
     *
     * The measurements on screen were taken over a different range, and
     * drawing them under an axis that has moved would put every one of them
     * at a frequency it was not measured at. Choosing a band is a statement
     * about what to look at next, so what is on screen from looking somewhere
     * else goes.
     */
    survey_clear(s);
    s->session.bins = 0;
    s->field_lower_hz = from;
    s->field_upper_hz = to;
    s->view_lower_hz = from;
    s->view_upper_hz = to;
    snprintf(s->session.status, sizeof(s->session.status),
             "%s: %.3f to %.3f MHz. Press Sweep.", entry->name, from / 1e6,
             to / 1e6);
    return 0;
}

/*
 * The band list's rows. Longer than the site and antenna lists, which fit
 * whatever they hold -- fifty-four allocations do not, so this one scrolls
 * and uses the same arithmetic the candidate list does.
 */
#define SURVEY_BAND_METRICS ((struct row_list_metrics){ 0.0f, 22.0f, 0.0f })
#define SURVEY_BAND_ROWS 14

static Rectangle survey_band_menu(Rectangle button) {
    Rectangle menu = { button.x, button.y + button.height,
                       button.width + 190.0f,
                       (float)SURVEY_BAND_ROWS * 22.0f };
    return menu;
}

/*
 * The nth strongest candidate on screen, counting from one, or -1.
 *
 * By power rather than by frequency: a script asking for "the strongest" and
 * getting whatever happens to be lowest in the band is asking a different
 * question. Kept next to the walk it shares an ordering idea with.
 */
static int survey_strongest_visible(const struct survey_view *s, int rank) {
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

/* Point the receiver at a candidate and start measuring it. The candidate is
   placed off centre on purpose: the receiver's own DC spike sits at the middle
   of the span, and a carrier measured on top of it would be measuring the
   receiver -- survey_session_measure() owns that offset. */
static void survey_select(struct app *app, int index) {
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
    survey_session_measure(ss, hz, GetTime(), &event);
    survey_obey(app, &event);
}

/*
 * Step the list to a rank and bring it into view.
 *
 * Selecting and scrolling were one thing that had to happen together and were
 * not: Up and Down moved the selection perfectly well, and once it passed the
 * last drawn row nothing on screen changed, which reads exactly like a key
 * that does nothing. Clicking a peak in the chart is the same move over a
 * bigger distance -- the loudest carrier in a band can be fortieth in the
 * list -- so it goes through here too.
 */
static void survey_follow_selection(struct app *app, Rectangle list) {
    struct survey_view *s = &app->survey;
    int count, rank;

    if (s->selected < 0)
        return;
    /* Read after the selection, not before: survey_select moves the window
       when the new candidate is zoomed past, which changes what counts as
       visible and so what a rank means. */
    count = survey_visible_count(s);
    rank = survey_visible_rank(s, s->selected);
    s->list_scroll = row_list_scroll_to(s->list_scroll, rank, count,
                                           row_list_rows(list, SURVEY_LIST_METRICS));
}

static void survey_walk_to(struct app *app, int rank, Rectangle list) {
    int index = survey_nth_visible(&app->survey, rank);

    if (index < 0)
        return;
    survey_select(app, index);
    survey_follow_selection(app, list);
}

void update_survey(struct app *app, double now, int spectrum_updated) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;
    struct survey_block block = survey_block_of(app);
    struct survey_session_event event;
    int was_sweeping = survey_session_sweeping(ss);

    survey_session_tick(ss, &block, spectrum_updated, now, &event);
    if (was_sweeping && event.sweep_finished)
        survey_confirm_if_asked(app);
    survey_obey(app, &event);
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
            survey_select(app, best);
    }
}

/*
 * Keep the survey a narrowing sweep is about to replace, so Reset zoom can put
 * it back without re-sweeping.
 *
 * The measurements are the session's to keep; the spelling of the range is
 * this view's, because it is what someone typed.
 */
static void survey_keep_current(struct survey_view *s) {
    if (s->session.bins <= 0)
        return;
    survey_session_keep(&s->session);
    snprintf(s->kept_from, sizeof(s->kept_from), "%s", s->from);
    snprintf(s->kept_to, sizeof(s->kept_to), "%s", s->to);
}

/*
 * What Sweep will sweep: the window when it is narrower than the range that
 * window sits on, and the whole range otherwise. Returns 1 when that narrows
 * the sweep, which is when the survey being replaced is worth keeping.
 *
 * The range is the swept one once there is a sweep, and the range in the
 * fields before that -- a distinction worth naming, because requiring a sweep
 * to already exist made the first sweep of a freshly opened survey ignore the
 * zoom it had just been given.
 */
static int survey_sweep_target(const struct survey_view *s, double *from,
                               double *to) {
    struct freq_window w = freq_window_of(s);

    return freq_window_sweep_target(&w, from, to);
}

/* Point the range fields at a span and sweep it. */
static void survey_sweep_span(struct app *app, double from, double to) {
    struct survey_view *s = &app->survey;

    survey_format_hz(s->from, sizeof(s->from), (uint32_t)llround(from));
    s->from_length = (int)strlen(s->from);
    survey_format_hz(s->to, sizeof(s->to), (uint32_t)llround(to));
    s->to_length = (int)strlen(s->to);
    survey_start(app);
}

void handle_survey_input(struct app *app) {
    struct survey_view *s = &app->survey;
    struct survey_session *ss = &s->session;
    struct survey_layout l = survey_layout_now();
    int character;

    survey_refresh_fields(s);
    /*
     * Clamp the scroll once, here, where it can be written back.
     *
     * The draw is handed a const app and can only clamp its own copy, so a
     * scroll left too high by a list that shrank -- a zoom that narrowed the
     * window, a sweep that found fewer -- would have the draw showing the
     * last page while the hit test still counted rows from the old offset,
     * and a click would select a different candidate from the one under it.
     */
    s->list_scroll = row_list_clamp_scroll(
        s->list_scroll, survey_visible_count(s),
        row_list_rows(l.peak_list, SURVEY_LIST_METRICS));

    /* Typing into whichever range field has focus. The same spellings the
       Settings panel takes, because parse_frequency is the same parser. */
    /* A list that is down is the nearest thing to the surface, so Escape
       closes it before it does anything else. */
    if (IsKeyPressed(KEY_ESCAPE) &&
        (s->band_menu_open || s->site_menu_open || s->antenna_menu_open)) {
        s->band_menu_open = 0;
        s->site_menu_open = 0;
        s->antenna_menu_open = 0;
        return;
    }
    if (s->focus >= 0 && IsKeyPressed(KEY_ESCAPE)) {
        if (s->focus == 3 || s->focus == 4)
            survey_commit_installation(app);
        s->focus = -1;
        return;
    }
    while (s->focus >= 0 && (character = GetCharPressed()) != 0) {
        struct survey_field f = survey_field_at(s, s->focus);
        int valid = f.numeric
                        ? ((character >= '0' && character <= '9') ||
                           character == '.' || character == 'k' ||
                           character == 'K' || character == 'm' ||
                           character == 'M' || character == 'g' ||
                           character == 'G')
                        : (character >= ' ' && character < 127);
        if (f.text && valid && *f.length < f.capacity - 1) {
            f.text[(*f.length)++] = (char)character;
            f.text[*f.length] = '\0';
        }
    }
    if (s->focus >= 0 && IsKeyPressed(KEY_BACKSPACE)) {
        struct survey_field f = survey_field_at(s, s->focus);
        if (f.text && *f.length > 0)
            f.text[--(*f.length)] = '\0';
    }
    {
        /* Moving focus away from a name is what commits it. */
        int was = s->focus;
        if (clicked(l.from_field))
            s->focus = 0;
        if (clicked(l.to_field))
            s->focus = 1;
        if (clicked(l.dwell_field))
            s->focus = 2;
        if (clicked(l.site_field))
            s->focus = 3;
        if (clicked(l.antenna_field))
            s->focus = 4;
        if (was != s->focus && (was == 3 || was == 4))
            survey_commit_installation(app);
    }
    if (survey_session_confirming(ss)) {
        /* Everything else waits: the receiver is somewhere the operator did
           not put it, and a click that retunes now would strand the pass. */
        if (clicked(l.confirm_button) || IsKeyPressed(KEY_ESCAPE)) {
            struct survey_session_event event;

            survey_session_confirm_abandon(ss, &event);
            survey_obey(app, &event);
        }
        return;
    }
    if (clicked(l.antenna_menu_button)) {
        s->antenna_menu_open = !s->antenna_menu_open &&
                               app->config.antenna_count > 0;
        s->site_menu_open = 0;
    }
    if (s->antenna_menu_open) {
        int row = survey_menu_row_at(l.antenna_field,
                                     app->config.antenna_count,
                                     GetMousePosition());
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (row >= 0) {
                snprintf(s->antenna, sizeof(s->antenna), "%s",
                         app->config.antennas[row]);
                s->antenna_length = (int)strlen(s->antenna);
                survey_commit_installation(app);
            }
            s->antenna_menu_open = 0;
            return;
        }
    }
    /*
     * The band list. It sits in front of the range fields because choosing a
     * band fills them in, and it is checked before them so a click landing on
     * the open list is not also read as a click on whatever it covers.
     */
    if (clicked(l.band_button)) {
        s->band_menu_open = !s->band_menu_open;
        s->site_menu_open = 0;
        s->antenna_menu_open = 0;
        return;
    }
    if (s->band_menu_open) {
        Rectangle menu = survey_band_menu(l.band_button);
        struct row_list_metrics m = SURVEY_BAND_METRICS;
        int count = survey_band_count(app->device.tune_lower_hz,
                                      app->device.tune_upper_hz);
        float wheel = GetMouseWheelMove();

        s->band_scroll = row_list_clamp_scroll(s->band_scroll, count,
                                               SURVEY_BAND_ROWS);
        if (wheel != 0.0f &&
            CheckCollisionPointRec(GetMousePosition(), menu)) {
            s->band_scroll = row_list_clamp_scroll(
                s->band_scroll - (int)wheel * ROW_LIST_WHEEL_ROWS, count,
                SURVEY_BAND_ROWS);
            return;
        }
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            int rank = row_list_rank_at(menu, m, s->band_scroll, count,
                                        SURVEY_BAND_ROWS, GetMousePosition());
            const struct band_plan_entry *entry =
                rank >= 0 ? survey_band_at(rank, app->device.tune_lower_hz,
                                           app->device.tune_upper_hz) : NULL;
            double from = 0.0, to = 0.0;

            (void)entry; (void)from; (void)to;
            if (rank >= 0)
                survey_choose_band(app, rank + 1);
            s->band_menu_open = 0;
            return;
        }
    }
    if (clicked(l.site_menu_button)) {
        s->site_menu_open = !s->site_menu_open && app->config.site_count > 0;
        s->antenna_menu_open = 0;
    }
    if (s->site_menu_open) {
        int row = survey_menu_row_at(l.site_field, app->config.site_count,
                                          GetMousePosition());
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (row >= 0) {
                /* Picking an existing site rather than retyping it is the
                   whole point: one place spelled two ways is two places, and
                   nothing downstream can tell. */
                snprintf(s->site, sizeof(s->site), "%s",
                         app->config.sites[row].label);
                s->site_length = (int)strlen(s->site);
                survey_commit_installation(app);
            }
            s->site_menu_open = 0;
            return;
        }
    }
    if (clicked(l.watch_button)) {
        if (ss->watching) {
            survey_session_watch_stop(ss);
        } else {
            struct survey_session_event event;

            /* No limit: a watch started by hand runs until it is stopped. */
            if (survey_session_watch(ss, app->config.site[0] != '\0', 0,
                                     GetTime(), &event) < 0) {
                /* The session says why in its own words; which field to fix
                   is the window's to know. */
                if (!app->config.site[0])
                    s->focus = 3;
            } else {
                survey_obey(app, &event);
            }
        }
        return;
    }
    if ((clicked(l.confirm_button) ||
         (s->focus < 0 && IsKeyPressed(KEY_A))) &&
        !survey_session_confirming(ss) && !survey_session_sweeping(ss)) {
        struct survey_session_event event;

        if (survey_session_change_count(ss) == 0)
            snprintf(ss->status, sizeof(ss->status),
                     "Nothing to ask about: this sweep matches what the site"
                     " has heard before.");
        else if (!app->receiver_mode)
            snprintf(ss->status, sizeof(ss->status),
                     "Asking again needs a live receiver.");
        else if (survey_session_confirm_changes(ss, GetTime(), &event) > 0)
            survey_obey(app, &event);
        return;
    }
    if (clicked(l.save_button)) {
        survey_commit_installation(app);
        survey_save_sweep(app);
        s->focus = -1;
    }
    /* One Sweep, and it sweeps what the chart is showing. Zoomed in, that is
       the window; zoomed out or never zoomed, the window is the whole range in
       the fields, so it is the same thing. There used to be two buttons, and
       they did the same thing except in the one case where the view had been
       narrowed -- a distinction the operator had to keep in their head to use
       the pair correctly.

       Editing the range re-anchors the window on it, so the fields and the
       window can never disagree about what Sweep is about to do. */
    if (clicked(l.sweep_button) || IsKeyPressed(KEY_ENTER)) {
        double from;
        double to;
        int narrowing = survey_sweep_target(s, &from, &to);

        s->focus = -1;
        if (narrowing) {
            /* Sweeping the window throws away everything outside it, and
               getting that back would cost minutes; Reset zoom restores it
               from here instead. */
            survey_keep_current(s);
            survey_sweep_span(app, from, to);
        } else {
            survey_start(app);
        }
        return;
    }
    if (survey_session_sweeping(ss) && clicked(l.stop_button)) {
        struct survey_session_event event;

        survey_session_stop(ss, &event);
        survey_obey(app, &event);
        return;
    }

    /* Zoom and pan. The chart can hold 1.7 GHz, where a 200 kHz carrier is a
       fifth of a pixel; zoom narrows the window and Left/Right walk it,
       without resampling anything -- the same measurements, drawn larger.

       Zoom is read as a character rather than as a key, which matters more
       than it looks. raylib names keys after physical positions on a US
       keyboard, so KEY_EQUAL and KEY_MINUS are the keys a US board prints
       = and - on. On a Portuguese layout the key printed + sits where a US
       board has [, and the one printed - sits where it has /, so binding the
       physical keys meant the zoom did nothing at all here -- and, because a
       window spanning the whole sweep cannot pan, Left and Right looked dead
       too. GetCharPressed reports what the layout actually produced.

       The keypad and the US positions stay as fallbacks: a numpad + is the
       same key everywhere. */
    if (s->focus < 0) {
        int typed;
        while ((typed = GetCharPressed()) != 0) {
            if (typed == '+' || typed == '=')
                survey_zoom(s, 1.0 / SURVEY_ZOOM_STEP);
            else if (typed == '-' || typed == '_')
                survey_zoom(s, SURVEY_ZOOM_STEP);
            else if (typed == '0')
                survey_reset_view(s);
        }
    }
    if (IsKeyPressed(KEY_KP_ADD) || IsKeyPressedRepeat(KEY_KP_ADD) ||
        IsKeyPressed(KEY_EQUAL) || IsKeyPressedRepeat(KEY_EQUAL)) {
        survey_zoom(s, 1.0 / SURVEY_ZOOM_STEP);
        return;
    }
    if (IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressedRepeat(KEY_KP_SUBTRACT) ||
        IsKeyPressed(KEY_MINUS) || IsKeyPressedRepeat(KEY_MINUS)) {
        survey_zoom(s, SURVEY_ZOOM_STEP);
        return;
    }
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        survey_pan(app, -SURVEY_PAN_FRACTION);
        return;
    }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        survey_pan(app, SURVEY_PAN_FRACTION);
        return;
    }
    /* The wheel zooms too, about the same anchor, since a hand already on the
       mouse to click a candidate should not have to reach for a key. */
    float wheel = GetMouseWheelMove();
    /* Over the list the wheel scrolls it; over the chart it zooms. Same hand,
       same wheel, whichever panel it is over. */
    if (wheel != 0.0f &&
        CheckCollisionPointRec(GetMousePosition(), l.peak_list)) {
        int count = survey_visible_count(s);
        s->list_scroll = row_list_clamp_scroll(
            s->list_scroll - (int)wheel * ROW_LIST_WHEEL_ROWS, count,
            row_list_rows(l.peak_list, SURVEY_LIST_METRICS));
        return;
    }
    if (wheel != 0.0f &&
        CheckCollisionPointRec(GetMousePosition(), l.chart)) {
        survey_zoom(s, wheel > 0.0f ? 1.0 / SURVEY_ZOOM_STEP
                                    : SURVEY_ZOOM_STEP);
        return;
    }

    /* Up and Down walk the candidate list. The scale keys mean nothing in
       this view, and a list you can step through is how you compare two
       carriers without hunting for them with the pointer. */
    int visible = survey_visible_count(s);
    if (visible > 0 &&
        (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))) {
        int rank = s->selected >= 0 ? survey_visible_rank(s, s->selected) : -1;
        survey_walk_to(app, (rank + 1) % visible, l.peak_list);
        return;
    }
    if (visible > 0 &&
        (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))) {
        int rank = s->selected >= 0 ? survey_visible_rank(s, s->selected) : 0;
        if (rank <= 0)
            rank = visible;
        survey_walk_to(app, rank - 1, l.peak_list);
        return;
    }

    /* Back out one level: the zoom first, then the region sweep that narrowed
       the swept range, then the tuner's full span. It never starts a sweep on
       its own -- a full sweep is minutes, and that is not something a button
       press should commit you to without saying so. */
    if (clicked(l.reset_button) && !survey_session_sweeping(ss)) {
        s->focus = -1;
        if (s->view_upper_hz > s->view_lower_hz &&
            (s->view_lower_hz > ss->lower_hz + 1.0 ||
             s->view_upper_hz < ss->upper_hz - 1.0)) {
            survey_reset_view(s);
            snprintf(ss->status, sizeof(ss->status),
                     "Showing the whole sweep, %.3f - %.3f MHz.",
                     ss->lower_hz / 1e6, ss->upper_hz / 1e6);
        } else if (survey_session_has_kept(ss)) {
            /* Put the earlier survey back on the chart, measurements and all.
               Restoring only the range fields left the chart still showing
               the region, which is not what "reset" means to anyone looking
               at it. */
            survey_session_restore(ss);
            snprintf(s->from, sizeof(s->from), "%s", s->kept_from);
            s->from_length = (int)strlen(s->from);
            snprintf(s->to, sizeof(s->to), "%s", s->kept_to);
            s->to_length = (int)strlen(s->to);
            s->selected = -1;
            s->hover = -1;
            s->list_scroll = 0;
            survey_refresh_fields(s);
            survey_reset_view(s);
            snprintf(ss->status, sizeof(ss->status),
                     "Back to the sweep of %.3f - %.3f MHz; %d candidates.",
                     ss->lower_hz / 1e6, ss->upper_hz / 1e6, ss->peak_count);
        } else {
            snprintf(s->from, sizeof(s->from), "24M");
            s->from_length = (int)strlen(s->from);
            snprintf(s->to, sizeof(s->to), "1766M");
            s->to_length = (int)strlen(s->to);
            snprintf(ss->status, sizeof(ss->status),
                     "Range set to the whole tuner; press Sweep to survey it.");
        }
        return;
    }

    /* Hover and selection, in the chart and in the list. */
    struct sdrgui_survey_params params = {
        l.chart, ss->power, ss->bins, SURVEY_SENTINEL_DBFS,
        survey_data_lower(s), survey_data_upper(s),
        s->view_upper_hz > s->view_lower_hz ? s->view_lower_hz
                                            : survey_data_lower(s),
        s->view_upper_hz > s->view_lower_hz ? s->view_upper_hz
                                            : survey_data_upper(s),
        NULL, 0, ss->peaks, ss->peak_count, survey_suspicious_now(app),
        s->selected, -1,
        survey_session_sweeping(ss) ? (ss->step * ss->bins) / (ss->step_count > 0 ? ss->step_count : 1)
                    : ss->bins,
        survey_session_sweeping(ss), 0, 0.0, 0.0, "",
        NULL   /* peak_flags: this copy only hit-tests, and never draws */
    };
    Vector2 mouse = GetMousePosition();
    double hz_at = sdrgui_survey_chart_hz_at(l.chart, &params, mouse);
    s->hover = sdrgui_survey_chart_peak_at(l.chart, &params, mouse);

    /* Press, drag, release: a rectangle across the chart zooms to what it
       covers. A press that does not move is a click, and a click selects the
       candidate under it -- so the two gestures share a button without either
       having to be modal. */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && isfinite(hz_at)) {
        s->drag_active = 1;
        s->drag_from_x = mouse.x;
        s->drag_from_hz = hz_at;
        s->drag_to_hz = hz_at;
    }
    if (s->drag_active) {
        if (isfinite(hz_at))
            s->drag_to_hz = hz_at;
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            double from = s->drag_from_hz < s->drag_to_hz ? s->drag_from_hz
                                                          : s->drag_to_hz;
            double to = s->drag_from_hz < s->drag_to_hz ? s->drag_to_hz
                                                        : s->drag_from_hz;
            int dragged = fabsf(mouse.x - s->drag_from_x) >= 5.0f;
            s->drag_active = 0;
            if (dragged && to - from >= SURVEY_MIN_SPAN_HZ) {
                s->view_lower_hz = from;
                s->view_upper_hz = to;
                survey_clamp_view(s);
                snprintf(ss->status, sizeof(ss->status),
                         "Zoomed to %.3f - %.3f MHz.   0 shows the whole sweep"
                         " again.", s->view_lower_hz / 1e6,
                         s->view_upper_hz / 1e6);
            } else if (dragged) {
                snprintf(ss->status, sizeof(ss->status),
                         "That is narrower than %.0f kHz; nothing to zoom to.",
                         SURVEY_MIN_SPAN_HZ / 1e3);
            } else if (s->hover >= 0) {
                /* A chart peak can be any rank in the list -- the loudest
                   carrier in a band is often fortieth -- so the list follows
                   it rather than highlighting a row it never drew. */
                survey_select(app, s->hover);
                survey_follow_selection(app, l.peak_list);
            }
            return;
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, l.peak_list)) {
        int count = survey_visible_count(s);
        int fits = row_list_rows(l.peak_list, SURVEY_LIST_METRICS);
        int rank = row_list_rank_at(l.peak_list, SURVEY_LIST_METRICS, s->list_scroll,
                                      count, fits, mouse);
        int index = rank >= 0 ? survey_nth_visible(s, rank) : -1;
        if (index >= 0)
            survey_select(app, index);
        return;
    }

    /* Scan around the selected candidate: a short sweep centred on it, at the
       dwell now in the field. This is the drill-down the survey is for -- the
       wide sweep says something is at 943.2 MHz, and a few megahertz swept
       around it says what its neighbourhood looks like, in bins as fine as the
       FFT allows rather than the hundreds of kilohertz a full-tuner sweep can
       afford. Reset zoom comes back, because the survey it replaces is kept. */
    if (s->selected >= 0 && clicked(l.scan_button) && !survey_session_sweeping(ss)) {
        double centre = ss->report_valid
                            ? ss->report.centre_hz
                            : survey_bin_hz(s, ss->peaks[s->selected].index);
        double from = centre - SURVEY_SCAN_HALF_SPAN_HZ;
        double to = centre + SURVEY_SCAN_HALF_SPAN_HZ;

        if (from < 1000000.0)
            from = 1000000.0;
        s->focus = -1;
        survey_keep_current(s);
        survey_sweep_span(app, from, to);
        return;
    }

    /* Watch it over time: tune to the candidate and open the waterfall on it.
       The tuning puts the carrier 300 kHz off centre, as the measurement does,
       so what appears as a stripe is the signal rather than the receiver's own
       DC spike sitting in the middle of the span. The waterfall's history is
       cleared, because rows drawn at other frequencies say nothing about this
       one. */
    if (s->selected >= 0 && clicked(l.waterfall_button)) {
        double centre = ss->report_valid
                            ? ss->report.centre_hz
                            : survey_bin_hz(s, ss->peaks[s->selected].index);

        survey_session_stop(ss, NULL);
        if (app->receiver_mode &&
            retune_receiver(app, (uint32_t)llround(centre - SURVEY_OFFSET_HZ),
                            app->applied_ppm) < 0) {
            snprintf(ss->status, sizeof(ss->status),
                     "The receiver would not tune to %.4f MHz.", centre / 1e6);
            return;
        }
        /*
         * The handoff, and it is a commit rather than a restore: the operator
         * picked this candidate, so putting back whatever the sweep started
         * from is the opposite of what was just asked for. Committed here,
         * after the retune succeeded and before the waterfall is rebuilt, so
         * a waterfall that cannot be rebuilt still keeps the chosen tuning --
         * which is what this path has always done, now said out loud.
         */
        receiver_commit(app, &s->lease_token);
        if (recreate_waterfall(app, app->plot, 1) < 0)
            return;
        /*
         * The tab as well as the view, and that is what was missing: the
         * survey used to be one of the Scope views, so setting app->view was
         * the whole of switching to another. It is a tab of its own now and
         * the Scope views are behind theirs, so this set a view nobody was
         * looking at and the button did nothing at all.
         *
         * view_survey_leave normally puts back the tuning the sweep borrowed,
         * which is the opposite of what has just been asked for -- the lease
         * commit above is what stops it, by giving the claim up without
         * asking for a restore.
         */
        app->view = VIEW_WATERFALL;
        set_tab(app, TAB_SCOPE);
        return;
    }

    /* The handoff: point a decoder at what was found, which is an invitation
       to go and find out, not a claim about what it is. */
    if (s->selected >= 0 && ss->report_valid && clicked(l.inspect_button)) {
        const struct band_plan_entry *entry =
            band_plan_lookup(ss->report.centre_hz);
        enum band_plan_decoder decoder = entry ? entry->decoder
                                               : BAND_PLAN_NONE;

        if (decoder == BAND_PLAN_GSM) {
            int arfcn = gsm_arfcn_for_hz(ss->report.centre_hz);
            view_survey_leave(app);
            set_decode(app, DECODE_GSM);
            set_tab(app, TAB_DECODE);
            if (arfcn > 0)
                gsm_tune_selected(app, arfcn);
            app->gsm_analysis_mode = 1;
        } else if (decoder == BAND_PLAN_ADSB) {
            view_survey_leave(app);
            set_decode(app, DECODE_ADSB);
            set_tab(app, TAB_DECODE);
            retune_receiver(app, DEFAULT_FREQUENCY, app->applied_ppm);
        } else if (decoder == BAND_PLAN_LTE) {
            /*
             * Snapped to the channel raster, not tuned to where the energy
             * was. A Zadoff-Chu correlation wants the carrier's centre, and
             * the survey reports the middle of a maximum -- which on a
             * lopsided carrier is not the same place. enter_lte then moves it
             * to 1.92 MS/s, which the cell search refuses to work without
             * (ADR-0014).
             */
            int earfcn = lte_earfcn_for_hz(ss->report.centre_hz);
            uint32_t centre = 0;

            view_survey_leave(app);
            set_decode(app, DECODE_LTE);
            if (earfcn > 0 && lte_earfcn_downlink_hz((unsigned int)earfcn,
                                                     &centre) == 0)
                retune_receiver(app, centre, app->applied_ppm);
            set_tab(app, TAB_DECODE);
        } else if (decoder == BAND_PLAN_FM) {
            /*
             * Tuned, and deliberately no band scan.
             *
             * Opening the FM tab normally starts one, which is right when
             * somebody has come to find out what is on air. Arriving from the
             * survey they have already chosen a frequency, and spending
             * twenty-five seconds walking the band before playing it would
             * answer a question they did not ask. enter_fm only scans when it
             * has no results, so seeding the frequency and tuning first is
             * enough to keep it quiet.
             */
            double hz = ss->report.centre_hz;

            view_survey_leave(app);
            set_decode(app, DECODE_FM);
            snprintf(app->fm.frequency, sizeof(app->fm.frequency), "%.1f",
                     hz / 1e6);
            app->fm.frequency_length = (int)strlen(app->fm.frequency);
            fm_tune(app, hz);
            set_tab(app, TAB_DECODE);
        }
    }
}

/*
 * What the confirmation pass concluded about this frequency, or 0 when it
 * never asked.
 *
 * The sweep's own suspicion comes from arithmetic on the frequency
 * (`survey_suspect_at`) and is available for every candidate; this comes from
 * a measurement and only exists where somebody looked. They live in the same
 * field and are read together, because a row wants to show both.
 */
static unsigned survey_confirmed_flags_at(const struct app *app, double hz) {
    const struct survey_session *ss = &app->survey.session;
    const struct survey_confirm_target *target;
    double tolerance = ss->plan.bin_hz > 0.0 ? ss->plan.bin_hz : 1e5;

    if (ss->confirm.count <= 0)
        return 0u;
    target = survey_confirm_for(ss->confirm.target, ss->confirm.count, hz,
                               tolerance);
    return target ? target->suspicion : 0u;
}

static void draw_peak_list(const struct app *app, Rectangle rect) {
    const struct survey_session *ss = &app->survey.session;
    const struct survey_view *s = &app->survey;
    char text[160];

    int visible = survey_visible_count(s);

    DrawRectangleRec(rect, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(rect, 1.0f, (Color){ 82, 109, 126, 255 });
    if (visible == ss->peak_count)
        snprintf(text, sizeof(text), "Candidates (%d)", ss->peak_count);
    else
        snprintf(text, sizeof(text), "Candidates (%d of %d, in view)", visible,
                 ss->peak_count);
    DrawText(text, (int)rect.x + 12, (int)rect.y + 10, 16,
             (Color){ 151, 174, 188, 255 });
    /* A sweep that is mostly the receiver talking to itself should say so
       before anyone clicks into it. */
    int suspicious = survey_suspicious_now(app);
    {
        /* And how many the closer look found nothing at, which is a separate
           count because it is a separate finding -- and one only a
           confirmation pass can produce. */
        int empty = 0, k;
        for (k = 0; k < ss->confirm.count; k++)
            if (survey_suspect_empty(ss->confirm.target[k].suspicion))
                empty++;
        /* Short enough to survive the panel. "%d marked *   %d marked ~" did
           not: at the default window it came out "1 marked *   10 mar...",
           losing the symbol that names the second count. */
        if (suspicious > 0 && empty > 0)
            snprintf(text, sizeof(text), "* %d   ~ %d", suspicious, empty);
        else if (suspicious > 0)
            snprintf(text, sizeof(text), "%d marked *", suspicious);
        else if (empty > 0)
            snprintf(text, sizeof(text), "%d marked ~", empty);
        else
            text[0] = '\0';
        if (text[0])
            sdrgui_text_fit(text,
                            (int)rect.x + 12 +
                                MeasureText("Candidates (000)", 16) + 14,
                            (int)rect.y + 10, 16, rect.width - 190.0f,
                            (Color){ 250, 190, 74, 255 });
    }
    DrawText("   FREQUENCY       LEVEL    WIDTH   SHAPE      SEEN",
             (int)rect.x + 12, (int)rect.y + 30, 15,
             (Color){ 126, 151, 166, 255 });

    if (visible == 0) {
        DrawText(survey_session_sweeping(ss) ? "sweeping..."
                             : ss->peak_count > 0 ? "none in this window"
                                                 : "nothing found yet",
                 (int)rect.x + 12, (int)rect.y + 56, 16,
                 (Color){ 150, 172, 188, 255 });
        return;
    }
    int fits = row_list_rows(rect, SURVEY_LIST_METRICS);
    int scroll = row_list_clamp_scroll(s->list_scroll, visible, fits);
    int rows = visible - scroll;
    if (rows > fits)
        rows = fits;
    for (int row = 0; row < rows; row++) {
        int i = survey_nth_visible(s, scroll + row);
        float y = row_list_row_y(rect, SURVEY_LIST_METRICS, row);
        Color color = (Color){ 213, 226, 234, 255 };
        if (i < 0)
            break;
        if (i == s->selected) {
            DrawRectangle((int)rect.x + 4, (int)y - 3, (int)rect.width - 8, 22,
                          (Color){ 255, 174, 62, 40 });
            color = (Color){ 255, 202, 105, 255 };
        } else if (i == s->hover) {
            color = (Color){ 255, 255, 255, 255 };
        }
        double hz = survey_bin_hz(s, ss->peaks[i].index);
        /*
         * Through the carrier this maximum belongs to, not through its own
         * frequency. The pass asks about carriers at their measured centre,
         * and a station's shoulders are maxima of the same signal several
         * kilohertz away -- matching each on its own frequency against a
         * 2.4 kHz tolerance found nothing at all, and the count in the header
         * disagreed with the rows. survey_store.c matches the same way and
         * for the same reason.
         */
        const struct survey_carrier *row_carrier = survey_carrier_at(s, hz);
        unsigned asked = survey_confirmed_flags_at(
            app, row_carrier ? row_carrier->centre_hz : hz);
        int suspect = survey_suspect_warns(survey_suspect_at(app, hz, 0.0) |
                                           asked);
        /* Its own marker, because it is its own finding: the receiver's comb
           says unplug the antenna, and this says the frequency is empty
           however often it was seen. */
        int empty = survey_suspect_empty(asked);

        /* The marker leads the row. Trailing it put it at the end of the
           longest line in the panel, where sdrgui_text_fit ellipsised it away
           on exactly the rows that needed it. */
        {
            /*
             * What was measured, what shape it is, and what this site has
             * heard of it before. The prominence gave way to the width and
             * the shape: prominence is already why the row is here at all,
             * and how wide a thing is says more about what it is.
             */
            const struct survey_carrier *carrier = survey_carrier_at(s, hz);
            const struct site_entry *known =
                ss->history_loaded
                    ? site_history_find(&ss->history,
                                        carrier ? carrier->centre_hz : hz,
                                        ss->plan.bin_hz > 0.0 ? ss->plan.bin_hz
                                                             : 1e5)
                    : NULL;
            enum site_seen seen = ss->history_loaded
                ? site_history_seen(&ss->history, known, 1) : SITE_SEEN_UNKNOWN;
            char width[16];

            if (carrier && carrier->width_hz >= 1e6)
                snprintf(width, sizeof(width), "%.1fM", carrier->width_hz / 1e6);
            else if (carrier)
                snprintf(width, sizeof(width), "%.0fk", carrier->width_hz / 1e3);
            else
                snprintf(width, sizeof(width), "-");
            snprintf(text, sizeof(text),
                     "%s %10.4f MHz  %6.1f dBFS  %6s  %-9s  %s",
                     empty ? "~" : suspect ? "*" : " ", hz / 1e6,
                     (double)ss->peaks[i].power_dbfs, width,
                     carrier ? survey_shape_name(
                                   survey_carrier_shape(carrier->width_hz))
                             : "-",
                     site_seen_name(seen));
        }
        if ((suspect || empty) && i != s->selected && i != s->hover)
            color = (Color){ 178, 168, 140, 255 };
        sdrgui_text_fit(text, (int)rect.x + 12, (int)y, 17,
                        rect.width - 24.0f, color);
    }
    /*
     * Where in the list this is. It used to say "... 43 more" and leave it
     * there, which named the problem without offering a way out of it.
     */
    if (visible > fits) {
        float track_x = rect.x + rect.width - 7.0f;
        float track_y = rect.y + SURVEY_LIST_METRICS.header_h;
        float track_h = (float)fits * SURVEY_LIST_METRICS.row_h;
        float thumb_h = track_h * (float)fits / (float)visible;
        float thumb_y = track_y + track_h * (float)scroll / (float)visible;

        snprintf(text, sizeof(text), "%d-%d of %d   wheel or Up/Down",
                 scroll + 1, scroll + rows, visible);
        DrawText(text, (int)rect.x + 12,
                 (int)(rect.y + rect.height - 19.0f), 15,
                 (Color){ 126, 151, 166, 255 });

        if (thumb_h < 12.0f)
            thumb_h = 12.0f;
        if (thumb_y + thumb_h > track_y + track_h)
            thumb_y = track_y + track_h - thumb_h;
        DrawRectangle((int)track_x, (int)track_y, 4, (int)track_h,
                      (Color){ 30, 42, 52, 255 });
        DrawRectangle((int)track_x, (int)thumb_y, 4, (int)thumb_h,
                      (Color){ 108, 138, 158, 255 });
    }
}

/*
 * One line of the panel's prose, if there is room for it above the buttons.
 *
 * Returns 1 when it drew and 0 when the line did not fit, so a caller can
 * stop rather than carry on drawing into the controls. Off the bottom edge is
 * worse than absent -- panel_rows.h says the same for the views that have a
 * table of fields, and this panel's content is prose of a length that depends
 * on what was measured, so it needs the rule at every line rather than a row
 * capacity worked out once.
 */
static int detail_line(const struct survey_layout *l, int *y, int indent,
                       int size, int step, const char *text, Color color) {
    if ((float)(*y + size) > l->detail_text.y + l->detail_text.height)
        return 0;
    sdrgui_text_fit(text, (int)l->detail_text.x + indent, *y, size,
                    l->detail_text.width - (float)indent - 12.0f, color);
    *y += step;
    return 1;
}

static void draw_detail(const struct app *app, const struct survey_layout *l) {
    const struct survey_session *ss = &app->survey.session;
    const struct survey_view *s = &app->survey;
    Rectangle rect = l->detail;
    char text[220];
    int y = (int)rect.y + 10;
    const int line = 22;

    DrawRectangleRec(rect, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(rect, 1.0f, (Color){ 82, 109, 126, 255 });

    if (s->selected < 0) {
        DrawText("Selected candidate", (int)rect.x + 12, y, 16,
                 (Color){ 151, 174, 188, 255 });
        DrawText("click a candidate in the chart or the list",
                 (int)rect.x + 12, y + 30, 17, (Color){ 150, 172, 188, 255 });
        return;
    }
    draw_button(l->scan_button, "Scan this frequency", 1);
    draw_button(l->waterfall_button, "Open waterfall", 1);

    double shown_hz = ss->report_valid ? ss->report.centre_hz
                                      : survey_bin_hz(s, ss->peaks[s->selected].index);
    snprintf(text, sizeof(text), "Selected  %.4f MHz%s", shown_hz / 1e6,
             ss->report_valid ? "  (measured)" : "");
    DrawText(text, (int)rect.x + 12, y, 18, (Color){ 235, 242, 246, 255 });
    y += 28;

    if (survey_session_measuring(ss)) {
        DrawText("measuring...", (int)rect.x + 12, y, 17,
                 (Color){ 250, 190, 74, 255 });
        y += line;
    }
    if (ss->report_valid) {
        snprintf(text, sizeof(text), "peak power         %.1f dBFS",
                 (double)ss->report.peak_dbfs);
        DrawText(text, (int)rect.x + 12, y, 17, (Color){ 213, 226, 234, 255 });
        y += line;
        snprintf(text, sizeof(text), "above local floor  %.1f dB",
                 (double)ss->report.prominence_db);
        DrawText(text, (int)rect.x + 12, y, 17, (Color){ 213, 226, 234, 255 });
        y += line;
        snprintf(text, sizeof(text), "occupied bandwidth %.1f kHz  (-%.0f dB)",
                 ss->report.bandwidth_hz / 1e3,
                 (double)ss->report.bandwidth_ref_db);
        DrawText(text, (int)rect.x + 12, y, 17, (Color){ 213, 226, 234, 255 });
        y += line;

        if (ss->measure.blocks > 0) {
            double duty = survey_measure_duty(&ss->measure);

            snprintf(text, sizeof(text), "duty               %s  (%d/%d blocks)",
                     survey_measure_duty_label(duty), ss->measure.hits,
                     ss->measure.blocks);
            DrawText(text, (int)rect.x + 12, y, 17,
                     (Color){ 213, 226, 234, 255 });
            y += line;
        }
        if (ss->measure.hits > 1) {
            snprintf(text, sizeof(text), "stability          +/- %.1f kHz",
                     survey_measure_spread_hz(&ss->measure) / 1e3);
            DrawText(text, (int)rect.x + 12, y, 17,
                     (Color){ 213, 226, 234, 255 });
            y += line;
        }
    } else if (!survey_session_measuring(ss)) {
        DrawText("nothing measurable at that frequency now",
                 (int)rect.x + 12, y, 17, (Color){ 250, 190, 74, 255 });
        y += line;
    }

    /*
     * What kind of thing it is, before what the frequency is allocated to and
     * before the receiver-spur warning -- the same ordering argument as the
     * one below, one step further back. A reader who has already read
     * "Aeronautical radionavigation -- ILS markers" is reading everything
     * after it as detail about a beacon; the measurement of the signal itself
     * has to come first to have a chance of being believed over the label.
     */
    if (ss->report_valid || ss->carrier_valid) {
        struct signal_findings findings;
        int k;

        signal_findings_from(ss->carrier_valid ? &ss->carrier : NULL,
                             &ss->bursts, &ss->envelope,
                             survey_measure_duty(&ss->measure),
                             ss->measure.hits, ss->measure.blocks,
                             survey_measure_spread_hz(&ss->measure),
                             &findings);
        y += 6;
        for (k = 0; k < findings.count; k++) {
            /* The first line is the claim and the rest qualify it, so they
               are indented and quieter -- the arrangement the band plan and
               the spur warning below already use. */
            if (!detail_line(l, &y, k ? 24 : 12, k ? 16 : 17,
                             k ? line - 2 : line, findings.line[k],
                             k ? (Color){ 126, 151, 166, 255 }
                               : (Color){ 213, 226, 234, 255 }))
                break;
        }
    }

    /*
     * What the measurement suggests about the candidate itself, before the
     * band plan says what the frequency is allocated to. The order matters:
     * the allocation is what a reader believes by default, and a warning
     * printed after it reads as a footnote to it rather than a reason to
     * doubt it.
     */
    unsigned suspect = survey_suspect_at(app, shown_hz,
                                         ss->report_valid
                                             ? ss->report.bandwidth_hz
                                             : 0.0) |
                       survey_confirmed_flags_at(app, shown_hz);
    if (survey_suspect_empty(suspect)) {
        y += 6;
        detail_line(l, &y, 12, 17, line,
                    "the closer look found nothing here",
                    (Color){ 250, 190, 74, 255 });
        detail_line(l, &y, 24, 16, line - 2,
                    "no carrier, and the envelope varies like noise",
                    (Color){ 200, 165, 110, 255 });
        detail_line(l, &y, 24, 15, line - 2,
                    "however many looks saw it: a bar is cleared by noise",
                    (Color){ 126, 151, 166, 255 });
    }
    if (survey_suspect_warns(suspect)) {
        y += 6;
        detail_line(l, &y, 12, 17, line,
                    "this looks like the receiver, not the band",
                    (Color){ 250, 190, 74, 255 });
        detail_line(l, &y, 24, 16, line - 2, survey_suspect_reason(suspect),
                    (Color){ 200, 165, 110, 255 });
        if (suspect & SURVEY_SUSPECT_UNRESOLVED)
            detail_line(l, &y, 24, 16, line - 2,
                        "and too narrow for this FFT to resolve: a bare "
                        "carrier", (Color){ 200, 165, 110, 255 });
        /* Sufficient, not necessary: a spur the dongle radiates and hears
           back through its own antenna goes when the antenna does, and most
           of this comb behaves that way. What survives unplugging is
           certainly the receiver; what does not is not thereby a signal. */
        detail_line(l, &y, 24, 15, line - 2,
                    "unplug the antenna and sweep again: what stays is "
                    "the receiver", (Color){ 126, 151, 166, 255 });
    } else if (suspect & SURVEY_SUSPECT_UNRESOLVED) {
        /* Not a warning on its own -- plenty of real services are this narrow
           -- but worth saying that the width reported is the instrument's
           floor and not the signal's. */
        y += 6;
        detail_line(l, &y, 12, 16, line - 2,
                    "too narrow for this FFT to resolve: the width above "
                    "is its floor", (Color){ 126, 151, 166, 255 });
    }

    /* The band plan, and what it is not. */
    const struct band_plan_entry *entry = band_plan_lookup(shown_hz);
    y += 6;
    if (entry) {
        snprintf(text, sizeof(text), "band plan: %s", entry->name);
        detail_line(l, &y, 12, 17, line, text,
                    (Color){ 149, 205, 232, 255 });
        if (entry->note)
            detail_line(l, &y, 24, 16, line - 2, entry->note,
                        (Color){ 126, 151, 166, 255 });
        detail_line(l, &y, 12, 15, line - 2,
                    "a frequency lookup, not a detection",
                    (Color){ 126, 151, 166, 255 });
        if (band_plan_can_inspect(entry->decoder) && ss->report_valid)
            draw_button(l->inspect_button,
                        band_plan_inspect_label(entry->decoder), 1);
    } else {
        detail_line(l, &y, 12, 16, line - 2,
                    "band plan: nothing allocated here that this table knows",
                    (Color){ 126, 151, 166, 255 });
    }
}

/*
 * What the site's memory adds to the chart: a tick under every candidate this
 * site has not heard before, and a hollow one where something it has heard is
 * absent this time.
 *
 * Under the trace rather than over it. A survey is a measurement and the
 * memory is an interpretation of it, so the interpretation must not be
 * mistaken for a peak that was measured.
 */
static void survey_draw_history_marks(const struct app *app,
                                      const struct survey_layout *l,
                                      const struct sdrgui_survey_params *p) {
    const struct survey_session *ss = &app->survey.session;
    const Color fresh = { 120, 214, 140, 255 };
    const Color absent = { 190, 140, 120, 255 };
    float base = l->chart.y + l->chart.height - 34.0f;
    int i;

    if (!ss->history_loaded || ss->history.sweeps == 0)
        return;
    for (i = 0; i < ss->carrier_count; i++) {
        float x;
        if (ss->carrier_status[i] != SITE_STATUS_NEW)
            continue;
        x = sdrgui_survey_chart_x_at(l->chart, p, ss->carriers[i].centre_hz);
        if (x != x)                       /* NaN: off screen */
            continue;
        DrawTriangle((Vector2){ x, base }, (Vector2){ x - 5.0f, base + 9.0f },
                     (Vector2){ x + 5.0f, base + 9.0f }, fresh);
    }
    for (i = 0; i < ss->missing_count; i++) {
        float x = sdrgui_survey_chart_x_at(l->chart, p, ss->missing[i]->hz);
        if (x != x)
            continue;
        DrawLineEx((Vector2){ x - 5.0f, base + 9.0f },
                   (Vector2){ x + 5.0f, base + 9.0f }, 2.0f, absent);
        DrawLineEx((Vector2){ x, base }, (Vector2){ x, base + 9.0f }, 1.0f,
                   absent);
    }
}

/*
 * A picker, open over the chart. Drawn last, because a menu that renders under
 * the chart is not clickable.
 *
 * One routine for both: the site and the antenna are the same control, and
 * writing it twice is how the two would drift apart -- one of them growing a
 * hover highlight or a current-entry mark that the other lacked.
 */
static void survey_draw_menu(Rectangle field, int count, int chosen,
                             const char *(*label)(const struct app *, int,
                                                  char *, size_t),
                             const struct app *app) {
    Rectangle menu;
    int i, hovered;

    if (count <= 0)
        return;
    menu = survey_menu_rect(field, count);
    hovered = survey_menu_row_at(field, count, GetMousePosition());
    DrawRectangleRec(menu, (Color){ 22, 28, 36, 250 });
    DrawRectangleLinesEx(menu, 1.0f, (Color){ 90, 116, 132, 255 });
    for (i = 0; i < count; i++) {
        float y = menu.y + 4.0f + SURVEY_SITE_ROW_H * (float)i;
        char text[CONFIG_VALUE_MAX + 24];
        if (i == hovered)
            DrawRectangle((int)menu.x + 1, (int)y, (int)menu.width - 2,
                          (int)SURVEY_SITE_ROW_H, (Color){ 255, 174, 62, 40 });
        sdrgui_text_fit(label(app, i, text, sizeof(text)), (int)menu.x + 10,
                        (int)y + 5, 16, menu.width - 20.0f,
                        i == chosen ? (Color){ 255, 174, 62, 255 }
                                    : (Color){ 176, 198, 212, 255 });
    }
}

/* A site carries its correction: it is what makes two sites' numbers
   comparable, and switching site changes it. */
static const char *survey_site_label(const struct app *app, int i, char *out,
                                     size_t size) {
    snprintf(out, size, "%s   %+d ppm", app->config.sites[i].label,
             app->config.sites[i].ppm);
    return out;
}

static const char *survey_antenna_label(const struct app *app, int i,
                                        char *out, size_t size) {
    snprintf(out, size, "%s", app->config.antennas[i]);
    return out;
}

static void survey_draw_pickers(const struct app *app,
                                const struct survey_layout *l) {
    const struct survey_view *s = &app->survey;
    int chosen = -1, i;

    if (s->site_menu_open) {
        for (i = 0; i < app->config.site_count; i++)
            if (!strcmp(app->config.sites[i].label, app->config.site))
                chosen = i;
        survey_draw_menu(l->site_field, app->config.site_count, chosen,
                         survey_site_label, app);
    }
    if (s->antenna_menu_open) {
        for (i = 0; i < app->config.antenna_count; i++)
            if (!strcmp(app->config.antennas[i], app->config.antenna))
                chosen = i;
        survey_draw_menu(l->antenna_field, app->config.antenna_count, chosen,
                         survey_antenna_label, app);
    }
    /*
     * The band list, drawn its own way rather than through survey_draw_menu.
     * That one sizes itself to whatever it holds, which works for a handful
     * of sites and not for fifty-four allocations -- this one is a fixed
     * height and scrolls, using the same arithmetic the candidate list does.
     */
    if (s->band_menu_open) {
        Rectangle menu = survey_band_menu(l->band_button);
        struct row_list_metrics m = SURVEY_BAND_METRICS;
        int count = survey_band_count(app->device.tune_lower_hz,
                                      app->device.tune_upper_hz);
        int scroll = row_list_clamp_scroll(s->band_scroll, count,
                                           SURVEY_BAND_ROWS);
        int hovered = row_list_rank_at(menu, m, scroll, count,
                                       SURVEY_BAND_ROWS, GetMousePosition());
        int rows = count - scroll;
        int row;

        if (rows > SURVEY_BAND_ROWS)
            rows = SURVEY_BAND_ROWS;
        DrawRectangleRec(menu, (Color){ 12, 19, 28, 255 });
        DrawRectangleLinesEx(menu, 1.0f, (Color){ 108, 138, 158, 255 });
        for (row = 0; row < rows; row++) {
            const struct band_plan_entry *entry =
                survey_band_at(scroll + row, app->device.tune_lower_hz,
                               app->device.tune_upper_hz);
            float y = row_list_row_y(menu, m, row);
            char text[128];

            if (!entry)
                break;
            if (scroll + row == hovered)
                DrawRectangle((int)menu.x + 1, (int)y, (int)menu.width - 2,
                              (int)m.row_h, (Color){ 255, 174, 62, 46 });
            snprintf(text, sizeof(text), "%7.1f-%-7.1f  %s",
                     entry->lower_hz / 1e6, entry->upper_hz / 1e6,
                     entry->name);
            sdrgui_text_fit(text, (int)menu.x + 8, (int)y + 3, 15,
                            menu.width - 16.0f,
                            band_plan_can_inspect(entry->decoder)
                                ? (Color){ 99, 228, 170, 255 }
                                : (Color){ 213, 226, 234, 255 });
        }
        if (count > SURVEY_BAND_ROWS) {
            float track_h = (float)SURVEY_BAND_ROWS * m.row_h;
            float thumb_h = track_h * (float)SURVEY_BAND_ROWS / (float)count;
            float thumb_y = menu.y + track_h * (float)scroll / (float)count;

            if (thumb_h < 12.0f)
                thumb_h = 12.0f;
            if (thumb_y + thumb_h > menu.y + track_h)
                thumb_y = menu.y + track_h - thumb_h;
            DrawRectangle((int)(menu.x + menu.width - 6.0f), (int)menu.y, 4,
                          (int)track_h, (Color){ 30, 42, 52, 255 });
            DrawRectangle((int)(menu.x + menu.width - 6.0f), (int)thumb_y, 4,
                          (int)thumb_h, (Color){ 108, 138, 158, 255 });
        }
    }
}

/*
 * What the cursor is over, and what this site remembers of it.
 *
 * The chart can say a candidate is new; only this can say how new -- whether
 * the site has heard it once before or in every sweep for a month. That is the
 * difference between a signal worth investigating and one the operator
 * already knows about, and it is the reason the history is kept.
 */
static void survey_draw_popup(const struct app *app,
                              const struct survey_layout *l,
                              const struct sdrgui_survey_params *p) {
    const struct survey_session *ss = &app->survey.session;
    const struct survey_view *s = &app->survey;
    Vector2 mouse = GetMousePosition();
    char title[96], detail[160];
    const struct site_entry *entry = NULL;
    float width, height, x, y;
    double tolerance;

    if (s->site_menu_open || s->antenna_menu_open ||
        !CheckCollisionPointRec(mouse, l->chart))
        return;
    tolerance = ss->plan.bin_hz > 0.0 ? ss->plan.bin_hz : 1e5;
    if (s->hover >= 0 && s->hover < ss->peak_count) {
        double peak_hz = survey_plan_bin_centre(&ss->plan,
                                                ss->peaks[s->hover].index);
        const struct survey_carrier *carrier = NULL;
        const struct band_plan_entry *band;
        double hz = peak_hz;
        int k;

        /* Which signal that maximum belongs to. The reader pointed at a bump;
           what they want to know about is the carrier it is part of. */
        for (k = 0; k < ss->carrier_count; k++)
            if (peak_hz >= ss->carriers[k].lower_hz &&
                peak_hz <= ss->carriers[k].upper_hz) {
                carrier = &ss->carriers[k];
                hz = carrier->centre_hz;
                break;
            }
        band = band_plan_lookup(hz);
        if (carrier)
            snprintf(title, sizeof(title),
                     "%.3f MHz   %.1f dBFS   %.0f kHz wide%s   %s",
                     hz / 1e6, (double)carrier->peak_dbfs,
                     carrier->width_hz / 1e3,
                     carrier->peaks > 1 ? "" : "",
                     band ? band->name : "no band plan entry");
        else
            snprintf(title, sizeof(title), "%.3f MHz   %.1f dBFS   %s",
                     hz / 1e6, (double)ss->peaks[s->hover].power_dbfs,
                     band ? band->name : "no band plan entry");
        entry = ss->history_loaded
                    ? site_history_find(&ss->history, hz, tolerance) : NULL;
        if (!ss->history_loaded || ss->history.sweeps == 0)
            snprintf(detail, sizeof(detail),
                     "no history for this site yet -- save this sweep to start"
                     " one");
        else
            survey_history_line(&ss->history, entry, detail, sizeof(detail));
    } else {
        /* Not over a candidate. It may still be over the mark left where
           something this site knows about has gone quiet. */
        int i, nearest = -1;
        float best = 7.0f;
        for (i = 0; i < ss->missing_count; i++) {
            float mx = sdrgui_survey_chart_x_at(l->chart, p, ss->missing[i]->hz);
            float gap = mx - mouse.x;
            if (mx != mx)
                continue;
            if (gap < 0.0f)
                gap = -gap;
            if (gap < best) {
                best = gap;
                nearest = i;
            }
        }
        if (nearest < 0)
            return;
        entry = ss->missing[nearest];
        snprintf(title, sizeof(title), "%.3f MHz   not heard this sweep",
                 entry->hz / 1e6);
        survey_history_line(&ss->history, entry, detail, sizeof(detail));
    }

    width = (float)MeasureText(strlen(title) > strlen(detail) ? title : detail,
                               16) + 24.0f;
    height = 52.0f;
    x = mouse.x + 16.0f;
    y = mouse.y + 16.0f;
    /* Kept inside the chart, so a candidate at the right edge does not put its
       own description off the window. */
    if (x + width > l->chart.x + l->chart.width)
        x = mouse.x - width - 16.0f;
    if (y + height > l->chart.y + l->chart.height)
        y = mouse.y - height - 16.0f;
    DrawRectangle((int)x, (int)y, (int)width, (int)height,
                  (Color){ 18, 24, 32, 245 });
    DrawRectangleLines((int)x, (int)y, (int)width, (int)height,
                       (Color){ 90, 116, 132, 255 });
    sdrgui_text_fit(title, (int)x + 12, (int)y + 8, 16, width - 24.0f,
                    (Color){ 214, 230, 240, 255 });
    sdrgui_text_fit(detail, (int)x + 12, (int)y + 28, 16, width - 24.0f,
                    (Color){ 150, 172, 188, 255 });
}

void draw_survey(struct app *app) {
    struct survey_session *ss = &app->survey.session;
    struct survey_view *s = &app->survey;
    struct survey_layout l = survey_layout_now();
    char text[240];

    survey_refresh_fields(s);

    DrawText("Range", (int)l.from_field.x,
             (int)(l.from_field.y - l.label_offset), (int)l.label_height,
             (Color){ 157, 180, 194, 255 });
    DrawText("to", (int)l.to_field.x,
             (int)(l.to_field.y - l.label_offset), (int)l.label_height,
             (Color){ 157, 180, 194, 255 });
    DrawText("dwell (s)", (int)l.dwell_field.x,
             (int)(l.dwell_field.y - l.label_offset), (int)l.label_height,
             (Color){ 157, 180, 194, 255 });
    sdrgui_text_field(l.from_field, s->from, s->focus == 0);
    sdrgui_text_field(l.to_field, s->to, s->focus == 1);
    sdrgui_text_field(l.dwell_field, s->dwell, s->focus == 2);
    DrawText("site", (int)l.site_field.x,
             (int)(l.site_field.y - l.label_offset), (int)l.label_height,
             s->site[0] ? (Color){ 157, 180, 194, 255 }
                        : (Color){ 214, 168, 90, 255 });
    DrawText("antenna", (int)l.antenna_field.x,
             (int)(l.antenna_field.y - l.label_offset), (int)l.label_height,
             (Color){ 157, 180, 194, 255 });
    sdrgui_text_field(l.site_field, s->site, s->focus == 3);
    draw_button(l.site_menu_button, s->site_menu_open ? "^" : "v",
                app->config.site_count > 0);
    sdrgui_text_field(l.antenna_field, s->antenna, s->focus == 4);
    draw_button(l.antenna_menu_button, s->antenna_menu_open ? "^" : "v",
                app->config.antenna_count > 0);
    draw_button(l.save_button, "Save survey", ss->peak_count > 0 && s->site[0]);
    {
        int asking = survey_session_confirming(ss);
        int changes = survey_session_change_count(ss);

        if (asking)
            snprintf(text, sizeof(text), "Asking %d/%d",
                     ss->confirm.index + 1, ss->confirm.count);
        else if (changes > 0)
            snprintf(text, sizeof(text), "Ask again (%d)", changes);
        else
            snprintf(text, sizeof(text), "Ask again");
        draw_button(l.confirm_button, text,
                    asking || (changes > 0 && app->receiver_mode));
        if (ss->watching)
            snprintf(text, sizeof(text), "Watching %d", ss->watch_sweeps);
        else
            snprintf(text, sizeof(text), "Watch");
        draw_button(l.watch_button, text, ss->watching);
    }
    draw_button(l.sweep_button, survey_session_sweeping(ss) ? "Sweeping" : "Sweep",
                !survey_session_sweeping(ss));
    draw_button(l.reset_button, "Reset zoom", 0);
    draw_button(l.band_button, "Band...", app->survey.band_menu_open);
    if (survey_session_sweeping(ss))
        draw_button(l.stop_button, "Stop", 0);

    if (survey_session_sweeping(ss)) {
        snprintf(text, sizeof(text),
                 "step %d / %d   %.3f MHz   bin %.0f kHz   dwell %.2f s",
                 ss->step + 1, ss->step_count,
                 app->applied_frequency / 1e6,
                 survey_bin_width_hz(s) / 1e3, ss->dwell_seconds);
        sdrgui_text_fit(text, (int)l.header_left, (int)l.status_y, 17,
                        l.header_right - l.header_left,
                        (Color){ 250, 190, 74, 255 });
    } else {
        sdrgui_text_fit(ss->status, (int)l.header_left, (int)l.status_y, 17,
                        l.header_right - l.header_left,
                        (Color){ 190, 208, 218, 255 });
    }

    /* Before the first sweep the range comes from the fields, so the axis is
       labelled with what is about to be swept rather than with zeroes, and the
       zoom has an extent to work on. */
    double shown_lower = survey_data_lower(s);
    double shown_upper = survey_data_upper(s);
    double window_lower = s->view_upper_hz > s->view_lower_hz
                              ? s->view_lower_hz : shown_lower;
    double window_upper = s->view_upper_hz > s->view_lower_hz
                              ? s->view_upper_hz : shown_upper;
    struct sdrgui_survey_band bands[SURVEY_MAX_BANDS];
    int band_count = survey_visible_bands(bands, SURVEY_MAX_BANDS,
                                          window_lower, window_upper);
    struct sdrgui_survey_params params = {
        l.chart, ss->power, ss->bins, SURVEY_SENTINEL_DBFS, shown_lower,
        shown_upper, window_lower, window_upper, bands, band_count,
        ss->peaks, ss->peak_count, survey_suspicious_now(app),
        s->selected, s->hover,
        survey_session_sweeping(ss) ? (ss->step * ss->bins) / (ss->step_count > 0 ? ss->step_count : 1)
                    : ss->bins,
        survey_session_sweeping(ss), s->drag_active, s->drag_from_hz, s->drag_to_hz,
        app->receiver_mode ? "press Sweep to survey the range"
                           : "a sweep needs a live receiver",
        NULL   /* peak_flags: filled in below where the chart is drawn */
    };
    /*
     * One flag word per peak, in the order the chart reads them, so a spur, an
     * empty frequency and a station do not draw the same mark. Through the
     * carrier each maximum belongs to, for the reason the candidate list
     * records: the pass asks about carriers at their measured centre and a
     * station's shoulders are maxima several kilohertz away.
     */
    {
        static unsigned flags[SURVEY_MAX_PEAKS];
        int i;

        for (i = 0; i < ss->peak_count && i < SURVEY_MAX_PEAKS; i++) {
            double hz = survey_bin_hz(s, ss->peaks[i].index);
            const struct survey_carrier *held = survey_carrier_at(s, hz);

            flags[i] = survey_suspect_at(app, hz, 0.0) |
                       survey_confirmed_flags_at(app,
                                                 held ? held->centre_hz : hz);
        }
        params.peak_flags = flags;
    }
    sdrgui_survey_chart(&params);
    survey_draw_history_marks(app, &l, &params);
    draw_peak_list(app, l.peak_list);
    draw_detail(app, &l);
    /* Last, so they sit over the chart and the panels rather than under. */
    survey_draw_pickers(app, &l);
    survey_draw_popup(app, &l, &params);
}
