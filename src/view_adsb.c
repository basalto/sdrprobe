#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "view.h"
#include "adsb_layout.h"
#include "sdrgui.h"

/*
 * The Decode tab's Mode S / ADS-B screen: the message log, and an analysis
 * mode that draws one frame's trace.
 *
 * The log says what decoded. The analysis mode says what did not and why --
 * where the frame was found, how far each pulse-position bit stood from its
 * decision boundary, and the magnitudes behind both -- which is the difference
 * between "nothing is transmitting" and "frames are arriving and failing".
 */

/* The decisions below are in adsb_analysis.h; these hand it what it needs out
   of struct app. */
int adsb_tuned(const struct app *app) {
    return adsb_receiver_ready(app->applied_frequency,
                               app->applied_sample_rate, DEFAULT_FREQUENCY);
}

static struct adsb_layout adsb_layout_now(void) {
    return adsb_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

/* Analysis panels are worth drawing only when Mode S could actually be there;
   off 1090 MHz the retune affordance is the useful thing to show, not three
   empty charts. */
static int adsb_analysis_showing(const struct app *app) {
    return adsb_analysis_visible(app->adsb.analysis_mode, adsb_tuned(app));
}

static void adsb_format(const struct adsb_message *msg,
                        struct adsb_log_entry *entry, double now) {
    memset(entry, 0, sizeof(*entry));
    time_t wall = time(NULL);
    struct tm local;
    localtime_r(&wall, &local);
    strftime(entry->stamp, sizeof(entry->stamp), "%H:%M:%S", &local);
    snprintf(entry->icao, sizeof(entry->icao), "%06X", msg->icao);
    int pos = 0;
    for (int b = 0; b < msg->byte_count &&
                    pos + 2 < (int)sizeof(entry->raw); b++)
        pos += snprintf(entry->raw + pos, sizeof(entry->raw) - (size_t)pos,
                        "%02X", msg->bytes[b]);
    entry->time = now;
    entry->highlight = 1;
    switch (msg->kind) {
    case ADSB_KIND_IDENTIFICATION:
        snprintf(entry->label, sizeof(entry->label), "ID");
        snprintf(entry->detail, sizeof(entry->detail), "callsign %s",
                 msg->callsign);
        break;
    case ADSB_KIND_AIRBORNE_POSITION:
        snprintf(entry->label, sizeof(entry->label), "POS");
        if (msg->has_position)
            snprintf(entry->detail, sizeof(entry->detail),
                     "lat %.4f  lon %.4f  alt %d ft", msg->latitude_deg,
                     msg->longitude_deg, msg->altitude_ft);
        else if (msg->has_altitude)
            snprintf(entry->detail, sizeof(entry->detail),
                     "alt %d ft  (awaiting %s frame)", msg->altitude_ft,
                     msg->cpr_odd ? "even" : "odd");
        else
            snprintf(entry->detail, sizeof(entry->detail),
                     "position (awaiting pair)");
        break;
    case ADSB_KIND_VELOCITY:
        snprintf(entry->label, sizeof(entry->label), "VEL");
        snprintf(entry->detail, sizeof(entry->detail),
                 "%.0f kt  heading %.0f deg", msg->ground_speed_kt,
                 msg->heading_deg);
        break;
    default:
        snprintf(entry->label, sizeof(entry->label), "MSG");
        snprintf(entry->detail, sizeof(entry->detail), "DF%d TC%d",
                 msg->downlink_format, msg->type_code);
        break;
    }
}

void update_adsb(struct app *app, double now) {
    if (!app->have_samples || app->pair_count == 0)
        return;
    struct adsb_frame_trace trace;
    struct adsb_demod_stats stats;
    memset(&trace, 0, sizeof(trace));
    size_t count = adsb_demod(&app->adsb.decoder, app->magnitudes,
                              app->pair_count, now, app->adsb.scratch,
                              sizeof(app->adsb.scratch) /
                                  sizeof(app->adsb.scratch[0]),
                              &trace, &stats);
    app->adsb.block_stats = stats;
    adsb_totals_add(&app->adsb.totals, &stats);
    adsb_trace_keep(&app->adsb.trace, &app->adsb.good_trace, &trace);
    size_t emitted = count;
    size_t capacity = sizeof(app->adsb.scratch) / sizeof(app->adsb.scratch[0]);
    if (emitted > capacity)
        emitted = capacity;
    /* Fade the previous rows' highlight before adding new ones. */
    adsb_log_fade(app->adsb.log, app->adsb.log_count);
    for (size_t i = 0; i < emitted; i++) {
        struct adsb_log_entry entry;
        adsb_format(&app->adsb.scratch[i], &entry, now);
        adsb_log_push(app->adsb.log, &app->adsb.log_count, &entry);
        app->adsb.frames_total++;
        if (app->adsb.scratch[i].has_position)
            app->adsb.positions_total++;
    }
}

void handle_adsb_input(struct app *app) {
    struct adsb_layout l = adsb_layout_now();

    if (IsKeyPressed(KEY_ESCAPE)) {
        set_tab(app, TAB_SCOPE);
        return;
    }
    if (clicked(l.view_toggle)) {
        app->adsb.analysis_mode = !app->adsb.analysis_mode;
        return;
    }
    if (clicked(l.record_button)) {
        /* No channel and no carrier offset: Mode S sits on 1090 MHz itself,
           so the recorded centre is the carrier. */
        start_capture_record(app, "adsb", "adsb", 0, 0.0,
                             ACQUISITION_RECORD_BUTTON_SECONDS);
        return;
    }
    if (adsb_analysis_showing(app) && clicked(l.hold_button)) {
        app->adsb.hold_last_good = !app->adsb.hold_last_good;
        return;
    }
    if (!adsb_tuned(app) && app->receiver_mode &&
        clicked(l.retune_button)) {
        retune_receiver(app, DEFAULT_FREQUENCY, app->applied_ppm);
    }
}

/* The trace the charts draw: the most recent attempt, or the last frame that
   passed its CRC when the reader has pinned that instead. */
static const struct adsb_frame_trace *adsb_shown_trace(const struct app *app) {
    return adsb_trace_shown(&app->adsb.trace, &app->adsb.good_trace,
                            app->adsb.hold_last_good);
}

/* One line above the charts saying which frame they are showing. A frame that
   failed its CRC has no address to print -- those bits are noise that landed
   in the address field -- so the outcome is all this line claims. */
static void draw_trace_caption(const struct app *app, Rectangle first_chart,
                               const struct adsb_frame_trace *trace,
                               int recording, const char *record_path,
                               uint64_t record_bytes) {
    char text[400];
    int y = (int)first_chart.y - 22;

    if (recording) {
        snprintf(text, sizeof(text), "Recording raw I/Q to %s  (%.1f MB)",
                 record_path, record_bytes / 1e6);
        DrawText(text, (int)first_chart.x, y, 17,
                 (Color){ 255, 202, 105, 255 });
        return;
    }
    if (!trace->valid) {
        DrawText("No Mode S frame traced yet", (int)first_chart.x, y, 17,
                 (Color){ 151, 174, 188, 255 });
        return;
    }
    const char *held = app->adsb.hold_last_good && app->adsb.good_trace.valid
                           ? "   [held]"
                           : "";
    if (trace->crc_ok)
        snprintf(text, sizeof(text),
                 "Last frame   DF%d   %d bits   CRC valid   ICAO %06X%s",
                 trace->downlink_format, trace->bit_count, trace->icao, held);
    else
        snprintf(text, sizeof(text),
                 "Last frame   DF%d   %d bits   CRC FAILED   no address%s",
                 trace->downlink_format, trace->bit_count, held);
    DrawText(text, (int)first_chart.x, y, 17,
             trace->crc_ok ? (Color){ 120, 230, 255, 255 }
                           : (Color){ 255, 140, 130, 255 });
}

static void draw_trace_charts(const struct adsb_layout *l,
                              const struct adsb_frame_trace *trace) {
    const char *waiting = "waiting for a Mode S frame...";
    int bits = trace->valid ? trace->bit_count : 0;

    /* Preamble score either side of the accepted offset. A lock is a peak
       standing alone; the axis top follows the peak so a weak frame still
       fills the chart. */
    float peak = 1.0f;
    for (int i = 0; i < ADSB_TRACE_LANDSCAPE; i++)
        if (trace->landscape[i] > peak)
            peak = trace->landscape[i];
    struct sdrgui_burst_chart_params params = {
        l->chart[0], trace->valid ? trace->landscape : NULL,
        trace->valid ? ADSB_TRACE_LANDSCAPE : 0, SDRGUI_BURST_LINE,
        0.0f, peak * 1.1f, "Preamble Score Landscape", waiting
    };
    sdrgui_burst_chart(&params);

    /* How far each pulse-position bit stood from its decision boundary. */
    params.plot = l->chart[1];
    params.data = trace->valid ? trace->confidence : NULL;
    params.count = bits;
    params.type = SDRGUI_BURST_BAR;
    params.y_min = 0.0f;
    params.y_max = 1.0f;
    params.title = "Pulse-Position Bit Confidence";
    sdrgui_burst_chart(&params);

    /* The frame as the receiver saw it, over the preamble's own level. */
    params.plot = l->chart[2];
    params.data = trace->valid ? trace->envelope : NULL;
    params.count = trace->valid
                       ? ADSB_PREAMBLE_SAMPLES + bits * ADSB_SAMPLES_PER_BIT
                       : 0;
    params.type = SDRGUI_BURST_LINE;
    params.y_max = 1.2f;
    params.title = "Frame Magnitude Envelope";
    sdrgui_burst_chart(&params);
}

/* Every bit's decision plotted as its signed margin against its amplitude:
   two tight clusters left and right is a clean frame, a smear through the
   middle is a frame that decoded by luck. Not a constellation -- Mode S
   carries no modulated symbols and no phase. */
static void draw_decision_scatter(const struct app *app,
                                  const struct adsb_layout *l,
                                  const struct adsb_frame_trace *trace) {
    float x[ADSB_LONG_BITS];
    float y[ADSB_LONG_BITS];
    int n = trace->valid ? trace->bit_count : 0;

    if (n > ADSB_LONG_BITS)
        n = ADSB_LONG_BITS;
    for (int i = 0; i < n; i++) {
        x[i] = trace->margin[i];
        /* A clean bit puts half the preamble's energy in one half-interval, so
           centre the axis on that: 0 is the expected amplitude, not zero
           signal. Clamp so an outlier stays at the rim of the panel. */
        y[i] = trace->amplitude[i] * 2.0f - 1.0f;
        if (y[i] > 1.4f)
            y[i] = 1.4f;
        if (y[i] < -1.4f)
            y[i] = -1.4f;
    }
    struct sdrgui_constellation_params params = {
        l->scatter, x, y, trace->bit, n, "Bit decisions",
        "waiting for a Mode S frame..."
    };
    sdrgui_constellation(&params);
    draw_button(l->hold_button, "Hold last good", app->adsb.hold_last_good);
}

void draw_adsb(struct app *app) {
    struct adsb_layout l = adsb_layout_now();
    const struct adsb_demod_stats *total = &app->adsb.totals;
    const struct adsb_demod_stats *block = &app->adsb.block_stats;
    int analysis = adsb_analysis_showing(app);
    uint64_t record_bytes = 0;
    char record_path[ACQUISITION_PATH_MAX];
    int recording = acquisition_recording_status(&app->acq, &record_bytes,
                                                 record_path,
                                                 sizeof(record_path));
    int header_x = (int)l.header_left;
    char text[400];

    draw_button(l.record_button, recording ? "Recording..." : "Record 2s",
                recording);

    snprintf(text, sizeof(text),
             "1090 MHz extended squitter   frames decoded: %llu   positions: %llu",
             (unsigned long long)app->adsb.frames_total,
             (unsigned long long)app->adsb.positions_total);
    sdrgui_text_fit(text, header_x, 88, 17, l.header_right - l.header_left,
                    (Color){ 187, 205, 216, 255 });

    /* The decode funnel. Preambles with no decodes behind them is the state
       the message log cannot express: an empty log looks the same whether the
       band is silent or every frame is failing its parity. */
    snprintf(text, sizeof(text),
             "funnel   preambles %llu -> shaped %llu -> CRC failed %llu -> decoded %llu   block %llu/%llu/%llu/%llu",
             (unsigned long long)total->preambles,
             (unsigned long long)total->attempts,
             (unsigned long long)total->crc_failed,
             (unsigned long long)total->decoded,
             (unsigned long long)block->preambles,
             (unsigned long long)block->attempts,
             (unsigned long long)block->crc_failed,
             (unsigned long long)block->decoded);
    Color funnel_color = (Color){ 151, 174, 188, 255 };
    if (total->attempts > 0 && total->decoded == 0)
        funnel_color = (Color){ 250, 190, 74, 255 };
    sdrgui_text_fit(text, header_x, 110, 16, l.header_right - l.header_left,
                    funnel_color);

    draw_button(l.view_toggle, analysis ? "View: Log" : "View: Analysis", 0);

    if (!adsb_tuned(app)) {
        if (app->receiver_mode) {
            draw_button(l.retune_button, "Retune to 1090 MHz", 1);
        } else {
            DrawText("Capture is not 1090 MHz / 2 MS/s; no Mode S expected",
                     470, 88, 17, (Color){ 250, 190, 74, 255 });
        }
    }

    /* Log mode has no caption row of its own for a recording notice -- the
       analysis mode puts it above the charts -- so the log's own caption
       carries it. */
    char log_caption[400];
    if (recording && !analysis)
        snprintf(log_caption, sizeof(log_caption),
                 "Recording raw I/Q to %s  (%.1f MB)", record_path,
                 record_bytes / 1e6);
    else
        snprintf(log_caption, sizeof(log_caption),
                 "Decoded messages (newest first)");

    struct sdrgui_message_log_row rows[ADSB_LOG_CAPACITY];
    for (int i = 0; i < app->adsb.log_count; i++) {
        rows[i].time = app->adsb.log[i].stamp;
        rows[i].icao = app->adsb.log[i].icao;
        rows[i].label = app->adsb.log[i].label;
        rows[i].detail = app->adsb.log[i].detail;
        rows[i].raw = app->adsb.log[i].raw;
        rows[i].highlight = app->adsb.log[i].highlight;
    }
    struct sdrgui_message_log_params params = {
        analysis ? l.log_split : l.log_full, rows, app->adsb.log_count,
        log_caption,
        app->have_samples ? "Listening for Mode S frames..."
                          : "Waiting for samples..."
    };
    sdrgui_message_log(&params);

    if (analysis) {
        const struct adsb_frame_trace *trace = adsb_shown_trace(app);
        draw_trace_caption(app, l.chart[0], trace, recording, record_path,
                           record_bytes);
        draw_trace_charts(&l, trace);
        draw_decision_scatter(app, &l, trace);
    }
}
