#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "debug_log.h"
#include "sdrgui.h"
#include "sdrgui_geometry.h"
#include "srd_dsp.h"
#include "srd_frame.h"
#include "srd_layout.h"
#include "srd_log.h"
#include "srd_record.h"
#include "view.h"

/* Whether the receiver is where the SRD band is now. Off 430-440 MHz nothing
   `update_srd()` finds can be a SRD remote control or a sensor -- see
   srd_receiver_ready() in srd_dsp.h for why both the frequency and the rate
   matter. */
int srd_tuned(const struct app *app) {
    return srd_receiver_ready(app->applied.frequency_hz,
                              app->applied.sample_rate_hz);
}

void view_srd_defaults(struct app *app) {
    if (!app)
        return;
    memset(&app->srd, 0, sizeof(app->srd));
    app->srd.polarity = SRD_MANCHESTER_THOMAS;
    app->srd.selected_log = -1;
    snprintf(app->srd.record_seconds, sizeof(app->srd.record_seconds), "%.0f",
             SRD_RECORD_SECONDS_DEFAULT);
    app->srd.record_seconds_length = (int)strlen(app->srd.record_seconds);
    srd_session_reset(&app->srd.session);
}

/*
 * Entering the SRD view retunes the receiver to 434 MHz, the same shape as
 * enter_gsm()/enter_lte(): a "Retune to 434 MHz" button asks an operator to
 * land a click on it, and the reason this view exists is to be listening at
 * the SRD band the moment somebody presses a SRD remote control, not after they notice
 * the header still reads the previous band and reach for the mouse. Only
 * when it is not already there -- arriving with the receiver already parked
 * in the band (a capture, or a previous session) must not retune it away
 * from wherever it already was set, the same guard park_in_band() makes for
 * LTE.
 *
 * The manual button stays for the cases this cannot reach: file playback,
 * where there is one tuning and nothing to move (srd_tuned() then reports
 * the mismatch as informational text instead), and a retune that fails --
 * the button offers a retry with app->receiver_error already in place.
 */
void enter_srd(struct app *app) {
    if (!app->receiver_mode)
        return;
    if (srd_tuned(app)) {
        receiver_borrow(app, &app->srd.lease_token);
        return;
    }
    receiver_borrow_at(app, &app->srd.lease_token, SRD_CENTER_HZ, 0);
}

/* Leave the SRD decode view: give back whatever tuning was borrowed on the
   way in. A no-op if entry never took hold (file playback, or a refused
   retune left the token unclaimed). */
void leave_srd(struct app *app) {
    receiver_return(app, &app->srd.lease_token);
}

static void remember_frame(struct app *app, double now, const struct srd_frame *f,
                           double carrier_hz, double chip_us) {
    struct srd_view *s = &app->srd;

    for (int i = SRD_LOG_CAPACITY - 1; i > 0; i--)
        s->log[i] = s->log[i - 1];

    s->log[0].at = now;
    s->log[0].kind = f->kind;
    s->log[0].modulation = f->modulation;
    s->log[0].byte_count = f->byte_count;
    s->log[0].bit_count = f->bit_count;
    s->log[0].carrier_hz = carrier_hz;
    s->log[0].absolute_hz = srd_log_absolute_hz(app->applied.frequency_hz,
                                                carrier_hz);
    s->log[0].chip_us = chip_us;
    s->log[0].error_count = f->error_count;
    size_t to_copy = f->byte_count;
    if (to_copy > sizeof(s->log[0].bytes))
        to_copy = sizeof(s->log[0].bytes);
    memcpy(s->log[0].bytes, f->bytes, to_copy);

    if (s->log_count < SRD_LOG_CAPACITY)
        s->log_count++;

    debug_log_write("srd", "%s %s %zu bytes, %.1f us",
                    f->modulation == SRD_MOD_FSK2 ? "2FSK" : "OOK",
                    f->kind == SRD_FRAME_FULL ? "FULL" :
                    f->kind == SRD_FRAME_REPEAT ? "REPEAT" : "GENERIC",
                    f->byte_count, chip_us);
}

static void remember_undecoded_detection(struct app *app, double now,
                                         double carrier_hz, double chip_us,
                                         size_t chip_count, const uint8_t *raw_chips,
                                         enum srd_modulation mod,
                                         enum srd_frame_kind kind) {
    struct srd_view *s = &app->srd;

    for (int i = SRD_LOG_CAPACITY - 1; i > 0; i--)
        s->log[i] = s->log[i - 1];

    s->log[0].at = now;
    /* The session decided this, so the headless report cannot disagree. */
    s->log[0].kind = kind;
    s->log[0].modulation = mod;
    s->log[0].byte_count = 0;
    s->log[0].bit_count = chip_count;
    s->log[0].carrier_hz = carrier_hz;
    s->log[0].absolute_hz = srd_log_absolute_hz(app->applied.frequency_hz,
                                                carrier_hz);
    s->log[0].chip_us = chip_us;
    s->log[0].error_count = 0;
    memset(s->log[0].bytes, 0, sizeof(s->log[0].bytes));

    if (raw_chips && chip_count > 0) {
        size_t max_chips = chip_count > 256 ? 256 : chip_count;
        s->log[0].byte_count = srd_pack_bits(raw_chips, max_chips,
                                            s->log[0].bytes,
                                            sizeof(s->log[0].bytes));
    }

    if (s->log_count < SRD_LOG_CAPACITY)
        s->log_count++;

    debug_log_write("srd-undecoded", "%s burst %zu chips (%.1f us)",
                    mod == SRD_MOD_FSK2 ? "2FSK" : "OOK", chip_count, chip_us);
}

void update_srd(struct app *app, double now) {
    struct srd_view *s = &app->srd;
    struct srd_session_event event;
    double sample_rate = (double)app->applied.sample_rate_hz;
    float full_scale = app->device.full_scale > 0.0f ? app->device.full_scale : 127.5f;

    srd_session_feed(&s->session, app->frame.i_samples, app->frame.q_samples,
                     app->frame.pair_count, sample_rate, full_scale,
                     s->polarity, now, &event);

    for (int i = 0; i < event.frame_count; i++) {
        struct srd_session_frame_event *fe = &event.frames[i];
        remember_frame(app, fe->at, &fe->frame, fe->carrier_hz, fe->chip_us);
    }

    for (int i = 0; i < event.undecoded_count; i++) {
        struct srd_session_undecoded_event *ue = &event.undecoded[i];
        remember_undecoded_detection(app, ue->at, ue->carrier_hz, ue->chip_us,
                                     ue->chip_count, ue->raw_chips,
                                     ue->modulation, ue->kind);
        if (s->auto_save_staging && (now - s->last_auto_save_time > 1.0)) {
            mkdir("captures", 0755);
            mkdir("captures/staging", 0755);
            time_t raw_t = time(NULL);
            struct tm local_t;
            localtime_r(&raw_t, &local_t);
            char stamp[32];
            strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local_t);
            char staging_path[256];
            snprintf(staging_path, sizeof(staging_path),
                     "captures/staging/undecoded_%s.bin", stamp);
            double age = (double)(app->frame.pair_count - ue->offset_pairs) / sample_rate;
            if (age < 0.0)
                age = 0.0;
            double dur = (double)ue->pair_count / sample_rate + 0.100;
            iq_ring_save_slice(&app->acq.ring, age, dur, staging_path, "srd_undecoded");
            s->last_auto_save_time = now;
        }
    }
}

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

static void srd_freq_show(struct app *app) {
    struct srd_view *s = &app->srd;

    if (s->freq_typing)
        return;
    snprintf(s->freq_text, sizeof(s->freq_text), "%.4f",
             (double)app->applied.frequency_hz / 1e6);
}

/* Returns 1 when the receiver moved. */
static int srd_freq_commit(struct app *app) {
    struct srd_view *s = &app->srd;
    double mhz = atof(s->freq_text);
    int moved = 0;

    s->freq_typing = 0;
    if (mhz > 0.0 && app->receiver_mode) {
        double hz = mhz * 1e6;

        /*
         * Typed, so it is not clamped to the allocation the way an arrow
         * press is -- a reader who types 868.2 has said something
         * deliberate, and the view's own status line already says when the
         * receiver is somewhere this decoder cannot help with. What it is
         * clamped to is what the tuner can reach, which retune_receiver()
         * reports on rather than guessing about.
         */
        if (hz > 0.0 && hz < 4e9 &&
            retune_receiver(app, (uint32_t)llround(hz), app->applied.ppm) == 0)
            moved = 1;
    }
    srd_freq_show(app);
    return moved;
}

static void srd_tune_arrow(struct app *app, int direction) {
    uint32_t target;

    if (!app->receiver_mode)
        return;
    target = srd_tune_step(app->applied.frequency_hz,
                           app->applied.sample_rate_hz, direction);
    if (target != app->applied.frequency_hz)
        retune_receiver(app, target, app->applied.ppm);
    srd_freq_show(app);
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
            set_tab(app, TAB_SCOPE);
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

static void draw_identity(const struct app *app, Rectangle box) {
    const struct srd_view *s = &app->srd;
    struct panel_rows rows = panel_rows_for(box, SRD_PANEL_CAPTION_DROP,
                                            SRD_PANEL_ROW_HEIGHT, 0.0f,
                                            0.0f, 0.0f);
    char text[96];
    int y = (int)rows.first_y;
    int r = 0;

    DrawRectangleLinesEx(box, 1.0f, (Color){ 48, 66, 88, 255 });
    DrawText("Parameters", (int)box.x + 10, (int)box.y + 8, 14,
             (Color){ 150, 176, 202, 255 });

    if (s->log_count == 0 && s->session.last_chip_us == 0.0) {
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
        snprintf(text, sizeof(text), "line code    Manchester (%s)",
                 s->polarity == SRD_MANCHESTER_THOMAS ? "Thomas" : "IEEE");
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 226, 236, 245, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "chip period  %.1f us (%.0f chip/s)",
                 s->session.last_chip_us, s->session.last_chip_us > 0.0 ? 1e6 / s->session.last_chip_us : 0.0);
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 190, 210, 228, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "carrier      %+.1f kHz (%+.1f dB)",
                 s->session.last_carrier_hz / 1e3, s->session.last_over_floor_db);
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

static void draw_log(const struct app *app, Rectangle box) {
    const struct srd_view *s = &app->srd;
    struct sdrgui_message_log_params params;
    static struct sdrgui_message_log_row rows[SRD_LOG_CAPACITY];
    static char at[SRD_LOG_CAPACITY][16];
    static char freq[SRD_LOG_CAPACITY][16];
    static char type[SRD_LOG_CAPACITY][16];
    static char detail[SRD_LOG_CAPACITY][64];
    static char raw[SRD_LOG_CAPACITY][64];
    int i;

    for (i = 0; i < s->log_count; i++) {
        snprintf(at[i], sizeof(at[i]), "%6.1fs", s->log[i].at);
        snprintf(type[i], sizeof(type[i]), "%s",
                 s->log[i].kind == SRD_FRAME_FULL ? "FULL" :
                 s->log[i].kind == SRD_FRAME_REPEAT ? "REPEAT" :
                 s->log[i].kind == SRD_FRAME_GENERIC ? "GENERIC" :
                 s->log[i].kind == SRD_FRAME_FSK_DETECTED ? "WAKEUP" : "UNDECODED");

        if (s->log[i].kind == SRD_FRAME_FULL && s->log[i].byte_count >= 10) {
            snprintf(detail[i], sizeof(detail[i]),
                     "hdr %02X  tag %02X  trailer %02X",
                     s->log[i].bytes[0], s->log[i].bytes[1], s->log[i].bytes[9]);
        } else if (s->log[i].kind == SRD_FRAME_REPEAT && s->log[i].byte_count >= 3) {
            snprintf(detail[i], sizeof(detail[i]),
                     "hdr %02X  tag %02X  trailer %02X",
                     s->log[i].bytes[0], s->log[i].bytes[1], s->log[i].bytes[2]);
        } else if (s->log[i].kind == SRD_FRAME_FSK_DETECTED) {
            if (s->log[i].chip_us > 0.0 && s->log[i].bit_count > 0) {
                snprintf(detail[i], sizeof(detail[i]),
                         "preamble %zu chips (%.0fus, %.1f kbd)",
                         s->log[i].bit_count, s->log[i].chip_us,
                         1e3 / s->log[i].chip_us);
            } else {
                snprintf(detail[i], sizeof(detail[i]), "preamble / wakeup burst");
            }
        } else if (s->log[i].kind == SRD_FRAME_UNDECODED) {
            snprintf(detail[i], sizeof(detail[i]), "detected burst (no frame)");
        } else if (srd_device_type_of(s->log[i].kind, s->log[i].bytes,
                                          s->log[i].byte_count) ==
                       SRD_DEVICE_REMOTE_FSK) {
            /* The prefix test used to be spelled out here as well as in
               srd_frame.c, which is two places to disagree about what the
               protocol is. */
            uint32_t id = ((uint32_t)s->log[i].bytes[4] << 24) |
                          ((uint32_t)s->log[i].bytes[5] << 16) |
                          ((uint32_t)s->log[i].bytes[6] << 8) |
                          (uint32_t)s->log[i].bytes[7];
            uint16_t seq = ((uint16_t)s->log[i].bytes[8] << 8) | s->log[i].bytes[9];
            uint8_t flags = s->log[i].bytes[3];
            snprintf(detail[i], sizeof(detail[i]),
                     "id %08X  seq %04X  flg %02X", id, seq, flags);
        } else if (s->log[i].byte_count >= 8) {
            uint32_t id = ((uint32_t)s->log[i].bytes[0] << 24) |
                          ((uint32_t)s->log[i].bytes[1] << 16) |
                          ((uint32_t)s->log[i].bytes[2] << 8) |
                          (uint32_t)s->log[i].bytes[3];
            snprintf(detail[i], sizeof(detail[i]),
                     "id %08X  %zu bytes", id, s->log[i].byte_count);
        } else {
            snprintf(detail[i], sizeof(detail[i]), "%zu bytes (%zu bits)",
                     s->log[i].byte_count,
                     s->log[i].bit_count ? s->log[i].bit_count : s->log[i].byte_count * 8);
        }

        char hex_buf[64] = {0};
        for (size_t b = 0; b < s->log[i].byte_count && b < 16; b++) {
            char byte_str[8];
            snprintf(byte_str, sizeof(byte_str), "%02X ", s->log[i].bytes[b]);
            strncat(hex_buf, byte_str, sizeof(hex_buf) - strlen(hex_buf) - 1);
        }
        snprintf(raw[i], sizeof(raw[i]), "%s", hex_buf);

        snprintf(freq[i], sizeof(freq[i]), "%.4f",
                 s->log[i].absolute_hz / 1e6);

        rows[i].time = at[i];
        rows[i].freq = freq[i];
        rows[i].id = type[i];
        rows[i].label = (s->log[i].modulation == SRD_MOD_FSK2) ? "2FSK" : "OOK";
        /*
         * Named only where the decoded frame's own structure identifies the
         * device -- srd_frame_device_type() decides, and everything else
         * reads "unknown". The modulation and the chip period are in their
         * own columns for a reader who wants to go further than the evidence
         * does.
         */
        rows[i].type = srd_device_type_name(
            srd_device_type_of(s->log[i].kind, s->log[i].bytes,
                               s->log[i].byte_count));
        rows[i].detail = detail[i];
        rows[i].raw = raw[i];
        rows[i].highlight = (i == 0);
    }

    memset(&params, 0, sizeof(params));
    params.plot = box;
    params.rows = rows;
    params.count = s->log_count;
    params.caption = "Decoded SRD Frames";
    params.empty_notice = "no frames decoded yet (waiting for burst)";
    params.id_heading = "KIND";
    params.label_heading = "MOD";
    params.type_heading = "TYPE";
    params.freq_heading = "MHz";
    params.selected_row = s->selected_log;
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
static int srd_markers_build(const struct app *app,
                             struct sdrgui_waterfall_marker *markers,
                             char labels[][32], int max) {
    const struct srd_view *s = &app->srd;
    int marker_count = 0;
    double now_sec = GetTime();

    for (int k = 0; k < s->log_count && k < max; k++) {
            /* Where it was, not where the receiver is now. */
            markers[k].frequency_hz = s->log[k].absolute_hz;
            markers[k].bandwidth_hz = (s->log[k].modulation == SRD_MOD_FSK2) ? 45000.0 : 25000.0;
            markers[k].age_seconds = now_sec - s->log[k].at;
            markers[k].duration_seconds = 0.025;
            markers[k].id = k;
            markers[k].highlighted = (k == s->selected_log);
            markers[k].color = (s->log[k].kind == SRD_FRAME_FSK_DETECTED)
                                   ? (Color){ 120, 160, 200, 180 }
                                   : (s->log[k].kind == SRD_FRAME_UNDECODED)
                                         ? (Color){ 250, 160, 80, 220 }
                                         : (s->log[k].modulation == SRD_MOD_FSK2)
                                               ? (Color){ 80, 220, 240, 220 }
                                               : (Color){ 100, 230, 150, 220 };
            if (s->log[k].kind == SRD_FRAME_FSK_DETECTED) {
                snprintf(labels[k], sizeof(labels[k]), "WAKEUP");
            } else if (s->log[k].kind == SRD_FRAME_UNDECODED) {
                /*
                 * No label. A detected burst with no frame is the commonest
                 * thing on this band by a wide margin -- one live sweep put
                 * 35 of them on screen at once -- and every one of them said
                 * the same word. The brackets already say a burst was there
                 * and how wide it was; the word added nothing and buried the
                 * markers that carry a sequence number or a decode.
                 */
                labels[k][0] = '\0';
            } else if (s->log[k].byte_count >= 14 &&
                       ((s->log[k].bytes[0] == 0x27 && s->log[k].bytes[1] == 0xE5) ||
                        (s->log[k].bytes[0] == 0xD8 && s->log[k].bytes[1] == 0x1A))) {
                uint16_t seq = ((uint16_t)s->log[k].bytes[8] << 8) | s->log[k].bytes[9];
                snprintf(labels[k], sizeof(labels[k]), "seq %04X", seq);
            } else {
                snprintf(labels[k], sizeof(labels[k]), "%s",
                         s->log[k].kind == SRD_FRAME_FULL ? "FULL" :
                         s->log[k].kind == SRD_FRAME_REPEAT ? "REPEAT" : "GENERIC");
            }
            markers[k].label = labels[k][0] ? labels[k] : NULL;
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
    static char labels[SRD_LOG_CAPACITY][32];
    struct sdrgui_marker_layout layout[SRD_LOG_CAPACITY];
    int widths[SRD_LOG_CAPACITY];
    struct sdrgui_marker_axes axes;
    Vector2 mouse = GetMousePosition();
    int count, laid, i, hit;

    if (s->analysis_mode || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;
    count = srd_markers_build(app, markers, labels, SRD_LOG_CAPACITY);
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
    char text[192];

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
    } else if (!srd_tuned(app)) {
        /* The button is a convenience and the field beside it is the
           affordance, so a window with no room for the button loses nothing
           that cannot be typed: srd_layout_for() gives it zero width rather
           than a clipped stub. */
        if (app->receiver_mode && l.retune_button.width > 0.0f) {
            draw_button(l.retune_button, "Retune to 434 MHz", 1);
        } else {
            sdrgui_text_fit(
                app->receiver_mode
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
                 s->session.transmissions_found, s->session.frames_decoded,
                 s->session.last_carrier_hz / 1e3);
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
        static char labels[SRD_LOG_CAPACITY][32];
        int marker_count = srd_markers_build(app, markers, labels,
                                             SRD_LOG_CAPACITY);

        draw_waterfall_rect_with_markers(app, 0, l.waterfall, &s->window,
                                         markers, marker_count, NULL);

        draw_log(app, l.log_full);
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

    draw_identity(app, l.identity);
    draw_log(app, l.log_split);
}

Rectangle srd_waterfall_rect(const struct app *app) {
    struct srd_layout l = srd_layout_for((float)GetScreenWidth(),
                                         (float)GetScreenHeight());

    if (app->srd.analysis_mode)
        return (Rectangle){ 0, 0, 0, 0 };
    return l.waterfall;
}
