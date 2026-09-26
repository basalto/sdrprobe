#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan_layout.h"
#include "view.h"
#include "sdrgui.h"
#include "debug_log.h"

/*
 * The GSM 900 band scan: sweep the downlink, chart each channel's power, and
 * flag the ones carrying an FCCH tone so a calibration reference can be
 * picked.
 *
 * Split from the calibration overlay it feeds because it barely touches it --
 * one field, now behind calibration_select_channel(). The scan produces a
 * choice; calibration consumes it.
 */

/* Both selectors, and the plan below, are in scan_plan.h; these are the
   adapters that hand it the arrays out of struct app. */
int scan_strongest_arfcn(const struct app *app) {
    return scan_select_strongest(app->bandscan.power);
}

int scan_strongest_bcch(const struct app *app) {
    return scan_select_bcch(app->bandscan.power, app->bandscan.bcch_conf);
}

int start_scan(struct app *app) {
    if (!app->receiver_mode) {
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Channel scan requires a live receiver");
        return -1;
    }
    if (scan_plan_make((double)app->applied.sample_rate_hz, &app->bandscan.plan) !=
        SCAN_PLAN_OK) {
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Channel scan requires a sample rate of at least 1 MS/s");
        return -1;
    }
    app->bandscan.plan.step_count = app->bandscan.plan.step_count;
    for (int arfcn = 0; arfcn <= SCAN_ARFCN_LAST; arfcn++) {
        app->bandscan.power[arfcn] = SCAN_SENTINEL_DBFS;
        app->bandscan.bcch_conf[arfcn] = 0.0f;
    }
    app->gsm.selected_arfcn = 0;
    app->bandscan.step = 0;
    /*
     * Borrowed from whatever the GSM view had tuned, so finishing puts the
     * receiver back on the channel being inspected rather than on whatever
     * was on screen before GSM was entered. A rescan while the claim is still
     * held keeps it: where the scan should return to has not changed.
     */
    if (receiver_lease_token_active(&app->bandscan.lease_token)) {
        if (retune_receiver(app,
                            (uint32_t)llround(app->bandscan.plan.first_center_hz),
                            app->applied.ppm) < 0)
            return -1;
    } else if (receiver_borrow_at(app, &app->bandscan.lease_token,
                                  (uint32_t)llround(app->bandscan.plan.first_center_hz),
                                  0) < 0) {
        return -1;
    }
    app->bandscan.step_started_at = monotonic_seconds();
    app->bandscan.running = 1;
    app->bandscan.open = 1;
    debug_log_write("gsm-scan", "begin, %d steps, %.1f s",
                    app->bandscan.plan.step_count,
                    app->bandscan.plan.step_count *
                        (SCAN_STEP_SETTLE_SECONDS + SCAN_STEP_PROBE_SECONDS));
    return 0;
}


static int scan_arfcn_at(const struct app *app, Vector2 point) {
    return sdrgui_scan_chart_channel_at(app->gui->plot, 124, point);
}

void draw_scan(struct app *app) {
    char text[160];
    struct scan_layout l = scan_layout_for((float)GetScreenWidth());
    Rectangle back = l.back;
    Rectangle rescan = l.rescan;
    int strongest = scan_strongest_arfcn(app);
    int strongest_bcch = scan_strongest_bcch(app);

    DrawText("GSM 900 channel power scan", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    draw_button(rescan, "Rescan", !app->bandscan.running);
    draw_button(back, "Back", 0);

    if (app->bandscan.running)
        snprintf(text, sizeof(text),
                 "Scanning ARFCN band... step %d / %d",
                 app->bandscan.step + 1, app->bandscan.plan.step_count);
    else if (strongest_bcch > 0)
        snprintf(text, sizeof(text),
                 "Strongest BCCH ARFCN %d at %.1f dBFS (conf %.2f)   click a green channel to calibrate it",
                 strongest_bcch, app->bandscan.power[strongest_bcch],
                 app->bandscan.bcch_conf[strongest_bcch]);
    else if (strongest > 0)
        snprintf(text, sizeof(text),
                 "No BCCH detected; strongest channel ARFCN %d at %.1f dBFS   click a channel to try it",
                 strongest, app->bandscan.power[strongest]);
    else
        snprintf(text, sizeof(text),
                 "No channels measured; press Rescan");
    DrawText(text, 24, 54, 18, (Color){ 190, 208, 218, 255 });

    int hover = (!app->bandscan.running) ? scan_arfcn_at(app, GetMousePosition())
                                     : 0;
    struct sdrgui_scan_chart_params params = {
        app->gui->plot, app->bandscan.power, app->bandscan.bcch_conf, 124,
        SCAN_SENTINEL_DBFS, SCAN_BCCH_MIN_CONF, hover,
        GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ, app->gsm.selected_arfcn,
        "no channel measured yet"
    };
    sdrgui_scan_chart(&params);
}

/*
 * Stop owning the receiver, for the paths that abandon a scan from outside it
 * -- leaving the GSM view while one runs, or opening calibration. The lease
 * refuses an out-of-order return, so the inner claim has to go first or the
 * outer owner cannot give the receiver back at all.
 *
 * A no-op when no scan is running, so callers need no guard.
 */
void scan_release_receiver(struct app *app) {
    receiver_return(app, &app->bandscan.lease_token);
}

void handle_scan_input(struct app *app) {
    struct scan_layout l = scan_layout_for((float)GetScreenWidth());
    Rectangle back = l.back;
    Rectangle rescan = l.rescan;

    if (clicked(back) || IsKeyPressed(KEY_ESCAPE)) {
        receiver_return(app, &app->bandscan.lease_token);
        app->bandscan.running = 0;
        app->bandscan.open = 0;
        return;
    }
    if (app->bandscan.running)
        return;
    if (clicked(rescan)) {
        start_scan(app);
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int arfcn = scan_arfcn_at(app, GetMousePosition());
        if (arfcn > 0 && app->bandscan.power[arfcn] > SCAN_SENTINEL_DBFS) {
    calibration_select_channel(app, arfcn);
            app->bandscan.open = 0;
            start_calibration(app);
        }
    }
}
