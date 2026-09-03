#include <math.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h>

#include "app.h"
#include "fm_scan.h"
#include "fm_layout.h"
#include "sdrgui.h"
#include "view.h"
#include "debug_log.h"
#include "sdr_dsp.h"
#include "row_list.h"

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

void view_fm_defaults(struct app *app) {
    memset(&app->fm, 0, sizeof(app->fm));
    rds_station_init(&app->fm.station);
    /* Empty until the scan says what is out there. Nothing here knows the
       band's occupants and guessing one wires this site into the source. */
    app->fm.frequency[0] = '\0';
    app->fm.frequency_length = 0;
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

    /*
     * The multiplex spectrum for the charts, a few times a second. It
     * averages thirty-two windows of a 2048-point transform, which is far
     * more work than a frame needs and produces a picture that does not
     * change at frame rate anyway.
     */
    if (fm->analysis_mode && now - fm->spectrum_at > 0.25) {
        double rate = (double)app->applied_sample_rate;
        size_t want;

        /* Ask for the whole transform, then keep the part of it a multiplex
           actually occupies -- the bin width is only known afterwards. */
        fm->spectrum_bins = fm_multiplex_spectrum(multiplex, n, rate,
                                                  fm->spectrum,
                                                  FM_MPX_SPECTRUM_BINS,
                                                  &fm->spectrum_bin_hz);
        if (fm->spectrum_bin_hz > 0.0) {
            want = (size_t)(FM_MPX_SPECTRUM_TOP_HZ / fm->spectrum_bin_hz);
            if (want > 0 && want < fm->spectrum_bins)
                fm->spectrum_bins = want;
        }
        fm->spectrum_at = now;

        /*
         * And the sound's own spectrum. The multiplex chart shows this band
         * too, at its left edge, but not as it is heard: de-emphasis has not
         * been applied there and the pilot has not been taken out, and both
         * of those are most of the difference between what is transmitted and
         * what comes out of a speaker.
         */
        if (fm->audio_trace_count > 0) {
            fm->audio_spectrum_bins =
                fm_multiplex_spectrum(fm->audio_trace, fm->audio_trace_count,
                                      fm_audio_rate(&fm->audio),
                                      fm->audio_spectrum,
                                      FM_MPX_SPECTRUM_BINS,
                                      &fm->audio_spectrum_bin_hz);
            if (fm->audio_spectrum_bin_hz > 0.0) {
                size_t want = (size_t)(16000.0 / fm->audio_spectrum_bin_hz);
                if (want > 0 && want < fm->audio_spectrum_bins)
                    fm->audio_spectrum_bins = want;
            }
        }
    }

    /*
     * The sound, from the same multiplex the decode reads. One block in is
     * one block of audio out, so the ring neither fills nor empties except
     * when a block is dropped.
     */
    {
        static int16_t pcm[FM_AUDIO_RING];
        static int16_t frames[FM_AUDIO_RING * 2];
        size_t made, k;

        /* The path is built once and kept, so the charts have something to
           draw before anything is played and the level follower is already
           settled when Play is pressed. */
        if (fm->audio.decimate < 1 ||
            fm->audio.sample_rate != (double)app->applied_sample_rate)
            fm_audio_init(&fm->audio, (double)app->applied_sample_rate);

        /* One pass gives both: the sum signal for the charts and the two
           channels for the card. */
        made = fm_audio_decode(&fm->audio, multiplex, n, pcm, frames,
                               FM_AUDIO_RING);

        if (fm->playing) {
            for (k = 0; k < made; k++) {
                size_t next = (fm->audio_tail + 1) & (FM_AUDIO_RING - 1);
                if (next == fm->audio_head)
                    break;  /* the card is not keeping up; drop the newest */
                fm->audio_ring[fm->audio_tail * 2] = frames[k * 2];
                fm->audio_ring[fm->audio_tail * 2 + 1] = frames[k * 2 + 1];
                fm->audio_tail = next;
            }
        }

        /* The tail of it, as floats, for the charts. */
        if (made > 0) {
            size_t keep = made > FM_AUDIO_TRACE ? FM_AUDIO_TRACE : made;
            for (k = 0; k < keep; k++)
                fm->audio_trace[k] = (float)pcm[made - keep + k] / 32768.0f;
            fm->audio_trace_count = keep;
        }
    }

    /* One timing search and one axis over the whole window, which is what
       makes this the same answer the offline decode gives. */
    fm->soft_count = fm_rds_soft_bits(fm->bb_i, fm->bb_q, fm->bb_count,
                                      fm->soft, FM_VIEW_SOFT_BITS,
                                      &fm->timing_offset, &fm->axis_radians);
    if (fm->analysis_mode)
        fm_rds_timing_scores(fm->bb_i, fm->bb_q, fm->bb_count,
                             fm->timing_energy);
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

/*
 * What the band walk found: one row per carrier, with what it managed to read
 * in the three quarters of a second it stopped there.
 *
 * A row with a pilot and no identification is a station carrying no RDS,
 * which is worth listing rather than hiding -- it is still a station, and a
 * list that quietly dropped it would read as the band being emptier than it
 * is.
 */
static void draw_scan_list(const struct app *app, Rectangle rect) {
    const struct fm_scan *scan = &app->fm.scan;
    int y = draw_panel(rect, scan->running ? "Scanning band II"
                                           : "Band II");
    char row[160];
    int i, rows;

    sdrgui_text_fit(scan->status, (int)rect.x + 12, y, 15,
                    rect.width - 24.0f, row_label);
    y += 24;
    if (scan->found_count == 0) {
        sdrgui_text_fit(scan->running ? "looking..."
                                      : "press Scan band",
                        (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        row_label);
        return;
    }
    DrawText("   MHz       LEVEL    PILOT  RDS  STATION",
             (int)rect.x + 12, y, 15, row_label);

    {
        struct row_list_metrics m = FM_SCAN_LIST_METRICS;
        int fits = row_list_rows(rect, m);
        int scroll = row_list_clamp_scroll(scan->list_scroll,
                                           scan->found_count, fits);
        int hovered = row_list_rank_at(rect, m, scroll, scan->found_count,
                                       fits, GetMousePosition());

        rows = scan->found_count - scroll;
        if (rows > fits)
            rows = fits;
        for (i = 0; i < rows; i++) {
            const struct fm_found_station *f = &scan->found[scroll + i];
            float row_y = row_list_row_y(rect, m, i);
            char name[32];
            Color colour;

            if (scroll + i == hovered)
                DrawRectangle((int)rect.x + 4, (int)row_y - 2,
                              (int)rect.width - 8, (int)m.row_h,
                              (Color){ 255, 174, 62, 40 });
            /* The carrier being listened to now, so choosing another from
               the list is a move from somewhere rather than from nowhere. */
            if (fabs((double)app->applied_frequency - f->frequency_hz) < 50000.0)
                DrawRectangle((int)rect.x + 4, (int)row_y - 2,
                              (int)rect.width - 8, (int)m.row_h,
                              (Color){ 99, 228, 170, 34 });

            if (f->ps[0])
                snprintf(name, sizeof(name), "%s", f->ps);
            else if (f->pi_valid)
                snprintf(name, sizeof(name), "0x%04X", f->pi);
            else
                snprintf(name, sizeof(name), "--");
            snprintf(row, sizeof(row), "%8.1f  %6.1f dBFS  %-5s %-4s %s",
                     f->frequency_hz / 1e6, (double)f->power_dbfs,
                     f->pilot ? "yes" : "no", f->rds ? "yes" : "no", name);
            colour = f->ps[0] ? row_good : f->pilot ? row_value : row_label;
            sdrgui_text_fit(row, (int)rect.x + 12, (int)row_y, 15,
                            rect.width - 24.0f, colour);
        }

        if (scan->found_count > fits) {
            float track_x = rect.x + rect.width - 7.0f;
            float track_y = rect.y + m.header_h;
            float track_h = (float)fits * m.row_h;
            float thumb_h = track_h * (float)fits / (float)scan->found_count;
            float thumb_y = track_y + track_h * (float)scroll /
                                          (float)scan->found_count;

            /* Not "Up/Down": those are the waterfall's scale on this
               screen. Saying otherwise sends a reader to press a key that
               does something else. */
            snprintf(row, sizeof(row),
                     "%d-%d of %d   wheel to scroll, click to listen",
                     scroll + 1, scroll + rows, scan->found_count);
            DrawText(row, (int)rect.x + 12,
                     (int)(rect.y + rect.height - 19.0f), 15, row_label);
            if (thumb_h < 12.0f)
                thumb_h = 12.0f;
            if (thumb_y + thumb_h > track_y + track_h)
                thumb_y = track_y + track_h - thumb_h;
            DrawRectangle((int)track_x, (int)track_y, 4, (int)track_h,
                          (Color){ 30, 42, 52, 255 });
            DrawRectangle((int)track_x, (int)thumb_y, 4, (int)thumb_h,
                          (Color){ 108, 138, 158, 255 });
        } else if (!scan->running) {
            DrawText("click a station to listen to it", (int)rect.x + 12,
                     (int)(rect.y + rect.height - 19.0f), 15, row_label);
        }
    }
}

static void draw_signal_panel(const struct app *app, Rectangle rect) {
    const struct fm_view *fm = &app->fm;
    int locked = fm_pilot_locked(&fm->front.pilot);
    int y = draw_panel(rect, "Signal");
    char text[96];

    draw_row(rect, y, "pilot", locked ? "locked" : "no lock",
             locked ? row_good : row_weak);
    y += 20;
    if (fm->audio_error[0]) {
        draw_row(rect, y, "audio", fm->audio_error, row_weak);
        y += 20;
    } else if (fm->playing) {
        snprintf(text, sizeof(text), "%.0f Hz %s", fm_audio_rate(&fm->audio),
                 fm_audio_is_stereo(&fm->audio) ? "stereo" : "mono");
        draw_row(rect, y, "audio", text, row_good);
        y += 20;
    }
    /* Whether the station sends stereo, which is a fact about it rather than
       about the sound card, so it is worth saying even when nothing is
       playing. */
    draw_row(rect, y, "broadcast",
             fm_audio_is_stereo(&fm->audio) ? "stereo" : "mono",
             fm_audio_is_stereo(&fm->audio) ? row_good : row_value);
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

/*
 * The multiplex, as a spectrum.
 *
 * The chart that answers the question none of the panels can: whether this
 * station is carrying RDS at all. Three humps -- the pilot at 19 kHz, the
 * stereo subcarrier at 38, the RDS band at 57 -- and a station with the first
 * two and not the third is a perfectly ordinary station that simply is not
 * sending any. Every number on the signal panel reads the same for that as
 * for a station sending it too weakly to read.
 */
static void draw_multiplex_chart(const struct app *app, Rectangle rect) {
    const struct fm_view *fm = &app->fm;
    struct sdrgui_burst_chart_params params;
    char title[128];
    double top_khz = fm->spectrum_bins > 0
                         ? fm->spectrum_bin_hz * (double)fm->spectrum_bins /
                               1000.0
                         : 0.0;

    memset(&params, 0, sizeof(params));
    snprintf(title, sizeof(title),
             "multiplex 0-%.0f kHz: pilot 19, stereo 38, RDS 57", top_khz);
    params.plot = rect;
    params.data = fm->spectrum;
    params.count = (int)fm->spectrum_bins;
    params.type = SDRGUI_BURST_LINE;
    params.y_min = -70.0f;
    params.y_max = 2.0f;
    params.title = title;
    params.empty_notice = "no multiplex yet";
    sdrgui_burst_chart(&params);
}

/*
 * The symbols, as a constellation.
 *
 * Two lobes either side of the origin is a decode. One blob around it is a
 * subcarrier arriving and not resolving. A ring is an axis the estimator has
 * not settled on. All three read as small numbers on the panels, and only
 * this tells them apart.
 */
static void draw_constellation_chart(const struct app *app, Rectangle rect) {
    const struct fm_view *fm = &app->fm;
    static float points_i[512];
    static float points_q[512];
    struct sdrgui_constellation_params params;
    size_t count;

    count = fm_rds_symbols(fm->bb_i, fm->bb_q, fm->bb_count,
                           fm->timing_offset, points_i, points_q, 512);
    memset(&params, 0, sizeof(params));
    params.plot = rect;
    params.x = points_i;
    params.y = points_q;
    params.count = (int)count;
    params.caption = "RDS symbols, before the axis is chosen";
    params.empty_notice = "no symbols yet";
    sdrgui_constellation(&params);
}

/*
 * The timing search, all sixteen offsets.
 *
 * fm_rds_soft_bits takes the best and moves on. This shows how it won: a
 * clear peak is a symbol clock nobody need think about, and a winner barely
 * over its neighbours is one about to slip -- which every panel reports
 * identically right up until it does.
 */
static void draw_timing_chart(const struct app *app, Rectangle rect) {
    const struct fm_view *fm = &app->fm;
    struct sdrgui_burst_chart_params params;
    char title[96];

    memset(&params, 0, sizeof(params));
    snprintf(title, sizeof(title), "symbol timing: offset %d of %d wins",
             fm->timing_offset, FM_RDS_SAMPLES_PER_SYMBOL);
    params.plot = rect;
    params.data = fm->timing_energy;
    params.count = FM_RDS_SAMPLES_PER_SYMBOL;
    params.type = SDRGUI_BURST_BAR;
    params.y_min = 0.0f;
    params.y_max = 1.05f;
    params.title = title;
    params.empty_notice = "no symbols yet";
    sdrgui_burst_chart(&params);
}

/*
 * Which groups the station is sending.
 *
 * A station sending only type 0 has a name and no radio text, and that is a
 * fact about the station rather than a fault in the receiver -- which is
 * exactly what an empty radio-text line does not say.
 */
static void draw_groups_chart(const struct app *app, Rectangle rect) {
    const struct rds_station *s = &app->fm.station;
    static float counts[16];
    struct sdrgui_burst_chart_params params;
    int i;
    float top = 1.0f;

    for (i = 0; i < 16; i++) {
        counts[i] = (float)(s->groups_by_type[i * 2] +
                            s->groups_by_type[i * 2 + 1]);
        if (counts[i] > top)
            top = counts[i];
    }
    memset(&params, 0, sizeof(params));
    params.plot = rect;
    params.data = counts;
    params.count = 16;
    params.type = SDRGUI_BURST_BAR;
    params.y_min = 0.0f;
    /* A round number, not 1.15 times whatever the tallest bar is: the axis
       label's width sets the chart's left gutter, so an axis that follows the
       data makes the plot shift sideways every time the count crosses ten. */
    params.y_max = sdrgui_nice_ceiling(top);
    params.title = "groups by type: 0 carries the name, 2 the radio text";
    params.empty_notice = "no groups yet";
    sdrgui_burst_chart(&params);
}

/*
 * The sound.
 *
 * The device is opened on the first press rather than at startup: a machine
 * with no sound card should not have every run of this program complain about
 * it, and most runs of this program never ask for audio.
 */
/* In frames, not samples: the ring holds two entries per frame. */
static size_t audio_pending(const struct fm_view *fm) {
    return (fm->audio_tail - fm->audio_head) & (FM_AUDIO_RING - 1);
}

void fm_play(struct app *app) {
    struct fm_view *fm = &app->fm;

    if (fm->playing) {
        fm->playing = 0;
        if (fm->audio_ready)
            StopAudioStream(fm->audio_stream);
        fm->audio_head = fm->audio_tail = 0;
        debug_log_write("fm-audio", "stopped");
        return;
    }
    if (fm_audio_init(&fm->audio, (double)app->applied_sample_rate) < 0) {
        snprintf(app->fm.audio_error, sizeof(app->fm.audio_error),
                 "The sample rate is too low to carry audio.");
        return;
    }
    if (!fm->audio_ready) {
        InitAudioDevice();
        if (!IsAudioDeviceReady()) {
            snprintf(app->fm.audio_error, sizeof(app->fm.audio_error),
                     "No audio device.");
            debug_log_write("fm-audio", "no device");
            return;
        }
        SetAudioStreamBufferSizeDefault(FM_AUDIO_CHUNK);
        fm->audio_stream = LoadAudioStream((unsigned)fm_audio_rate(&fm->audio),
                                           16, 2);
        fm->audio_ready = 1;
    }
    fm->audio_error[0] = '\0';
    fm->audio_head = fm->audio_tail = 0;
    fm->playing = 1;
    PlayAudioStream(fm->audio_stream);
    debug_log_write("fm-audio", "playing at %.0f Hz",
                    fm_audio_rate(&fm->audio));
}

void fm_audio_close(struct app *app) {
    if (!app->fm.audio_ready)
        return;
    StopAudioStream(app->fm.audio_stream);
    UnloadAudioStream(app->fm.audio_stream);
    CloseAudioDevice();
    app->fm.audio_ready = 0;
    app->fm.playing = 0;
}

/*
 * Hand the sound card whatever has arrived.
 *
 * Called every frame rather than every block, because the card asks on its own
 * schedule and a block is four of its buffers. Nothing is pushed unless a
 * whole chunk is ready: a partial one played now is a click, and the sound is
 * already going to click whenever the acquisition slot drops a block.
 */
void update_fm_audio(struct app *app) {
    struct fm_view *fm = &app->fm;

    if (!fm->playing || !fm->audio_ready)
        return;
    while (IsAudioStreamProcessed(fm->audio_stream) &&
           audio_pending(fm) >= FM_AUDIO_CHUNK) {
        static int16_t chunk[FM_AUDIO_CHUNK * 2];
        size_t k;

        for (k = 0; k < FM_AUDIO_CHUNK; k++) {
            chunk[k * 2] = fm->audio_ring[fm->audio_head * 2];
            chunk[k * 2 + 1] = fm->audio_ring[fm->audio_head * 2 + 1];
            fm->audio_head = (fm->audio_head + 1) & (FM_AUDIO_RING - 1);
        }
        UpdateAudioStream(fm->audio_stream, chunk, FM_AUDIO_CHUNK);
    }
}

/*
 * Entering the view puts the receiver in the band.
 *
 * Without this the FM view opens on whatever the last screen was pointed at
 * -- 1090 MHz by default, which is ADS-B -- so the waterfall shows a megahertz
 * of nothing, the panels report no pilot, and the frequency field says 89.6
 * while the receiver is nine hundred megahertz away. A scan then "returns" to
 * 1090 when it finishes, which is correct by the convention every other scan
 * follows and useless here.
 *
 * Only when it is outside band II, so switching away and back does not throw
 * away a station the operator tuned by hand.
 */
void enter_fm(struct app *app) {
    if (!app->receiver_mode)
        return;
    /*
     * Only the first time. Switching tabs away and back would otherwise
     * throw away both the list and whichever station was being listened to,
     * and start half a minute of tuning nobody asked for.
     */
    if (app->fm.scan.found_count > 0 || app->fm.scan.running)
        return;
    fm_scan_begin(app);
}

/* Whether the scan list is on screen, which the layout needs to know before
   it can say where anything is. */
int fm_scan_showing(const struct app *app) {
    return app->fm.scan.running || app->fm.scan.found_count > 0;
}

/*
 * The sound, as a waveform.
 *
 * Whether the station is modulating at all, which nothing else on the screen
 * says: a carrier can be received perfectly, decode its RDS perfectly, and be
 * silent, and every other chart here reports that as success. A flat line is
 * dead air; a trace clipping against the edges is a level follower that has
 * not caught up with a station much louder than the last one.
 */
static void draw_audio_wave_chart(const struct app *app, Rectangle rect) {
    const struct fm_view *fm = &app->fm;
    struct sdrgui_burst_chart_params params;

    memset(&params, 0, sizeof(params));
    params.plot = rect;
    params.data = fm->audio_trace;
    params.count = (int)fm->audio_trace_count;
    params.type = SDRGUI_BURST_LINE;
    params.y_min = -1.0f;
    params.y_max = 1.0f;
    params.title = "audio: the last tenth of a second";
    params.empty_notice = "no audio yet";
    sdrgui_burst_chart(&params);
}

/*
 * And its spectrum, as it is heard.
 *
 * The multiplex chart carries this band at its left edge and not as a
 * listener gets it: de-emphasis has not been applied there and the pilot has
 * not been taken out, and those two are most of the difference between what a
 * transmitter sends and what a speaker produces. Speech rolling off by 4 kHz
 * and music carrying to 15 are different pictures, and a station that is
 * modulating but whose audio is all below 300 Hz is a fault this is the only
 * chart to show.
 */
static void draw_audio_spectrum_chart(const struct app *app, Rectangle rect) {
    const struct fm_view *fm = &app->fm;
    struct sdrgui_burst_chart_params params;
    char title[96];
    double top_khz = fm->audio_spectrum_bins > 0
                         ? fm->audio_spectrum_bin_hz *
                               (double)fm->audio_spectrum_bins / 1000.0
                         : 0.0;

    memset(&params, 0, sizeof(params));
    snprintf(title, sizeof(title), "audio spectrum, 0-%.0f kHz, de-emphasised",
             top_khz);
    params.plot = rect;
    params.data = fm->audio_spectrum;
    params.count = (int)fm->audio_spectrum_bins;
    params.type = SDRGUI_BURST_LINE;
    params.y_min = -70.0f;
    params.y_max = 2.0f;
    params.title = title;
    params.empty_notice = "no audio yet";
    sdrgui_burst_chart(&params);
}

void draw_fm(struct app *app) {
    struct fm_layout l = fm_layout_now(fm_scan_showing(app));

    GuiLabel((Rectangle){ l.frequency_field.x, l.frequency_field.y - 18.0f,
                          120.0f, 16.0f }, "MHz");
    sdrgui_text_field(l.frequency_field, app->fm.frequency, 1);
    draw_button(l.tune_button, "Tune", 0);

    draw_button(l.play_button, app->fm.playing ? "Stop" : "Play",
                app->fm.playing);
    draw_button(l.scan_button,
                app->fm.scan.running ? "Stop" : "Scan band",
                app->fm.scan.running);
    draw_button(l.view_toggle,
                app->fm.analysis_mode ? "View: Signal" : "View: Charts", 0);

    if (app->fm.analysis_mode) {
        /* Top row: the signal, from the air inwards. Bottom row: what came
           out of it -- the symbols, the groups, and the sound. */
        draw_multiplex_chart(app, l.chart[0]);
        draw_audio_wave_chart(app, l.chart[1]);
        draw_audio_spectrum_chart(app, l.chart[2]);
        draw_constellation_chart(app, l.chart[3]);
        draw_timing_chart(app, l.chart[4]);
        draw_groups_chart(app, l.chart[5]);
        return;
    }

    if (fm_scan_showing(app))
        draw_scan_list(app, l.scan_list);
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
    struct fm_layout l = fm_layout_now(fm_scan_showing(app));
    int character;

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

    if (clicked(l.play_button)) {
        fm_play(app);
        return;
    }
    if (clicked(l.view_toggle)) {
        app->fm.analysis_mode = !app->fm.analysis_mode;
        return;
    }
    if (clicked(l.scan_button)) {
        if (app->fm.scan.running)
            fm_scan_stop(app);
        else
            fm_scan_begin(app);
        return;
    }
    /*
     * The list: wheel to scroll, click to listen.
     *
     * Reachable while the scan is still running, because the whole point of
     * watching a band walk is stopping it when something interesting turns
     * up -- and a list you can only use once it has finished is a list you
     * wait for.
     */
    if (fm_scan_showing(app)) {
        struct row_list_metrics m = FM_SCAN_LIST_METRICS;
        struct fm_scan *scan = &app->fm.scan;
        int fits = row_list_rows(l.scan_list, m);
        float wheel = GetMouseWheelMove();

        scan->list_scroll = row_list_clamp_scroll(scan->list_scroll,
                                                  scan->found_count, fits);
        if (wheel != 0.0f &&
            CheckCollisionPointRec(GetMousePosition(), l.scan_list)) {
            scan->list_scroll = row_list_clamp_scroll(
                scan->list_scroll - (int)wheel * ROW_LIST_WHEEL_ROWS,
                scan->found_count, fits);
            return;
        }
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN) ||
            IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
            /* Up and Down are the waterfall's scale on this screen, and the
               list has the wheel and the pointer. Left here as a comment
               rather than a binding so the next reader does not add one and
               take the scale keys away again. */
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            int rank = row_list_rank_at(l.scan_list, m, scan->list_scroll,
                                        scan->found_count, fits,
                                        GetMousePosition());
            if (rank >= 0) {
                /* Choosing one stops the walk: the receiver cannot be in two
                   places, and carrying on would tune away from the station
                   just asked for. */
                if (scan->running)
                    fm_scan_stop(app);
                snprintf(app->fm.frequency, sizeof(app->fm.frequency), "%.1f",
                         scan->found[rank].frequency_hz / 1e6);
                app->fm.frequency_length = (int)strlen(app->fm.frequency);
                fm_tune(app, scan->found[rank].frequency_hz);
                return;
            }
        }
    }
    if (app->fm.scan.running)
        return;   /* the receiver is walking the band; leave it there */

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

/*
 * Walking band II.
 *
 * Two passes, and fm_scan.h has the arithmetic and the argument. The first
 * sweeps the band as a spectrum -- thirteen tunings, under three seconds --
 * and marks every channel standing over the local floor. The second visits
 * only those, for the three quarters of a second a pilot needs plus enough
 * for an identification.
 *
 * It does not try to read a *name* while scanning. Four segments arriving and
 * repeating is a second or two of groups on a good signal and never on a
 * marginal one, so a scan that waited for names would spend most of its time
 * on the stations it was never going to read. The scan says where to stop;
 * stopping is what reads the name.
 */
void fm_scan_begin(struct app *app) {
    struct fm_scan *scan = &app->fm.scan;
    int i;

    if (!app->receiver_mode) {
        snprintf(scan->status, sizeof(scan->status),
                 "A band scan needs a live receiver.");
        return;
    }
    if (fm_scan_plan_for((double)app->applied_sample_rate, &scan->plan) !=
        FM_SCAN_OK) {
        snprintf(scan->status, sizeof(scan->status),
                 "The sample rate is too low to sweep the band.");
        return;
    }
    for (i = 0; i < (int)(sizeof(scan->power) / sizeof(scan->power[0])); i++)
        scan->power[i] = FM_SCAN_SENTINEL_DBFS;
    scan->found_count = 0;
    scan->visiting = 0;
    scan->step = 0;
    scan->sweeping = 1;
    scan->return_frequency = app->applied_frequency;
    scan->return_valid = 1;
    if (retune_receiver(app, (uint32_t)llround(scan->plan.first_center_hz),
                        app->applied_ppm) < 0) {
        scan->sweeping = 0;
        return;
    }
    scan->step_started_at = GetTime();
    scan->running = 1;
    snprintf(scan->status, sizeof(scan->status),
             "Sweeping %d steps, about %.0f s, then the carriers it finds",
             scan->plan.step_count, fm_scan_sweep_seconds(&scan->plan));
    debug_log_write("fm-scan", "begin, %d steps", scan->plan.step_count);
}

void fm_scan_stop(struct app *app) {
    struct fm_scan *scan = &app->fm.scan;

    if (!scan->running)
        return;
    scan->running = 0;
    scan->sweeping = 0;
    if (app->receiver_mode && scan->return_valid)
        retune_receiver(app, scan->return_frequency, app->applied_ppm);
    snprintf(scan->status, sizeof(scan->status),
             "Stopped; %d carrier%s found.", scan->found_count,
             scan->found_count == 1 ? "" : "s");
    debug_log_write("fm-scan", "stopped, %d found", scan->found_count);
}

/* Turn the swept powers into the list of channels worth visiting. */
static void fm_scan_choose(struct app *app) {
    struct fm_scan *scan = &app->fm.scan;
    int count = fm_scan_channel_count();
    double floor_dbfs;
    int i, measured = 0;

    /* The floor is the median of what was measured, so a band with a dozen
       strong stations in it does not raise its own threshold out of reach of
       the weak ones -- which a mean would. */
    {
        static float sorted[206];
        for (i = 0; i < count; i++)
            if (scan->power[i] > FM_SCAN_SENTINEL_DBFS)
                sorted[measured++] = scan->power[i];
        if (measured == 0) {
            snprintf(scan->status, sizeof(scan->status),
                     "The sweep measured nothing.");
            return;
        }
        {
            int a, b;
            for (a = 1; a < measured; a++) {
                float key = sorted[a];
                for (b = a - 1; b >= 0 && sorted[b] > key; b--)
                    sorted[b + 1] = sorted[b];
                sorted[b + 1] = key;
            }
        }
        floor_dbfs = sorted[measured / 2];
    }

    for (i = 0; i < count && scan->found_count < FM_SCAN_MAX_FOUND; i++) {
        struct fm_found_station *f;

        if (!fm_scan_is_carrier(scan->power[i], floor_dbfs))
            continue;
        /*
         * One entry per carrier, not per channel over the threshold. An FM
         * carrier is 200 kHz wide on a 100 kHz raster, so a strong station
         * puts its neighbours over the floor too and a list built channel by
         * channel reports every station two or three times.
         */
        if (scan->found_count > 0) {
            f = &scan->found[scan->found_count - 1];
            if (i - f->channel <= 2) {
                if (scan->power[i] > f->power_dbfs) {
                    f->channel = i;
                    f->frequency_hz = fm_scan_channel_hz(i);
                    f->power_dbfs = scan->power[i];
                }
                continue;
            }
        }
        f = &scan->found[scan->found_count++];
        memset(f, 0, sizeof(*f));
        f->channel = i;
        f->frequency_hz = fm_scan_channel_hz(i);
        f->power_dbfs = scan->power[i];
    }
    snprintf(scan->status, sizeof(scan->status),
             "%d carrier%s over the floor; visiting each for %.1f s",
             scan->found_count, scan->found_count == 1 ? "" : "s",
             FM_SCAN_VISIT_SECONDS);
    debug_log_write("fm-scan", "swept, %d carriers, floor %.1f dBFS",
                    scan->found_count, floor_dbfs);
}

void update_fm_scan(struct app *app, double now, int have_block) {
    struct fm_scan *scan = &app->fm.scan;
    double elapsed;

    if (!scan->running)
        return;
    elapsed = now - scan->step_started_at;

    if (scan->sweeping) {
        if (elapsed < FM_SCAN_STEP_SETTLE_SECONDS || !have_block)
            return;
        if (elapsed < FM_SCAN_STEP_SETTLE_SECONDS +
                      FM_SCAN_STEP_MEASURE_SECONDS) {
            /* Measure into the channel grid while the step is still open, so
               a step contributes every block it saw rather than only its
               last. */
            double centre = (double)app->applied_frequency;
            sdr_dsp_channel_powers(app->spectrum_average, SDR_DSP_FFT_SIZE,
                                   centre - app->applied_sample_rate / 2.0,
                                   centre + app->applied_sample_rate / 2.0,
                                   centre - scan->plan.accept_half_hz,
                                   centre + scan->plan.accept_half_hz,
                                   FM_BAND_LOWER_HZ, FM_CHANNEL_SPACING_HZ,
                                   0, fm_scan_channel_count() - 1,
                                   scan->power);
            return;
        }
        scan->step++;
        if (scan->step >= scan->plan.step_count) {
            scan->sweeping = 0;
            fm_scan_choose(app);
            if (scan->found_count == 0) {
                fm_scan_stop(app);
                return;
            }
            scan->visiting = 0;
            fm_tune(app, scan->found[0].frequency_hz);
            scan->step_started_at = now;
            return;
        }
        retune_receiver(app,
                        (uint32_t)llround(scan->plan.first_center_hz +
                                          (double)scan->step *
                                              scan->plan.step_hz),
                        app->applied_ppm);
        scan->step_started_at = now;
        return;
    }

    /* Pass two: one carrier at a time, reading whatever arrives. */
    if (elapsed < FM_SCAN_VISIT_SETTLE_SECONDS)
        return;
    if (elapsed < FM_SCAN_VISIT_SETTLE_SECONDS + FM_SCAN_VISIT_SECONDS) {
        if (have_block)
            update_fm(app, now);
        return;
    }
    {
        struct fm_found_station *f = &scan->found[scan->visiting];
        const struct rds_station *s = &app->fm.station;

        f->pilot = fm_pilot_locked(&app->fm.front.pilot);
        f->rds = s->funnel.groups > 0;
        f->pi_valid = s->pi_valid;
        f->pi = s->pi;
        if (s->ps_valid)
            snprintf(f->ps, sizeof(f->ps), "%s", s->ps);
        debug_log_write("fm-scan", "%.1f MHz pilot %d rds %d pi 0x%04X",
                        f->frequency_hz / 1e6, f->pilot, f->rds, f->pi);

        scan->visiting++;
        if (scan->visiting >= scan->found_count) {
            int with_rds = 0, i, best = 0;

            for (i = 0; i < scan->found_count; i++) {
                if (scan->found[i].rds)
                    with_rds++;
                if (scan->found[i].power_dbfs >
                    scan->found[best].power_dbfs)
                    best = i;
            }
            scan->running = 0;
            /*
             * It ends on the loudest station rather than back where it
             * started. Every other scan here restores the tuning it borrowed,
             * because the operator was listening to something and asked a
             * question about the band; this one *is* how a station gets
             * chosen, so returning would put the receiver on whatever the
             * previous screen happened to be pointed at -- 1090 MHz on a
             * fresh start, which is not in the band at all.
             */
            fm_tune(app, scan->found[best].frequency_hz);
            snprintf(app->fm.frequency, sizeof(app->fm.frequency), "%.1f",
                     scan->found[best].frequency_hz / 1e6);
            app->fm.frequency_length = (int)strlen(app->fm.frequency);
            snprintf(scan->status, sizeof(scan->status),
                     "%d carrier%s, %d carrying RDS. Listening to the "
                     "strongest.", scan->found_count,
                     scan->found_count == 1 ? "" : "s", with_rds);
            debug_log_write("fm-scan", "done, %d found, tuned %.1f MHz",
                            scan->found_count,
                            scan->found[best].frequency_hz / 1e6);
            return;
        }
        fm_tune(app, scan->found[scan->visiting].frequency_hz);
        scan->step_started_at = now;
        snprintf(scan->status, sizeof(scan->status),
                 "Visiting %d of %d: %.1f MHz", scan->visiting + 1,
                 scan->found_count,
                 scan->found[scan->visiting].frequency_hz / 1e6);
    }
}
