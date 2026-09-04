#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "view.h"
#include "survey_layout.h"
#include "row_list.h"
#include "survey_window.h"
#include "survey_suspect.h"
#include "survey_store.h"
#include "band_plan_view.h"
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
 */

int survey_editing(const struct app *app) {
    return app->tab == TAB_SURVEY &&
           app->survey.focus >= 0;
}

static int survey_start(struct app *app);
static void survey_keep_current(struct survey_view *s);
static void survey_sweep_span(struct app *app, double from, double to);

/* Start the same range again, for a watch. Returns 0 when it began. */
static int survey_start_sweep_again(struct app *app) {
    struct survey_view *s = &app->survey;
    if (!app->receiver_mode || s->lower_hz >= s->upper_hz)
        return -1;
    survey_sweep_span(app, s->lower_hz, s->upper_hz);
    return s->sweeping ? 0 : -1;
}
static int survey_sweep_target(const struct survey_view *s, double *from,
                               double *to);

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

/*
 * The window arithmetic lives in survey_window.h, as plain doubles that
 * tests/survey_window_test.c can exercise without a window or a receiver.
 * These are the adapters between it and the view's own state: the range that
 * exists is what was swept once there is a sweep, and what the fields say
 * before that.
 */
static struct survey_window survey_window_of(const struct survey_view *s) {
    struct survey_window w;

    w.data_lower_hz = s->bins > 0 ? s->lower_hz : s->field_lower_hz;
    w.data_upper_hz = s->bins > 0 ? s->upper_hz : s->field_upper_hz;
    w.view_lower_hz = s->view_lower_hz;
    w.view_upper_hz = s->view_upper_hz;
    return w;
}

static void survey_window_put(struct survey_view *s,
                              const struct survey_window *w) {
    s->view_lower_hz = w->view_lower_hz;
    s->view_upper_hz = w->view_upper_hz;
}

/* The frequency at the middle of a survey bin. */
/*
 * What is suspicious about a frequency this survey found. The bandwidth is
 * only known once the candidate has been measured, so it is passed as 0 until
 * then and the two frequency tests carry the warning on their own.
 */
static unsigned survey_suspect_at(const struct app *app, double hz,
                                  double bandwidth_hz) {
    return survey_suspect(&app->survey.plan, hz, bandwidth_hz,
                          (double)app->applied_sample_rate, SDR_DSP_FFT_SIZE,
                          app->remove_dc);
}

/* How many of this sweep's candidates resemble the receiver. Recomputed each
   frame rather than stored: it is a few hundred multiplications, and a stored
   count is one more thing that can disagree with the list beside it. */
static int survey_suspicious_now(const struct app *app) {
    return survey_suspect_count(&app->survey.plan, app->survey.peaks,
                                app->survey.peak_count,
                                (double)app->applied_sample_rate,
                                SDR_DSP_FFT_SIZE, app->remove_dc);
}

static double survey_bin_hz(const struct survey_view *s, int bin) {
    struct survey_window w = survey_window_of(s);

    /* Bins span what was swept, so before a sweep there is nothing to index
       into and the range's low edge is the honest answer. */
    if (s->bins <= 0)
        return s->lower_hz;
    return survey_window_bin_hz(&w, s->bins, bin);
}

static double survey_bin_width_hz(const struct survey_view *s) {
    if (s->bins <= 0)
        return 0.0;
    return (s->upper_hz - s->lower_hz) / (double)s->bins;
}

/*
 * The range everything is measured against: what was swept once a sweep has
 * run, and what is typed in the fields before that. Without the second half
 * the view has no extent until the first sweep, and zooming, panning and
 * dragging a rectangle all quietly did nothing on a freshly opened survey --
 * they were dividing by a span of zero.
 */
static double survey_data_lower(const struct survey_view *s) {
    return survey_window_of(s).data_lower_hz;
}

static double survey_data_upper(const struct survey_view *s) {
    return survey_window_of(s).data_upper_hz;
}

static void survey_clamp_view(struct survey_view *s) {
    struct survey_window w = survey_window_of(s);

    survey_window_clamp(&w, SURVEY_MIN_SPAN_HZ);
    survey_window_put(s, &w);
}

static void survey_reset_view(struct survey_view *s) {
    struct survey_window w = survey_window_of(s);

    survey_window_reset(&w);
    survey_window_put(s, &w);
}

/* Keep the field range current, and before the first sweep keep the window on
   it: editing the range should move the chart it is about to sweep. */
/* The hour of the day, for the clock the history keeps. */
static int survey_hour_now(void) {
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    return local.tm_hour;
}

static void survey_history_refresh(struct app *app);

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
 * The spectrum is only handed over when the whole survey came from one tuning.
 * Across a swept range it belongs to whichever step happened to be last, and a
 * bandwidth measured out of it would be a number about the wrong signal --
 * the same rule the headless report follows, and the reason both now go
 * through survey_candidates_from().
 */
static void survey_save_sweep(struct app *app) {
    struct survey_view *s = &app->survey;
    struct survey_candidate candidates[SURVEY_MAX_PEAKS];
    char path[256];
    int count;

    if (s->peak_count <= 0) {
        snprintf(s->status, sizeof(s->status),
                 "Nothing to save yet: sweep first.");
        return;
    }
    if (!s->site[0]) {
        /* Refused rather than saved as unknown. Two sweeps with no site
           compare as the same place, which is the one way this archive can
           mislead instead of merely disappoint. */
        snprintf(s->status, sizeof(s->status),
                 "Name the site first -- a sweep without one cannot be "
                 "compared with another.");
        s->focus = 3;
        return;
    }
    count = survey_candidates_from(app, &s->plan, s->peaks, s->peak_count,
                                   s->plan.step_count <= 1
                                       ? app->spectrum_average : NULL,
                                   candidates, SURVEY_MAX_PEAKS);
    /*
     * The archive and the memory are written together. If they drift apart --
     * a sweep in surveys/ that the history never saw -- the window starts
     * calling known signals new, which is worse than having no memory at all.
     */
    {
        double hz[SURVEY_CARRIER_MAX];
        float level[SURVEY_CARRIER_MAX], prom[SURVEY_CARRIER_MAX];
        int i;
        /* Carriers, not candidates. The site remembers signals; a candidate
           is one maximum of one, and a station with four would arrive as four
           things to be surprised by next time. */
        for (i = 0; i < s->carrier_count; i++) {
            hz[i] = s->carriers[i].centre_hz;
            level[i] = s->carriers[i].peak_dbfs;
            prom[i] = s->carriers[i].prominence_db;
        }
        if (!s->history_loaded)
            site_history_load(app->config.site, &s->history);
        site_history_init(&s->history, app->config.site);
        site_history_load(app->config.site, &s->history);
        site_history_merge(&s->history, hz, level, prom, s->carrier_count,
                           s->plan.bin_hz, survey_hour_now());
        site_history_save(&s->history);
    }
    if (survey_store_write(app, &s->plan, candidates, count, s->carriers,
                           s->carrier_count, path, sizeof(path)) < 0) {
        snprintf(s->status, sizeof(s->status),
                 "Could not write the survey; see the terminal.");
        return;
    }
    {
        /* The name, not the path: the status line is one line and the
           directory is always the same one. */
        const char *slash = strrchr(path, '/');
        snprintf(s->status, sizeof(s->status),
                 "Saved %d candidates to surveys/%.64s -- compare sweeps "
                 "with scripts/survey_tool.py diff", count,
                 slash ? slash + 1 : path);
    }
    survey_history_refresh(app);
}

/*
 * The confirmation pass: revisit each thing the sweep called new or missing
 * and give it a proper look.
 *
 * It does not touch the survey's power array. Re-sweeping a narrow span would
 * throw away the wide sweep that produced the claims in the first place, and
 * getting that back costs minutes -- so this tunes to each target, folds
 * several blocks into one spectrum, and asks whether a carrier is there. That
 * is also better evidence than a re-sweep: six blocks on one frequency against
 * the one or two the sweep could spare.
 */
static int survey_confirm_begin(struct app *app) {
    struct survey_view *s = &app->survey;
    int i, count = 0;

    if (!app->receiver_mode)
        return -1;
    for (i = 0; i < s->carrier_count && count < SURVEY_CONFIRM_MAX; i++) {
        if (s->carrier_status[i] != SITE_STATUS_NEW)
            continue;
        s->confirm.target[count].hz = s->carriers[i].centre_hz;
        s->confirm.target[count].claim = SURVEY_CLAIM_NEW;
        s->confirm.target[count].verdict = SURVEY_VERDICT_PENDING;
        s->confirm.target[count].prominence_db = 0.0f;
        count++;
    }
    for (i = 0; i < s->missing_count && count < SURVEY_CONFIRM_MAX; i++) {
        s->confirm.target[count].hz = s->missing[i]->hz;
        s->confirm.target[count].claim = SURVEY_CLAIM_MISSING;
        s->confirm.target[count].verdict = SURVEY_VERDICT_PENDING;
        s->confirm.target[count].prominence_db = 0.0f;
        count++;
    }
    if (count == 0)
        return -1;

    s->confirm.count = count;
    s->confirm.index = 0;
    s->confirm.confirmed = 0;
    s->confirm.refuted = 0;
    s->confirm.running = 1;
    s->confirm.return_frequency = app->applied_frequency;
    if (retune_receiver(app, (uint32_t)llround(s->confirm.target[0].hz),
                        app->applied_ppm) < 0) {
        s->confirm.running = 0;
        return -1;
    }
    s->confirm.settled = 0;
    s->confirm.looks = 0;
    s->confirm.started_at = GetTime();
    return 0;
}

/* Fold what the closer look found back into what the site remembers, then
   forget the claims: they have been answered. */
static void survey_confirm_finish(struct app *app) {
    struct survey_view *s = &app->survey;

    s->confirm.running = 0;
    if (app->receiver_mode && s->confirm.return_frequency)
        retune_receiver(app, s->confirm.return_frequency, app->applied_ppm);
    /*
     * Printed as well as shown. A pass started from the command line has
     * nobody watching the status line, and a verdict nobody can read is a
     * verdict that may as well not have been reached (ADR-0012).
     */
    if (s->confirm.printed) {
        int i;
        printf("# confirm <frequency_hz> <claim> <verdict> <prominence_db>\n");
        for (i = 0; i < s->confirm.count; i++) {
            const struct survey_confirm_target *target = &s->confirm.target[i];
            printf("confirm %.0f %s %s %.1f\n", target->hz,
                   target->claim == SURVEY_CLAIM_NEW ? "new" : "missing",
                   target->verdict == SURVEY_VERDICT_CONFIRMED ? "confirmed"
                                                               : "refuted",
                   (double)target->prominence_db);
        }
        printf("confirm-summary asked %d confirmed %d refuted %d\n",
               s->confirm.count, s->confirm.confirmed, s->confirm.refuted);
        fflush(stdout);
        s->confirm.printed = 0;
    }
    snprintf(s->status, sizeof(s->status),
             "Asked again about %d: %d held up, %d did not. %s",
             s->confirm.count, s->confirm.confirmed, s->confirm.refuted,
             s->confirm.refuted
                 ? "A sweep step is a tenth of a second; this was six blocks."
                 : "The sweep had them right.");
}

static void survey_confirm_step(struct app *app, double now, int have_block) {
    struct survey_view *s = &app->survey;
    struct survey_confirm_target *target;

    if (!s->confirm.running)
        return;
    target = &s->confirm.target[s->confirm.index];

    if (!s->confirm.settled) {
        if (now - s->confirm.started_at >= SURVEY_CONFIRM_SETTLE_SECONDS) {
            s->confirm.settled = 1;
            s->confirm.started_at = now;
        }
        return;
    }
    if (have_block) {
        s->confirm.looks++;
        if (s->confirm.looks < SURVEY_CONFIRM_LOOKS)
            return;
    } else if (now - s->confirm.started_at < 3.0) {
        return;                        /* still waiting for blocks */
    }

    {
        /* The averaged spectrum has had every block of this dwell folded into
           it, so the question is simply whether a carrier stands above the
           local floor here. */
        struct sdr_carrier_report report;
        int measured = sdr_dsp_characterise_carrier(
            app->spectrum_average, SDR_DSP_FFT_SIZE,
            (double)app->applied_frequency,
            (double)app->applied_sample_rate, target->hz, 200000.0, 20.0f,
            app->magnitude_sorted, &report);
        int present = measured && survey_confirm_present(report.prominence_db);

        target->prominence_db = measured ? report.prominence_db : 0.0f;
        target->verdict = (signed char)survey_confirm_verdict(target->claim,
                                                              present);
        if (target->verdict == SURVEY_VERDICT_CONFIRMED)
            s->confirm.confirmed++;
        else
            s->confirm.refuted++;

        /*
         * Teach the site what the closer look found, not what the sweep
         * guessed. A "new" that held up is worth remembering; one that did not
         * was noise and must not enter the history, or the next sweep will
         * call it missing and the noise becomes a permanent ghost.
         */
        if (app->config.site[0] && s->history_loaded &&
            survey_confirm_should_record(target->claim, target->verdict)) {
            /*
             * Recorded at the carrier's measured centre, not at the peak that
             * pointed here. A 200 kHz FM signal has several local maxima and
             * the sweep reports each; remembering each would fill the history
             * with five entries for one station, all of them "new" next time
             * something moved by a bin.
             */
            site_history_record_one(&s->history,
                                    measured ? report.centre_hz : target->hz,
                                    report.peak_dbfs, report.prominence_db,
                                    (double)app->applied_sample_rate /
                                        SDR_DSP_FFT_SIZE,
                                    survey_hour_now());
            site_history_save(&s->history);
        }
    }

    s->confirm.index++;
    if (s->confirm.index >= s->confirm.count) {
        survey_confirm_finish(app);
        survey_history_refresh(app);
        return;
    }
    if (retune_receiver(app,
                        (uint32_t)llround(s->confirm.target[s->confirm.index].hz),
                        app->applied_ppm) < 0) {
        survey_confirm_finish(app);
        return;
    }
    s->confirm.settled = 0;
    s->confirm.looks = 0;
    s->confirm.started_at = now;
}

/*
 * One sweep of a watch: fold what was found into what the site knows, and
 * count what changed.
 *
 * The folding is the point. A watch that only looked would learn nothing --
 * every sweep would find the same signals "new", because new means "this site
 * has not heard it" and nothing would ever have been written down. It is also
 * the only way an hour accumulates enough sweeps for a daily pattern to be
 * visible at all.
 */
static void survey_watch_fold(struct app *app) {
    struct survey_view *s = &app->survey;
    double hz[SURVEY_CARRIER_MAX];
    float level[SURVEY_CARRIER_MAX], prom[SURVEY_CARRIER_MAX];
    int i;

    if (!app->config.site[0])
        return;
    if (!s->history_loaded)
        site_history_load(app->config.site, &s->history);
    for (i = 0; i < s->carrier_count; i++) {
        hz[i] = s->carriers[i].centre_hz;
        level[i] = s->carriers[i].peak_dbfs;
        prom[i] = s->carriers[i].prominence_db;
    }
    s->watch_appeared = site_history_merge(&s->history, hz, level, prom,
                                           s->carrier_count, s->plan.bin_hz,
                                           survey_hour_now());
    s->watch_lost = site_history_lost_now(&s->history);
    s->watch_total_appeared += s->watch_appeared;
    s->watch_total_lost += s->watch_lost;
    s->watch_sweeps++;
    site_history_save(&s->history);
}

/*
 * Reload what this site has heard, and work out how this sweep compares.
 *
 * Done when the site changes or a sweep ends rather than per frame: it reads
 * a file and walks every candidate against every remembered entry, and
 * neither answer changes between frames.
 */
static void survey_history_refresh(struct app *app) {
    struct survey_view *s = &app->survey;
    double hz[SURVEY_CARRIER_MAX];
    double tolerance;
    int i;

    s->missing_count = 0;
    memset(s->carrier_status, 0, sizeof(s->carrier_status));
    s->history_loaded = 0;
    if (!app->config.site[0]) {
        site_history_init(&s->history, "");
        return;
    }
    if (site_history_load(app->config.site, &s->history) < 0)
        return;
    s->history_loaded = 1;

    tolerance = s->plan.bin_hz > 0.0 ? s->plan.bin_hz : 1e5;
    for (i = 0; i < s->carrier_count; i++) {
        hz[i] = s->carriers[i].centre_hz;
        s->carrier_status[i] = (signed char)site_history_status(
            &s->history, hz[i], tolerance);
    }
    if (s->carrier_count > 0)
        s->missing_count = site_history_missing(
            &s->history, hz, s->carrier_count, s->plan.lower_hz,
            s->plan.upper_hz, tolerance, s->missing,
            (int)(sizeof(s->missing) / sizeof(s->missing[0])));
}

/* Which signal a maximum belongs to, or NULL. The list and the popup both ask,
   because a reader points at a bump and wants to know about the carrier. */
static const struct survey_carrier *survey_carrier_at(const struct survey_view *s,
                                                      double hz) {
    int i;
    for (i = 0; i < s->carrier_count; i++)
        if (hz >= s->carriers[i].lower_hz && hz <= s->carriers[i].upper_hz)
            return &s->carriers[i];
    return NULL;
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
                snprintf(s->status, sizeof(s->status),
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
    if (s->bins > 0)
        survey_clamp_view(s);
}

/* Zoom about an anchor: the selected candidate when there is one, so zooming
   in keeps what you picked in sight, otherwise the middle of the window. */
/* Zoom about the selected candidate when there is one, so zooming in keeps
   what you picked in sight; otherwise about the middle. */
static void survey_zoom(struct survey_view *s, double factor) {
    struct survey_window w = survey_window_of(s);
    int has_anchor = s->selected >= 0 && s->selected < s->peak_count;
    double anchor = has_anchor
                        ? survey_bin_hz(s, s->peaks[s->selected].index)
                        : 0.0;

    survey_window_zoom(&w, factor, anchor, has_anchor, SURVEY_MIN_SPAN_HZ);
    survey_window_put(s, &w);
}

/* Panning a window that already spans the whole sweep cannot move it, and a
   key that silently does nothing reads as a broken key. Say which it is. */
static void survey_pan(struct app *app, double fraction) {
    struct survey_view *s = &app->survey;
    struct survey_window w = survey_window_of(s);
    double span = w.view_upper_hz - w.view_lower_hz;

    if (!survey_window_pan(&w, fraction, SURVEY_MIN_SPAN_HZ))
        snprintf(s->status, sizeof(s->status),
                 "Already showing %s of the range; zoom in first (+ or the "
                 "wheel over the chart).",
                 span >= (w.data_upper_hz - w.data_lower_hz) - 1.0
                     ? "all"
                     : "the end");
    survey_window_put(s, &w);
}

/* Candidates inside the window on screen. Zooming into a band and still
   being shown a list of what is loudest elsewhere is no use, so the list, the
   count and the Up/Down walk all follow the window. */
static int survey_peak_visible(const struct survey_view *s, int index) {
    struct survey_window w = survey_window_of(s);

    if (index < 0 || index >= s->peak_count)
        return 0;
    return survey_window_bin_visible(&w, s->bins, s->peaks[index].index);
}

static int survey_visible_count(const struct survey_view *s) {
    int count = 0;

    for (int i = 0; i < s->peak_count; i++)
        if (survey_peak_visible(s, i))
            count++;
    return count;
}

/* The nth visible candidate, as an index into peaks; -1 when there is none. */
static int survey_nth_visible(const struct survey_view *s, int n) {
    int seen = 0;

    for (int i = 0; i < s->peak_count; i++) {
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

    for (int i = 0; i < s->peak_count; i++) {
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
    s->dwell_seconds = SURVEY_DWELL_DEFAULT;
    s->list_scroll = 0;
    s->selected = -1;
    s->hover = -1;
    /* No field is focused until one is clicked, so the number keys keep
       switching views the way they do in every other Scope view. */
    s->focus = -1;
    snprintf(s->status, sizeof(s->status),
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

    if (app->receiver_mode && !s->return_valid) {
        s->return_frequency = app->applied_frequency;
        s->return_valid = 1;
    }
    /* A range given on the command line arrives here, and sweeps without
       being asked twice: someone who typed it has already asked. */
    if (app->options.survey_seen && !s->sweeping) {
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

    s->sweeping = 0;
    s->measuring = 0;
    if (app->receiver_mode && s->return_valid)
        retune_receiver(app, s->return_frequency, app->applied_ppm);
    s->return_valid = 0;
}

static void survey_clear(struct survey_view *s) {
    for (int i = 0; i < s->bins; i++)
        s->power[i] = SURVEY_SENTINEL_DBFS;
    s->peak_count = 0;
    s->list_scroll = 0;
    s->selected = -1;
    s->hover = -1;
    s->report_valid = 0;
    s->measuring = 0;
}

static int survey_start(struct app *app) {
    struct survey_view *s = &app->survey;
    uint32_t from_hz;
    uint32_t to_hz;

    if (!app->receiver_mode) {
        snprintf(s->status, sizeof(s->status),
                 "A sweep needs a live receiver: a capture holds one tuning.");
        return -1;
    }
    if (parse_frequency(s->from, &from_hz) < 0 ||
        parse_frequency(s->to, &to_hz) < 0) {
        snprintf(s->status, sizeof(s->status),
                 "Use Hz or a K/M/G value, for example 88M");
        return -1;
    }
    if (parse_seconds(s->dwell, &s->dwell_seconds) < 0) {
        snprintf(s->status, sizeof(s->status),
                 "Dwell must be between %.2f and %.0f seconds.",
                 SURVEY_DWELL_MIN, SURVEY_DWELL_MAX);
        return -1;
    }

    switch (survey_plan_make((double)from_hz, (double)to_hz,
                             (double)app->applied_sample_rate,
                             SDR_DSP_FFT_SIZE, s->dwell_seconds, &s->plan)) {
    case SURVEY_PLAN_BAD_RANGE:
        snprintf(s->status, sizeof(s->status),
                 "The high edge must be above the low one.");
        return -1;
    case SURVEY_PLAN_BAD_DWELL:
        snprintf(s->status, sizeof(s->status),
                 "Dwell must be between %.2f and %.0f seconds.",
                 SURVEY_DWELL_MIN, SURVEY_DWELL_MAX);
        return -1;
    case SURVEY_PLAN_BAD_RATE:
        snprintf(s->status, sizeof(s->status),
                 "Sample rate is too low to sweep.");
        return -1;
    case SURVEY_PLAN_OK:
        break;
    }

    s->lower_hz = s->plan.lower_hz;
    s->upper_hz = s->plan.upper_hz;
    s->bins = s->plan.bins;
    survey_clear(s);

    survey_reset_view(s);
    s->step_count = s->plan.step_count;
    s->step = 0;
    s->step_folded = 0;
    s->sweeping = 1;
    view_survey_enter(app);

    double first = survey_plan_step_centre(&s->plan, 0);
    if (retune_receiver(app, (uint32_t)llround(first), app->applied_ppm) < 0) {
        s->sweeping = 0;
        snprintf(s->status, sizeof(s->status),
                 "The receiver would not tune to %.3f MHz.", first / 1e6);
        return -1;
    }
    s->step_started_at = GetTime();
    /* What this is going to cost, before it is spent: a long dwell over a wide
       range is minutes, and knowing that up front is the difference between
       patience and pressing Stop. */
    snprintf(s->status, sizeof(s->status),
             "Sweeping %.3f - %.3f MHz in %d steps, %.2f s each: about %s.",
             s->lower_hz / 1e6, s->upper_hz / 1e6, s->step_count,
             s->dwell_seconds,
             s->plan.seconds < 90.0 ? "a minute" : "a few minutes");
    return 0;
}

/* Fold the usable middle of the current spectrum into the survey array. The
   outer fifth of each step is discarded: the tuner's response rolls off at the
   edges of its span, so a signal there reads low, and the next step covers it
   properly anyway. */
static void survey_fold_block(struct app *app) {
    struct survey_view *s = &app->survey;
    double rate = (double)app->applied_sample_rate;
    double centre = (double)app->applied_frequency;
    double bin_hz = rate / (double)SDR_DSP_FFT_SIZE;
    double lower = centre - rate / 2.0;

    for (int i = 0; i < SDR_DSP_FFT_SIZE; i++) {
        double hz = lower + ((double)i + 0.5) * bin_hz;
        int bin;

        if (!survey_fold_keeps(hz, centre, rate))
            continue;
        bin = survey_plan_bin_at(&s->plan, hz);
        if (bin < 0)
            continue;
        s->power[bin] = survey_fold_hold(s->power[bin],
                                         app->spectrum_average[i]);
    }
}

static void survey_find_peaks(struct app *app) {
    struct survey_view *s = &app->survey;

    s->peak_count = sdr_dsp_find_peaks(s->power, s->bins, SURVEY_SENTINEL_DBFS,
                                       SURVEY_MIN_PROMINENCE_DB,
                                       SURVEY_BANDWIDTH_DB,
                                       app->magnitude_sorted, s->peaks,
                                       SURVEY_MAX_PEAKS);
    /* Maxima into signals, before anything asks what changed: a carrier's
       shoulders are not separate things to notice. */
    s->carrier_count = survey_carriers_from(
        s->power, s->bins, SURVEY_SENTINEL_DBFS,
        survey_plan_bin_centre(&s->plan, 0),
        s->plan.bin_hz > 0.0 ? s->plan.bin_hz : 1.0, SURVEY_BANDWIDTH_DB,
        s->peaks, s->peak_count,
        s->carriers, SURVEY_CARRIER_MAX);
    /* The peaks just changed, so what is new and what is absent has
       changed with them. */
    survey_history_refresh(app);
    /*
     * A sweep asked for on the command line can ask again by itself, which is
     * the only way the pass is reachable without somebody to click it
     * (ADR-0012). Once: a second sweep is a new question, not a repeat.
     */
    if (app->options.survey_watch > 0 && !app->survey.watching &&
        app->survey.watch_sweeps == 0 && app->config.site[0]) {
        app->survey.watching = 1;
        app->survey.watch_started_at = GetTime();
    }
    if (app->options.survey_confirm && !app->survey.confirm.running) {
        app->options.survey_confirm = 0;
        /* The caller writes its own "swept N steps" line after this returns,
           so there is no point announcing the pass here; it announces itself
           on the button and reports when it is done. */
        if (survey_confirm_begin(app) == 0)
            app->survey.confirm.printed = 1;
    }
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

    if (rank < 1 || s->peak_count <= 0)
        return -1;
    for (i = 0; i < s->peak_count; i++)
        taken[i] = 0;
    for (r = 0; r < rank; r++) {
        best = -1;
        for (i = 0; i < s->peak_count; i++) {
            if (taken[i] || !survey_peak_visible(s, i))
                continue;
            if (best < 0 ||
                s->peaks[i].power_dbfs > s->peaks[best].power_dbfs)
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
   receiver. */
static void survey_select(struct app *app, int index) {
    struct survey_view *s = &app->survey;
    double hz;

    if (index < 0 || index >= s->peak_count)
        return;
    s->selected = index;
    s->report_valid = 0;
    survey_measure_reset(&s->measure);
    hz = survey_bin_hz(s, s->peaks[index].index);
    s->measure_expected_hz = hz;
    /* Stepping the list with Up/Down can land on a candidate the window is
       zoomed past; follow it rather than selecting something invisible. */
    if (hz < s->view_lower_hz || hz > s->view_upper_hz) {
        double span = s->view_upper_hz - s->view_lower_hz;
        s->view_lower_hz = hz - span / 2.0;
        s->view_upper_hz = s->view_lower_hz + span;
        survey_clamp_view(s);
    }

    if (!app->receiver_mode) {
        snprintf(s->status, sizeof(s->status),
                 "%.4f MHz selected; measuring needs a live receiver.",
                 hz / 1e6);
        return;
    }
    if (retune_receiver(app, (uint32_t)llround(hz - SURVEY_OFFSET_HZ),
                        app->applied_ppm) < 0) {
        snprintf(s->status, sizeof(s->status),
                 "The receiver would not tune to %.4f MHz.", hz / 1e6);
        return;
    }
    s->measuring = 1;
    s->measure_started_at = GetTime();
    snprintf(s->status, sizeof(s->status), "Measuring %.4f MHz", hz / 1e6);
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

/* One block's worth of measurement of the selected candidate. */
static void survey_measure_block(struct app *app) {
    struct survey_view *s = &app->survey;
    struct sdr_carrier_report report;
    int found;

    found = sdr_dsp_characterise_carrier(app->spectrum_average,
                                         SDR_DSP_FFT_SIZE,
                                         (double)app->applied_frequency,
                                         (double)app->applied_sample_rate,
                                         s->measure_expected_hz, 200000.0,
                                         SURVEY_BANDWIDTH_DB,
                                         app->magnitude_sorted, &report);
    /* The duty rule -- what counts as the candidate being up in this block --
       is in survey_sweep.h with the rest of the arithmetic. */
    if (survey_measure_observe(&s->measure, found, report.prominence_db,
                               report.centre_hz)) {
        s->report = report;
        s->report_valid = 1;
    }
}

void update_survey(struct app *app, double now, int spectrum_updated) {
    struct survey_view *s = &app->survey;

    if (s->confirm.running) {
        /* A block arriving is what advances it, the same as the sweep. */
        survey_confirm_step(app, now, spectrum_updated);
        return;
    }
    if (s->sweeping) {
        double elapsed = now - s->step_started_at;
        enum survey_step_phase phase = survey_step_phase_at(
            elapsed, s->dwell_seconds, s->step, s->step_count);

        if (phase == SURVEY_STEP_SETTLING)
            return;
        /* Every block that arrives during the dwell is folded in, and the fold
           is a peak hold, so a burst anywhere inside the dwell leaves its mark
           even though the blocks either side of it were quiet. That is the
           whole point of dwelling: one block only ever catches what happened
           to be transmitting at that instant. */
        survey_fold_block(app);
        s->step_folded = 1;
        if (phase == SURVEY_STEP_DWELLING) {
            survey_find_peaks(app);
            return;
        }
        s->step++;
        if (phase == SURVEY_STEP_FINISHED) {
            s->sweeping = 0;
            survey_find_peaks(app);
            snprintf(s->status, sizeof(s->status),
                     "Swept %.3f - %.3f MHz in %d steps; %d candidates%s."
                     "   Up/Down or click to inspect one.",
                     s->lower_hz / 1e6, s->upper_hz / 1e6, s->step_count,
                     s->peak_count,
                     s->peak_count >= SURVEY_MAX_PEAKS
                         ? " (as many as this view holds)"
                         : " found");
            /*
             * A watch folds the sweep in, says what changed, and goes round
             * again. It does not park the receiver back where it started --
             * it is about to move it anyway.
             */
            if (s->watching) {
                survey_watch_fold(app);
                if (app->options.survey_watch > 0) {
                    /* A watch started from the command line has nobody
                       reading the status line. */
                    printf("watch sweep %d carriers %d appeared %d quiet %d\n",
                           s->watch_sweeps, s->carrier_count,
                           s->watch_appeared, s->watch_lost);
                    fflush(stdout);
                    if (s->watch_sweeps >= app->options.survey_watch) {
                        printf("watch-summary sweeps %d appeared %d quiet %d\n",
                               s->watch_sweeps, s->watch_total_appeared,
                               s->watch_total_lost);
                        fflush(stdout);
                        s->watching = 0;
                    }
                }
                snprintf(s->status, sizeof(s->status),
                         "Watching %s: sweep %d, %d appeared, %d went quiet"
                         "  (%d and %d since the watch began)",
                         app->config.site[0] ? app->config.site : "nowhere",
                         s->watch_sweeps, s->watch_appeared, s->watch_lost,
                         s->watch_total_appeared, s->watch_total_lost);
                if (survey_start_sweep_again(app) == 0)
                    return;
                s->watching = 0;
            }
            /* Back where the operator was, until they pick a candidate. */
            if (app->receiver_mode && s->return_valid)
                retune_receiver(app, s->return_frequency, app->applied_ppm);
            /*
             * Unless a script asked for one. The detail panel and its Inspect
             * button only exist once a candidate has been chosen and
             * measured, so without this there is no way to photograph that
             * screen -- and it is a screen this program has twice shipped
             * broken (CLAUDE.md).
             */
            if (app->options.survey_select > 0) {
                int rank = app->options.survey_select;
                int best = survey_strongest_visible(s, rank);
                app->options.survey_select = 0;
                if (best >= 0)
                    survey_select(app, best);
            }
            return;
        }
        double next = survey_plan_step_centre(&s->plan, s->step);
        if (retune_receiver(app, (uint32_t)llround(next), app->applied_ppm) < 0) {
            s->sweeping = 0;
            snprintf(s->status, sizeof(s->status),
                     "Stopped: the receiver would not tune to %.3f MHz.",
                     next / 1e6);
            return;
        }
        s->step_folded = 0;
        s->step_started_at = now;
        return;
    }

    if (s->measuring) {
        survey_measure_block(app);
        if (now - s->measure_started_at >= SURVEY_MEASURE_SECONDS) {
            s->measuring = 0;
            snprintf(s->status, sizeof(s->status),
                     "Measured %.4f MHz over %d blocks.",
                     s->measure_expected_hz / 1e6, s->measure.blocks);
        }
    }
}

/* Keep the survey a narrowing sweep is about to replace, so Reset zoom can
   put it back without re-sweeping. */
static void survey_keep_current(struct survey_view *s) {
    struct survey_snapshot *keep = &s->previous;

    if (s->bins <= 0)
        return;
    keep->valid = 1;
    keep->lower_hz = s->lower_hz;
    keep->upper_hz = s->upper_hz;
    keep->bins = s->bins;
    memcpy(keep->power, s->power, (size_t)s->bins * sizeof(*s->power));
    memcpy(keep->peaks, s->peaks, (size_t)s->peak_count * sizeof(*s->peaks));
    keep->peak_count = s->peak_count;
    snprintf(keep->from, sizeof(keep->from), "%s", s->from);
    snprintf(keep->to, sizeof(keep->to), "%s", s->to);
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
    struct survey_window w = survey_window_of(s);

    return survey_window_sweep_target(&w, from, to);
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
    if (s->confirm.running) {
        /* Everything else waits: the receiver is somewhere the operator did
           not put it, and a click that retunes now would strand the pass. */
        if (clicked(l.confirm_button) || IsKeyPressed(KEY_ESCAPE))
            survey_confirm_finish(app);
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
        s->watching = !s->watching;
        if (s->watching) {
            s->watch_sweeps = 0;
            s->watch_appeared = s->watch_lost = 0;
            s->watch_total_appeared = s->watch_total_lost = 0;
            s->watch_started_at = GetTime();
            if (!app->config.site[0]) {
                /* Without a site there is nothing to fold into, so a watch
                   would sweep for hours and learn nothing. */
                s->watching = 0;
                snprintf(s->status, sizeof(s->status),
                         "Name the site first -- a watch has nowhere to put "
                         "what it learns.");
                s->focus = 3;
            } else if (!s->sweeping && survey_start_sweep_again(app) < 0) {
                s->watching = 0;
                snprintf(s->status, sizeof(s->status),
                         "Watching needs a live receiver and a range swept "
                         "once.");
            }
        } else {
            snprintf(s->status, sizeof(s->status),
                     "Watch stopped after %d sweeps: %d appeared, %d went "
                     "quiet.", s->watch_sweeps, s->watch_total_appeared,
                     s->watch_total_lost);
        }
        return;
    }
    if ((clicked(l.confirm_button) ||
         (s->focus < 0 && IsKeyPressed(KEY_A))) &&
        !s->confirm.running && !s->sweeping) {
        int changes = 0, i;
        for (i = 0; i < s->carrier_count; i++)
            if (s->carrier_status[i] == SITE_STATUS_NEW)
                changes++;
        changes += s->missing_count;
        if (changes == 0)
            snprintf(s->status, sizeof(s->status),
                     "Nothing to ask about: this sweep matches what the site"
                     " has heard before.");
        else if (survey_confirm_begin(app) < 0)
            snprintf(s->status, sizeof(s->status),
                     "Asking again needs a live receiver.");
        else
            snprintf(s->status, sizeof(s->status),
                     "Asking again about %d %s, %.0f s.", changes,
                     changes == 1 ? "change" : "changes",
                     survey_confirm_seconds(changes));
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
    if (s->sweeping && clicked(l.stop_button)) {
        s->sweeping = 0;
        survey_find_peaks(app);
        snprintf(s->status, sizeof(s->status),
                 "Stopped after %d of %d steps; %d candidates so far.",
                 s->step, s->step_count, s->peak_count);
        if (app->receiver_mode && s->return_valid)
            retune_receiver(app, s->return_frequency, app->applied_ppm);
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
    if (clicked(l.reset_button) && !s->sweeping) {
        s->focus = -1;
        if (s->view_upper_hz > s->view_lower_hz &&
            (s->view_lower_hz > s->lower_hz + 1.0 ||
             s->view_upper_hz < s->upper_hz - 1.0)) {
            survey_reset_view(s);
            snprintf(s->status, sizeof(s->status),
                     "Showing the whole sweep, %.3f - %.3f MHz.",
                     s->lower_hz / 1e6, s->upper_hz / 1e6);
        } else if (s->previous.valid) {
            /* Put the earlier survey back on the chart, measurements and all.
               Restoring only the range fields left the chart still showing the
               region, which is not what "reset" means to anyone looking at
               it. */
            struct survey_snapshot *keep = &s->previous;
            s->lower_hz = keep->lower_hz;
            s->upper_hz = keep->upper_hz;
            s->bins = keep->bins;
            memcpy(s->power, keep->power,
                   (size_t)keep->bins * sizeof(*s->power));
            memcpy(s->peaks, keep->peaks,
                   (size_t)keep->peak_count * sizeof(*s->peaks));
            s->peak_count = keep->peak_count;
            snprintf(s->from, sizeof(s->from), "%s", keep->from);
            s->from_length = (int)strlen(s->from);
            snprintf(s->to, sizeof(s->to), "%s", keep->to);
            s->to_length = (int)strlen(s->to);
            s->selected = -1;
            s->hover = -1;
            s->report_valid = 0;
            s->measuring = 0;
            keep->valid = 0;
            survey_refresh_fields(s);
            survey_reset_view(s);
            snprintf(s->status, sizeof(s->status),
                     "Back to the sweep of %.3f - %.3f MHz; %d candidates.",
                     s->lower_hz / 1e6, s->upper_hz / 1e6, s->peak_count);
        } else {
            snprintf(s->from, sizeof(s->from), "24M");
            s->from_length = (int)strlen(s->from);
            snprintf(s->to, sizeof(s->to), "1766M");
            s->to_length = (int)strlen(s->to);
            snprintf(s->status, sizeof(s->status),
                     "Range set to the whole tuner; press Sweep to survey it.");
        }
        return;
    }

    /* Hover and selection, in the chart and in the list. */
    struct sdrgui_survey_params params = {
        l.chart, s->power, s->bins, SURVEY_SENTINEL_DBFS,
        survey_data_lower(s), survey_data_upper(s),
        s->view_upper_hz > s->view_lower_hz ? s->view_lower_hz
                                            : survey_data_lower(s),
        s->view_upper_hz > s->view_lower_hz ? s->view_upper_hz
                                            : survey_data_upper(s),
        NULL, 0, s->peaks, s->peak_count, survey_suspicious_now(app),
        s->selected, -1,
        s->sweeping ? (s->step * s->bins) / (s->step_count > 0 ? s->step_count : 1)
                    : s->bins,
        s->sweeping, 0, 0.0, 0.0, ""
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
                snprintf(s->status, sizeof(s->status),
                         "Zoomed to %.3f - %.3f MHz.   0 shows the whole sweep"
                         " again.", s->view_lower_hz / 1e6,
                         s->view_upper_hz / 1e6);
            } else if (dragged) {
                snprintf(s->status, sizeof(s->status),
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
    if (s->selected >= 0 && clicked(l.scan_button) && !s->sweeping) {
        double centre = s->report_valid
                            ? s->report.centre_hz
                            : survey_bin_hz(s, s->peaks[s->selected].index);
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
        double centre = s->report_valid
                            ? s->report.centre_hz
                            : survey_bin_hz(s, s->peaks[s->selected].index);

        s->sweeping = 0;
        s->measuring = 0;
        if (app->receiver_mode &&
            retune_receiver(app, (uint32_t)llround(centre - SURVEY_OFFSET_HZ),
                            app->applied_ppm) < 0) {
            snprintf(s->status, sizeof(s->status),
                     "The receiver would not tune to %.4f MHz.", centre / 1e6);
            return;
        }
        /* Keep this tuning: leaving the survey normally puts back whatever it
           was before the sweep, which is the opposite of what was just asked
           for. */
        s->return_valid = 0;
        if (recreate_waterfall(app, app->plot, 1) < 0)
            return;
        app->view = VIEW_WATERFALL;
        return;
    }

    /* The handoff: point a decoder at what was found, which is an invitation
       to go and find out, not a claim about what it is. */
    if (s->selected >= 0 && s->report_valid && clicked(l.inspect_button)) {
        const struct band_plan_entry *entry =
            band_plan_lookup(s->report.centre_hz);
        enum band_plan_decoder decoder = entry ? entry->decoder
                                               : BAND_PLAN_NONE;

        if (decoder == BAND_PLAN_GSM) {
            int arfcn = gsm_arfcn_for_hz(s->report.centre_hz);
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
            int earfcn = lte_earfcn_for_hz(s->report.centre_hz);
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
            double hz = s->report.centre_hz;

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

static void draw_peak_list(const struct app *app, Rectangle rect) {
    const struct survey_view *s = &app->survey;
    char text[160];

    int visible = survey_visible_count(s);

    DrawRectangleRec(rect, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(rect, 1.0f, (Color){ 82, 109, 126, 255 });
    if (visible == s->peak_count)
        snprintf(text, sizeof(text), "Candidates (%d)", s->peak_count);
    else
        snprintf(text, sizeof(text), "Candidates (%d of %d, in view)", visible,
                 s->peak_count);
    DrawText(text, (int)rect.x + 12, (int)rect.y + 10, 16,
             (Color){ 151, 174, 188, 255 });
    /* A sweep that is mostly the receiver talking to itself should say so
       before anyone clicks into it. */
    int suspicious = survey_suspicious_now(app);
    if (suspicious > 0) {
        snprintf(text, sizeof(text), "%d marked *", suspicious);
        sdrgui_text_fit(text, (int)rect.x + 12 + MeasureText("Candidates (000)",
                                                             16) + 14,
                        (int)rect.y + 10, 16, rect.width - 190.0f,
                        (Color){ 250, 190, 74, 255 });
    }
    DrawText("   FREQUENCY       LEVEL    WIDTH   SHAPE      SEEN",
             (int)rect.x + 12, (int)rect.y + 30, 15,
             (Color){ 126, 151, 166, 255 });

    if (visible == 0) {
        DrawText(s->sweeping ? "sweeping..."
                             : s->peak_count > 0 ? "none in this window"
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
        double hz = survey_bin_hz(s, s->peaks[i].index);
        int suspect = survey_suspect_warns(survey_suspect_at(app, hz, 0.0));

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
                s->history_loaded
                    ? site_history_find(&s->history,
                                        carrier ? carrier->centre_hz : hz,
                                        s->plan.bin_hz > 0.0 ? s->plan.bin_hz
                                                             : 1e5)
                    : NULL;
            enum site_seen seen = s->history_loaded
                ? site_history_seen(&s->history, known, 1) : SITE_SEEN_UNKNOWN;
            char width[16];

            if (carrier && carrier->width_hz >= 1e6)
                snprintf(width, sizeof(width), "%.1fM", carrier->width_hz / 1e6);
            else if (carrier)
                snprintf(width, sizeof(width), "%.0fk", carrier->width_hz / 1e3);
            else
                snprintf(width, sizeof(width), "-");
            snprintf(text, sizeof(text),
                     "%s %10.4f MHz  %6.1f dBFS  %6s  %-9s  %s",
                     suspect ? "*" : " ", hz / 1e6,
                     (double)s->peaks[i].power_dbfs, width,
                     carrier ? survey_shape_name(
                                   survey_carrier_shape(carrier->width_hz))
                             : "-",
                     site_seen_name(seen));
        }
        if (suspect && i != s->selected && i != s->hover)
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

static void draw_detail(const struct app *app, const struct survey_layout *l) {
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

    double shown_hz = s->report_valid ? s->report.centre_hz
                                      : survey_bin_hz(s, s->peaks[s->selected].index);
    snprintf(text, sizeof(text), "Selected  %.4f MHz%s", shown_hz / 1e6,
             s->report_valid ? "  (measured)" : "");
    DrawText(text, (int)rect.x + 12, y, 18, (Color){ 235, 242, 246, 255 });
    y += 28;

    if (s->measuring) {
        DrawText("measuring...", (int)rect.x + 12, y, 17,
                 (Color){ 250, 190, 74, 255 });
        y += line;
    }
    if (s->report_valid) {
        snprintf(text, sizeof(text), "peak power         %.1f dBFS",
                 (double)s->report.peak_dbfs);
        DrawText(text, (int)rect.x + 12, y, 17, (Color){ 213, 226, 234, 255 });
        y += line;
        snprintf(text, sizeof(text), "above local floor  %.1f dB",
                 (double)s->report.prominence_db);
        DrawText(text, (int)rect.x + 12, y, 17, (Color){ 213, 226, 234, 255 });
        y += line;
        snprintf(text, sizeof(text), "occupied bandwidth %.1f kHz  (-%.0f dB)",
                 s->report.bandwidth_hz / 1e3,
                 (double)s->report.bandwidth_ref_db);
        DrawText(text, (int)rect.x + 12, y, 17, (Color){ 213, 226, 234, 255 });
        y += line;

        if (s->measure.blocks > 0) {
            double duty = survey_measure_duty(&s->measure);

            snprintf(text, sizeof(text), "duty               %s  (%d/%d blocks)",
                     survey_measure_duty_label(duty), s->measure.hits,
                     s->measure.blocks);
            DrawText(text, (int)rect.x + 12, y, 17,
                     (Color){ 213, 226, 234, 255 });
            y += line;
        }
        if (s->measure.hits > 1) {
            snprintf(text, sizeof(text), "stability          +/- %.1f kHz",
                     survey_measure_spread_hz(&s->measure) / 1e3);
            DrawText(text, (int)rect.x + 12, y, 17,
                     (Color){ 213, 226, 234, 255 });
            y += line;
        }
    } else if (!s->measuring) {
        DrawText("nothing measurable at that frequency now",
                 (int)rect.x + 12, y, 17, (Color){ 250, 190, 74, 255 });
        y += line;
    }

    /*
     * What the measurement suggests about the candidate itself, before the
     * band plan says what the frequency is allocated to. The order matters:
     * the allocation is what a reader believes by default, and a warning
     * printed after it reads as a footnote to it rather than a reason to
     * doubt it.
     */
    unsigned suspect = survey_suspect_at(app, shown_hz,
                                         s->report_valid
                                             ? s->report.bandwidth_hz
                                             : 0.0);
    if (survey_suspect_warns(suspect)) {
        y += 6;
        sdrgui_text_fit("this looks like the receiver, not the band",
                        (int)rect.x + 12, y, 17, rect.width - 24.0f,
                        (Color){ 250, 190, 74, 255 });
        y += line;
        sdrgui_text_fit(survey_suspect_reason(suspect), (int)rect.x + 24, y, 16,
                        rect.width - 36.0f, (Color){ 200, 165, 110, 255 });
        y += line - 2;
        if (suspect & SURVEY_SUSPECT_UNRESOLVED) {
            sdrgui_text_fit("and too narrow for this FFT to resolve: a bare "
                            "carrier",
                            (int)rect.x + 24, y, 16, rect.width - 36.0f,
                            (Color){ 200, 165, 110, 255 });
            y += line - 2;
        }
        /* Sufficient, not necessary: a spur the dongle radiates and hears
           back through its own antenna goes when the antenna does, and most
           of this comb behaves that way. What survives unplugging is
           certainly the receiver; what does not is not thereby a signal. */
        sdrgui_text_fit("unplug the antenna and sweep again: what stays is "
                        "the receiver",
                        (int)rect.x + 24, y, 15, rect.width - 36.0f,
                        (Color){ 126, 151, 166, 255 });
        y += line - 2;
    } else if (suspect & SURVEY_SUSPECT_UNRESOLVED) {
        /* Not a warning on its own -- plenty of real services are this narrow
           -- but worth saying that the width reported is the instrument's
           floor and not the signal's. */
        y += 6;
        sdrgui_text_fit("too narrow for this FFT to resolve: the width above "
                        "is its floor",
                        (int)rect.x + 12, y, 16, rect.width - 24.0f,
                        (Color){ 126, 151, 166, 255 });
        y += line - 2;
    }

    /* The band plan, and what it is not. */
    const struct band_plan_entry *entry = band_plan_lookup(shown_hz);
    y += 6;
    if (entry) {
        snprintf(text, sizeof(text), "band plan: %s", entry->name);
        sdrgui_text_fit(text, (int)rect.x + 12, y, 17, rect.width - 24.0f,
                        (Color){ 149, 205, 232, 255 });
        y += line;
        if (entry->note) {
            sdrgui_text_fit(entry->note, (int)rect.x + 24, y, 16,
                            rect.width - 36.0f, (Color){ 126, 151, 166, 255 });
            y += line - 2;
        }
        sdrgui_text_fit("a frequency lookup, not a detection",
                        (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        (Color){ 126, 151, 166, 255 });
        if (band_plan_can_inspect(entry->decoder) && s->report_valid)
            draw_button(l->inspect_button,
                        band_plan_inspect_label(entry->decoder), 1);
    } else {
        sdrgui_text_fit("band plan: nothing allocated here that this table knows",
                        (int)rect.x + 12, y, 16, rect.width - 24.0f,
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
    const struct survey_view *s = &app->survey;
    const Color fresh = { 120, 214, 140, 255 };
    const Color absent = { 190, 140, 120, 255 };
    float base = l->chart.y + l->chart.height - 34.0f;
    int i;

    if (!s->history_loaded || s->history.sweeps == 0)
        return;
    for (i = 0; i < s->carrier_count; i++) {
        float x;
        if (s->carrier_status[i] != SITE_STATUS_NEW)
            continue;
        x = sdrgui_survey_chart_x_at(l->chart, p, s->carriers[i].centre_hz);
        if (x != x)                       /* NaN: off screen */
            continue;
        DrawTriangle((Vector2){ x, base }, (Vector2){ x - 5.0f, base + 9.0f },
                     (Vector2){ x + 5.0f, base + 9.0f }, fresh);
    }
    for (i = 0; i < s->missing_count; i++) {
        float x = sdrgui_survey_chart_x_at(l->chart, p, s->missing[i]->hz);
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
    const struct survey_view *s = &app->survey;
    Vector2 mouse = GetMousePosition();
    char title[96], detail[160];
    const struct site_entry *entry = NULL;
    float width, height, x, y;
    double tolerance;

    if (s->site_menu_open || s->antenna_menu_open ||
        !CheckCollisionPointRec(mouse, l->chart))
        return;
    tolerance = s->plan.bin_hz > 0.0 ? s->plan.bin_hz : 1e5;
    if (s->hover >= 0 && s->hover < s->peak_count) {
        double peak_hz = survey_plan_bin_centre(&s->plan,
                                                s->peaks[s->hover].index);
        const struct survey_carrier *carrier = NULL;
        const struct band_plan_entry *band;
        double hz = peak_hz;
        int k;

        /* Which signal that maximum belongs to. The reader pointed at a bump;
           what they want to know about is the carrier it is part of. */
        for (k = 0; k < s->carrier_count; k++)
            if (peak_hz >= s->carriers[k].lower_hz &&
                peak_hz <= s->carriers[k].upper_hz) {
                carrier = &s->carriers[k];
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
                     hz / 1e6, (double)s->peaks[s->hover].power_dbfs,
                     band ? band->name : "no band plan entry");
        entry = s->history_loaded
                    ? site_history_find(&s->history, hz, tolerance) : NULL;
        if (!s->history_loaded || s->history.sweeps == 0)
            snprintf(detail, sizeof(detail),
                     "no history for this site yet -- save this sweep to start"
                     " one");
        else
            survey_history_line(&s->history, entry, detail, sizeof(detail));
    } else {
        /* Not over a candidate. It may still be over the mark left where
           something this site knows about has gone quiet. */
        int i, nearest = -1;
        float best = 7.0f;
        for (i = 0; i < s->missing_count; i++) {
            float mx = sdrgui_survey_chart_x_at(l->chart, p, s->missing[i]->hz);
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
        entry = s->missing[nearest];
        snprintf(title, sizeof(title), "%.3f MHz   not heard this sweep",
                 entry->hz / 1e6);
        survey_history_line(&s->history, entry, detail, sizeof(detail));
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
    draw_button(l.save_button, "Save survey", s->peak_count > 0 && s->site[0]);
    {
        int changes = 0, i;
        for (i = 0; i < s->carrier_count; i++)
            if (s->carrier_status[i] == SITE_STATUS_NEW)
                changes++;
        changes += s->missing_count;
        if (s->confirm.running)
            snprintf(text, sizeof(text), "Asking %d/%d",
                     s->confirm.index + 1, s->confirm.count);
        else if (changes > 0)
            snprintf(text, sizeof(text), "Ask again (%d)", changes);
        else
            snprintf(text, sizeof(text), "Ask again");
        draw_button(l.confirm_button, text,
                    s->confirm.running || (changes > 0 && app->receiver_mode));
        if (s->watching)
            snprintf(text, sizeof(text), "Watching %d", s->watch_sweeps);
        else
            snprintf(text, sizeof(text), "Watch");
        draw_button(l.watch_button, text, s->watching);
    }
    draw_button(l.sweep_button, s->sweeping ? "Sweeping" : "Sweep",
                !s->sweeping);
    draw_button(l.reset_button, "Reset zoom", 0);
    if (s->sweeping)
        draw_button(l.stop_button, "Stop", 0);

    if (s->sweeping) {
        snprintf(text, sizeof(text),
                 "step %d / %d   %.3f MHz   bin %.0f kHz   dwell %.2f s",
                 s->step + 1, s->step_count,
                 app->applied_frequency / 1e6,
                 survey_bin_width_hz(s) / 1e3, s->dwell_seconds);
        sdrgui_text_fit(text, (int)l.header_left, (int)l.status_y, 17,
                        l.header_right - l.header_left,
                        (Color){ 250, 190, 74, 255 });
    } else {
        sdrgui_text_fit(s->status, (int)l.header_left, (int)l.status_y, 17,
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
        l.chart, s->power, s->bins, SURVEY_SENTINEL_DBFS, shown_lower,
        shown_upper, window_lower, window_upper, bands, band_count,
        s->peaks, s->peak_count, survey_suspicious_now(app),
        s->selected, s->hover,
        s->sweeping ? (s->step * s->bins) / (s->step_count > 0 ? s->step_count : 1)
                    : s->bins,
        s->sweeping, s->drag_active, s->drag_from_hz, s->drag_to_hz,
        app->receiver_mode ? "press Sweep to survey the range"
                           : "a sweep needs a live receiver"
    };
    sdrgui_survey_chart(&params);
    survey_draw_history_marks(app, &l, &params);
    draw_peak_list(app, l.peak_list);
    draw_detail(app, &l);
    /* Last, so they sit over the chart and the panels rather than under. */
    survey_draw_pickers(app, &l);
    survey_draw_popup(app, &l, &params);
}
