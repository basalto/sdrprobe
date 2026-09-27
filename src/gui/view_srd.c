#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "model/srd_view_model.h"
#include "runtime/app.h"
#include "runtime/debug_log.h"
#include "gui/sdrgui.h"
#include "gui/sdrgui_geometry.h"
#include "tech/srd_dsp.h"
#include "tech/srd_frame.h"
#include "gui/srd_layout.h"
#include "tech/srd_log.h"
#include "tech/srd_record.h"
#include "gui/view.h"

/* Whether the receiver is where the SRD band is now. Off 430-440 MHz nothing
   `update_srd()` finds can be a SRD remote control or a sensor -- see
   srd_receiver_ready() in srd_dsp.h for why both the frequency and the rate
   matter. */



/* Leave the SRD decode view: give back whatever tuning was borrowed on the
   way in. A no-op if entry never took hold (file playback, or a refused
   retune left the token unclaimed). */




/*
 * The frequency field's two directions: the receiver writes it, and the
 * reader reads it back.
 *
 * Kept in MHz to four decimals, which is 100 Hz -- the same resolution the
 * Scope's centre field uses, so the two say the same thing about the same
 * receiver.
 */
static void handle_log_click(struct app *app);
static void handle_marker_click(struct app *app);

/*
 * The screen's data, gathered once wherever it is needed.
 *
 * Everything this view *decides* -- which sentence a row carries, what a
 * marker is labelled, what the frame proves the device to be, whether the
 * receiver can expect to hear anything -- is `srd_view_model_build()`'s, so
 * this drawing and the browser's cannot come to different answers about the
 * same burst (`web-visualization/16`). What is left here is colours,
 * rectangles and fonts.
 */
static void srd_model_of(const struct app *app, struct srd_view_model *m) {
    srd_view_model_build(&app->srd, app->applied.frequency_hz,
                         app->applied.sample_rate_hz, app->receiver_mode, m);
}




void handle_srd_input(struct app *app) {
    struct srd_layout l = srd_layout_for((float)GetScreenWidth(),
                                         (float)GetScreenHeight());
    struct srd_view *s = &app->srd;
    int character;

    /*
     * Escape is one step out, not two: out of the field first if the
     * duration has focus, out of the tab otherwise -- the same shape as
     * handle_fm_input(), and for the same reason: leaving the tab straight
     * from a half-typed duration would discard it with no way back.
     */
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (s->freq_typing) {
            /* Abandoned rather than committed: Escape is how a reader takes
               back a half-typed frequency, so the field goes back to
               whatever the receiver is actually on. */
            s->freq_typing = 0;
            srd_freq_show(app);
        } else if (s->typing) {
            s->typing = 0;
        } else {
            set_tab(app, TAB_SCOPE, GetTime());
        }
        return;
    }

    /*
     * The frequency field takes the characters when it has focus. Both
     * fields on this screen are digits-and-a-dot, and whichever has focus
     * takes them -- there is deliberately no third state where a keystroke
     * belongs to neither.
     */
    while (s->freq_typing && (character = GetCharPressed()) != 0) {
        size_t length = strlen(s->freq_text);

        if (((character >= '0' && character <= '9') || character == '.') &&
            length + 1 < sizeof(s->freq_text)) {
            s->freq_text[length] = (char)character;
            s->freq_text[length + 1] = '\0';
        }
    }
    if (s->freq_typing && IsKeyPressed(KEY_BACKSPACE)) {
        size_t length = strlen(s->freq_text);

        if (length > 0)
            s->freq_text[length - 1] = '\0';
    }
    if (s->freq_typing &&
        (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))) {
        srd_freq_commit(app);
        return;
    }

    if (clicked(l.freq_down)) {
        srd_tune_arrow(app, -1);
        return;
    }
    if (clicked(l.freq_up)) {
        srd_tune_arrow(app, +1);
        return;
    }
    if (clicked(l.freq_field)) {
        s->freq_typing = 1;
        s->typing = 0;
        return;
    }

    while (s->typing && (character = GetCharPressed()) != 0) {
        if (((character >= '0' && character <= '9') || character == '.') &&
            s->record_seconds_length < (int)sizeof(s->record_seconds) - 1) {
            s->record_seconds[s->record_seconds_length++] = (char)character;
            s->record_seconds[s->record_seconds_length] = '\0';
        }
    }
    if (s->typing && IsKeyPressed(KEY_BACKSPACE) &&
        s->record_seconds_length > 0)
        s->record_seconds[--s->record_seconds_length] = '\0';

    if (clicked(l.record_seconds)) {
        s->typing = 1;
        s->freq_typing = 0;
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        s->typing = 0;
        /* Clicking away commits what was typed rather than discarding it,
           which is what a spreadsheet does and what scope_field_commit()
           already does for the Scope's three fields. */
        if (s->freq_typing)
            srd_freq_commit(app);
    }

    /* After the click-away, so a row clicked with a half-typed frequency
       still commits it first, and before the buttons, none of which share a
       rectangle with the log. */
    handle_log_click(app);
    /* And the same selection by pointing at the waterfall. Both are here, in
       the input phase, rather than one of them inside a draw. */
    handle_marker_click(app);

    if (clicked(l.record_button)) {
        double seconds;

        if (srd_record_seconds(s->record_seconds, &seconds) < 0) {
            snprintf(s->record_error, sizeof(s->record_error),
                     "duration must be %.1f-%.0f s",
                     SRD_RECORD_SECONDS_MIN, SRD_RECORD_SECONDS_MAX);
        } else {
            s->record_error[0] = '\0';
            start_capture_record(app, "srd", "srd", 0, 0.0, seconds);
        }
        return;
    }

    if (clicked(l.auto_save_button)) {
        s->auto_save_staging = !s->auto_save_staging;
        return;
    }

    if (clicked(l.view_toggle)) {
        s->analysis_mode = !s->analysis_mode;
        return;
    }

    if (!srd_tuned(app) && app->receiver_mode && l.retune_button.width > 0.0f &&
        clicked(l.retune_button)) {
        /* Frequency only, the way handle_adsb_input() retunes to 1090 MHz:
           the sample rate is not this view's to change, and the default is
           already 2 MS/s, which covers the whole 2 MHz band. */
        retune_receiver(app, SRD_CENTER_HZ, app->applied.ppm);
        return;
    }
}

static void draw_identity(const struct srd_view_model *m, Rectangle box) {
    struct panel_rows rows = panel_rows_for(box, SRD_PANEL_CAPTION_DROP,
                                            SRD_PANEL_ROW_HEIGHT, 0.0f,
                                            0.0f, 0.0f);
    char text[96];
    int y = (int)rows.first_y;
    int r = 0;

    DrawRectangleLinesEx(box, 1.0f, (Color){ 48, 66, 88, 255 });
    DrawText("Parameters", (int)box.x + 10, (int)box.y + 8, 14,
             (Color){ 150, 176, 202, 255 });

    if (!m->have_parameters) {
        sdrgui_text_fit("no transmissions heard yet", (int)box.x + 10, y, 14,
                        box.width - 20.0f, (Color){ 120, 140, 160, 255 });
        return;
    }

    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "modulation   OOK (ASK)");
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 226, 236, 245, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "line code    %s", m->line_code);
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 226, 236, 245, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "chip period  %.1f us (%.0f chip/s)",
                 m->last_chip_us, m->last_chip_rate_hz);
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 190, 210, 228, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "carrier      %+.1f kHz (%+.1f dB)",
                 m->last_carrier_offset_hz / 1e3, m->last_over_floor_db);
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 190, 210, 228, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "framing      6x750us sync, 80b/24b");
        sdrgui_text_fit(text, (int)box.x + 10, y, 14, box.width - 20.0f,
                        (Color){ 150, 176, 202, 255 });
    }
}

static void draw_log(const struct srd_view_model *m, int selected,
                     Rectangle box) {
    struct sdrgui_message_log_params params;
    static struct sdrgui_message_log_row rows[SRD_LOG_CAPACITY];
    static char at[SRD_LOG_CAPACITY][16];
    static char freq[SRD_LOG_CAPACITY][16];
    int i;

    for (i = 0; i < m->log_count; i++) {
        const struct srd_frame_view *v = &m->log[i];

        snprintf(at[i], sizeof(at[i]), "%6.1fs", v->entry.at);
        snprintf(freq[i], sizeof(freq[i]), "%.4f",
                 v->entry.absolute_hz / 1e6);

        rows[i].time = at[i];
        rows[i].freq = freq[i];
        rows[i].id = srd_frame_kind_name(v->entry.kind);
        rows[i].label = srd_modulation_name(v->entry.modulation);
        /*
         * Named only where the decoded frame's own structure identifies the
         * device -- `srd_device_type_of()` decides and the model asks it, so
         * this cannot come to a different answer from the browser's table.
         * The modulation and the chip period are in their own columns for a
         * reader who wants to go further than the evidence does.
         */
        rows[i].type = v->device;
        rows[i].detail = v->detail;
        rows[i].raw = v->hex;
        rows[i].highlight = (i == 0);
    }

    memset(&params, 0, sizeof(params));
    params.plot = box;
    params.rows = rows;
    params.count = m->log_count;
    params.caption = "Decoded SRD Frames";
    params.empty_notice = "no frames decoded yet (waiting for burst)";
    params.id_heading = "KIND";
    params.label_heading = "MOD";
    params.type_heading = "TYPE";
    params.freq_heading = "MHz";
    params.selected_row = selected;
    /*
     * Drawn, and nothing more. Selecting a row and tuning to it used to
     * happen here, which meant a `const struct app *` with the const cast
     * away -- the only cast of its kind in this file. It is
     * handle_log_click() below now, in the input phase, over
     * srd_log_row_intent().
     */
    sdrgui_message_log(&params);
}

/*
 * Clicking a log row: select it, and put the receiver where that burst was.
 *
 * The frequency in the row is the one useful thing a reader can act on, and
 * getting the receiver there is otherwise a typed number or a run of arrow
 * presses. What the click means is srd_log_row_intent()'s to say -- including
 * the three cases that select without tuning -- so this only finds the row and
 * obeys.
 */
static void handle_log_click(struct app *app) {
    struct srd_view *s = &app->srd;
    struct srd_layout l = srd_layout_for((float)GetScreenWidth(),
                                         (float)GetScreenHeight());
    Rectangle box = s->analysis_mode ? l.log_split : l.log_full;
    struct srd_row_intent intent;
    int row;

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;
    row = sdrgui_message_log_row_at(box, s->log_count, GetMousePosition());
    intent = srd_log_row_intent(row, s->log_count,
                                row >= 0 && row < s->log_count
                                    ? s->log[row].absolute_hz : 0.0,
                                app->receiver_mode);
    if (intent.action == SRD_ROW_NONE)
        return;
    s->selected_log = intent.row;
    if (intent.action == SRD_ROW_SELECT_AND_TUNE)
        retune_receiver(app, intent.tune_hz, app->applied.ppm);
}

/*
 * The markers this view puts on the waterfall, built once for whoever asks.
 *
 * Extracted from `draw_srd()` because the click that selects one now happens
 * in the input phase (ADR-0012: a draw may not decide), and the hit test has
 * to be laid out against the *same* markers the drawing laid out -- a second
 * construction here would be a second answer. `labels` is the caller's
 * storage because a marker points at its label rather than carrying it.
 */
static int srd_markers_build(const struct srd_view_model *m, int selected,
                             double now_sec,
                             struct sdrgui_waterfall_marker *markers,
                             int max) {
    int marker_count = 0;
    int k;

    for (k = 0; k < m->log_count && k < max; k++) {
        const struct srd_log_entry *e = &m->log[k].entry;

        /* Where it was, not where the receiver is now. */
        markers[k].frequency_hz = e->absolute_hz;
        markers[k].bandwidth_hz =
            (e->modulation == SRD_MOD_FSK2) ? 45000.0 : 25000.0;
        markers[k].age_seconds = now_sec - e->at;
        markers[k].duration_seconds = 0.025;
        markers[k].id = k;
        markers[k].highlighted = (k == selected);
        markers[k].color = (e->kind == SRD_FRAME_FSK_DETECTED)
                               ? (Color){ 120, 160, 200, 180 }
                               : (e->kind == SRD_FRAME_UNDECODED)
                                     ? (Color){ 250, 160, 80, 220 }
                                     : (e->modulation == SRD_MOD_FSK2)
                                           ? (Color){ 80, 220, 240, 220 }
                                           : (Color){ 100, 230, 150, 220 };
        /* The label is the model's, including the empty one an undecoded
           burst gets -- a marker with no label is a decision about what is
           worth saying, and it used to be four branches here with the
           2-FSK protocol's byte prefix written out in one of them. */
        markers[k].label = m->log[k].marker_label[0]
                               ? m->log[k].marker_label : NULL;
        marker_count++;
    }
    return marker_count;
}

/*
 * Clicking a marker on the waterfall selects the burst it stands for.
 *
 * The same selection the log rows offer, by pointing at where the burst was
 * heard instead of at a row -- and until this moved here it was done by
 * `sdrgui_waterfall()` writing an out-parameter from inside its draw loop.
 */
static void handle_marker_click(struct app *app) {
    struct srd_view *s = &app->srd;
    struct srd_layout l = srd_layout_for((float)GetScreenWidth(),
                                         (float)GetScreenHeight());
    struct sdrgui_waterfall_marker markers[SRD_LOG_CAPACITY];
    struct sdrgui_marker_layout layout[SRD_LOG_CAPACITY];
    struct srd_view_model m;
    int widths[SRD_LOG_CAPACITY];
    struct sdrgui_marker_axes axes;
    Vector2 mouse = GetMousePosition();
    int count, laid, i, hit;

    if (s->analysis_mode || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;
    srd_model_of(app, &m);
    count = srd_markers_build(&m, s->selected_log, GetTime(), markers,
                              SRD_LOG_CAPACITY);
    if (count <= 0)
        return;
    for (i = 0; i < count; i++)
        widths[i] = (markers[i].label && markers[i].label[0])
                        ? MeasureText(markers[i].label, 12) : 0;
    axes = waterfall_marker_axes(app, l.waterfall, &s->window);
    laid = sdrgui_waterfall_marker_layout(markers, count, widths, axes, layout,
                                          SRD_LOG_CAPACITY);
    hit = sdrgui_waterfall_marker_at(layout, laid, mouse.x, mouse.y);
    if (hit >= 0)
        s->selected_log = hit;
}

void draw_srd(struct app *app) {
    struct srd_view *s = &app->srd;
    struct srd_layout l = srd_layout_for((float)GetScreenWidth(),
                                         (float)GetScreenHeight());
    uint64_t record_bytes = 0;
    char record_path[ACQUISITION_PATH_MAX];
    int recording = acquisition_recording_status(&app->acq, &record_bytes,
                                                 record_path,
                                                 sizeof(record_path));
    struct srd_view_model m;
    char text[192];

    srd_model_of(app, &m);

    snprintf(text, sizeof(text),
             "SRD 430-440 MHz  OOK / 2-FSK / Manchester");
    sdrgui_text_fit(text, (int)l.header_left, 78, 20,
                    l.header_right - l.header_left,
                    (Color){ 226, 236, 245, 255 });

    /*
     * The tuning group. The field is rewritten from the receiver every frame
     * it is not being typed into, so it cannot drift from what the receiver
     * is on -- the same promise scope_header_sync() makes about the Scope's
     * three fields.
     */
    srd_freq_show(app);
    draw_button(l.freq_down, "<", 0);
    sdrgui_text_field(l.freq_field, s->freq_text, s->freq_typing);
    draw_button(l.freq_up, ">", 0);
    sdrgui_text_fit("MHz", (int)(l.freq_up.x + l.freq_up.width + 6.0f),
                    (int)(l.freq_up.y + 6.0f), 14, SRD_FREQ_UNIT_W - 6.0f,
                    (Color){ 126, 151, 166, 255 });

    if (s->record_error[0]) {
        sdrgui_text_fit(s->record_error, (int)l.header_second_left, 106, 16,
                        l.header_right - l.header_second_left,
                        (Color){ 235, 150, 120, 255 });
    } else if (!m.ready) {
        /* The button is a convenience and the field beside it is the
           affordance, so a window with no room for the button loses nothing
           that cannot be typed: srd_layout_for() gives it zero width rather
           than a clipped stub. */
        if (m.readiness == SRD_NOT_READY_RECEIVER &&
            l.retune_button.width > 0.0f) {
            draw_button(l.retune_button, "Retune to 434 MHz", 1);
        } else {
            sdrgui_text_fit(
                m.readiness == SRD_NOT_READY_RECEIVER
                    ? "Receiver is outside 430-440 MHz; type a frequency"
                    : "Capture is not 430-440 MHz / >=1 MS/s; no SRD signal expected",
                (int)l.header_second_left, 106, 16,
                l.header_right - l.header_second_left,
                (Color){ 250, 190, 74, 255 });
        }
    } else {
        snprintf(text, sizeof(text),
                 "%d transmission(s) found   %d frame(s) decoded   "
                 "last carrier %+.1f kHz",
                 m.transmissions, m.frames,
                 m.last_carrier_offset_hz / 1e3);
        sdrgui_text_fit(text, (int)l.header_second_left, 106, 16,
                        l.header_right - l.header_second_left,
                        (Color){ 150, 176, 202, 255 });
    }

    sdrgui_text_field(l.record_seconds, s->record_seconds, s->typing);
    draw_button(l.record_button, recording ? "Recording..." : "Record",
                recording);
    draw_button(l.auto_save_button,
                s->auto_save_staging ? "Auto-save: ON" : "Auto-save",
                s->auto_save_staging);
    draw_button(l.view_toggle,
                s->analysis_mode ? "Show log" : "Show charts", 0);

    if (!s->analysis_mode) {
        struct sdrgui_waterfall_marker markers[SRD_LOG_CAPACITY];
        int marker_count = srd_markers_build(&m, s->selected_log, GetTime(),
                                             markers, SRD_LOG_CAPACITY);

        draw_waterfall_rect_with_markers(app, 0, l.waterfall, &s->window,
                                         markers, marker_count, NULL);

        draw_log(&m, s->selected_log, l.log_full);
        return;
    }

    /* Envelope waveform */
    {
        float max_val = 0.0f;
        for (int i = 0; i < s->session.last_envelope_count; i++) {
            if (s->session.last_envelope[i] > max_val)
                max_val = s->session.last_envelope[i];
        }
        if (max_val <= 0.0f)
            max_val = 1.0f;

        struct sdrgui_burst_chart_params b;
        memset(&b, 0, sizeof(b));
        b.plot = l.envelope;
        b.data = s->session.last_envelope;
        b.count = s->session.last_envelope_count;
        b.type = SDRGUI_BURST_LINE;
        b.y_min = 0.0f;
        b.y_max = max_val * 1.05f;
        b.title = "Demodulated Envelope (Work Rate)";
        b.empty_notice = "no envelope data";
        sdrgui_burst_chart(&b);
    }

    /* Chip sequence */
    {
        float chip_float[512];
        for (int i = 0; i < s->session.last_chips_count && i < 512; i++)
            chip_float[i] = (float)s->session.last_chips[i];

        struct sdrgui_burst_chart_params b;
        memset(&b, 0, sizeof(b));
        b.plot = l.chips;
        b.data = chip_float;
        b.count = s->session.last_chips_count;
        b.type = SDRGUI_BURST_BAR;
        b.y_min = 0.0f;
        b.y_max = 1.0f;
        b.title = "Discretised Chips (Preamble, Delimiter, Data)";
        b.empty_notice = "no chips recovered";
        sdrgui_burst_chart(&b);
    }

    draw_identity(&m, l.identity);
    draw_log(&m, s->selected_log, l.log_split);
}

Rectangle srd_waterfall_rect(const struct app *app) {
    struct srd_layout l = srd_layout_for((float)GetScreenWidth(),
                                         (float)GetScreenHeight());

    if (app->srd.analysis_mode)
        return (Rectangle){ 0, 0, 0, 0 };
    return l.waterfall;
}
