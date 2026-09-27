#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui/view.h"
#include "runtime/calibration_gate.h"
#include "tech/lte_dsp.h"
#include "gui/chrome_layout.h"
#include "model/calibration_view_model.h"
#include "gui/calibration_layout.h"
#include "runtime/calibration_nav.h"
#include "tech/lte_scan.h"
#include "runtime/debug_log.h"

/*
 * Time here is monotonic_seconds(), not raylib's GetTime(). Only differences
 * are ever taken, so the epoch does not matter -- but raylib's clock needs a
 * window, and the calibration has to be runnable without one. A gate this
 * program will not open unless somebody clicks is a decision no check can
 * reach, which is the thing ADR-0012 forbids.
 */
#include "gui/sdrgui.h"

/*
 * Which bands this receiver offers, and which of them is chosen.
 *
 * Two helpers rather than open code at four sites: the row of buttons, the
 * label under the scan, and the layout that places them all have to agree
 * about the same list, and they used to agree by all reading one compiled-in
 * literal -- which is why they agreed and were all wrong on any tuner but an
 * R820T.
 */

/* Whether the 2G scan left results worth returning to. Its powers survive the
   overlay closing -- only a fresh scan clears them -- so reopening the list
   costs nothing and shows what was actually measured. */
static int calibration_scan_has_results(const struct app *app) {
    int arfcn;
    for (arfcn = 1; arfcn <= 124; arfcn++)
        if (app->bandscan.power[arfcn] > SCAN_SENTINEL_DBFS)
            return 1;
    return 0;
}

void adjust_waterfall_scale(struct app *app, int zoom_in) {
    app->waterfall_lower_dbfs += zoom_in ? DB_SCALE_STEP : -DB_SCALE_STEP;
    app->waterfall_lower_dbfs = fmaxf(
        SDR_DSP_DBFS_FLOOR,
        fminf(app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS - 20.0f));
    render_waterfall(app);
}

void handle_calibration_input(struct app *app) {
    struct calibration_layout cl =
        calibration_layout_now(app->cal.technology == 1,
                               cal_band_count(app));
    Rectangle tech_2g = cl.tech[0];
    Rectangle tech_4g = cl.tech[1];
    Rectangle tech_5g = cl.tech[2];
    Rectangle scan = cl.scan;
    Rectangle start = cl.start;
    Rectangle apply_ppm = cl.apply_ppm;
    Rectangle back = cl.back;

    int inputs_changed = 0;

    /*
     * The 4G controls: pick a band, scan it, pick a cell. Typing an EARFCN
     * still works and is faster when you know one, but nobody knows one for a
     * band they have not looked at -- which is the whole reason GSM has a scan
     * and this needed one.
     */
    if (app->cal.technology == 1 && !app->cal.running) {
        int b;
        for (b = 0; b < CALIBRATION_LTE_BANDS; b++)
            if (clicked(cl.lte_band[b])) {
                app->cal.lte_band = b;
                app->lte.scan.found_count = 0;
                inputs_changed = 1;
            }
        if (clicked(cl.lte_scan) && !app->cal.lte_scanning) {
            const struct lte_band *band =
                cal_selected_band(app);
            if (!app->receiver_mode) {
                snprintf(app->cal.status,
                         sizeof(app->cal.status),
                         "A band scan needs a live receiver");
            } else {
                /* The scan runs on LTE's own grid, like everything else that
                   looks for a cell (ADR-0014). */
                int acquired;
                if (calibration_borrow(app, &acquired) == 0 &&
                    retune_receiver_at_rate(app, app->applied.frequency_hz,
                                            LTE_SAMPLE_RATE_HZ,
                                            app->applied.ppm) == 0 &&
                    band &&
                    lte_scan_begin(app, band->band, monotonic_seconds()) == 0) {
                    app->cal.lte_scanning = 1;
                    snprintf(app->cal.status,
                             sizeof(app->cal.status),
                             "Scanning band %d, about %.0f s for the first "
                             "pass", band->band,
                             lte_scan_first_pass_seconds(band));
                } else {
                    snprintf(app->cal.status,
                             sizeof(app->cal.status),
                             "Could not start the band scan");
                }
            }
            return;
        }
        if (!app->cal.lte_scanning && app->lte.scan.found_count > 0) {
            int row = calibration_cell_row_at(cl.cell_list,
                                              app->lte.scan.found_count,
                                              GetMousePosition());
            if (row >= 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                /* Picking a cell is choosing the EARFCN and starting on it,
                   which is what the operator meant by clicking it. */
                snprintf(app->cal.channel, sizeof(app->cal.channel), "%u",
                         app->lte.scan.found[row].earfcn);
                app->cal.channel_length = (int)strlen(app->cal.channel);
                start_calibration(app);
                return;
            }
        }
    }

    if (!app->cal.running && clicked(tech_2g)) {
        calibration_select_technology(app, 0);
        inputs_changed = 1;
    }
    if (!app->cal.running && clicked(tech_4g)) {
        calibration_select_technology(app, 1);
        inputs_changed = 1;
    }
    if (!app->cal.running && clicked(tech_5g)) {
        calibration_select_technology(app, 2);
        inputs_changed = 1;
    }

    int character;
    while (app->cal.technology <= 1 &&
           (character = GetCharPressed()) != 0) {
        if (character >= '0' && character <= '9' &&
            app->cal.channel_length <
                (int)sizeof(app->cal.channel) - 1) {
            app->cal.channel[app->cal.channel_length++] =
                (char)character;
            app->cal.channel[app->cal.channel_length] = '\0';
            inputs_changed = 1;
        }
    }
    if (app->cal.technology <= 1 &&
        IsKeyPressed(KEY_BACKSPACE) &&
        app->cal.channel_length > 0) {
        app->cal.channel[--app->cal.channel_length] = '\0';
        inputs_changed = 1;
    }
    if (inputs_changed) {
        app->cal.track.stable = 0;
        app->cal.track.measurements = 0;
        app->cal.track.recent_count = 0;
        app->cal.track.recent_head = 0;
        app->cal.track.recent_center = 0.0;
        app->cal.track.recent_spread = 0.0;
        app->cal.track.recent_sem = 0.0;
        if (app->cal.running)
            snprintf(app->cal.status,
                     sizeof(app->cal.status),
                     "Editing target ARFCN; press Start to retune");
    }
    if ((clicked(start) || IsKeyPressed(KEY_ENTER)) &&
        app->cal.technology == 0)
        start_calibration(app);
    if (clicked(scan) && app->cal.technology == 0)
        start_scan(app);
    /*
     * Claiming a correction from before calibrations named a receiver. The
     * operator's explicit act, which is what ADR-0018 requires instead of the
     * program guessing whose crystal an old number compensates.
     */
    if (cl.claim_ppm.width > 0.0f && clicked(cl.claim_ppm)) {
        int legacy = 0;

        if (installation_legacy_ppm(&app->installation, &legacy) &&
            installation_claim_legacy(&app->installation) == 0 &&
            installation_commit(&app->installation, &app->config) == 0)
            snprintf(app->cal.status,
                     sizeof(app->cal.status),
                     "Claimed %+d PPM for this receiver at \"%s\"", legacy,
                     app->installation.site);
        else
            snprintf(app->cal.status,
                     sizeof(app->cal.status),
                     "Nothing to claim, or this receiver has no identity");
    }
    if (clicked(apply_ppm) && app->cal.track.stable) {
        int was = app->applied.ppm;
        if (retune_receiver(app, app->cal.tune_hz,
                            app->cal.suggested_ppm) == 0) {
            /* What was applied and what it replaced. Overwriting a measured
               correction is the most destructive thing this program does to
               its own state, and until this it happened with a line on a
               stream a windowed run does not capture. */
            debug_log_write("cal",
                            "apply %+d ppm (was %+d) source %s measurements %d "
                            "sem_ppm %.2f",
                            app->cal.suggested_ppm, was,
                            calibration_source_name(app->cal.track.source),
                            app->cal.track.measurements,
                            app->cal.track.recent_sem);
            app->options.ppm = app->cal.suggested_ppm;
            if (app->cal.technology == 1) {
                app->cal.lte_valid = 1;
                app->cal.lte_ppm = app->cal.suggested_ppm;
            }
            /*
             * A measured correction belongs to where it was measured **and to
             * the crystal it compensates** (ADR-0018). One writer, which is
             * what installation_commit() is for.
             */
            if (installation_record_ppm(&app->installation,
                                        app->cal.suggested_ppm) == 0)
                installation_commit(&app->installation, &app->config);
            app->cal.track.measurements = 0;
            app->cal.track.recent_count = 0;
            app->cal.track.recent_head = 0;
            app->cal.track.stable = 0;
            app->cal.started_at = monotonic_seconds();
            snprintf(app->cal.status,
                     sizeof(app->cal.status),
                     "Applied %+d PPM; measuring residual error",
                     app->applied.ppm);
            /* The health indicator turns green only for an FCCH-backed lock;
               record the calibrated channel so drift can be re-checked. */
            if (app->cal.track.source == CALIBRATION_SOURCE_FCCH) {
                int arfcn = 0;
                parse_int(app->cal.channel, &arfcn);
                app->cal.gsm_valid = 1;
                app->cal.gsm_expected_hz = app->cal.expected_hz;
                app->cal.gsm_tune_hz = app->cal.tune_hz;
                app->cal.gsm_ppm = app->applied.ppm;
                app->cal.gsm_arfcn = arfcn;
                app->cal.drift_health = CAL_HEALTH_GOOD;
                app->cal.drift_ppm = 0.0;
                app->cal.drift_notice[0] = '\0';
                app->cal.drift_phase = DRIFT_IDLE;
                app->cal.drift_last_check_at = monotonic_seconds();
            } else {
                app->cal.gsm_valid = 0;
                app->cal.drift_health = CAL_HEALTH_UNKNOWN;
                app->cal.drift_notice[0] = '\0';
            }
        }
    }
    /*
     * Back is one step up, Exit leaves. Escape follows Back while there is a
     * step to take and Exit once there is not, so it never traps and never
     * skips the list on the way out.
     */
    {
        enum calibration_back target =
            calibration_back_target(app->cal.technology,
                                    app->cal.running,
                                    calibration_scan_has_results(app));
        int escape = IsKeyPressed(KEY_ESCAPE);

        if ((clicked(back) || escape) && target != CALIBRATION_BACK_NONE) {
            if (calibration_stop_measuring(app) == 0 &&
                target == CALIBRATION_BACK_SCAN)
                app->bandscan.open = 1;
            return;
        }
        if (clicked(cl.exit) || escape) {
            close_calibration(app);
            /* Out of calibration is out to the survey: it is where the
               program opens and the only view that says what is on air
               rather than what one tuning looks like. */
            if (!app->cal.open)
                app->tab = TAB_SURVEY;
        }
    }
}

Rectangle calibration_chart_rect(const struct app *app) {
    return calibration_layout_now(app->cal.technology == 1,
                                  cal_band_count(app)).chart;
}

void draw_calibration(struct app *app) {
    char text[256];
    struct calibration_view_model m;
    struct calibration_layout cl =
        calibration_layout_now(app->cal.technology == 1,
                               cal_band_count(app));
    Rectangle tech_2g = cl.tech[0];
    Rectangle tech_4g = cl.tech[1];

    /*
     * What this screen says, decided once (`web-visualization/16`, `/17`):
     * the residual and its guard, what measured it, whether the gate is
     * satisfied and -- when it is not -- which clause is outstanding. The
     * drawing picks a colour for a verdict it was handed.
     */
    calibration_view_model_build(&app->cal, GetTime(), app->applied.ppm, &m);
    Rectangle tech_5g = cl.tech[2];
    Rectangle scan = cl.scan;
    Rectangle channel = cl.channel;
    Rectangle start = cl.start;
    Rectangle apply_ppm = cl.apply_ppm;
    Rectangle back = cl.back;

    DrawText("Cellular frequency calibration", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    DrawText("Technology", 24, 50, 16, (Color){ 157, 180, 194, 255 });
    draw_button(tech_2g, "2G", app->cal.technology == 0);
    draw_button(tech_4g, "4G", app->cal.technology == 1);
    draw_button(tech_5g, "5G", app->cal.technology == 2);
    sdrgui_text_fit(app->cal.technology == 0
                        ? "Band: GSM 900"
                        : app->cal.technology == 1
                              ? "Band: LTE, 1.92 MS/s while measuring"
                              : "Band: unavailable",
                    (int)cl.band_label.x, (int)cl.band_label.y, 17,
                    cl.band_label.width, (Color){ 209, 221, 228, 255 });
    DrawText(app->cal.technology == 1 ? "EARFCN" : "ARFCN",
             (int)channel.x, 50, 16, (Color){ 157, 180, 194, 255 });
    /*
     * The picker takes the chart's rectangle, so exactly one of them is drawn.
     * Both were, and the waterfall went over the list -- an overlap no
     * geometry check can see, because the two agree about where they are.
     */
    if (app->cal.technology == 1 && !app->cal.running) {
        char row[128];
        int b, i, rows;

        {
            int bands[LTE_BANDS_MAX];
            int count = view_lte_bands(app, bands);

            for (b = 0; b < count; b++) {
                snprintf(row, sizeof(row), "Band %d", bands[b]);
                draw_button(cl.lte_band[b], row, b == app->cal.lte_band);
            }
        }
        draw_button(cl.lte_scan,
                    app->cal.lte_scanning ? "Scanning" : "Scan band",
                    app->cal.lte_scanning);

        DrawRectangleRec(cl.cell_list, (Color){ 6, 10, 17, 255 });
        DrawRectangleLinesEx(cl.cell_list, 1.0f, (Color){ 82, 109, 126, 255 });
        snprintf(row, sizeof(row), "Cells found (%d)",
                 app->lte.scan.found_count);
        DrawText(row, (int)cl.cell_list.x + 10, (int)cl.cell_list.y + 8, 16,
                 (Color){ 151, 174, 188, 255 });
        rows = app->lte.scan.found_count;
        if (rows > calibration_cell_rows(cl.cell_list))
            rows = calibration_cell_rows(cl.cell_list);
        if (rows == 0) {
            DrawText(app->cal.lte_scanning ? "scanning..."
                                           : "pick a band and press Scan",
                     (int)cl.cell_list.x + 10, (int)cl.cell_list.y + 34, 16,
                     (Color){ 126, 151, 166, 255 });
        }
        for (i = 0; i < rows; i++) {
            const struct lte_found_cell *found = &app->lte.scan.found[i];
            float y = cl.cell_list.y + 30.0f +
                      CALIBRATION_CELL_ROW_H * (float)i;
            int hovered = calibration_cell_row_at(cl.cell_list, rows,
                                                  GetMousePosition()) == i;
            if (hovered)
                DrawRectangle((int)cl.cell_list.x + 1, (int)y,
                              (int)cl.cell_list.width - 2,
                              (int)CALIBRATION_CELL_ROW_H,
                              (Color){ 255, 174, 62, 40 });
            /* The correlation is on the row because it is what decides
               whether the offset this cell yields is worth believing. */
            snprintf(row, sizeof(row),
                     "EARFCN %-6u %9.3f MHz   cell %-4d PSS %.2f",
                     found->earfcn, found->frequency_hz / 1e6, found->pci,
                     (double)found->pss);
            DrawText(row, (int)cl.cell_list.x + 10, (int)y + 5, 16,
                     found->pss >= CALIBRATION_MIN_PSS
                         ? (Color){ 213, 226, 234, 255 }
                         : (Color){ 150, 140, 120, 255 });
        }
    }
    /* 4G takes a channel too now. The field said N/A because it was written
       when only 2G did, and nothing made it say otherwise when 4G started
       working -- so the overlay offered an EARFCN caption over a field that
       refused to show one. */
    sdrgui_text_field(channel,
                      app->cal.technology <= 1
                          ? app->cal.channel
                          : "N/A",
                      app->cal.technology <= 1);
    draw_button(start, app->cal.running ? "Retune" : "Start",
                app->cal.technology == 0);
    draw_button(apply_ppm, "Apply PPM", app->cal.track.stable);
    /*
     * Only when there is one to claim, and only when it fits. An unassigned
     * correction is invisible otherwise -- the operator would have to read a
     * stderr line from startup to know it exists.
     */
    {
        int legacy = 0;

        if (cl.claim_ppm.width > 0.0f &&
            installation_legacy_ppm(&app->installation, &legacy)) {
            char label[48];

            snprintf(label, sizeof(label), "Claim %+d PPM", legacy);
            draw_button(cl.claim_ppm, label, 0);
        }
    }
    draw_button(scan, "Scan", app->cal.technology == 0);
    draw_button_enabled(back, "Back",
                        calibration_back_target(app->cal.technology,
                                                app->cal.running,
                                                calibration_scan_has_results(app))
                            != CALIBRATION_BACK_NONE);
    draw_button(cl.exit, "Exit", 0);

    snprintf(text, sizeof(text),
             "expected: %.6f MHz   tuned center: %.6f MHz   current correction: %+d PPM",
             app->cal.expected_hz / 1000000.0,
             app->applied.frequency_hz / 1000000.0, app->applied.ppm);
    sdrgui_text_fit(text, (int)cl.status[0].x, (int)cl.status[0].y, 17,
                    cl.status[0].width, (Color){ 190, 208, 218, 255 });
    if (m.measurements > 0) {
        /*
         * The residual is the model's, guarded. This divided by
         * `app->cal.expected_hz` with nothing checking it was non-zero --
         * reachable only because a measurement implies a carrier, which is
         * an invariant this line was not the right place to rely on.
         */
        snprintf(text, sizeof(text),
                 "measured: %.6f MHz   offset: %+.1f kHz   observed: %+.2f PPM   center: %+.2f +/- %.2f PPM (SEM %.2f)   by %s",
                 app->cal.measured_hz / 1000000.0,
                 app->cal.offset_hz / 1000.0,
                 m.observed_ppm, m.centre_ppm, m.spread_ppm, m.sem_ppm,
                 m.source);
        sdrgui_text_fit(text, (int)cl.status[1].x, (int)cl.status[1].y, 17,
                        cl.status[1].width, (Color){ 255, 205, 91, 255 });
        /*
         * And **why it has not locked**, which nothing on this screen used
         * to say: the gate returns one bit, so a reader could not tell "it
         * needs four more seconds" from "the scatter is too wide to ever
         * settle". `calibration_gate_unmet()` names the first unsatisfied
         * clause, which is the one that will clear first.
         */
        snprintf(text, sizeof(text),
                 "peak: %.1f dBFS   guard floor: %.1f dBFS   prominence: %.1f dB   suggested correction: %+d PPM%s%s",
                 app->cal.peak_dbfs, app->cal.floor_dbfs,
                 app->cal.prominence_db, m.suggested_ppm,
                 m.unmet[0] ? "   waiting on: " : "", m.unmet);
        sdrgui_text_fit(text, (int)cl.status[2].x, (int)cl.status[2].y, 17,
                        cl.status[2].width,
                 m.locked ? (Color){ 99, 228, 170, 255 }
                          : (Color){ 250, 190, 74, 255 });
    }
    sdrgui_text_fit(m.status, (int)cl.status[3].x,
                    (int)cl.status[3].y, 17, cl.status[3].width,
             (Color){ 158, 204, 230, 255 });

    /* One or the other, never both. Before a cell is chosen the chart has
       nothing to say, and the picker is what the operator needs to see. */
    if (app->cal.technology == 1 && !app->cal.running)
        return;

    /*
     * Into the layout's chart, not app->plot.
     *
     * draw_waterfall draws into the Scope's plot rectangle, which begins
     * higher up the window than this overlay's chart does -- so the waterfall
     * came out over the status line, while the expected and measured markers
     * below were placed against cl.chart and therefore against a different
     * chart from the one they marked. check-layout could not see either: it
     * compares the rectangles the layout returns, and app->plot is not one of
     * them.
     */
    draw_waterfall_rect(app, 1, cl.chart, &app->cal.window);
    if (app->cal.expected_hz > 0) {
        double full_lower = (double)app->applied.frequency_hz -
                            app->applied.sample_rate_hz / 2.0;
        double full_upper = (double)app->applied.frequency_hz +
                            app->applied.sample_rate_hz / 2.0;
        double lower = (double)app->cal.expected_hz -
                       CALIBRATION_VIEW_HALF_WIDTH_HZ;
        double upper = (double)app->cal.expected_hz +
                       CALIBRATION_VIEW_HALF_WIDTH_HZ;
        if (lower < full_lower)
            lower = full_lower;
        if (upper > full_upper)
            upper = full_upper;
        if (upper - lower <= 1.0) {
            lower = full_lower;
            upper = full_upper;
        }
        float expected_x = cl.chart.x +
                           (float)((app->cal.expected_hz - lower) /
                                   (upper - lower)) * cl.chart.width;
        DrawLine((int)expected_x, (int)cl.chart.y, (int)expected_x,
                 (int)(cl.chart.y + cl.chart.height),
                 (Color){ 87, 229, 173, 230 });
        DrawText("expected", (int)expected_x + 5, (int)cl.chart.y + 5, 16,
                 (Color){ 111, 244, 191, 255 });
        if (app->cal.track.measurements > 0) {
            float measured_x = cl.chart.x +
                               (float)((app->cal.measured_hz - lower) /
                                       (upper - lower)) * cl.chart.width;
            DrawLine((int)measured_x, (int)cl.chart.y, (int)measured_x,
                     (int)(cl.chart.y + cl.chart.height),
                     (Color){ 255, 181, 59, 240 });
            DrawText("measured", (int)measured_x + 5,
                      (int)cl.chart.y + 25, 16,
                      (Color){ 255, 202, 105, 255 });
        }
    }
}




/*
 * One circle per reference. Two, because two independent measurements of one
 * crystal are worth far more than either alone -- and the moment they stop
 * agreeing is the moment to distrust the correction, which a single dot could
 * never show.
 */
void draw_health_indicator(const struct app *app) {
    struct chrome_layout chrome = chrome_layout_now();
    struct sdrgui_health_params gsm;
    struct sdrgui_health_params lte;
    Vector2 pointer = GetMousePosition();
    double now = monotonic_seconds();

    memset(&gsm, 0, sizeof(gsm));
    gsm.centre = chrome.gsm_dot;
    gsm.state = app->cal.drift_health;
    gsm.label = "GSM cal";
    gsm.channel_name = "ARFCN";
    gsm.channel = app->cal.gsm_arfcn;
    gsm.notice = app->cal.drift_notice;
    gsm.banner = chrome.gsm_banner;
    gsm.hover = chrome.hover;
    /*
     * The hit test is geometry and lives with the rest of it, so this asks
     * rather than measuring a distance here (sdrgui_point_in_circle).
     */
    gsm.hovered = sdrgui_point_in_circle(chrome.gsm_dot,
                                         SDRGUI_HEALTH_DOT_RADIUS, pointer.x,
                                         pointer.y);
    /*
     * A green dot said nothing until this. The detail is what a reader wants
     * from it: the correction applied, the reference that measured it, and
     * how long ago -- so that "calibrated" can be told from "calibrated this
     * morning, by something that has since gone off the air".
     */
    gsm.have_detail = app->cal.gsm_valid;
    gsm.ppm = app->cal.gsm_ppm;
    gsm.source = "FCCH tone";
    gsm.measured_ago = app->cal.drift_last_check_at > 0.0
                           ? now - app->cal.drift_last_check_at
                           : -1.0;
    sdrgui_health_dot(&gsm);

    memset(&lte, 0, sizeof(lte));
    lte.centre = chrome.lte_dot;
    /*
     * No drift monitor behind this one yet, so it says only what it knows:
     * measuring now, measured and applied, or nothing yet. Claiming a health
     * it has not checked would be worse than an honest grey.
     */
    if (app->cal.open && app->cal.running &&
        app->cal.technology == 1)
        lte.state = SDRGUI_HEALTH_CHECKING;
    else if (app->cal.lte_valid)
        lte.state = SDRGUI_HEALTH_GOOD;
    else
        lte.state = SDRGUI_HEALTH_UNKNOWN;
    lte.label = "LTE cal";
    lte.channel_name = "EARFCN";
    lte.channel = app->cal.lte_earfcn;
    lte.notice = NULL;
    lte.banner = chrome.lte_banner;
    lte.hover = chrome.hover;
    lte.hovered = sdrgui_point_in_circle(chrome.lte_dot,
                                         SDRGUI_HEALTH_DOT_RADIUS, pointer.x,
                                         pointer.y);
    lte.have_detail = app->cal.lte_valid;
    lte.ppm = app->cal.lte_ppm;
    lte.source = "4G cell";
    /* No re-check behind it, so it cannot say when -- and says nothing
       rather than borrowing the GSM check's clock. */
    lte.measured_ago = -1.0;
    sdrgui_health_dot(&lte);
}
