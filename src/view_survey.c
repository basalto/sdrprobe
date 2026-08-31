#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "view.h"
#include "survey_layout.h"
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

/* The frequency at the middle of a survey bin. */
static double survey_bin_hz(const struct survey_view *s, int bin) {
    if (s->bins <= 0)
        return s->lower_hz;
    return s->lower_hz + (s->upper_hz - s->lower_hz) *
                             ((double)bin + 0.5) / (double)s->bins;
}

static double survey_bin_width_hz(const struct survey_view *s) {
    if (s->bins <= 0)
        return 0.0;
    return (s->upper_hz - s->lower_hz) / (double)s->bins;
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
    double span;
    double step_span;

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
    if (to_hz <= from_hz) {
        snprintf(s->status, sizeof(s->status),
                 "The high edge must be above the low one.");
        return -1;
    }

    span = (double)to_hz - (double)from_hz;
    step_span = (double)app->applied_sample_rate * SURVEY_USABLE_SPAN;
    if (step_span < 1.0) {
        snprintf(s->status, sizeof(s->status), "Sample rate is too low to sweep.");
        return -1;
    }

    s->lower_hz = (double)from_hz;
    s->upper_hz = (double)to_hz;
    /* Fine bins on a narrow range, as many as the array allows on a wide one. */
    s->bins = (int)(span / SURVEY_MIN_BIN_HZ);
    if (s->bins > SURVEY_BINS)
        s->bins = SURVEY_BINS;
    if (s->bins < 16)
        s->bins = 16;
    survey_clear(s);

    s->step_count = (int)ceil(span / step_span);
    if (s->step_count < 1)
        s->step_count = 1;
    s->step = 0;
    s->step_folded = 0;
    s->sweeping = 1;
    view_survey_enter(app);

    double first = s->lower_hz + step_span / 2.0;
    if (retune_receiver(app, (uint32_t)llround(first), app->applied_ppm) < 0) {
        s->sweeping = 0;
        snprintf(s->status, sizeof(s->status),
                 "The receiver would not tune to %.3f MHz.", first / 1e6);
        return -1;
    }
    s->step_started_at = GetTime();
    snprintf(s->status, sizeof(s->status), "Sweeping %.3f - %.3f MHz",
             s->lower_hz / 1e6, s->upper_hz / 1e6);
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
    double usable = rate * SURVEY_USABLE_SPAN / 2.0;

    for (int i = 0; i < SDR_DSP_FFT_SIZE; i++) {
        double hz = lower + ((double)i + 0.5) * bin_hz;
        if (fabs(hz - centre) > usable)
            continue;
        if (hz < s->lower_hz || hz >= s->upper_hz)
            continue;
        int bin = (int)((hz - s->lower_hz) / (s->upper_hz - s->lower_hz) *
                        (double)s->bins);
        if (bin < 0 || bin >= s->bins)
            continue;
        float power = app->spectrum_average[i];
        /* Peak-hold within the bin: a survey bin is usually wider than an FFT
           bin, and a narrow carrier averaged with the noise beside it is a
           carrier the survey would miss. */
        if (s->power[bin] <= SURVEY_SENTINEL_DBFS || power > s->power[bin])
            s->power[bin] = power;
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
    s->measure_blocks = 0;
    s->measure_hits = 0;
    s->measure_centre_sum = 0.0;
    s->measure_centre_square_sum = 0.0;
    s->measure_first_prominence = 0.0f;
    hz = survey_bin_hz(s, s->peaks[index].index);
    s->measure_expected_hz = hz;

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

    if (!sdr_dsp_characterise_carrier(app->spectrum_average, SDR_DSP_FFT_SIZE,
                                      (double)app->applied_frequency,
                                      (double)app->applied_sample_rate,
                                      s->measure_expected_hz, 200000.0,
                                      SURVEY_BANDWIDTH_DB,
                                      app->magnitude_sorted, &report)) {
        s->measure_blocks++;
        return;
    }
    s->measure_blocks++;
    if (s->measure_first_prominence <= 0.0f)
        s->measure_first_prominence = report.prominence_db;
    /* "Up" means at least half the prominence it was first seen with: a
       bursty transmitter is present in some blocks and absent in others, and
       that fraction is the duty this view reports. */
    if (report.prominence_db >= s->measure_first_prominence / 2.0f) {
        s->measure_hits++;
        s->measure_centre_sum += report.centre_hz;
        s->measure_centre_square_sum += report.centre_hz * report.centre_hz;
        s->report = report;
        s->report_valid = 1;
    }
}

void update_survey(struct app *app, double now) {
    struct survey_view *s = &app->survey;

    if (s->sweeping) {
        double elapsed = now - s->step_started_at;
        double step_span = (double)app->applied_sample_rate * SURVEY_USABLE_SPAN;

        if (elapsed < SURVEY_SETTLE_SECONDS)
            return;
        if (!s->step_folded) {
            survey_fold_block(app);
            s->step_folded = 1;
            survey_find_peaks(app);
            return;
        }
        s->step++;
        if (s->step >= s->step_count) {
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
        double next = s->lower_hz + ((double)s->step + 0.5) * step_span;
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
                     s->measure_expected_hz / 1e6, s->measure_blocks);
        }
    }
}

void handle_survey_input(struct app *app) {
    struct survey_view *s = &app->survey;
    struct survey_layout l = survey_layout_now();
    int character;

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
        char *text = s->focus == 0 ? s->from : s->to;
        int *length = s->focus == 0 ? &s->from_length : &s->to_length;
        int capacity = s->focus == 0 ? (int)sizeof(s->from)
                                     : (int)sizeof(s->to);
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
    }
    if (clicked(l.from_field))
        s->focus = 0;
    if (clicked(l.to_field))
        s->focus = 1;
    if (clicked(l.sweep_button) || IsKeyPressed(KEY_ENTER)) {
        s->focus = -1;
        survey_start(app);
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

    /* Up and Down walk the candidate list. The scale keys mean nothing in
       this view, and a list you can step through is how you compare two
       carriers without hunting for them with the pointer. */
    if (s->peak_count > 0 &&
        (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))) {
        int next = s->selected + 1;
        if (next >= s->peak_count)
            next = 0;
        survey_select(app, next);
        return;
    }
    if (s->peak_count > 0 &&
        (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))) {
        int previous = s->selected <= 0 ? s->peak_count - 1 : s->selected - 1;
        survey_select(app, previous);
        return;
    }

    /* Hover and selection, in the chart and in the list. */
    struct sdrgui_survey_params params = {
        l.chart, s->power, s->bins, SURVEY_SENTINEL_DBFS, s->lower_hz,
        s->upper_hz, s->peaks, s->peak_count, s->selected, -1,
        s->sweeping ? (s->step * s->bins) / (s->step_count > 0 ? s->step_count : 1)
                    : s->bins,
        s->sweeping, ""
    };
    s->hover = sdrgui_survey_chart_peak_at(l.chart, &params, GetMousePosition());
    if (s->hover >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        survey_select(app, s->hover);
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), l.peak_list)) {
        int row = (int)((GetMousePosition().y - (l.peak_list.y + 44.0f)) / 22.0f);
        if (row >= 0 && row < s->peak_count)
            survey_select(app, row);
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

    DrawRectangleRec(rect, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(rect, 1.0f, (Color){ 82, 109, 126, 255 });
    snprintf(text, sizeof(text), "Candidates (%d)", s->peak_count);
    DrawText(text, (int)rect.x + 12, (int)rect.y + 10, 16,
             (Color){ 151, 174, 188, 255 });
    DrawText("FREQUENCY        LEVEL     ABOVE FLOOR", (int)rect.x + 12,
             (int)rect.y + 30, 15, (Color){ 126, 151, 166, 255 });

    if (s->peak_count == 0) {
        DrawText(s->sweeping ? "sweeping..." : "nothing found yet",
                 (int)rect.x + 12, (int)rect.y + 56, 16,
                 (Color){ 150, 172, 188, 255 });
        return;
    }
    int rows = (int)((rect.height - 54.0f) / 22.0f);
    if (rows > s->peak_count)
        rows = s->peak_count;
    for (int i = 0; i < rows; i++) {
        float y = rect.y + 44.0f + (float)i * 22.0f;
        Color color = (Color){ 213, 226, 234, 255 };
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
    if (rows < s->peak_count) {
        snprintf(text, sizeof(text), "... %d more", s->peak_count - rows);
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

        if (s->measure_blocks > 0) {
            double fraction = (double)s->measure_hits /
                              (double)s->measure_blocks;
            const char *duty = fraction > 0.9 ? "continuous"
                             : fraction > 0.3 ? "intermittent"
                                              : "bursty";
            snprintf(text, sizeof(text), "duty               %s  (%d/%d blocks)",
                     duty, s->measure_hits, s->measure_blocks);
            DrawText(text, (int)rect.x + 12, y, 17,
                     (Color){ 213, 226, 234, 255 });
            y += line;
        }
        if (s->measure_hits > 1) {
            double mean = s->measure_centre_sum / (double)s->measure_hits;
            double variance = s->measure_centre_square_sum /
                                  (double)s->measure_hits - mean * mean;
            if (variance < 0.0)
                variance = 0.0;
            snprintf(text, sizeof(text), "stability          +/- %.1f kHz",
                     sqrt(variance) / 1e3);
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

    DrawText("Range", (int)l.from_field.x, (int)l.from_field.y - 20, 16,
             (Color){ 157, 180, 194, 255 });
    DrawText("to", (int)l.to_field.x, (int)l.to_field.y - 20, 16,
             (Color){ 157, 180, 194, 255 });
    sdrgui_text_field(l.from_field, s->from, s->focus == 0);
    sdrgui_text_field(l.to_field, s->to, s->focus == 1);
    draw_button(l.sweep_button, s->sweeping ? "Sweeping" : "Sweep",
                !s->sweeping);
    if (s->sweeping)
        draw_button(l.stop_button, "Stop", 0);

    if (s->sweeping) {
        snprintf(text, sizeof(text),
                 "step %d / %d   %.3f MHz   bin %.0f kHz",
                 s->step + 1, s->step_count,
                 app->applied_frequency / 1e6,
                 survey_bin_width_hz(s) / 1e3);
        sdrgui_text_fit(text, (int)l.header_left, 168, 17,
                        l.header_right - l.header_left,
                        (Color){ 250, 190, 74, 255 });
    } else {
        sdrgui_text_fit(s->status, (int)l.header_left, 168, 17,
                        l.header_right - l.header_left,
                        (Color){ 190, 208, 218, 255 });
    }

    /* Before the first sweep the chart has no range of its own, so it shows
       the one in the fields: an axis labelled with the range about to be swept
       is worth more than an axis labelled zero five times. */
    double shown_lower = s->lower_hz;
    double shown_upper = s->upper_hz;
    if (s->bins <= 0 || shown_upper <= shown_lower) {
        uint32_t from_hz;
        uint32_t to_hz;
        if (parse_frequency(s->from, &from_hz) == 0 &&
            parse_frequency(s->to, &to_hz) == 0 && to_hz > from_hz) {
            shown_lower = (double)from_hz;
            shown_upper = (double)to_hz;
        }
    }
    struct sdrgui_survey_params params = {
        l.chart, s->power, s->bins, SURVEY_SENTINEL_DBFS, shown_lower,
        shown_upper, s->peaks, s->peak_count, s->selected, s->hover,
        s->sweeping ? (s->step * s->bins) / (s->step_count > 0 ? s->step_count : 1)
                    : s->bins,
        s->sweeping,
        app->receiver_mode ? "press Sweep to survey the range"
                           : "a sweep needs a live receiver"
    };
    sdrgui_survey_chart(&params);
    draw_peak_list(app, l.peak_list);
    draw_detail(app, &l);
}
