#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int scan_strongest_arfcn(const struct app *app) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        if (app->scan_power[arfcn] > best_power) {
            best_power = app->scan_power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

int scan_strongest_bcch(const struct app *app) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        if (app->scan_bcch_conf[arfcn] >= SCAN_BCCH_MIN_CONF &&
            app->scan_power[arfcn] > best_power) {
            best_power = app->scan_power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

int start_scan(struct app *app) {
    if (!app->receiver_mode) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Channel scan requires a live RTL-SDR receiver");
        return -1;
    }
    double accept_half = app->applied_sample_rate / 2.0 - SCAN_EDGE_MARGIN_HZ;
    if (accept_half < 100000.0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Channel scan requires a sample rate of at least 1 MS/s");
        return -1;
    }
    app->bandscan.accept_half_hz = accept_half;
    app->bandscan.step_hz = 2.0 * accept_half;
    app->bandscan.first_center_hz = SCAN_BAND_LOWER_HZ + accept_half;
    int count = 0;
    double center = app->bandscan.first_center_hz;
    while (center - accept_half < SCAN_BAND_UPPER_HZ) {
        count++;
        center += app->bandscan.step_hz;
    }
    app->scan_step_count = count;
    for (int arfcn = 0; arfcn < 125; arfcn++) {
        app->scan_power[arfcn] = SCAN_SENTINEL_DBFS;
        app->scan_bcch_conf[arfcn] = 0.0f;
    }
    app->scan_selected_arfcn = 0;
    app->bandscan.return_frequency = app->applied_frequency;
    app->scan_step = 0;
    if (retune_receiver(app, (uint32_t)llround(app->bandscan.first_center_hz),
                        app->applied_ppm) < 0)
        return -1;
    app->bandscan.step_started_at = GetTime();
    app->scan_running = 1;
    app->scan_open = 1;
    return 0;
}

void update_scan(struct app *app) {
    if (!app->scan_running || !app->spectrum_ready)
        return;
    double elapsed = GetTime() - app->bandscan.step_started_at;
    if (elapsed < SCAN_STEP_SETTLE_SECONDS)
        return;

    double center = (double)app->applied_frequency;
    double lower = center - app->applied_sample_rate / 2.0;
    double upper = center + app->applied_sample_rate / 2.0;
    sdr_dsp_channel_powers(app->spectrum_average, SDR_DSP_FFT_SIZE,
                              lower, upper,
                              center - app->bandscan.accept_half_hz,
                              center + app->bandscan.accept_half_hz,
                              GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ,
                              1, 124, app->scan_power);

    /* Flag BCCH channels: probe each channel in this step's window for its
       FCCH tone (carrier + 67.708 kHz). FCCH is intermittent and a strong
       neighbour can lower its coherence, so keep the peak coherence seen at
       the tone offset across the step's blocks (never clearing it) and treat a
       channel as BCCH at a slightly relaxed scan threshold. */
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        double channel = GSM900_BASE_HZ +
                         (double)arfcn * GSM900_ARFCN_SPACING_HZ;
        if (channel < center - app->bandscan.accept_half_hz ||
            channel > center + app->bandscan.accept_half_hz)
            continue;
        struct gsm_fcch_result fcch;
        double target = channel - center + GSM_FCCH_TONE_HZ;
        gsm_fcch_detect(app->i_samples, app->q_samples,
                               app->pair_count, app->applied_sample_rate,
                               target, GSM_FCCH_SEARCH_HALF_HZ, &fcch);
        if (fcch.confidence > app->scan_bcch_conf[arfcn])
            app->scan_bcch_conf[arfcn] = fcch.confidence;
    }

    if (elapsed < SCAN_STEP_SETTLE_SECONDS + SCAN_STEP_PROBE_SECONDS)
        return; /* keep probing this step for more FCCH bursts */

    app->scan_step++;
    if (app->scan_step >= app->scan_step_count) {
        app->scan_running = 0;
        int bcch = scan_strongest_bcch(app);
        int chosen = bcch > 0 ? bcch : scan_strongest_arfcn(app);
        if (chosen > 0 && app->gsm_autoselect_pending &&
            app->tab == TAB_DECODE && app->decode == DECODE_GSM &&
            !app->calibration_open) {
            app->gsm_autoselect_pending = 0;
            app->gsm_analysis_mode = 1;     /* Default to Burst mode after scan */
            gsm_tune_selected(app, chosen); /* show the best channel above */
        } else {
            app->scan_selected_arfcn = chosen;
        }
        return;
    }
    double next = app->bandscan.first_center_hz +
                  (double)app->scan_step * app->bandscan.step_hz;
    if (retune_receiver(app, (uint32_t)llround(next), app->applied_ppm) < 0) {
        app->scan_running = 0;
        return;
    }
    app->bandscan.step_started_at = GetTime();
}

static int scan_arfcn_at(const struct app *app, Vector2 point) {
    return sdrgui_scan_chart_channel_at(app->plot, 124, point);
}

void draw_scan(struct app *app) {
    char text[160];
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };
    Rectangle rescan = { (float)GetScreenWidth() - 212.0f, 18, 88, 34 };
    int strongest = scan_strongest_arfcn(app);
    int strongest_bcch = scan_strongest_bcch(app);

    DrawText("GSM 900 channel power scan", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    draw_button(rescan, "Rescan", !app->scan_running);
    draw_button(back, "Back", 0);

    if (app->scan_running)
        snprintf(text, sizeof(text),
                 "Scanning ARFCN band... step %d / %d",
                 app->scan_step + 1, app->scan_step_count);
    else if (strongest_bcch > 0)
        snprintf(text, sizeof(text),
                 "Strongest BCCH ARFCN %d at %.1f dBFS (conf %.2f)   click a green channel to calibrate it",
                 strongest_bcch, app->scan_power[strongest_bcch],
                 app->scan_bcch_conf[strongest_bcch]);
    else if (strongest > 0)
        snprintf(text, sizeof(text),
                 "No BCCH detected; strongest channel ARFCN %d at %.1f dBFS   click a channel to try it",
                 strongest, app->scan_power[strongest]);
    else
        snprintf(text, sizeof(text),
                 "No channels measured; press Rescan");
    DrawText(text, 24, 54, 18, (Color){ 190, 208, 218, 255 });

    int hover = (!app->scan_running) ? scan_arfcn_at(app, GetMousePosition())
                                     : 0;
    struct sdrgui_scan_chart_params params = {
        app->plot, app->scan_power, app->scan_bcch_conf, 124,
        SCAN_SENTINEL_DBFS, SCAN_BCCH_MIN_CONF, hover,
        GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ, app->scan_selected_arfcn
    };
    sdrgui_scan_chart(&params);
}

void handle_scan_input(struct app *app) {
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };
    Rectangle rescan = { (float)GetScreenWidth() - 212.0f, 18, 88, 34 };

    if (clicked(back) || IsKeyPressed(KEY_ESCAPE)) {
        if (app->receiver_mode)
            retune_receiver(app, app->bandscan.return_frequency, app->applied_ppm);
        app->scan_running = 0;
        app->scan_open = 0;
        return;
    }
    if (app->scan_running)
        return;
    if (clicked(rescan)) {
        start_scan(app);
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int arfcn = scan_arfcn_at(app, GetMousePosition());
        if (arfcn > 0 && app->scan_power[arfcn] > SCAN_SENTINEL_DBFS) {
    calibration_select_channel(app, arfcn);
            app->scan_open = 0;
            start_calibration(app);
        }
    }
}
