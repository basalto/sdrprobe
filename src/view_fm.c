#include <math.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h>

#include "app.h"
#include "fm_layout.h"
#include "sdrgui.h"
#include "view.h"

#include "raygui.h"

/*
 * FM broadcast, and what a station's RDS says about itself.
 *
 * This file draws and decides nothing (ADR-0007 in spirit, ADR-0012 in law):
 * the pilot, the subcarrier and the block code are all in fm_dsp.c and rds.c,
 * where checks reach them with no window and no receiver. What is left here is
 * feeding blocks in, keeping the window of soft bits, and putting three panels
 * on screen.
 *
 * The one thing worth knowing before reading it: unlike every other decode
 * view, this one cannot work a block at a time. The pilot needs a quarter
 * second before its lock means anything -- four blocks -- and the symbol
 * clock and the bitstream run straight through a block boundary. So the front
 * end is kept in struct fm_view across blocks, and the soft bits accumulate
 * into a window rather than being decoded and discarded.
 */

static const Color panel_edge = { 82, 109, 126, 255 };
static const Color panel_caption = { 151, 174, 188, 255 };
static const Color row_label = { 126, 151, 166, 255 };
static const Color row_value = { 213, 226, 234, 255 };
static const Color row_good = { 99, 228, 170, 255 };
static const Color row_weak = { 250, 190, 74, 255 };

/* A few local frequencies, so the common case is one click rather than eight
   keystrokes. They are the strongest carriers a sweep of band II found here;
   any frequency can still be typed. */
static const double FM_PRESETS[FM_LAYOUT_STATIONS] = {
    89.6e6, 94.4e6, 100.3e6
};

void view_fm_defaults(struct app *app) {
    memset(&app->fm, 0, sizeof(app->fm));
    rds_station_init(&app->fm.station);
    snprintf(app->fm.frequency, sizeof(app->fm.frequency), "%.1f",
             FM_PRESETS[0] / 1e6);
    app->fm.frequency_length = (int)strlen(app->fm.frequency);
}

/*
 * One block of samples through the chain.
 *
 * The front end is rebuilt only when the sample rate changes under it, since
 * every rate it derives -- the loop gains, the emission clock -- comes from
 * that number and a stale one puts the pilot 450 Hz from where the loop is
 * looking, which a 10 Hz loop will never find.
 */
void update_fm(struct app *app, double now) {
    static float multiplex[SAMPLE_BLOCK_PAIRS];
    static float fresh_i[FM_VIEW_BASEBAND];
    static float fresh_q[FM_VIEW_BASEBAND];
    struct fm_view *fm = &app->fm;
    size_t n, bb;

    if (app->pair_count < 2)
        return;
    if (fm->front_rate != (int)app->applied_sample_rate) {
        if (fm_rds_front_init(&fm->front, (double)app->applied_sample_rate) < 0)
            return;
        fm->front_rate = (int)app->applied_sample_rate;
        fm->bb_count = 0;
        fm->soft_count = 0;
    }

    n = fm_discriminate_f(app->i_samples, app->q_samples, app->pair_count,
                          multiplex, SAMPLE_BLOCK_PAIRS);
    bb = fm_rds_front_feed(&fm->front, multiplex, n, fresh_i, fresh_q,
                           FM_VIEW_BASEBAND);
    if (bb == 0)
        return;

    /*
     * The window, in baseband. When it fills the oldest quarter goes, in
     * whole symbols -- dropping a fraction of one would shift the grid under
     * the timing search and cost a group at every wrap.
     */
    if (fm->bb_count + bb > FM_VIEW_BASEBAND) {
        size_t drop = FM_VIEW_BASEBAND / 4;
        drop -= drop % FM_RDS_SAMPLES_PER_SYMBOL;
        if (drop > fm->bb_count)
            drop = fm->bb_count;
        memmove(fm->bb_i, fm->bb_i + drop,
                (fm->bb_count - drop) * sizeof(fm->bb_i[0]));
        memmove(fm->bb_q, fm->bb_q + drop,
                (fm->bb_count - drop) * sizeof(fm->bb_q[0]));
        fm->bb_count -= drop;
    }
    if (bb > FM_VIEW_BASEBAND - fm->bb_count)
        bb = FM_VIEW_BASEBAND - fm->bb_count;
    memcpy(fm->bb_i + fm->bb_count, fresh_i, bb * sizeof(fresh_i[0]));
    memcpy(fm->bb_q + fm->bb_count, fresh_q, bb * sizeof(fresh_q[0]));
    fm->bb_count += bb;
    fm->blocks_seen++;

    /* One timing search and one axis over the whole window, which is what
       makes this the same answer the offline decode gives. */
    fm->soft_count = fm_rds_soft_bits(fm->bb_i, fm->bb_q, fm->bb_count,
                                      fm->soft, FM_VIEW_SOFT_BITS,
                                      &fm->timing_offset, &fm->axis_radians);
    if (fm->soft_count == 0)
        return;

    {
        long before = fm->station.funnel.groups;
        rds_decode(fm->soft, fm->soft_count, &fm->station, NULL, 0);
        if (fm->station.funnel.groups > 0 &&
            fm->station.funnel.groups != before)
            fm->last_group_at = now;
        fm->groups_total = fm->station.funnel.groups;
    }
}

static int draw_panel(Rectangle rect, const char *caption) {
    DrawRectangleRec(rect, (Color){ 17, 26, 37, 255 });
    DrawRectangleLinesEx(rect, 1.0f, panel_edge);
    sdrgui_text_fit(caption, (int)rect.x + 12, (int)rect.y + 10, 16,
                    rect.width - 24.0f, panel_caption);
    return (int)rect.y + 36;
}

static void draw_row(Rectangle rect, int y, const char *label,
                     const char *value, Color colour) {
    int label_x = (int)rect.x + 12;
    int value_x = label_x + 128;
    float room = rect.x + rect.width - 12.0f - (float)value_x;

    DrawText(label, label_x, y, 15, row_label);
    sdrgui_text_fit(value, value_x, y, 15, room, colour);
}

static void draw_signal_panel(const struct app *app, Rectangle rect) {
    const struct fm_view *fm = &app->fm;
    int locked = fm_pilot_locked(&fm->front.pilot);
    int y = draw_panel(rect, "Signal");
    char text[96];

    draw_row(rect, y, "pilot", locked ? "locked" : "no lock",
             locked ? row_good : row_weak);
    y += 20;
    if (locked) {
        snprintf(text, sizeof(text), "%.2f Hz", fm_pilot_hz(&fm->front.pilot));
        draw_row(rect, y, "at", text, row_value);
        y += 20;
        snprintf(text, sizeof(text), "%+.1f ppm",
                 fm_pilot_ppm(&fm->front.pilot));
        draw_row(rect, y, "sample clock", text, row_value);
        y += 20;
    }
    snprintf(text, sizeof(text), "%.2f", fm->front.pilot.coherence);
    draw_row(rect, y, "coherence", text,
             fm->front.pilot.coherence >= FM_PILOT_MIN_COHERENCE ? row_good
                                                                 : row_weak);
    y += 20;
    if (locked) {
        snprintf(text, sizeof(text), "%d/%d", fm->timing_offset,
                 FM_RDS_SAMPLES_PER_SYMBOL);
        draw_row(rect, y, "symbol timing", text, row_value);
        y += 20;
        snprintf(text, sizeof(text), "%+.2f rad", fm->axis_radians);
        draw_row(rect, y, "subcarrier axis", text, row_value);
    }
}

static void draw_station_panel(const struct app *app, Rectangle rect) {
    const struct rds_station *s = &app->fm.station;
    int y = draw_panel(rect, "Station");
    char text[96];

    if (s->pi_valid) {
        snprintf(text, sizeof(text), "0x%04X", s->pi);
        draw_row(rect, y, "identification", text, row_value);
        y += 20;
        snprintf(text, sizeof(text), "%d agreeing", s->pi_repeats);
        draw_row(rect, y, "", text, row_label);
        y += 20;
    } else {
        draw_row(rect, y, "identification", "--", row_label);
        y += 20;
    }

    /*
     * The name is shown only when it is whole and has repeated. A programme
     * service name arrives two characters at a time, so anything else puts a
     * station that does not exist on the screen -- and a reader has no way to
     * tell a half-arrived name from a short one.
     */
    if (s->ps_valid) {
        draw_row(rect, y, "name", s->ps, row_good);
    } else if (s->ps_segments) {
        snprintf(text, sizeof(text), "%d of 4 segments",
                 __builtin_popcount((unsigned)s->ps_segments));
        draw_row(rect, y, "name", text, row_weak);
    } else {
        draw_row(rect, y, "name", "--", row_label);
    }
    y += 20;

    if (s->pty_valid) {
        const char *name = rds_pty_name(s->pty);
        snprintf(text, sizeof(text), "%s", name ? name : "?");
        draw_row(rect, y, "programme type", text, row_value);
        y += 20;
        snprintf(text, sizeof(text), "%s%s",
                 s->tp ? "traffic programme" : "no traffic",
                 s->ta ? ", announcement now" : "");
        draw_row(rect, y, "", text, s->ta ? row_weak : row_label);
        y += 20;
    }
    if (s->rt_valid)
        sdrgui_text_fit(s->rt, (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        row_value);
}

/*
 * Where the decode stopped.
 *
 * Two empty panels look the same whether nothing is transmitting or every
 * block is failing its syndrome, and that difference is the whole diagnosis.
 * The LTE view had to learn this; there is no reason for this one to learn it
 * again.
 */
static void draw_funnel_panel(const struct app *app, Rectangle rect) {
    const struct rds_funnel *f = &app->fm.station.funnel;
    int y = draw_panel(rect, "Where the decode stopped");
    char text[96];

    snprintf(text, sizeof(text), "%ld", f->bits);
    draw_row(rect, y, "soft bits", text, row_value);
    y += 20;
    snprintf(text, sizeof(text), "%ld", f->blocks_matched);
    draw_row(rect, y, "blocks", text, f->blocks_matched ? row_value : row_weak);
    y += 20;
    snprintf(text, sizeof(text), "%ld", f->groups);
    draw_row(rect, y, "groups", text, f->groups ? row_value : row_weak);
    y += 20;
    snprintf(text, sizeof(text), "%ld", f->identified);
    draw_row(rect, y, "identified", text, f->identified ? row_value : row_weak);
    y += 20;
    snprintf(text, sizeof(text), "%ld", f->named);
    draw_row(rect, y, "named", text, f->named ? row_good : row_weak);
    y += 24;

    /* The reading, in words. A count is only useful to somebody who already
       knows what it should be. */
    if (!fm_pilot_locked(&app->fm.front.pilot))
        sdrgui_text_fit("no pilot: not an FM station, or not tuned to one",
                        (int)rect.x + 12, y, 15, rect.width - 24.0f, row_weak);
    else if (f->blocks_matched == 0)
        sdrgui_text_fit("a pilot but no blocks: this station carries no RDS",
                        (int)rect.x + 12, y, 15, rect.width - 24.0f, row_weak);
    else if (f->groups == 0)
        sdrgui_text_fit("blocks but no groups: too weak to hold sync",
                        (int)rect.x + 12, y, 15, rect.width - 24.0f, row_weak);
    else if (!app->fm.station.ps_valid)
        sdrgui_text_fit("groups arriving; the name needs all four segments",
                        (int)rect.x + 12, y, 15, rect.width - 24.0f, row_label);
    else
        sdrgui_text_fit("reading the station", (int)rect.x + 12, y, 15,
                        rect.width - 24.0f, row_good);
}

void draw_fm(struct app *app) {
    struct fm_layout l = fm_layout_now();
    char text[64];
    int i;

    GuiLabel((Rectangle){ l.frequency_field.x, l.frequency_field.y - 18.0f,
                          120.0f, 16.0f }, "MHz");
    sdrgui_text_field(l.frequency_field, app->fm.frequency, 1);
    draw_button(l.tune_button, "Tune", 0);
    for (i = 0; i < FM_LAYOUT_STATIONS; i++) {
        snprintf(text, sizeof(text), "%.1f", FM_PRESETS[i] / 1e6);
        draw_button(l.station_button[i], text,
                    fabs((double)app->applied_frequency - FM_PRESETS[i]) <
                        50000.0);
    }

    draw_waterfall_rect(app, 0, l.waterfall, 0.0);
    draw_signal_panel(app, l.signal_panel);
    draw_station_panel(app, l.station_panel);
    draw_funnel_panel(app, l.funnel_panel);
}

/* Whether the frequency field is taking keystrokes. The frame loop asks so
   the digits do not also reach the view switcher. */
int fm_editing(const struct app *app) {
    return app->fm.typing;
}

void handle_fm_input(struct app *app) {
    struct fm_layout l = fm_layout_now();
    int character;
    int i;

    /*
     * Escape is one step out, not two: out of the field if one has focus,
     * and out of the tab otherwise. Every other decode view leaves for the
     * Scope tab and this one did nothing at all, which reads as a stuck
     * screen -- and leaving the tab straight from a half-typed frequency
     * would throw the frequency away as well.
     */
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (app->fm.typing)
            app->fm.typing = 0;
        else
            set_tab(app, TAB_SCOPE);
        return;
    }

    while (app->fm.typing && (character = GetCharPressed()) != 0) {
        if (((character >= '0' && character <= '9') || character == '.') &&
            app->fm.frequency_length < (int)sizeof(app->fm.frequency) - 1) {
            app->fm.frequency[app->fm.frequency_length++] = (char)character;
            app->fm.frequency[app->fm.frequency_length] = '\0';
        }
    }
    if (app->fm.typing && IsKeyPressed(KEY_BACKSPACE) &&
        app->fm.frequency_length > 0)
        app->fm.frequency[--app->fm.frequency_length] = '\0';

    if (clicked(l.frequency_field))
        app->fm.typing = 1;
    else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        app->fm.typing = 0;

    for (i = 0; i < FM_LAYOUT_STATIONS; i++)
        if (clicked(l.station_button[i])) {
            snprintf(app->fm.frequency, sizeof(app->fm.frequency), "%.1f",
                     FM_PRESETS[i] / 1e6);
            app->fm.frequency_length = (int)strlen(app->fm.frequency);
            fm_tune(app, FM_PRESETS[i]);
            return;
        }

    if (clicked(l.tune_button) ||
        (app->fm.typing && IsKeyPressed(KEY_ENTER))) {
        double megahertz = atof(app->fm.frequency);
        if (megahertz >= 76.0 && megahertz <= 108.0)
            fm_tune(app, megahertz * 1e6);
    }
}

/*
 * Retune, and start the decode again from nothing.
 *
 * Everything in the view describes one carrier: the pilot the loop is tracking,
 * the bits in the window, the station assembled from them. Carrying any of it
 * across a retune would put the last station's name over the new one's signal
 * for as long as the window took to empty.
 */
void fm_tune(struct app *app, double hz) {
    if (retune_receiver(app, (uint32_t)llround(hz), app->applied_ppm) < 0)
        return;
    app->fm.soft_count = 0;
    app->fm.front_rate = 0;
    app->fm.blocks_seen = 0;
    app->fm.groups_total = 0;
    rds_station_init(&app->fm.station);
}
