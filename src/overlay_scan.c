#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan_layout.h"
#include "view.h"
#include "sdrgui.h"

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
    app->bandscan.step_started_at = GetTime();
    app->bandscan.running = 1;
    app->bandscan.open = 1;
    return 0;
}

void update_scan(struct app *app) {
    if (!app->bandscan.running || !app->frame.spectrum_ready)
        return;
    double elapsed = GetTime() - app->bandscan.step_started_at;
    enum scan_step_phase phase = scan_step_phase_at(elapsed, app->bandscan.step,
                                                    app->bandscan.plan.step_count);

    if (phase == SCAN_STEP_SETTLING)
        return;

    double center = (double)app->applied.frequency_hz;
    double lower = center - app->applied.sample_rate_hz / 2.0;
    double upper = center + app->applied.sample_rate_hz / 2.0;
    sdr_dsp_channel_powers(app->frame.spectrum_average, SDR_DSP_FFT_SIZE,
                              lower, upper,
                              center - app->bandscan.plan.accept_half_hz,
                              center + app->bandscan.plan.accept_half_hz,
                              GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ,
                              1, 124, app->bandscan.power);

    /* Flag BCCH channels: probe each channel in this step's window for its
       FCCH tone (carrier + 67.708 kHz). FCCH is intermittent and a strong
       neighbour can lower its coherence, so keep the peak coherence seen at
       the tone offset across the step's blocks (never clearing it) and treat a
       channel as BCCH at a slightly relaxed scan threshold. */
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        double channel = GSM900_BASE_HZ +
                         (double)arfcn * GSM900_ARFCN_SPACING_HZ;
        if (!scan_plan_covers(&app->bandscan.plan, center, channel))
            continue;
        struct gsm_fcch_result fcch;
        double target = channel - center + GSM_FCCH_TONE_HZ;
        gsm_fcch_detect(app->frame.i_samples, app->frame.q_samples,
                               app->frame.pair_count, app->applied.sample_rate_hz,
                               target, GSM_FCCH_SEARCH_HALF_HZ, &fcch);
        app->bandscan.bcch_conf[arfcn] =
            scan_hold_confidence(app->bandscan.bcch_conf[arfcn], fcch.confidence);
    }

    if (phase == SCAN_STEP_PROBING)
        return; /* keep probing this step for more FCCH bursts */

    app->bandscan.step++;
    if (phase == SCAN_STEP_FINISHED) {
        app->bandscan.running = 0;
        int chosen = scan_choose(app->bandscan.power, app->bandscan.bcch_conf);
        if (chosen > 0 && app->bandscan.autoselect &&
            app->tab == TAB_DECODE && app->decode == DECODE_GSM &&
            !app->cal.open) {
            app->bandscan.autoselect = 0;
            app->gsm.analysis_mode = 1;     /* Default to Burst mode after scan */
            gsm_tune_selected(app, chosen); /* show the best channel above */
            /* Keeping this channel is the point of having scanned, so the
               claim is given up rather than returned -- the same shape as the
               survey's "Open waterfall" handoff. */
            receiver_commit(app, &app->bandscan.lease_token);
        } else {
            /* Nobody asked to go anywhere, so put the receiver back on the
               channel the GSM view was inspecting. It used to be left on the
               last step of the sweep, which is a frequency nobody chose. */
            app->gsm.selected_arfcn = chosen;
            receiver_return(app, &app->bandscan.lease_token);
        }
        return;
    }
    double next = scan_plan_step_centre(&app->bandscan.plan, app->bandscan.step);
    if (retune_receiver(app, (uint32_t)llround(next), app->applied.ppm) < 0) {
        app->bandscan.running = 0;
        receiver_return(app, &app->bandscan.lease_token);
        return;
    }
    app->bandscan.step_started_at = GetTime();
}

static int scan_arfcn_at(const struct app *app, Vector2 point) {
    return sdrgui_scan_chart_channel_at(app->plot, 124, point);
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
        app->plot, app->bandscan.power, app->bandscan.bcch_conf, 124,
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
