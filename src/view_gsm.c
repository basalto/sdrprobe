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
    return sdrgui_scan_chart_channel_at(rect, 124, point);
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
    app->gsm.selected_hz = (double)expected;
    app->gsm.sch_valid = 0;
    gsm_continuity_reset(&app->gsm.continuity);
    memset(&app->gsm.cell, 0, sizeof(app->gsm.cell)); /* a different cell */
    if (app->receiver_mode) {
        if (!app->gsm.return_valid) {
            app->gsm.return_frequency = app->applied_frequency;
            app->gsm.return_valid = 1;
        }
        retune_receiver(app, expected - 400000U, app->applied_ppm);
    }
}

/* Note an SCH decode that cannot be right: T1 advances once per 1326 frames,
   so consecutive decodes seconds apart must agree to within 1. Flags only. */
/*
 * What the cell is saying, when this SCH is the one a broadcast block follows.
 *
 * The BCCH occupies frames 2 to 5 of the 51-multiframe, so only the SCH at
 * frame 1 has one behind it -- one in five. The other four are followed by
 * paging and access grants, which this does not read.
 */
int gsm_read_broadcast(struct app *app, const struct gsm_sch_result *sch,
                       struct gsm_si *si) {
    float soft[GSM_BCCH_BURSTS * GSM_BURST_DATA_BITS];
    float bursts[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];
    float coded[GSM_BCCH_CODED_BITS];
    struct gsm_bcch_block block;

    if (sch->frame_number % 51 != 1)
        return 0;
    memset(soft, 0, sizeof(soft));
    if (gsm_normal_bursts(app->i_samples, app->q_samples, app->pair_count,
                          (double)app->applied_sample_rate, sch,
                          GSM_BCCH_BURSTS, soft) < GSM_BCCH_BURSTS)
        return 0; /* the block ran past the end of this sample block */
    for (int b = 0; b < GSM_BCCH_BURSTS; b++)
        memcpy(bursts[b], &soft[b * GSM_BURST_DATA_BITS], sizeof(bursts[b]));
    gsm_bcch_deinterleave((const float (*)[GSM_BURST_DATA_BITS])bursts, coded);
    if (!gsm_bcch_decode_block(coded, &block))
        return 0; /* the Fire code refused it, so it is not a message */
    return gsm_si_parse(block.octets, si);
}

/* Fold one message into what the cell has said so far. */
static void gsm_cell_remember(struct gsm_cell *cell, const struct gsm_si *si) {
    cell->blocks++;
    cell->last_type = si->type;
    if (si->have_lai) {
        cell->have_lai = 1;
        cell->mcc = si->mcc;
        cell->mnc = si->mnc;
        cell->mnc_digits = si->mnc_digits;
        cell->lac = si->lac;
    }
    if (si->have_cell_id) {
        cell->have_cell_id = 1;
        cell->cell_id = si->cell_id;
    }
    if (si->neighbour_count > 0) {
        cell->neighbour_count = si->neighbour_count;
        memcpy(cell->neighbours, si->neighbours,
               (size_t)si->neighbour_count * sizeof(*si->neighbours));
    }
}

/* Attempt an SCH decode on the inspected channel's latest block. The channel
   carrier sits at +400 kHz (we tuned to expected - 400 kHz). */
void update_gsm_sch(struct app *app, double now) {
    if (app->gsm.selected_hz <= 0.0 || app->scan_running ||
        app->pair_count == 0)
        return;
    double offset = app->gsm.selected_hz - (double)app->applied_frequency;
    struct gsm_sch_result result;
    struct gsm_sch_symbols symbols;
    uint32_t options = 0;
    if (app->gsm.opt_filter) options |= GSM_OPT_FILTER;
    if (app->gsm.opt_finecfo) options |= GSM_OPT_FINECFO;
    if (app->gsm.opt_trellis) options |= GSM_OPT_TRELLIS;
    
    if (gsm_sch_decode(app->i_samples, app->q_samples, app->pair_count,
                       (double)app->applied_sample_rate, offset, options, &result,
                       &symbols)) {
        app->gsm.sch = result;
        app->gsm.sch_symbols = symbols;
        app->gsm.sch_valid = 1;
        app->gsm.sch_time = now;
        gsm_continuity_observe(&app->gsm.continuity, result.t1, result.bsic,
                               now);

        {
            struct gsm_si si;

            if (gsm_read_broadcast(app, &result, &si))
                gsm_cell_remember(&app->gsm.cell, &si);
        }
    }
}

static Rectangle gsm_record_button(void) {
    return gsm_layout_now().record_button;
}



/* Start a 2 s capture of the inspected channel. gsm_tune_selected tunes
   400 kHz below the channel, so the carrier sits that far above the recorded
   centre -- a decoder needs that and cannot recover it from the samples. */
void start_record(struct app *app) {
    char basename[64];
    int arfcn = app->scan_selected_arfcn > 0 ? app->scan_selected_arfcn : 0;

    snprintf(basename, sizeof(basename), "gsm_arfcn%d", arfcn);
    start_capture_record(app, basename, "gsm", arfcn,
                         arfcn > 0 ? 400000.0 : 0.0,
                         ACQUISITION_RECORD_BUTTON_SECONDS);
}

static Rectangle gsm_opt_button(int index) {
    if (index < 0)
        index = 0;
    if (index > 2)
        index = 2;
    return gsm_layout_now().opt_button[index];
}

/* Room for the header text, which shares its rows with the view toggle. */
static float gsm_header_width(int x) {
    float limit = gsm_layout_now().view_toggle.x - 12.0f - (float)x;
    return limit > 0.0f ? limit : 0.0f;
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
    draw_button(gsm_opt_button(0), "Filter", app->gsm.opt_filter);
    draw_button(gsm_opt_button(1), "FnCFO", app->gsm.opt_finecfo);
    draw_button(gsm_opt_button(2), "Trellis", app->gsm.opt_trellis);

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
                     app->scan_selected_arfcn, app->gsm.selected_hz / 1000000.0);
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
    sdrgui_text_fit(text, 322, 90, 17, gsm_header_width(322), (Color){ 190, 208, 218, 255 });

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
        sdrgui_text_fit(text, 322, 112, 16, gsm_header_width(322), quality_color);
    }

    if (app->scan_selected_arfcn > 0 && !app->scan_running) {
        Rectangle wf = gsm_burst_rect();

        /* SCH decode readout, printed above the bottom chart area. */
        if (app->gsm.sch_valid) {
            const struct gsm_sch_result *sch = &app->gsm.sch;
            snprintf(text, sizeof(text),
                     "SCH   BSIC %d  (NCC %d, BCC %d)   frame %d  (T1/T2/T3 %d/%d/%d)   match %.2f%s",
                     sch->bsic, sch->ncc, sch->bcc, sch->frame_number, sch->t1,
                     sch->t2, sch->t3, (double)sch->confidence,
                     app->gsm.continuity.implausible ? "  [T1 JUMPED]" : "");
            DrawText(text, (int)gsm_scan_rect().x, (int)gsm_scan_rect().y - 64,
                     18, (Color){ 120, 230, 255, 255 });

            /*
             * And what the cell says about itself, one line under it. The SCH
             * line above is measurement -- an identity code and a clock. This
             * one is the cell talking, so it is worded as such and coloured
             * apart.
             */
            const struct gsm_cell *cell = &app->gsm.cell;

            if (cell->blocks > 0) {
                int used = snprintf(text, sizeof(text), "BCCH  ");

                if (cell->have_lai)
                    used += snprintf(text + used, sizeof(text) - (size_t)used,
                                     "MCC %d  MNC %0*d  LAC %d   ", cell->mcc,
                                     cell->mnc_digits, cell->mnc, cell->lac);
                if (cell->have_cell_id)
                    used += snprintf(text + used, sizeof(text) - (size_t)used,
                                     "cell %d   ", cell->cell_id);
                if (cell->neighbour_count > 0) {
                    used += snprintf(text + used, sizeof(text) - (size_t)used,
                                     "neighbours");
                    for (int i = 0; i < cell->neighbour_count &&
                                    used < (int)sizeof(text) - 8; i++)
                        used += snprintf(text + used,
                                         sizeof(text) - (size_t)used, " %d",
                                         cell->neighbours[i]);
                }
                sdrgui_text_fit(text, (int)gsm_scan_rect().x,
                                (int)gsm_scan_rect().y - 42, 17,
                                gsm_constellation_rect().x +
                                    gsm_constellation_rect().width -
                                    gsm_scan_rect().x,
                                (Color){ 153, 235, 178, 255 });
            } else if (app->gsm.sch.frame_number % 51 == 1) {
                DrawText("BCCH  a broadcast block is due here, and did not "
                         "survive",
                         (int)gsm_scan_rect().x, (int)gsm_scan_rect().y - 42,
                         17, (Color){ 126, 151, 166, 255 });
            } else {
                DrawText("BCCH  waiting for the multiframe to come round",
                         (int)gsm_scan_rect().x, (int)gsm_scan_rect().y - 42,
                         17, (Color){ 126, 151, 166, 255 });
            }
        } else if (rec_active) {
            snprintf(text, sizeof(text), "Recording raw I/Q to %s  (%.1f MB)",
                     rec_path, rec_bytes / 1e6);
            DrawText(text, (int)gsm_scan_rect().x, (int)gsm_scan_rect().y - 64,
                     18, (Color){ 255, 202, 105, 255 });
        } else if (app->scan_selected_arfcn > 0 && app->receiver_mode) {
            DrawText("SCH   searching for a synchronisation burst...",
                     (int)gsm_scan_rect().x, (int)gsm_scan_rect().y - 64, 18,
                     (Color){ 151, 174, 188, 255 });
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
            const struct gsm_sch_symbols *sym = &app->gsm.sch_symbols;

            if (app->gsm.sch_valid && sym->count > 0) {
                bparams.data = sym->corr;
                bparams.count = sym->count;
                sdrgui_burst_chart(&bparams);

                /* Soft magnitudes are raw |Im|, so their scale is the
                   signal's: tens of thousands on a strong capture, thousandths
                   on a weak live burst. An absolute axis therefore shows
                   either nothing useful or nothing at all -- a live ARFCN drew
                   an empty panel while decoding perfectly well. What the chart
                   is for is which bits within one burst were weak, so it plots
                   each magnitude against the burst's own 90th percentile.

                   Not against the maximum, for the reason the constellation
                   below records: the largest bar would then touch the top of
                   every burst by construction, and how far the rest sat below
                   it would depend on that one bar. Genuine outliers keep going
                   past 1.0 instead, up to the axis top. */
                float soft[GSM_SCH_BURST_BITS];
                int soft_count = sym->count < GSM_SCH_BURST_BITS
                                     ? sym->count : GSM_SCH_BURST_BITS;
                double soft_sorted[GSM_SCH_BURST_BITS];
                for (int i = 0; i < soft_count; i++)
                    soft_sorted[i] = sym->soft_mag[i];
                qsort(soft_sorted, (size_t)soft_count, sizeof(*soft_sorted),
                      compare_double);
                double soft_reference =
                    soft_sorted[(int)((double)(soft_count - 1) * 0.9)];
                if (soft_reference < 1e-12)
                    soft_reference = 1e-12;
                for (int i = 0; i < soft_count; i++) {
                    float value = (float)(sym->soft_mag[i] / soft_reference);
                    soft[i] = value > 1.4f ? 1.4f : value;
                }
                bparams.plot = r_soft;
                bparams.data = soft;
                bparams.count = soft_count;
                bparams.type = SDRGUI_BURST_BAR;
                bparams.y_min = 0.0f;
                bparams.y_max = 1.4f;
                bparams.title = "Soft Symbol Magnitudes";
                sdrgui_burst_chart(&bparams);
                bparams.count = sym->count;

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
            draw_waterfall_rect(app, 1, wf, app->gsm.selected_hz);
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
        draw_waterfall_rect(app, 1, wf, app->gsm.selected_hz);

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
    const struct gsm_sch_symbols *sym_c = &app->gsm.sch_symbols;
    int n = app->gsm.sch_valid ? sym_c->count : 0;
    float cx[GSM_SCH_BURST_BITS];
    float cy[GSM_SCH_BURST_BITS];
    if (n > GSM_SCH_BURST_BITS)
        n = GSM_SCH_BURST_BITS;
    const unsigned char *color_bits =
        app->gsm.const_derotated ? sym_c->chan : sym_c->bit;
    /* Raw display coords per representation. */
    double cmag[GSM_SCH_BURST_BITS];
    for (int i = 0; i < n; i++) {
        float rx, ry;
        if (app->gsm.const_derotated) {
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
        if (app->gsm.const_amplitude) {
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
    draw_button(gsm_const_amp_button(), "Amp", app->gsm.const_amplitude);
    draw_button(gsm_const_derot_button(), "Derot", app->gsm.const_derotated);
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
            if (i == 0) app->gsm.opt_filter = !app->gsm.opt_filter;
            if (i == 1) app->gsm.opt_finecfo = !app->gsm.opt_finecfo;
            if (i == 2) app->gsm.opt_trellis = !app->gsm.opt_trellis;
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
            app->gsm.selected_hz = 0.0;
            return;
        }
    }

    if (clicked(gsm_const_amp_button())) {
        app->gsm.const_amplitude = !app->gsm.const_amplitude;
        return;
    }
    if (clicked(gsm_const_derot_button())) {
        app->gsm.const_derotated = !app->gsm.const_derotated;
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

/* The decode options this view starts with: all three refinements on, and the
   constellation showing amplitude rather than a unit circle. */
void view_gsm_defaults(struct app *app) {
    app->gsm.const_amplitude = 1;
    app->gsm.opt_filter = 1;
    app->gsm.opt_finecfo = 1;
    app->gsm.opt_trellis = 1;
}

/* Enter the GSM decode view: pick the default channel and show it in the
   waterfall above. Priority: the channel a calibration is using, else the last
   channel the user selected, else run a band scan and auto-pick the strongest
   BCCH when it finishes. */
void enter_gsm(struct app *app) {
    if (app->receiver_mode && !app->gsm.return_valid) {
        app->gsm.return_frequency = app->applied_frequency;
        app->gsm.return_valid = 1;
    }
    int arfcn = 0;
    if (app->gsm_cal_arfcn > 0)
        arfcn = app->gsm_cal_arfcn;
    else if (app->scan_selected_arfcn > 0)
        arfcn = app->scan_selected_arfcn;
    if (arfcn > 0) {
        gsm_tune_selected(app, arfcn);
        app->gsm_analysis_mode = 1; /* Default to Burst mode when inspecting */
    } else if (app->receiver_mode) {
        if (start_scan(app) == 0) {
            app->scan_open = 0;
            app->gsm_autoselect_pending = 1;
        }
    }
}

/* Leave the GSM decode view: stop any scan and restore the entry tuning. */
void leave_gsm(struct app *app) {
    app->scan_running = 0;
    app->scan_open = 0;
    app->gsm_autoselect_pending = 0;
    if (app->receiver_mode && app->gsm.return_valid)
        retune_receiver(app, app->gsm.return_frequency, app->applied_ppm);
    app->gsm.return_valid = 0;
    app->gsm.selected_hz = 0.0;
    app->gsm.sch_valid = 0;
}
