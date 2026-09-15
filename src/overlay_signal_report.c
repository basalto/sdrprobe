#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "view.h"
#include "overlay_signal_report.h"
#include "sdrgui.h"
#include "signal_analysis.h"
#include "signal_probe.h"
#include "srd_dsp.h"

static int point_in_rec(Vector2 p, Rectangle r) {
    return p.x >= r.x && p.x <= r.x + r.width &&
           p.y >= r.y && p.y <= r.y + r.height;
}

static int button_clicked(Rectangle r) {
    return point_in_rec(GetMousePosition(), r) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void waterfall_context_menu_open(struct app *app, Vector2 mouse,
                                 double freq_hz, double age_seconds,
                                 const char *technology) {
    if (!app)
        return;

    float w = 200.0f;
    float h = 100.0f;
    float screen_w = (float)GetScreenWidth();
    float screen_h = (float)GetScreenHeight();

    if (mouse.x + w > screen_w - 10.0f)
        mouse.x = screen_w - w - 10.0f;
    if (mouse.y + h > screen_h - 10.0f)
        mouse.y = screen_h - h - 10.0f;

    app->wf_menu.mouse_pos = mouse;
    app->wf_menu.clicked_freq_hz = freq_hz;
    app->wf_menu.clicked_age_seconds = age_seconds;
    snprintf(app->wf_menu.technology, sizeof(app->wf_menu.technology), "%s",
             technology ? technology : "raw");
    app->wf_menu.menu_open = 1;
    app->wf_menu.popup_open = 0;
}

void waterfall_context_close(struct app *app) {
    if (!app)
        return;
    app->wf_menu.menu_open = 0;
    app->wf_menu.popup_open = 0;
}

static void run_signal_report(struct app *app) {
    struct waterfall_signal_context *ctx = &app->wf_menu;

    struct iq_snapshot *snap = iq_ring_extract_snapshot(&app->acq.ring,
                                                        ctx->clicked_age_seconds,
                                                        2.0);
    if (!snap)
        return;

    float *i_s = malloc(snap->pair_count * sizeof(float));
    float *q_s = malloc(snap->pair_count * sizeof(float));
    float *mag = malloc(snap->pair_count * sizeof(float));
    if (!i_s || !q_s || !mag) {
        free(i_s);
        free(q_s);
        free(mag);
        iq_snapshot_free(snap);
        return;
    }

    struct device_profile dev = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);
    sdr_dsp_convert_iq(&dev, snap->data, snap->byte_count, i_s, q_s, mag, snap->pair_count);

    double offset_hz = ctx->clicked_freq_hz - (double)snap->frequency_hz;
    struct signal_analysis_result res;
    signal_analysis_run(i_s, q_s, snap->pair_count, (double)snap->sample_rate,
                        offset_hz, snap->full_scale, ctx->technology, &res);

    ctx->report_freq_hz = ctx->clicked_freq_hz;
    ctx->report_offset_hz = offset_hz;
    ctx->report_age_seconds = ctx->clicked_age_seconds;
    ctx->report_duration_seconds = res.duration_seconds;
    ctx->report_prominence_db = res.carrier_over_noise_db;
    ctx->report_standing_fraction = res.carrier_power_fraction;
    ctx->report_envelope_variation = res.envelope_variation;
    ctx->report_peak_mean_db = res.peak_over_mean_db;
    snprintf(ctx->report_modulation, sizeof(ctx->report_modulation), "%s",
             res.verdict_summary);
    if (res.has_technology_evidence) {
        snprintf(ctx->report_technology, sizeof(ctx->report_technology), "%s",
                 res.technology_finding);
    } else {
        ctx->report_technology[0] = '\0';
    }

    free(i_s);
    free(q_s);
    free(mag);
    iq_snapshot_free(snap);

    ctx->popup_open = 1;
}

int handle_waterfall_context_input(struct app *app) {
    if (!app)
        return 0;

    struct waterfall_signal_context *ctx = &app->wf_menu;
    Vector2 mouse = GetMousePosition();

    if (ctx->menu_open) {
        Rectangle r_menu = { ctx->mouse_pos.x, ctx->mouse_pos.y, 200.0f, 95.0f };
        Rectangle r_save = { ctx->mouse_pos.x + 8.0f, ctx->mouse_pos.y + 32.0f,
                             184.0f, 26.0f };
        Rectangle r_report = { ctx->mouse_pos.x + 8.0f, ctx->mouse_pos.y + 62.0f,
                               184.0f, 26.0f };

        if (button_clicked(r_save)) {
            mkdir("captures", 0755);
            time_t raw_t = time(NULL);
            struct tm local_t;
            localtime_r(&raw_t, &local_t);
            char stamp[32];
            strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local_t);
            char path[256];
            snprintf(path, sizeof(path), "captures/signal_%s.bin", stamp);

            if (iq_ring_save_slice(&app->acq.ring, ctx->clicked_age_seconds,
                                   2.0, path, ctx->technology) == 0) {
                snprintf(ctx->notice, sizeof(ctx->notice),
                         "Saved 2.0s capture to %s", path);
            } else {
                snprintf(ctx->notice, sizeof(ctx->notice),
                         "Failed to save slice from ring buffer");
            }
            ctx->notice_time = GetTime();
            ctx->menu_open = 0;
            return 1;
        }

        if (button_clicked(r_report)) {
            ctx->menu_open = 0;
            run_signal_report(app);
            return 1;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            if (!point_in_rec(mouse, r_menu)) {
                ctx->menu_open = 0;
                return 1;
            }
        }
        return 1;
    }

    if (ctx->popup_open) {
        float w = 460.0f;
        float h = 320.0f;
        float x = ((float)GetScreenWidth() - w) / 2.0f;
        float y = ((float)GetScreenHeight() - h) / 2.0f;
        Rectangle r_box = { x, y, w, h };
        Rectangle r_save = { x + 20.0f, y + h - 38.0f, 150.0f, 26.0f };
        Rectangle r_close = { x + w - 100.0f, y + h - 38.0f, 80.0f, 26.0f };

        if (button_clicked(r_close) || IsKeyPressed(KEY_ESCAPE)) {
            ctx->popup_open = 0;
            return 1;
        }

        if (button_clicked(r_save)) {
            mkdir("captures", 0755);
            time_t raw_t = time(NULL);
            struct tm local_t;
            localtime_r(&raw_t, &local_t);
            char stamp[32];
            strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local_t);
            char path[256];
            snprintf(path, sizeof(path), "captures/signal_%s.bin", stamp);

            if (iq_ring_save_slice(&app->acq.ring, ctx->report_age_seconds,
                                   ctx->report_duration_seconds, path,
                                   ctx->technology) == 0) {
                snprintf(ctx->notice, sizeof(ctx->notice),
                         "Saved capture to %s", path);
            }
            ctx->notice_time = GetTime();
            return 1;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !point_in_rec(mouse, r_box)) {
            ctx->popup_open = 0;
            return 1;
        }
        return 1;
    }

    return 0;
}

void draw_waterfall_context(struct app *app) {
    if (!app)
        return;

    struct waterfall_signal_context *ctx = &app->wf_menu;
    double now = GetTime();

    /* Status banner */
    if (ctx->notice[0] && (now - ctx->notice_time < 4.0)) {
        int tw = MeasureText(ctx->notice, 16);
        float bx = ((float)GetScreenWidth() - (float)tw) / 2.0f - 16.0f;
        DrawRectangle((int)bx, 48, tw + 32, 28, (Color){ 20, 36, 48, 240 });
        DrawRectangleLines((int)bx, 48, tw + 32, 28, (Color){ 100, 220, 140, 255 });
        DrawText(ctx->notice, (int)bx + 16, 54, 16, (Color){ 140, 240, 180, 255 });
    }

    /* Context menu */
    if (ctx->menu_open) {
        Rectangle r_menu = { ctx->mouse_pos.x, ctx->mouse_pos.y, 200.0f, 95.0f };
        Rectangle r_save = { ctx->mouse_pos.x + 8.0f, ctx->mouse_pos.y + 32.0f,
                             184.0f, 26.0f };
        Rectangle r_report = { ctx->mouse_pos.x + 8.0f, ctx->mouse_pos.y + 62.0f,
                               184.0f, 26.0f };

        DrawRectangleRec(r_menu, (Color){ 18, 28, 38, 250 });
        DrawRectangleLinesEx(r_menu, 1.0f, (Color){ 90, 130, 160, 255 });

        char hdr[64];
        snprintf(hdr, sizeof(hdr), "Signal %.3f MHz", ctx->clicked_freq_hz / 1e6);
        DrawText(hdr, (int)ctx->mouse_pos.x + 10, (int)ctx->mouse_pos.y + 9,
                 14, (Color){ 210, 230, 245, 255 });

        draw_button(r_save, "Save Signal (2.0s)", 0);
        draw_button(r_report, "Run Signal Report", 0);
    }

    /* Report popup modal */
    if (ctx->popup_open) {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                      (Color){ 0, 0, 0, 150 });

        float w = 460.0f;
        float h = 320.0f;
        float x = ((float)GetScreenWidth() - w) / 2.0f;
        float y = ((float)GetScreenHeight() - h) / 2.0f;
        Rectangle r_box = { x, y, w, h };
        Rectangle r_save = { x + 20.0f, y + h - 38.0f, 150.0f, 26.0f };
        Rectangle r_close = { x + w - 100.0f, y + h - 38.0f, 80.0f, 26.0f };

        DrawRectangleRec(r_box, (Color){ 16, 26, 36, 255 });
        DrawRectangleLinesEx(r_box, 1.5f, (Color){ 90, 140, 180, 255 });

        DrawText("Signal Analysis Report", (int)x + 20, (int)y + 16, 20,
                 (Color){ 230, 242, 252, 255 });

        char line[128];
        int text_y = (int)y + 54;
        int step = 26;

        snprintf(line, sizeof(line), "Frequency:    %.6f MHz  (%+.1f kHz)",
                 ctx->report_freq_hz / 1e6, ctx->report_offset_hz / 1e3);
        DrawText(line, (int)x + 20, text_y, 16, (Color){ 190, 215, 235, 255 });
        text_y += step;

        snprintf(line, sizeof(line), "Timestamp:    %.2f s ago  (span %.1f s)",
                 ctx->report_age_seconds, ctx->report_duration_seconds);
        DrawText(line, (int)x + 20, text_y, 16, (Color){ 190, 215, 235, 255 });
        text_y += step;

        snprintf(line, sizeof(line), "Modulation:   %s", ctx->report_modulation);
        DrawText(line, (int)x + 20, text_y, 16, (Color){ 250, 210, 110, 255 });
        text_y += step;

        if (ctx->report_technology[0]) {
            DrawText(ctx->report_technology, (int)x + 20, text_y, 14, (Color){ 120, 230, 255, 255 });
            text_y += step;
        }

        snprintf(line, sizeof(line), "Prominence:   %+.1f dB over noise floor",
                 ctx->report_prominence_db);
        DrawText(line, (int)x + 20, text_y, 16, (Color){ 190, 215, 235, 255 });
        text_y += step;

        snprintf(line, sizeof(line), "Standing:     %.1f%% of channel stands still",
                 ctx->report_standing_fraction * 100.0);
        DrawText(line, (int)x + 20, text_y, 16, (Color){ 190, 215, 235, 255 });
        text_y += step;

        snprintf(line, sizeof(line), "Envelope:     variation %.3f (noise is 0.523)",
                 ctx->report_envelope_variation);
        DrawText(line, (int)x + 20, text_y, 16, (Color){ 190, 215, 235, 255 });
        text_y += step;

        snprintf(line, sizeof(line), "Peak / Mean:  %.1f dB", ctx->report_peak_mean_db);
        DrawText(line, (int)x + 20, text_y, 16, (Color){ 190, 215, 235, 255 });
        text_y += step;

        draw_button(r_save, "Save Capture", 0);
        draw_button(r_close, "Close", 0);
    }
}
