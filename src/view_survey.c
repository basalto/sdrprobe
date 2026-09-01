#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "view.h"
#include "survey_layout.h"
#include "survey_window.h"
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
 * the code alone cannot (ADR-0011).
 */

int survey_editing(const struct app *app) {
    return app->tab == TAB_SCOPE && app->view == VIEW_SURVEY &&
           app->survey.focus >= 0;
}

static int survey_start(struct app *app);
static void survey_keep_current(struct survey_view *s);
static void survey_sweep_span(struct app *app, double from, double to);
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
    s->dwell_seconds = SURVEY_DWELL_DEFAULT;
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

void update_survey(struct app *app, double now) {
    struct survey_view *s = &app->survey;

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
            /* Back where the operator was, until they pick a candidate. */
            if (app->receiver_mode && s->return_valid)
                retune_receiver(app, s->return_frequency, app->applied_ppm);
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

    /* Typing into whichever range field has focus. The same spellings the
       Settings panel takes, because parse_frequency is the same parser. */
    if (s->focus >= 0 && IsKeyPressed(KEY_ESCAPE)) {
        s->focus = -1;
        return;
    }
    while (s->focus >= 0 && (character = GetCharPressed()) != 0) {
        int valid = (character >= '0' && character <= '9') ||
                    character == '.' || character == 'k' || character == 'K' ||
                    character == 'm' || character == 'M' || character == 'g' ||
                    character == 'G';
        char *text = s->focus == 0 ? s->from
                                   : s->focus == 1 ? s->to : s->dwell;
        int *length = s->focus == 0 ? &s->from_length
                                    : s->focus == 1 ? &s->to_length
                                                    : &s->dwell_length;
        int capacity = s->focus == 0 ? (int)sizeof(s->from)
                                     : s->focus == 1 ? (int)sizeof(s->to)
                                                     : (int)sizeof(s->dwell);
        if (valid && *length < capacity - 1) {
            text[(*length)++] = (char)character;
            text[*length] = '\0';
        }
    }
    if (s->focus >= 0 && IsKeyPressed(KEY_BACKSPACE)) {
        if (s->focus == 0 && s->from_length > 0)
            s->from[--s->from_length] = '\0';
        if (s->focus == 1 && s->to_length > 0)
            s->to[--s->to_length] = '\0';
        if (s->focus == 2 && s->dwell_length > 0)
            s->dwell[--s->dwell_length] = '\0';
    }
    if (clicked(l.from_field))
        s->focus = 0;
    if (clicked(l.to_field))
        s->focus = 1;
    if (clicked(l.dwell_field))
        s->focus = 2;
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
        survey_select(app, survey_nth_visible(s, (rank + 1) % visible));
        return;
    }
    if (visible > 0 &&
        (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))) {
        int rank = s->selected >= 0 ? survey_visible_rank(s, s->selected) : 0;
        if (rank <= 0)
            rank = visible;
        survey_select(app, survey_nth_visible(s, rank - 1));
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
        NULL, 0, s->peaks, s->peak_count, s->selected, -1,
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
                survey_select(app, s->hover);
            }
            return;
        }
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, l.peak_list)) {
        int row = (int)((mouse.y - (l.peak_list.y + 44.0f)) / 22.0f);
        int index = survey_nth_visible(s, row);
        if (row >= 0 && index >= 0)
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
        if (entry && entry->decoder == BAND_PLAN_GSM) {
            int arfcn = gsm_arfcn_for_hz(s->report.centre_hz);
            view_survey_leave(app);
            set_decode(app, DECODE_GSM);
            set_tab(app, TAB_DECODE);
            if (arfcn > 0)
                gsm_tune_selected(app, arfcn);
            app->gsm_analysis_mode = 1;
        } else if (entry && entry->decoder == BAND_PLAN_ADSB) {
            view_survey_leave(app);
            set_decode(app, DECODE_ADSB);
            set_tab(app, TAB_DECODE);
            retune_receiver(app, DEFAULT_FREQUENCY, app->applied_ppm);
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
    DrawText("FREQUENCY        LEVEL     ABOVE FLOOR", (int)rect.x + 12,
             (int)rect.y + 30, 15, (Color){ 126, 151, 166, 255 });

    if (visible == 0) {
        DrawText(s->sweeping ? "sweeping..."
                             : s->peak_count > 0 ? "none in this window"
                                                 : "nothing found yet",
                 (int)rect.x + 12, (int)rect.y + 56, 16,
                 (Color){ 150, 172, 188, 255 });
        return;
    }
    int rows = (int)((rect.height - 54.0f) / 22.0f);
    if (rows > visible)
        rows = visible;
    for (int row = 0; row < rows; row++) {
        int i = survey_nth_visible(s, row);
        float y = rect.y + 44.0f + (float)row * 22.0f;
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
        snprintf(text, sizeof(text), "%12.4f MHz  %6.1f dBFS  %5.1f dB",
                 survey_bin_hz(s, s->peaks[i].index) / 1e6,
                 (double)s->peaks[i].power_dbfs,
                 (double)s->peaks[i].prominence_db);
        sdrgui_text_fit(text, (int)rect.x + 12, (int)y, 17,
                        rect.width - 24.0f, color);
    }
    if (rows < visible) {
        snprintf(text, sizeof(text), "... %d more", visible - rows);
        DrawText(text, (int)rect.x + 12,
                 (int)(rect.y + rect.height - 20.0f), 15,
                 (Color){ 126, 151, 166, 255 });
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
        if (entry->decoder != BAND_PLAN_NONE && s->report_valid)
            draw_button(l->inspect_button,
                        entry->decoder == BAND_PLAN_GSM
                            ? "Inspect in Decode > GSM"
                            : "Inspect in Decode > ADS-B",
                        1);
    } else {
        sdrgui_text_fit("band plan: nothing allocated here that this table knows",
                        (int)rect.x + 12, y, 16, rect.width - 24.0f,
                        (Color){ 126, 151, 166, 255 });
    }
}

void draw_survey(struct app *app) {
    struct survey_view *s = &app->survey;
    struct survey_layout l = survey_layout_now();
    char text[240];

    survey_refresh_fields(s);

    DrawText("Range", (int)l.from_field.x, (int)l.from_field.y - 20, 16,
             (Color){ 157, 180, 194, 255 });
    DrawText("to", (int)l.to_field.x, (int)l.to_field.y - 20, 16,
             (Color){ 157, 180, 194, 255 });
    DrawText("dwell (s)", (int)l.dwell_field.x, (int)l.dwell_field.y - 20, 16,
             (Color){ 157, 180, 194, 255 });
    sdrgui_text_field(l.from_field, s->from, s->focus == 0);
    sdrgui_text_field(l.to_field, s->to, s->focus == 1);
    sdrgui_text_field(l.dwell_field, s->dwell, s->focus == 2);
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
        sdrgui_text_fit(text, (int)l.header_left, 168, 17,
                        l.header_right - l.header_left,
                        (Color){ 250, 190, 74, 255 });
    } else {
        sdrgui_text_fit(s->status, (int)l.header_left, 168, 17,
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
        s->peaks, s->peak_count, s->selected, s->hover,
        s->sweeping ? (s->step * s->bins) / (s->step_count > 0 ? s->step_count : 1)
                    : s->bins,
        s->sweeping, s->drag_active, s->drag_from_hz, s->drag_to_hz,
        app->receiver_mode ? "press Sweep to survey the range"
                           : "a sweep needs a live receiver"
    };
    sdrgui_survey_chart(&params);
    draw_peak_list(app, l.peak_list);
    draw_detail(app, &l);
}
