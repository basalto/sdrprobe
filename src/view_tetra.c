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
        t->log[0].bursts = t->bursts;
        t->log[0].blocks = t->blocks;
        t->log[0].broadcast = t->broadcast;
        return;
    }
    for (i = TETRA_LOG_CAPACITY - 1; i > 0; i--)
        t->log[i] = t->log[i - 1];
    t->log[0].at = now;
    t->log[0].mcc = mcc;
    t->log[0].mnc = mnc;
    t->log[0].colour = colour;
    t->log[0].la = la;
    t->log[0].bursts = t->bursts;
    t->log[0].blocks = t->blocks;
    t->log[0].broadcast = t->broadcast;
    if (t->log_count < TETRA_LOG_CAPACITY)
        t->log_count++;
}

static int field(const uint8_t *bits, int at, int count) {
    int v = 0, i;

    for (i = 0; i < count; i++)
        v = (v << 1) | (bits[at + i] & 1);
    return v;
}

void update_tetra(struct app *app, double now) {
    static float work_i[TETRA_MAX_WORK], work_q[TETRA_MAX_WORK];
    static struct tetra_symbols symbols;
    struct tetra_view *t = &app->tetra;
    struct tetra_burst_sync sync;
    const unsigned char *word = NULL;
    double coarse;
    size_t filtered;
    int at, k;

    t->bursts = t->blocks = t->broadcast = 0;
    if (app->pair_count < 64)
        return;
    coarse = tetra_coarse_offset_hz(app->i_samples, app->q_samples,
                                    app->pair_count,
                                    (double)app->applied_sample_rate, 12500.0);
    filtered = tetra_channel(app->i_samples, app->q_samples, app->pair_count,
                             (double)app->applied_sample_rate, coarse, work_i,
                             work_q, TETRA_MAX_WORK);
    if (filtered == 0 || !tetra_demodulate(work_i, work_q, filtered, coarse,
                                           &symbols))
        return;
    t->lock = symbols.lock;
    t->offset_hz = coarse + symbols.fine_offset_hz;

    /* The constellation is the phase *step*, which is what carries the dibit:
       four points at odd multiples of pi/4, not the eight the raw symbols
       make. Drawing the raw symbols would show a ring and say nothing. */
    t->point_count = symbols.count < TETRA_MAX_SYMBOLS ? symbols.count
                                                       : TETRA_MAX_SYMBOLS;
    for (k = 0; k < t->point_count; k++) {
        t->point_x[k] = (float)cos((double)symbols.step[k]);
        t->point_y[k] = (float)sin((double)symbols.step[k]);
        t->point_bit[k] = symbols.dibit[k];
    }

    /* And how much of a timeslot repeats, which is what a burst structure
       looks like from the outside -- it needs nothing from the standard. */
    t->profile_valid = 0;
    /* 200 to 280 covers the 255-symbol slot and no more: the finder wants
       four periods at the longest lag it is asked for, and a 65.5 ms block is
       about 1180 symbols. Asking as far as 400 would need 1600 and it would
       decline every time -- which is what the empty chart looked like. */
    if (tetra_burst_find(symbols.dibit, symbols.count, 200, 280, &sync) &&
        sync.period == TETRA_SLOT_SYMBOLS) {
        memcpy(t->profile, sync.profile, sizeof(t->profile));
        t->profile_fixed = sync.fixed;
        t->profile_valid = 1;
    }

    tetra_sync_dibits(&word);
    for (at = 60; at + TETRA_SYNC_SYMBOLS <= symbols.count; at++) {
        uint8_t block[TETRA_SB_MESSAGE_BITS];
        int hits = 0;

        for (k = 0; k < TETRA_SYNC_SYMBOLS; k++)
            if (symbols.dibit[at + k] == word[k])
                hits++;
        if (hits < 16)
            continue;
        t->bursts++;
        t->bursts_total++;
        if (!tetra_sync_block_decode(symbols.dibit + at - 60, block)) {
            t->blocks_failed++;
            continue;
        }
        t->blocks++;
        t->blocks_total++;
        t->mcc = field(block, 31, 10);
        t->mnc = field(block, 41, 14);
        t->colour = field(block, 4, 6);
        t->have_identity = 1;
        /* The broadcast channel is scrambled with the network's own colour
           code, so it cannot be read until the block above has given it up. */
        if (at + TETRA_BNCH_AT_SYMBOL + TETRA_BNCH_SCRAMBLED_BITS / 2 <=
            symbols.count) {
            uint8_t colour[TETRA_COLOUR_BITS];
            uint8_t sysinfo[TETRA_BNCH_MESSAGE_BITS];

            tetra_extended_colour(t->mcc, t->mnc, t->colour, colour);
            if (tetra_bnch_decode(symbols.dibit + at + TETRA_BNCH_AT_SYMBOL,
                                  colour, sysinfo)) {
                t->broadcast++;
                t->broadcast_total++;
                t->la = field(sysinfo, 82, 14);
            }
        }
    }
    if (t->blocks > 0)
        remember(app, now, t->mcc, t->mnc, t->colour, t->la);
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
    char text[96];
    int y = (int)box.y + 30;

    DrawRectangleLinesEx(box, 1.0f, (Color){ 48, 66, 88, 255 });
    DrawText("Network", (int)box.x + 10, (int)box.y + 8, 14,
             (Color){ 150, 176, 202, 255 });
    if (!t->have_identity) {
        sdrgui_text_fit("nothing has decoded yet", (int)box.x + 10, y, 14,
                        box.width - 20.0f, (Color){ 120, 140, 160, 255 });
        return;
    }
    snprintf(text, sizeof(text), "MCC  %d", t->mcc);
    DrawText(text, (int)box.x + 10, y, 18, (Color){ 226, 236, 245, 255 });
    y += 26;
    snprintf(text, sizeof(text), "MNC  %d", t->mnc);
    DrawText(text, (int)box.x + 10, y, 18, (Color){ 226, 236, 245, 255 });
    y += 26;
    snprintf(text, sizeof(text), "colour code  %d", t->colour);
    DrawText(text, (int)box.x + 10, y, 16, (Color){ 190, 210, 228, 255 });
    y += 24;
    if (t->broadcast_total > 0) {
        snprintf(text, sizeof(text), "location area  %d", t->la);
        DrawText(text, (int)box.x + 10, y, 16, (Color){ 190, 210, 228, 255 });
    } else {
        sdrgui_text_fit("location area unread", (int)box.x + 10, y, 16,
                        box.width - 20.0f, (Color){ 120, 140, 160, 255 });
    }
    y += 26;
    snprintf(text, sizeof(text), "lock %.2f   offset %+.0f Hz",
             (double)t->lock, t->offset_hz);
    sdrgui_text_fit(text, (int)box.x + 10, y, 14, box.width - 20.0f,
                    (Color){ 150, 176, 202, 255 });
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

    if (t->have_identity)
        snprintf(text, sizeof(text),
                 "TETRA  MCC %d  MNC %d  colour code %d  LA %s%d",
                 t->mcc, t->mnc, t->colour,
                 t->broadcast_total > 0 ? "" : "un", t->la);
    else
        snprintf(text, sizeof(text),
                 "TETRA  no network identity yet  (lock %.2f)",
                 (double)t->lock);
    sdrgui_text_fit(text, (int)l.header_left, 78, 20,
                    l.header_right - l.header_left,
                    (Color){ 226, 236, 245, 255 });

    /* The funnel, which is the diagnosis when nothing decodes: bursts found
       but no parity is a coding fault, no bursts at all is tuning or band. */
    snprintf(text, sizeof(text),
             "%llu burst(s)  %llu with parity  %llu failed  %llu broadcast",
             (unsigned long long)t->bursts_total,
             (unsigned long long)t->blocks_total,
             (unsigned long long)t->blocks_failed,
             (unsigned long long)t->broadcast_total);
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
