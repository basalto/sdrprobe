#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "view.h"
#include "tetra_layout.h"
#include "sdrgui.h"

/*
 * The Decode tab's TETRA screen: what the network says about itself, and how
 * that was read.
 *
 * The log says what decoded and when. The analysis mode says how -- the phase
 * steps the dibits came off, and which of the 255 symbol positions in a
 * timeslot repeat slot to slot. Both are worth having for the same reason the
 * ADS-B view has both: a log that is empty tells you nothing about whether
 * anything is arriving.
 *
 * There is no state to carry across blocks. A TETRA downlink is continuous and
 * this base station sends a synchronization burst in every timeslot, so one
 * 65.5 ms block holds several and each is decoded on its own.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void remember(struct app *app, double now, int mcc, int mnc, int colour,
                     int la) {
    struct tetra_view *t = &app->tetra;
    int i;

    /* One entry per identity, not one per burst: the identity repeats seventy
       times a second and is the same every time. What is worth a row is a
       change -- a different cell, or the first sight of one. */
    if (t->log_count > 0 && t->log[0].colour == colour && t->log[0].la == la) {
        t->log[0].bursts = t->session.bursts;
        t->log[0].blocks = t->session.blocks;
        t->log[0].broadcast = t->session.broadcast;
        return;
    }
    for (i = TETRA_LOG_CAPACITY - 1; i > 0; i--)
        t->log[i] = t->log[i - 1];
    t->log[0].at = now;
    t->log[0].mcc = mcc;
    t->log[0].mnc = mnc;
    t->log[0].colour = colour;
    t->log[0].la = la;
    t->log[0].bursts = t->session.bursts;
    t->log[0].blocks = t->session.blocks;
    t->log[0].broadcast = t->session.broadcast;
    if (t->log_count < TETRA_LOG_CAPACITY)
        t->log_count++;
}


/*
 * Feed the latest block to the decode, then take what the charts draw out of
 * it.
 *
 * Everything past `tetra_session_feed()` is drawing: the constellation is the
 * phase *step*, which is what carries the dibit -- four points at odd multiples
 * of pi/4, not the eight the raw symbols make. Drawing the raw symbols would
 * show a ring and say nothing.
 */
void update_tetra(struct app *app, double now) {
    struct tetra_view *t = &app->tetra;
    struct tetra_session_event event;
    int k;

    tetra_session_feed(&t->session, app->frame.i_samples, app->frame.q_samples,
                       app->frame.pair_count, (double)app->applied_sample_rate,
                       &event);

    t->point_count = 0;
    t->profile_valid = 0;
    if (!t->session.symbols_valid)
        return;

    t->point_count = t->session.symbols.count < TETRA_MAX_SYMBOLS
                         ? t->session.symbols.count
                         : TETRA_MAX_SYMBOLS;
    for (k = 0; k < t->point_count; k++) {
        t->point_x[k] = (float)cos((double)t->session.symbols.step[k]);
        t->point_y[k] = (float)sin((double)t->session.symbols.step[k]);
        t->point_bit[k] = t->session.symbols.dibit[k];
    }
    if (t->session.sync_valid) {
        memcpy(t->profile, t->session.sync.profile, sizeof(t->profile));
        t->profile_fixed = t->session.sync.fixed;
        t->profile_valid = 1;
    }
    if (t->session.blocks > 0)
        remember(app, now, t->session.mcc, t->session.mnc, t->session.colour,
                 t->session.la);
}

void handle_tetra_input(struct app *app) {
    struct tetra_layout l = tetra_layout_for((float)GetScreenWidth(),
                                             (float)GetScreenHeight());

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), l.view_toggle))
        app->tetra.analysis_mode = !app->tetra.analysis_mode;
}

static void draw_identity(const struct app *app, Rectangle box) {
    const struct tetra_view *t = &app->tetra;
    struct panel_rows rows = panel_rows_for(box, TETRA_PANEL_CAPTION_DROP,
                                            TETRA_PANEL_ROW_HEIGHT, 0.0f,
                                            0.0f, 0.0f);
    char text[96];
    int y = (int)rows.first_y;
    int r = 0;

    DrawRectangleLinesEx(box, 1.0f, (Color){ 48, 66, 88, 255 });
    DrawText("Network", (int)box.x + 10, (int)box.y + 8, 14,
             (Color){ 150, 176, 202, 255 });
    if (!t->session.have_identity) {
        sdrgui_text_fit("nothing has decoded yet", (int)box.x + 10, y, 14,
                        box.width - 20.0f, (Color){ 120, 140, 160, 255 });
        return;
    }
    /*
     * Ordered so a short panel keeps the identity. A row past the panel's
     * capacity is not drawn -- these used to run off the bottom edge, five of
     * them needing 146 px in a panel that holds 133 on a 1000x540 window.
     */
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "MCC  %d", t->session.mcc);
        DrawText(text, (int)box.x + 10, y, 18, (Color){ 226, 236, 245, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "MNC  %d", t->session.mnc);
        DrawText(text, (int)box.x + 10, y, 18, (Color){ 226, 236, 245, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "colour code  %d", t->session.colour);
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 190, 210, 228, 255 });
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        if (t->session.broadcast_total > 0) {
            snprintf(text, sizeof(text), "location area  %d", t->session.la);
            DrawText(text, (int)box.x + 10, y, 16,
                     (Color){ 190, 210, 228, 255 });
        } else {
            sdrgui_text_fit("location area unread", (int)box.x + 10, y, 16,
                            box.width - 20.0f, (Color){ 120, 140, 160, 255 });
        }
    }
    r++;
    if (panel_row_visible(&rows, r)) {
        y = (int)panel_row_y(&rows, r);
        snprintf(text, sizeof(text), "lock %.2f   offset %+.0f Hz",
                 (double)t->session.lock, t->session.offset_hz);
        sdrgui_text_fit(text, (int)box.x + 10, y, 14, box.width - 20.0f,
                        (Color){ 150, 176, 202, 255 });
    }
}

static void draw_log(const struct app *app, Rectangle box) {
    const struct tetra_view *t = &app->tetra;
    struct sdrgui_message_log_params params;
    static struct sdrgui_message_log_row rows[TETRA_LOG_CAPACITY];
    static char at[TETRA_LOG_CAPACITY][16];
    static char who[TETRA_LOG_CAPACITY][16];
    static char detail[TETRA_LOG_CAPACITY][64];
    static char counts[TETRA_LOG_CAPACITY][40];
    int i;

    /* The log component's columns were named for Mode S, which is where it
       came from; what they mean here is when, whose, and what was said. A row
       is one identity rather than one burst -- seventy a second all saying the
       same thing is not a log, it is a stuck key. */
    for (i = 0; i < t->log_count; i++) {
        snprintf(at[i], sizeof(at[i]), "%6.1fs", t->log[i].at);
        snprintf(who[i], sizeof(who[i]), "%d-%d", t->log[i].mcc,
                 t->log[i].mnc);
        snprintf(detail[i], sizeof(detail[i]), "colour %d   LA %d",
                 t->log[i].colour, t->log[i].la);
        snprintf(counts[i], sizeof(counts[i]), "%d burst / %d block / %d bcast",
                 t->log[i].bursts, t->log[i].blocks, t->log[i].broadcast);
        rows[i].time = at[i];
        rows[i].icao = who[i];
        rows[i].label = "SYNC";
        rows[i].detail = detail[i];
        rows[i].raw = counts[i];
        rows[i].highlight = (i == 0);
    }
    memset(&params, 0, sizeof(params));
    params.plot = box;
    params.rows = rows;
    params.count = t->log_count;
    params.caption = "Identities";
    params.empty_notice = "nothing has decoded yet";
    sdrgui_message_log(&params);
}

void draw_tetra(struct app *app) {
    struct tetra_view *t = &app->tetra;
    struct tetra_layout l = tetra_layout_for((float)GetScreenWidth(),
                                             (float)GetScreenHeight());
    char text[192];

    if (t->session.have_identity)
        snprintf(text, sizeof(text),
                 "TETRA  MCC %d  MNC %d  colour code %d  LA %s%d",
                 t->session.mcc, t->session.mnc, t->session.colour,
                 t->session.broadcast_total > 0 ? "" : "un", t->session.la);
    else
        snprintf(text, sizeof(text),
                 "TETRA  no network identity yet  (lock %.2f)",
                 (double)t->session.lock);
    sdrgui_text_fit(text, (int)l.header_left, 78, 20,
                    l.header_right - l.header_left,
                    (Color){ 226, 236, 245, 255 });

    /* The funnel, which is the diagnosis when nothing decodes: bursts found
       but no parity is a coding fault, no bursts at all is tuning or band. */
    snprintf(text, sizeof(text),
             "%llu burst(s)  %llu with parity  %llu failed  %llu broadcast",
             (unsigned long long)t->session.bursts_total,
             (unsigned long long)t->session.blocks_total,
             (unsigned long long)t->session.blocks_failed,
             (unsigned long long)t->session.broadcast_total);
    sdrgui_text_fit(text, (int)l.header_left, 106, 16,
                    l.header_right - l.header_left,
                    (Color){ 150, 176, 202, 255 });

    draw_button(l.view_toggle,
                t->analysis_mode ? "View: Analysis" : "View: Log", 0);

    if (!t->analysis_mode) {
        draw_log(app, l.log_full);
        return;
    }

    {
        struct sdrgui_constellation_params c;
        memset(&c, 0, sizeof(c));
        c.plot = l.constellation;
        c.x = t->point_x;
        c.y = t->point_y;
        c.bit = t->point_bit;
        c.count = t->point_count;
        c.caption = "Phase steps";
        c.empty_notice = "no symbols";
        sdrgui_constellation(&c);
    }
    {
        struct sdrgui_burst_chart_params b;
        char caption[80];

        snprintf(caption, sizeof(caption),
                 t->profile_valid
                     ? "Repeats within a 255-symbol slot: %d fixed"
                     : "Repeats within a slot",
                 t->profile_fixed);
        memset(&b, 0, sizeof(b));
        b.plot = l.profile;
        b.data = t->profile;
        b.count = t->profile_valid ? TETRA_SLOT_SYMBOLS : 0;
        b.type = SDRGUI_BURST_BAR;
        b.y_min = 0.0f;
        b.y_max = 1.0f;
        b.title = caption;
        b.empty_notice = "no burst grid found";
        sdrgui_burst_chart(&b);
    }
    draw_identity(app, l.identity);
    draw_log(app, l.log_split);
}
