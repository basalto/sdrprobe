#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "view.h"
#include "gsm_layout.h"
#include "sdrgui.h"

/* Convenience for the current window. The accessors below keep their old
   shape so call sites are unchanged; each is now a lookup, not a derivation. */
static struct gsm_layout gsm_layout_now(void) {
    return gsm_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

static Rectangle gsm_scan_button(void) {
    return gsm_layout_now().scan_button;
}

static Rectangle gsm_back_to_scan_button(void) {
    return gsm_layout_now().scan_button;
}

Rectangle gsm_waterfall_rect(void) {
    return gsm_layout_now().waterfall;
}

Rectangle gsm_scan_rect(void) {
    return gsm_layout_now().scan;
}

Rectangle gsm_burst_rect(void) {
    return gsm_layout_now().burst;
}

static Rectangle gsm_view_toggle_button(void) {
    return gsm_layout_now().view_toggle;
}

static Rectangle gsm_constellation_rect(void) {
    return gsm_layout_now().constellation;
}

static Rectangle gsm_const_amp_button(void) {
    return gsm_layout_now().const_amp_button;
}

static Rectangle gsm_const_derot_button(void) {
    return gsm_layout_now().const_derot_button;
}

static int gsm_scan_arfcn_at(Vector2 point, Rectangle rect) {
    if (!CheckCollisionPointRec(point, rect))
        return 0;
    double fraction = (point.x - rect.x) / rect.width;
    int arfcn = 1 + (int)(fraction * 124.0);
    if (arfcn < 1)
        arfcn = 1;
    if (arfcn > 124)
        arfcn = 124;
    return arfcn;
}

/* Select an ARFCN and show it in the waterfall above: on a live receiver retune
   to it (carrier at +400 kHz within the 2 MHz span) so its activity is visible;
   the waterfall then zooms to the selected carrier. */
void gsm_tune_selected(struct app *app, int arfcn) {
    uint32_t expected;
    if (arfcn < 1 || arfcn > 124 ||
        !gsm_downlink_hz((unsigned int)arfcn, &expected))
        return;
    app->scan_selected_arfcn = arfcn;
    app->gsm_selected_hz = (double)expected;
    app->gsm_sch_valid = 0;
    memset(&app->gsm_continuity, 0, sizeof(app->gsm_continuity));
    if (app->receiver_mode) {
        if (!app->gsm_return_valid) {
            app->gsm_return_frequency = app->applied_frequency;
            app->gsm_return_valid = 1;
        }
        retune_receiver(app, expected - 400000U, app->applied_ppm);
    }
}

/* Note an SCH decode that cannot be right: T1 advances once per 1326 frames,
   so consecutive decodes seconds apart must agree to within 1. Flags only. */
static void check_sch_continuity(struct gsm_sch_continuity *c,
                                 const struct gsm_sch_result *res) {
    c->implausible = c->have_last && abs(res->t1 - c->last_t1) > 1;
    c->last_t1 = res->t1;
    c->have_last = 1;
}

/* Attempt an SCH decode on the inspected channel's latest block. The channel
   carrier sits at +400 kHz (we tuned to expected - 400 kHz). */
void update_gsm_sch(struct app *app, double now) {
    if (app->gsm_selected_hz <= 0.0 || app->scan_running ||
        app->pair_count == 0)
        return;
    double offset = app->gsm_selected_hz - (double)app->applied_frequency;
    struct gsm_sch_result result;
    struct gsm_sch_symbols symbols;
    uint32_t options = 0;
    if (app->gsm_opt_filter) options |= GSM_OPT_FILTER;
    if (app->gsm_opt_finecfo) options |= GSM_OPT_FINECFO;
    if (app->gsm_opt_trellis) options |= GSM_OPT_TRELLIS;
    
    if (gsm_sch_decode(app->i_samples, app->q_samples, app->pair_count,
                       (double)app->applied_sample_rate, offset, options, &result,
                       &symbols)) {
        app->gsm_sch = result;
        app->gsm_sch_symbols = symbols;
        app->gsm_sch_valid = 1;
        app->gsm_sch_time = now;
        check_sch_continuity(&app->gsm_continuity, &result);
    }
}

static Rectangle gsm_record_button(void) {
    return gsm_layout_now().record_button;
}



/* Start a 2 s capture of the inspected channel. The tuning goes to
   acquisition only so it can be written into the capture's sidecar; the write
   itself happens on the acquisition thread, upstream of the display's lossy
   block slot. Files are timestamped so re-recording never overwrites one. */
void start_record(struct app *app) {
    mkdir("captures", 0755); /* ignore EEXIST */
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    char path[256];
    snprintf(path, sizeof(path), "captures/gsm_arfcn%d_%s.bin",
             app->scan_selected_arfcn > 0 ? app->scan_selected_arfcn : 0, stamp);

    struct acquisition_record_request req = {
        app->applied_frequency, app->applied_sample_rate,
        app->applied_gain_tenths, app->applied_manual_gain, app->applied_ppm,
        app->scan_selected_arfcn,
        /* gsm_tune_selected tunes 400 kHz below the channel, so the carrier
           sits that far above the recorded centre. */
        app->scan_selected_arfcn > 0 ? 400000.0 : 0.0,
        app->source_label, app->tuner_label
    };
    if (acquisition_start_recording(&app->acq, path, &req) < 0)
        fprintf(stderr, "Cannot start recording to %s: %s\n", path,
                strerror(errno));
}

static Rectangle gsm_opt_button(int index) {
    if (index < 0)
        index = 0;
    if (index > 2)
        index = 2;
    return gsm_layout_now().opt_button[index];
}

void draw_gsm(struct app *app) {
    char text[320];
    if (app->scan_selected_arfcn > 0 && !app->scan_running) {
        draw_button(gsm_view_toggle_button(), app->gsm_analysis_mode ? "View: Waterfall" : "View: Burst", 0);
        draw_button(gsm_back_to_scan_button(), "Back to Scan", 1);
    } else {
        draw_button(gsm_scan_button(),
                    app->scan_running ? "Scanning" : "Scan / Rescan",
                    !app->scan_running);
    }
    uint64_t rec_bytes = 0;
    char rec_path[ACQUISITION_PATH_MAX];
    int rec_active = acquisition_recording_status(&app->acq, &rec_bytes, rec_path,
                                     sizeof(rec_path));
    draw_button(gsm_record_button(),
                rec_active ? "Recording..." : "Record 2s", rec_active);
                
    DrawText("Features:", 322, 136, 15, (Color){ 151, 174, 188, 255 });
    draw_button(gsm_opt_button(0), "Filter", app->gsm_opt_filter);
    draw_button(gsm_opt_button(1), "FnCFO", app->gsm_opt_finecfo);
    draw_button(gsm_opt_button(2), "Trellis", app->gsm_opt_trellis);

    if (!app->receiver_mode)
        snprintf(text, sizeof(text),
                 "Band scan needs a live RTL-SDR receiver; waterfall shows the current tuning");
    else if (app->scan_running)
        snprintf(text, sizeof(text), "Scanning ARFCN band... step %d / %d",
                 app->scan_step + 1, app->scan_step_count);
    else {
        int bcch = scan_strongest_bcch(app);
        int strongest = scan_strongest_arfcn(app);
        if (app->scan_selected_arfcn > 0)
            snprintf(text, sizeof(text),
                     "Selected ARFCN %d (%.3f MHz)   click another channel to inspect it",
                     app->scan_selected_arfcn, app->gsm_selected_hz / 1000000.0);
        else if (bcch > 0)
            snprintf(text, sizeof(text),
                     "Strongest BCCH ARFCN %d at %.1f dBFS (conf %.2f)   click a channel to inspect it",
                     bcch, app->scan_power[bcch], app->scan_bcch_conf[bcch]);
        else if (strongest > 0)
            snprintf(text, sizeof(text),
                     "No BCCH detected; strongest ARFCN %d at %.1f dBFS   click a channel to inspect it",
                     strongest, app->scan_power[strongest]);
        else
            snprintf(text, sizeof(text),
                     "Press Scan to survey GSM 900 downlink channel power and locate BCCH carriers");
    }
    DrawText(text, 322, 90, 17, (Color){ 190, 208, 218, 255 });

    /* Signal quality of the inspected channel (estimated SNR for gain/lock). */
    if (app->signal_stats_ready) {
        const struct sdr_signal_stats *stats = &app->signal_stats;
        Color quality_color = (Color){ 90, 220, 164, 255 };
        if (stats->clipping_percent >= 0.1f || stats->headroom_db < 1.0f)
            quality_color = (Color){ 255, 102, 94, 255 };
        else if (stats->clipping_percent > 0.0f || stats->headroom_db < 3.0f)
            quality_color = (Color){ 250, 190, 74, 255 };
        snprintf(text, sizeof(text),
                 "noise (p10) %.2f   signal (p99.5) %.2f   estimated SNR %.1f dB   clipping %.4f%%   headroom %.1f dB",
                 stats->noise_magnitude, stats->signal_magnitude,
                 stats->snr_db, stats->clipping_percent, stats->headroom_db);
        DrawText(text, 322, 112, 16, quality_color);
    }

    if (app->scan_selected_arfcn > 0 && !app->scan_running) {
        Rectangle wf = gsm_burst_rect();

        /* SCH decode readout, printed above the bottom chart area. */
        if (app->gsm_sch_valid) {
            const struct gsm_sch_result *sch = &app->gsm_sch;
            snprintf(text, sizeof(text),
                     "SCH   BSIC %d  (NCC %d, BCC %d)   frame %d  (T1/T2/T3 %d/%d/%d)   match %.2f%s",
                     sch->bsic, sch->ncc, sch->bcc, sch->frame_number, sch->t1,
                     sch->t2, sch->t3, (double)sch->confidence,
                     app->gsm_continuity.implausible ? "  [T1 JUMPED]" : "");
            DrawText(text, (int)gsm_scan_rect().x, (int)gsm_scan_rect().y - 42, 18,
                     (Color){ 120, 230, 255, 255 });
        } else if (rec_active) {
            snprintf(text, sizeof(text), "Recording raw I/Q to %s  (%.1f MB)",
                     rec_path, rec_bytes / 1e6);
            DrawText(text, (int)gsm_scan_rect().x, (int)gsm_scan_rect().y - 42, 18,
                     (Color){ 255, 202, 105, 255 });
        } else if (app->scan_selected_arfcn > 0 && app->receiver_mode) {
            DrawText("SCH   searching for a synchronisation burst...", (int)gsm_scan_rect().x,
                     (int)gsm_scan_rect().y - 42, 18, (Color){ 151, 174, 188, 255 });
        }

        if (app->gsm_analysis_mode) {
            /* Burst Analysis Chart replaces Waterfall. sdrgui_burst_chart
               keeps its labels inside its own rectangle, so this gap is only
               breathing room between panels. */
            float gap = 14.0f;
            float w3 = (wf.width - 2.0f * gap) / 3.0f;
            Rectangle r_corr = { wf.x, wf.y, w3, wf.height };
            Rectangle r_soft = { wf.x + w3 + gap, wf.y, w3, wf.height };
            Rectangle r_phase = { wf.x + 2.0f * (w3 + gap), wf.y, w3, wf.height };

            struct sdrgui_burst_chart_params bparams = {
                r_corr, NULL, 0, SDRGUI_BURST_LINE, -1.0f, 1.0f, "Timing Correlation Landscape",
                "waiting for a synchronisation burst..."
            };
            const struct gsm_sch_symbols *sym = &app->gsm_sch_symbols;

            if (app->gsm_sch_valid && sym->count > 0) {
                bparams.data = sym->corr;
                bparams.count = sym->count;
                sdrgui_burst_chart(&bparams);

                bparams.plot = r_soft;
                bparams.data = sym->soft_mag;
                bparams.type = SDRGUI_BURST_BAR;
                bparams.y_min = 0.0f;
                float mx = 0.1f;
                for(int i=0; i<sym->count; i++) {
                    if (sym->soft_mag[i] > mx) mx = sym->soft_mag[i];
                }
                bparams.y_max = mx * 1.1f;
                bparams.title = "Soft Symbol Magnitudes (|Im|)";
                sdrgui_burst_chart(&bparams);

                bparams.plot = r_phase;
                bparams.data = sym->phase;
                bparams.type = SDRGUI_BURST_LINE;
                bparams.y_min = -3.14159f;
                float mn = 0.0f, mx_p = 0.0f;
                for(int i=0; i<sym->count; i++) {
                    if (sym->phase[i] < mn) mn = sym->phase[i];
                    if (sym->phase[i] > mx_p) mx_p = sym->phase[i];
                }
                bparams.y_min = mn - 1.0f;
                bparams.y_max = mx_p + 1.0f;
                bparams.title = "Differential Phase Trajectory";
                sdrgui_burst_chart(&bparams);
            } else {
                sdrgui_burst_chart(&bparams);
                bparams.plot = r_soft; bparams.title = "Soft Symbol Magnitudes (|Im|)"; sdrgui_burst_chart(&bparams);
                bparams.plot = r_phase; bparams.title = "Differential Phase Trajectory"; sdrgui_burst_chart(&bparams);
            }
        } else {
            DrawText(TextFormat("ARFCN waterfall - inspecting ARFCN %d",
                                app->scan_selected_arfcn),
                     (int)wf.x, (int)wf.y - 18, 16, (Color){ 151, 174, 188, 255 });
            draw_waterfall_rect(app, 1, wf, app->gsm_selected_hz);
        }



        /* Channel Power Scan Chart on bottom left */
        Rectangle sc = gsm_scan_rect();
        DrawText("Channel Power Scan", (int)sc.x, (int)sc.y - 18, 16, (Color){ 151, 174, 188, 255 });
        int hover = (!app->scan_running)
                        ? gsm_scan_arfcn_at(GetMousePosition(), sc)
                        : 0;
        struct sdrgui_scan_chart_params params = {
            sc, app->scan_power, app->scan_bcch_conf, 124, SCAN_SENTINEL_DBFS,
            SCAN_BCCH_MIN_CONF, hover, GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ,
            app->scan_selected_arfcn
        };
        sdrgui_scan_chart(&params);


    } else {
        /* Waterfall on Top */
        Rectangle wf = gsm_waterfall_rect();
        DrawText("ARFCN waterfall", (int)wf.x, (int)wf.y - 18, 16,
                 (Color){ 151, 174, 188, 255 });
        draw_waterfall_rect(app, 1, wf, app->gsm_selected_hz);

        /* Default Channel Power Scan Chart on Bottom */
        Rectangle sc = gsm_scan_rect();
        DrawText("Channel Power Scan", (int)sc.x, (int)sc.y - 18, 16, (Color){ 151, 174, 188, 255 });
        int hover = (!app->scan_running)
                        ? gsm_scan_arfcn_at(GetMousePosition(), sc)
                        : 0;
        struct sdrgui_scan_chart_params params = {
            sc, app->scan_power, app->scan_bcch_conf, 124, SCAN_SENTINEL_DBFS,
            SCAN_BCCH_MIN_CONF, hover, GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ,
            app->scan_selected_arfcn
        };
        sdrgui_scan_chart(&params);
    }

    /* Decode constellation on bottom right */
    Rectangle cst = gsm_constellation_rect();
    const struct gsm_sch_symbols *sym_c = &app->gsm_sch_symbols;
    int n = app->gsm_sch_valid ? sym_c->count : 0;
    float cx[GSM_SCH_BURST_BITS];
    float cy[GSM_SCH_BURST_BITS];
    if (n > GSM_SCH_BURST_BITS)
        n = GSM_SCH_BURST_BITS;
    const unsigned char *color_bits =
        app->gsm_const_derotated ? sym_c->chan : sym_c->bit;
    /* Raw display coords per representation. */
    double cmag[GSM_SCH_BURST_BITS];
    for (int i = 0; i < n; i++) {
        float rx, ry;
        if (app->gsm_const_derotated) {
            rx = sym_c->rot_i[i];
            ry = sym_c->rot_q[i];
        } else {
            rx = sym_c->diff_im[i];  /* map so bit 0 (im>0) sits on the right */
            ry = -sym_c->diff_re[i];
        }
        cx[i] = rx;
        cy[i] = ry;
        cmag[i] = sqrt((double)rx * rx + (double)ry * ry);
    }
    /* Scale the cloud by a high percentile, not by the largest sample. Scaling
       by the maximum puts that one sample on the box edge every frame by
       construction and squeezes everything else inward by however extreme it
       happens to be, which reads as a symbol that is permanently off on its
       own. On a real burst the largest magnitude runs from 1.04x the median to
       3.5x, so the effect is not rare. */
    double reference = 1e-9;
    if (n > 0) {
        double sorted[GSM_SCH_BURST_BITS];
        memcpy(sorted, cmag, (size_t)n * sizeof(*sorted));
        qsort(sorted, (size_t)n, sizeof(*sorted), compare_double);
        reference = sorted[(int)((double)(n - 1) * 0.9)];
        if (reference < 1e-9)
            reference = 1e-9;
    }
    for (int i = 0; i < n; i++) {
        if (app->gsm_const_amplitude) {
            cx[i] = (float)(cx[i] / reference);
            cy[i] = (float)(cy[i] / reference);
            /* Genuine outliers stay visible at the rim instead of drawing
               outside the box. */
            float mag = sqrtf(cx[i] * cx[i] + cy[i] * cy[i]);
            if (mag > 1.4f) {
                cx[i] *= 1.4f / mag;
                cy[i] *= 1.4f / mag;
            }
        } else {
            float mag = sqrtf(cx[i] * cx[i] + cy[i] * cy[i]);
            if (mag < 1e-9f)
                mag = 1e-9f;
            cx[i] /= mag; /* project onto the unit circle */
            cy[i] /= mag;
        }
    }
    struct sdrgui_constellation_params cparams = {
        cst, cx, cy, color_bits, n, "SCH decoded symbols",
        app->scan_selected_arfcn > 0 ? "waiting for a synchronisation burst..."
                                     : "select a channel to inspect"
    };
    sdrgui_constellation(&cparams);
    draw_button(gsm_const_amp_button(), "Amp", app->gsm_const_amplitude);
    draw_button(gsm_const_derot_button(), "Derot", app->gsm_const_derotated);
}

void handle_gsm_input(struct app *app) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        set_tab(app, TAB_SCOPE);
        return;
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
        adjust_waterfall_scale(app, 1);
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
        adjust_waterfall_scale(app, 0);
    if (app->scan_running)
        return;
    if (clicked(gsm_scan_button())) {
        if (start_scan(app) == 0) {
            app->scan_open = 0; /* the GSM view shows the scan inline */
            app->gsm_autoselect_pending = 1;
        }
        return;
    }
    if (clicked(gsm_record_button())) {
        start_record(app);
        return;
    }
    for (int i = 0; i < 3; i++) {
        if (clicked(gsm_opt_button(i))) {
            if (i == 0) app->gsm_opt_filter = !app->gsm_opt_filter;
            if (i == 1) app->gsm_opt_finecfo = !app->gsm_opt_finecfo;
            if (i == 2) app->gsm_opt_trellis = !app->gsm_opt_trellis;
            return;
        }
    }
    if (app->scan_selected_arfcn > 0 && !app->scan_running) {
        if (clicked(gsm_view_toggle_button())) {
            app->gsm_analysis_mode = !app->gsm_analysis_mode;
            return;
        }
        if (clicked(gsm_back_to_scan_button())) {
            app->scan_selected_arfcn = 0;
            app->gsm_selected_hz = 0.0;
            return;
        }
    }

    if (clicked(gsm_const_amp_button())) {
        app->gsm_const_amplitude = !app->gsm_const_amplitude;
        return;
    }
    if (clicked(gsm_const_derot_button())) {
        app->gsm_const_derotated = !app->gsm_const_derotated;
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int arfcn = gsm_scan_arfcn_at(GetMousePosition(), gsm_scan_rect());
        if (arfcn > 0 && app->scan_power[arfcn] > SCAN_SENTINEL_DBFS) {
            gsm_tune_selected(app, arfcn);
            app->gsm_analysis_mode = 1;
        }
    }
}

/* --- ADS-B decoder tab --- */

