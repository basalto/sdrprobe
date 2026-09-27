#include <math.h>
#include <stdio.h>
#include <string.h>

#include <raylib.h>

#include "runtime/app.h"
#include "tech/fm_scan.h"
#include "gui/fm_layout.h"
#include "model/fm_view_model.h"
#include "gui/panel_rows.h"
#include "gui/sdrgui.h"
#include "gui/view.h"
#include "runtime/debug_log.h"
#include "core/sdr_dsp.h"
#include "gui/row_list.h"
#include "gui/text_wrap.h"

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


/*
 * One block of samples through the chain.
 *
 * The front end is rebuilt only when the sample rate changes under it, since
 * every rate it derives -- the loop gains, the emission clock -- comes from
 * that number and a stale one puts the pilot 450 Hz from where the loop is
 * looking, which a 10 Hz loop will never find.
 */

static int draw_panel(Rectangle rect, const char *caption) {
    DrawRectangleRec(rect, (Color){ 17, 26, 37, 255 });
    DrawRectangleLinesEx(rect, 1.0f, panel_edge);
    sdrgui_text_fit(caption, (int)rect.x + 12, (int)rect.y + 10, 16,
                    rect.width - 24.0f, panel_caption);
    return (int)rect.y + 36;
}

/*
 * A row at an index, positioned by panel_rows.h rather than by counting
 * pixels between calls. A row past the panel's capacity is not drawn: this
 * panel wants eight rows and holds four on a 640x400 window, and the four it
 * could not fit used to be drawn off the bottom edge.
 */
static void draw_row_at(const struct panel_rows *rows, int index,
                        const char *label, const char *value, Color colour) {
    int y;

    if (!panel_row_visible(rows, index))
        return;
    y = (int)panel_row_y(rows, index);
    sdrgui_text_fit(label, (int)rows->label_x, y, 15, rows->label_width,
                    row_label);
    sdrgui_text_fit(value, (int)rows->value_x, y, 15, rows->value_width,
                    colour);
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
    /*
     * "Broadcast" rather than "pilot", and they are the same measurement: a
     * 19 kHz pilot is transmitted for one reason, which is to tell a receiver
     * that a difference signal is there. A column headed PILOT asks the
     * reader to know that; one headed BROADCAST tells them what it means.
     */
    DrawText("   MHz       LEVEL   BROADCAST  RDS  STATION",
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
            if (fabs((double)app->applied.frequency_hz - f->frequency_hz) < 50000.0)
                DrawRectangle((int)rect.x + 4, (int)row_y - 2,
                              (int)rect.width - 8, (int)m.row_h,
                              (Color){ 99, 228, 170, 34 });

            if (f->ps[0])
                snprintf(name, sizeof(name), "%s", f->ps);
            else if (f->pi_valid)
                snprintf(name, sizeof(name), "0x%04X", f->pi);
            else
                snprintf(name, sizeof(name), "--");
            snprintf(row, sizeof(row), "%8.1f  %6.1f dBFS  %-9s %-4s %s",
                     f->frequency_hz / 1e6, (double)f->power_dbfs,
                     f->stereo ? "stereo" : "mono",
                     f->rds ? "yes" : "no", name);
            colour = f->ps[0] ? row_good : f->stereo ? row_value : row_label;
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

static void draw_signal_panel(const struct fm_view_model *m, Rectangle rect) {
    /* 128 pixels of label gutter was a constant here; as a fraction with the
       same cap it holds on a wide panel and gives the value room on a narrow
       one, where these values are the part worth reading. */
    struct panel_rows rows = panel_rows_for(rect, FM_PANEL_CAPTION_DROP,
                                            FM_PANEL_ROW_HEIGHT, 0.0f,
                                            0.50f, 128.0f);
    char text[96];
    int r = 0;

    draw_panel(rect, "Signal");
    /*
     * Ordered so a short panel keeps what says whether anything is being
     * received at all. The rows below the lock are how well, and a panel with
     * room for four should spend them on whether rather than how well.
     */
    draw_row_at(&rows, r++, "pilot", m->pilot_locked ? "locked" : "no lock",
                m->pilot_locked ? row_good : row_weak);
    if (m->audio_error[0]) {
        draw_row_at(&rows, r++, "audio", m->audio_error, row_weak);
    } else if (m->playing) {
        snprintf(text, sizeof(text), "%.0f Hz %s", m->audio_rate_hz,
                 m->broadcast_stereo ? "stereo" : "mono");
        draw_row_at(&rows, r++, "audio", text, row_good);
    }
    /* Whether the station sends stereo, which is a fact about it rather than
       about the sound card, so it is worth saying even when nothing is
       playing. */
    draw_row_at(&rows, r++, "broadcast",
                m->broadcast_stereo ? "stereo" : "mono",
                m->broadcast_stereo ? row_good : row_value);
    if (m->pilot_locked) {
        snprintf(text, sizeof(text), "%.2f Hz", m->pilot_hz);
        draw_row_at(&rows, r++, "at", text, row_value);
        /* Not "sample clock": five stations here read between +2 and -57 ppm
           on one receiver, so this is the transmitter's pilot far more than
           it is this receiver's clock. */
        snprintf(text, sizeof(text), "%+.1f ppm", m->pilot_ppm);
        draw_row_at(&rows, r++, "pilot offset", text, row_value);
    }
    {
        snprintf(text, sizeof(text), "%.2f", m->pilot_coherence);
        draw_row_at(&rows, r++, "coherence", text,
                    m->pilot_coherence >= FM_PILOT_MIN_COHERENCE ? row_good
                                                                 : row_weak);
    }
    if (m->pilot_locked) {
        snprintf(text, sizeof(text), "%d/%d", m->timing_offset,
                 m->timing_samples_per_symbol);
        draw_row_at(&rows, r++, "symbol timing", text, row_value);
        snprintf(text, sizeof(text), "%+.2f rad", m->axis_radians);
        draw_row_at(&rows, r++, "subcarrier axis", text, row_value);
    }
}

static void draw_station_panel(const struct fm_view_model *m, Rectangle rect) {
    struct panel_rows rows = panel_rows_for(rect, FM_PANEL_CAPTION_DROP,
                                            FM_PANEL_ROW_HEIGHT, 0.0f,
                                            0.50f, 128.0f);
    char text[96];
    int r = 0;

    draw_panel(rect, "Station");
    /* The identity first: a panel with room for four rows should spend them
       on which station this is rather than on how sure the decoder is. */
    if (m->pi_valid) {
        snprintf(text, sizeof(text), "0x%04X", m->pi);
        draw_row_at(&rows, r++, "identification", text, row_value);
        snprintf(text, sizeof(text), "%d agreeing", m->pi_repeats);
        draw_row_at(&rows, r++, "", text, row_label);
    } else {
        draw_row_at(&rows, r++, "identification", "--", row_label);
    }

    /*
     * The name is shown only when it is whole and has repeated. A programme
     * service name arrives two characters at a time, so anything else puts a
     * station that does not exist on the screen -- and a reader has no way to
     * tell a half-arrived name from a short one.
     */
    if (m->ps_valid) {
        draw_row_at(&rows, r++, "name", m->ps, row_good);
    } else if (m->ps_segments) {
        snprintf(text, sizeof(text), "%d of 4 segments", m->ps_segments);
        draw_row_at(&rows, r++, "name", text, row_weak);
    } else {
        draw_row_at(&rows, r++, "name", "--", row_label);
    }

    if (m->pty_valid) {
        draw_row_at(&rows, r++, "programme type", m->pty_name, row_value);
        draw_row_at(&rows, r++, "", m->traffic,
                    m->tp && m->ta ? row_weak : row_label);
    }
    /*
     * Radio text, wrapped rather than ellipsised.
     *
     * Sixty-four characters into a panel a third of the window wide: handing
     * the whole of it to sdrgui_text_fit cut it at "Cultura em
     * antena2.rtp...", which is exactly where the useful part begins. How
     * many characters fit is a font question and belongs here; where the
     * breaks go is arithmetic and lives in text_wrap.h.
     */
    if (m->rt_valid) {
        struct text_wrap_line lines[4];
        float room = rect.width - 24.0f;
        int columns = (int)(room / (float)MeasureText("n", 15));
        int count, i;
        /* Radio text follows the rows rather than sitting at a fixed offset,
           and its own lines are 18 apart rather than 20 -- close enough that
           the panel reads as one column, tight enough that a four-line
           message fits under the fields. */
        int y = (int)panel_footer_after(&rows, r);

        if (!panel_row_visible(&rows, r))
            return;
        draw_row_at(&rows, r++, "radio text", "", row_label);
        y = (int)panel_row_y(&rows, r - 1) + 18;
        if (columns < 8)
            columns = 8;
        count = text_wrap(m->rt, columns, lines,
                          (int)(sizeof(lines) / sizeof(lines[0])));
        for (i = 0; i < count; i++) {
            char line[80];
            int length = lines[i].length;

            if (length > (int)sizeof(line) - 1)
                length = (int)sizeof(line) - 1;
            memcpy(line, m->rt + lines[i].start, (size_t)length);
            line[length] = '\0';
            /* And stop at the panel's floor, which is what the rows above
               now respect and this used to run past. */
            if ((float)y + 18.0f > rect.y + rect.height)
                break;
            sdrgui_text_fit(line, (int)rect.x + 12, y, 15, room, row_value);
            y += 18;
        }
    }
}

/*
 * Where the decode stopped.
 *
 * Two empty panels look the same whether nothing is transmitting or every
 * block is failing its syndrome, and that difference is the whole diagnosis.
 * The LTE view had to learn this; there is no reason for this one to learn it
 * again.
 */
/* The three emphases the sentence below is painted in, which the view model
   decides between (`enum fm_reading_tone`) so that a second reader of the
   same five counts cannot reach a different verdict about them. This is the
   only thing left here that the tone chooses: which colour a decided verdict
   is drawn in, which is presentation and belongs in a drawing. */
static Color fm_reading_color(enum fm_reading_tone tone) {
    switch (tone) {
    case FM_READING_GOOD:
        return row_good;
    case FM_READING_WEAK:
        return row_weak;
    case FM_READING_NEUTRAL:
    default:
        return row_label;
    }
}

static void draw_funnel_panel(const struct fm_view_model *m, Rectangle rect) {
    int y = draw_panel(rect, "Where the decode stopped");
    char text[96];

    snprintf(text, sizeof(text), "%ld", m->bits);
    draw_row(rect, y, "soft bits", text, row_value);
    y += 20;
    snprintf(text, sizeof(text), "%ld", m->blocks_matched);
    draw_row(rect, y, "blocks", text, m->blocks_matched ? row_value : row_weak);
    y += 20;
    snprintf(text, sizeof(text), "%ld", m->groups);
    draw_row(rect, y, "groups", text, m->groups ? row_value : row_weak);
    y += 20;
    snprintf(text, sizeof(text), "%ld", m->identified);
    draw_row(rect, y, "identified", text, m->identified ? row_value : row_weak);
    y += 20;
    snprintf(text, sizeof(text), "%ld", m->named);
    draw_row(rect, y, "named", text, m->named ? row_good : row_weak);
    y += 24;

    /* The reading, in words. A count is only useful to somebody who already
       knows what it should be -- and which of the five sentences it is was
       decided in `fm_view_model.c`, where a check can reach it, rather than
       here where only a person looking could (ADR-0012). */
    sdrgui_text_fit(m->reading, (int)rect.x + 12, y, 15, rect.width - 24.0f,
                    fm_reading_color(m->reading_tone));
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

    count = fm_rds_symbols(fm->session.bb_i, fm->session.bb_q, fm->session.bb_count,
                           fm->session.timing_offset, points_i, points_q, 512);
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
             fm->session.timing_offset, FM_RDS_SAMPLES_PER_SYMBOL);
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
    const struct rds_station *s = &app->fm.session.station;
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
            StopAudioStream(app->gui->fm_audio_stream);
        fm->audio_head = fm->audio_tail = 0;
        debug_log_write("fm-audio", "stopped");
        return;
    }
    if (fm_audio_init(&fm->audio, (double)app->applied.sample_rate_hz) < 0) {
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
        app->gui->fm_audio_stream = LoadAudioStream((unsigned)fm_audio_rate(&fm->audio),
                                           16, 2);
        fm->audio_ready = 1;
    }
    fm->audio_error[0] = '\0';
    fm->audio_head = fm->audio_tail = 0;
    fm->playing = 1;
    PlayAudioStream(app->gui->fm_audio_stream);
    debug_log_write("fm-audio", "playing at %.0f Hz",
                    fm_audio_rate(&fm->audio));
}

void fm_audio_close(struct app *app) {
    if (!app->fm.audio_ready)
        return;
    StopAudioStream(app->gui->fm_audio_stream);
    UnloadAudioStream(app->gui->fm_audio_stream);
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
    while (IsAudioStreamProcessed(app->gui->fm_audio_stream) &&
           audio_pending(fm) >= FM_AUDIO_CHUNK) {
        static int16_t chunk[FM_AUDIO_CHUNK * 2];
        size_t k;

        for (k = 0; k < FM_AUDIO_CHUNK; k++) {
            chunk[k * 2] = fm->audio_ring[fm->audio_head * 2];
            chunk[k * 2 + 1] = fm->audio_ring[fm->audio_head * 2 + 1];
            fm->audio_head = (fm->audio_head + 1) & (FM_AUDIO_RING - 1);
        }
        UpdateAudioStream(app->gui->fm_audio_stream, chunk, FM_AUDIO_CHUNK);
    }
}


/* Whether the scan list is on screen, which the layout needs to know before
   it can say where anything is. */

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
    /*
     * One model, built once, read by all three panels -- rather than each
     * panel reaching into `struct app` for its own copy of the same fields.
     * About 4 KB of stack, almost all of it the multiplex spectrum; it is a
     * frame's worth of drawing data, not a `struct app`, and the two lessons
     * this repository has about stack locals (`struct app` at ~9 MB,
     * `struct viewer_link` at ~12) are three orders of magnitude away.
     *
     * The charts below still read `app->fm` directly. They are the analysis
     * arrangement's own drawing detail and have no second reader yet; the
     * multiplex the model does carry is there for the one that is coming.
     */
    struct fm_view_model model;

    fm_view_model_build(&app->fm, &model);

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
                app->fm.analysis_mode ? "Show signal" : "Show charts", 0);

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

    struct sdrgui_waterfall_marker fm_markers[FM_SCAN_MAX_FOUND];
    int fm_mcnt = 0;
    for (int k = 0; k < app->fm.scan.found_count && k < FM_SCAN_MAX_FOUND; k++) {
        fm_markers[k].frequency_hz = (double)app->fm.scan.found[k].frequency_hz;
        fm_markers[k].bandwidth_hz = 150000.0;
        fm_markers[k].age_seconds = 0.0;
        fm_markers[k].duration_seconds = 0.0;
        fm_markers[k].id = k;
        fm_markers[k].highlighted = (app->fm.scan.found[k].frequency_hz == app->applied.frequency_hz);
        fm_markers[k].color = (Color){ 80, 220, 240, 200 };
        fm_markers[k].label = app->fm.scan.found[k].ps[0] ? app->fm.scan.found[k].ps : "FM";
        fm_mcnt++;
    }
    draw_waterfall_rect_with_markers(app, 0, l.waterfall, &app->fm.window,
                                     fm_markers, fm_mcnt, NULL);
    draw_signal_panel(&model, l.signal_panel);
    draw_station_panel(&model, l.station_panel);
    draw_funnel_panel(&model, l.funnel_panel);
}

/* Whether the frequency field is taking keystrokes. The frame loop asks so
   the digits do not also reach the view switcher. */

/* The scan list shares the row with the waterfall, so which rectangle the
   waterfall gets depends on whether the list is showing. */
Rectangle fm_waterfall_rect(const struct app *app) {
    return fm_layout_now(fm_scan_showing(app)).waterfall;
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
            set_tab(app, TAB_SCOPE, GetTime());
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
            fm_scan_begin(app, GetTime());
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







