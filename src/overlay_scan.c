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
