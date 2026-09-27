#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gui/view.h"
#include "runtime/debug_log.h"
#include "gui/tetra_layout.h"
#include "gui/sdrgui.h"
#include "gui/sdrgui_geometry.h"

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




void handle_tetra_input(struct app *app) {
    struct tetra_layout l = tetra_layout_for((float)GetScreenWidth(),
                                             (float)GetScreenHeight());

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), l.view_toggle)) {
        app->tetra.analysis_mode = !app->tetra.analysis_mode;
        return;
    }
    /* A row in the identity log. Selection only: a TETRA carrier is the one
       the receiver is already on. */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int row = sdrgui_message_log_row_at(
            app->tetra.analysis_mode ? l.log_split : l.log_full,
            app->tetra.log_count, GetMousePosition());

        if (row >= 0)
            app->tetra.selected_log = row;
    }
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

static void draw_log(struct app *app, Rectangle box) {
    struct tetra_view *t = &app->tetra;
    struct sdrgui_message_log_params params;
    static struct sdrgui_message_log_row rows[TETRA_LOG_CAPACITY];
    static char at[TETRA_LOG_CAPACITY][16];
    static char who[TETRA_LOG_CAPACITY][16];
    static char detail[TETRA_LOG_CAPACITY][64];
    static char counts[TETRA_LOG_CAPACITY][40];
    int i;

    /* Rows are one identity rather than one burst -- seventy a second all
       saying the same thing is not a log, it is a stuck key. */
    for (i = 0; i < t->log_count; i++) {
        snprintf(at[i], sizeof(at[i]), "%6.1fs", t->log[i].at);
        snprintf(who[i], sizeof(who[i]), "%d-%d", t->log[i].mcc,
                 t->log[i].mnc);
        snprintf(detail[i], sizeof(detail[i]), "colour %d   LA %d",
                 t->log[i].colour, t->log[i].la);
        snprintf(counts[i], sizeof(counts[i]), "%d burst / %d block / %d bcast",
                 t->log[i].bursts, t->log[i].blocks, t->log[i].broadcast);
        rows[i].time = at[i];
        rows[i].id = who[i];
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
    params.id_heading = "NETWORK";
    params.label_heading = "TYPE";
    params.selected_row = t->selected_log;
    /* Drawn, and nothing more: selecting a row is handle_tetra_input()'s. */
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
                t->analysis_mode ? "Show log" : "Show charts", 0);

    if (!t->analysis_mode) {
        struct sdrgui_waterfall_marker tetra_marker;
        int m_cnt = 0;
        char tetra_lbl[32];
        if (t->session.have_identity) {
            tetra_marker.frequency_hz = (double)app->applied.frequency_hz;
            tetra_marker.bandwidth_hz = 25000.0; /* 25 kHz channel */
            tetra_marker.age_seconds = 0.5;
            tetra_marker.duration_seconds = 0.0566; /* 56.6 ms slot */
            tetra_marker.id = 0;
            tetra_marker.highlighted = 1;
            tetra_marker.color = (Color){ 80, 220, 240, 220 };
            snprintf(tetra_lbl, sizeof(tetra_lbl), "LA %d", t->session.la);
            tetra_marker.label = tetra_lbl;
            m_cnt = 1;
        }

        draw_waterfall_rect_with_markers(app, 0, l.waterfall, &t->window,
                                         m_cnt ? &tetra_marker : NULL, m_cnt, NULL);
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

Rectangle tetra_waterfall_rect(const struct app *app) {
    (void)app;
    return tetra_layout_for((float)GetScreenWidth(), (float)GetScreenHeight()).waterfall;
}
