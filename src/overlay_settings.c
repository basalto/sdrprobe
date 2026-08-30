#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chrome_layout.h"
#include "view.h"
#include "sdrgui.h"
#include "raygui.h"

/*
 * The Settings panel: centre frequency, gain, PPM and the DC-spike filter,
 * plus the two buttons that open this panel and the calibration overlay.
 *
 * Applying a change can retune or restart acquisition, which is why
 * apply_settings reaches back into the application rather than only writing
 * fields.
 */

void open_settings(struct app *app) {
    snprintf(app->set.frequency, sizeof(app->set.frequency), "%u",
             app->applied_frequency);
    app->set.frequency_length = (int)strlen(app->set.frequency);
    snprintf(app->set.ppm, sizeof(app->set.ppm), "%d",
             app->applied_ppm);
    app->set.ppm_length = (int)strlen(app->set.ppm);
    app->set.focus = 0;
    app->set.gain_choice = 0;
    if (app->receiver_mode && app->applied_manual_gain) {
        for (int i = 0; i < app->supported_gain_count; i++)
            if (app->supported_gains[i] == app->applied_gain_tenths)
                app->set.gain_choice = i + 1;
    }
    app->settings_error[0] = '\0';
    app->set.remove_dc = app->remove_dc;
    app->set.auto_drift = app->auto_drift_check;
    app->settings_open = 1;
}

int apply_settings(struct app *app) {
    uint32_t frequency;
    int ppm;
    if (parse_frequency(app->set.frequency, &frequency) < 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Use Hz or a K/M/G value, for example 1090M");
        return -1;
    }
    if (parse_int(app->set.ppm, &ppm) < 0 || ppm < -1000 || ppm > 1000) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "PPM must be a signed integer from -1000 to 1000");
        return -1;
    }

    app->auto_drift_check = app->set.auto_drift;
    /* A manual PPM change is no longer FCCH-backed: drop to grey. */
    if (app->gsm_cal_valid && ppm != app->gsm_cal_ppm) {
        app->gsm_cal_valid = 0;
        app->drift_health = CAL_HEALTH_UNKNOWN;
        app->drift_notice[0] = '\0';
        app->drift_phase = DRIFT_IDLE;
    }

    if (!app->receiver_mode) {
        app->applied_frequency = frequency;
        app->options.frequency = frequency;
        app->options.ppm = ppm;
        app->applied_ppm = ppm;
        app->remove_dc = app->set.remove_dc;
        app->spectrum_ready = 0;
        app->spectrum_peak_ready = 0;
        if (recreate_waterfall(app, app->plot, 1) < 0) {
            snprintf(app->settings_error, sizeof(app->settings_error),
                     "Could not reset waterfall for the new frequency");
            return -1;
        }
        return 0;
    }

    int manual = app->set.gain_choice > 0;
    int gain = manual ? app->supported_gains[app->set.gain_choice - 1] : 0;
    int old_manual = app->applied_manual_gain;
    int old_gain = app->applied_gain_tenths;
    int old_ppm = app->applied_ppm;
    uint32_t old_frequency = app->applied_frequency;
    if (stop_acquisition(app) < 0)
        return -1;
    if (rtlsdr_set_tuner_gain_mode(app->dev, manual) < 0 ||
        (manual && rtlsdr_set_tuner_gain(app->dev, gain) < 0) ||
        set_frequency_correction(app->dev, ppm) < 0 ||
        rtlsdr_set_center_freq(app->dev, frequency) < 0 ||
        rtlsdr_reset_buffer(app->dev) < 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Receiver rejected the requested settings");
        rtlsdr_set_tuner_gain_mode(app->dev, old_manual);
        if (old_manual)
            rtlsdr_set_tuner_gain(app->dev, old_gain);
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        if (start_acquisition(app) < 0)
            snprintf(app->settings_error, sizeof(app->settings_error),
                     "Settings failed and acquisition could not restart");
        return -1;
    }
    uint32_t reported_frequency = rtlsdr_get_center_freq(app->dev);
    uint32_t difference = reported_frequency > frequency
                              ? reported_frequency - frequency
                              : frequency - reported_frequency;
    if (reported_frequency == 0 || difference > 1000U) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Frequency readback mismatch: requested %u, got %u",
                 frequency, reported_frequency);
        rtlsdr_set_tuner_gain_mode(app->dev, old_manual);
        if (old_manual)
            rtlsdr_set_tuner_gain(app->dev, old_gain);
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        if (start_acquisition(app) < 0)
            snprintf(app->settings_error, sizeof(app->settings_error),
                     "Readback failed and acquisition could not restart");
        return -1;
    }

    app->applied_frequency = reported_frequency;
    app->applied_manual_gain = manual;
    app->applied_gain_tenths = gain;
    app->options.frequency = frequency;
    app->options.ppm = ppm;
    app->applied_ppm = rtlsdr_get_freq_correction(app->dev);
    app->remove_dc = app->set.remove_dc;
    app->spectrum_ready = 0;
    app->spectrum_peak_ready = 0;
    if (recreate_waterfall(app, app->plot, 1) < 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Could not reset waterfall for the new frequency");
        rtlsdr_set_tuner_gain_mode(app->dev, old_manual);
        if (old_manual)
            rtlsdr_set_tuner_gain(app->dev, old_gain);
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        app->applied_manual_gain = old_manual;
        app->applied_gain_tenths = old_gain;
        app->applied_ppm = old_ppm;
        app->applied_frequency = old_frequency;
        start_acquisition(app);
        return -1;
    }
    if (start_acquisition(app) < 0)
        return -1;
    return 0;
}

static Rectangle settings_panel(void) {
    float width = 520.0f;
    float height = 380.0f;
    return (Rectangle){ ((float)GetScreenWidth() - width) * 0.5f,
                        ((float)GetScreenHeight() - height) * 0.5f,
                        width, height };
}

void handle_settings_input(struct app *app) {
    Rectangle panel = settings_panel();
    Rectangle frequency = { panel.x + 28.0f, panel.y + 83.0f,
                            300.0f, 38.0f };
    Rectangle ppm = { panel.x + 342.0f, panel.y + 83.0f,
                      panel.width - 370.0f, 38.0f };
    Rectangle gain_previous = { panel.x + 28.0f, panel.y + 164.0f,
                                42.0f, 38.0f };
    Rectangle gain_next = { panel.x + panel.width - 70.0f,
                            panel.y + 164.0f, 42.0f, 38.0f };
    Rectangle dc_toggle = { panel.x + 28.0f, panel.y + 218.0f,
                            22.0f, 22.0f };
    Rectangle drift_toggle = { panel.x + 28.0f, panel.y + 250.0f,
                               22.0f, 22.0f };
    Rectangle cancel = { panel.x + panel.width - 224.0f,
                         panel.y + panel.height - 55.0f, 92.0f, 34.0f };
    Rectangle apply = { panel.x + panel.width - 120.0f,
                        panel.y + panel.height - 55.0f, 92.0f, 34.0f };

    int character;
    while ((character = GetCharPressed()) != 0) {
        if (app->set.focus == 0) {
            int valid = (character >= '0' && character <= '9') ||
                        character == '.' || character == 'k' ||
                        character == 'K' || character == 'm' ||
                        character == 'M' || character == 'g' ||
                        character == 'G';
            if (valid && app->set.frequency_length <
                             (int)sizeof(app->set.frequency) - 1) {
                app->set.frequency[app->set.frequency_length++] =
                    (char)character;
                app->set.frequency[app->set.frequency_length] = '\0';
            }
        } else {
            int valid = (character >= '0' && character <= '9') ||
                        (character == '-' && app->set.ppm_length == 0);
            if (valid && app->set.ppm_length <
                             (int)sizeof(app->set.ppm) - 1) {
                app->set.ppm[app->set.ppm_length++] =
                    (char)character;
                app->set.ppm[app->set.ppm_length] = '\0';
            }
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (app->set.focus == 0 && app->set.frequency_length > 0)
            app->set.frequency[--app->set.frequency_length] = '\0';
        if (app->set.focus == 1 && app->set.ppm_length > 0)
            app->set.ppm[--app->set.ppm_length] = '\0';
    }
    if (clicked(frequency))
        app->set.focus = 0;
    if (clicked(ppm))
        app->set.focus = 1;

    if (app->receiver_mode && clicked(gain_previous)) {
        app->set.gain_choice--;
        if (app->set.gain_choice < 0)
            app->set.gain_choice = app->supported_gain_count;
    }
    if (app->receiver_mode && clicked(gain_next)) {
        app->set.gain_choice++;
        if (app->set.gain_choice > app->supported_gain_count)
            app->set.gain_choice = 0;
    }
    if (clicked(dc_toggle))
        app->set.remove_dc = !app->set.remove_dc;
    if (clicked(drift_toggle))
        app->set.auto_drift = !app->set.auto_drift;
    if (clicked(cancel)) {
        app->settings_open = 0;
        return;
    }
    if (clicked(apply) || IsKeyPressed(KEY_ENTER)) {
        if (apply_settings(app) == 0)
            app->settings_open = 0;
    }

    (void)frequency;
}

void draw_settings(const struct app *app) {
    Rectangle panel = settings_panel();
    Rectangle frequency = { panel.x + 28.0f, panel.y + 83.0f,
                            300.0f, 38.0f };
    Rectangle ppm = { panel.x + 342.0f, panel.y + 83.0f,
                      panel.width - 370.0f, 38.0f };
    Rectangle gain_previous = { panel.x + 28.0f, panel.y + 164.0f,
                                42.0f, 38.0f };
    Rectangle gain_next = { panel.x + panel.width - 70.0f,
                            panel.y + 164.0f, 42.0f, 38.0f };
    Rectangle dc_toggle = { panel.x + 28.0f, panel.y + 218.0f,
                            22.0f, 22.0f };
    Rectangle drift_toggle = { panel.x + 28.0f, panel.y + 250.0f,
                               22.0f, 22.0f };
    Rectangle cancel = { panel.x + panel.width - 224.0f,
                         panel.y + panel.height - 55.0f, 92.0f, 34.0f };
    Rectangle apply = { panel.x + panel.width - 120.0f,
                        panel.y + panel.height - 55.0f, 92.0f, 34.0f };
    char gain[64];

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, 165 });
    DrawRectangleRec(panel, (Color){ 12, 20, 29, 255 });
    DrawRectangleLinesEx(panel, 2.0f, (Color){ 111, 139, 154, 255 });
    DrawText("Acquisition settings", (int)panel.x + 28, (int)panel.y + 22,
             24, (Color){ 235, 242, 246, 255 });
    DrawText("Center frequency (Hz or K/M/G)", (int)frequency.x,
             (int)frequency.y - 23,
             17, (Color){ 166, 188, 201, 255 });
    sdrgui_text_field(frequency, app->set.frequency,
                      app->set.focus == 0);
    DrawText("PPM", (int)ppm.x, (int)ppm.y - 23, 17,
             (Color){ 166, 188, 201, 255 });
    sdrgui_text_field(ppm, app->set.ppm, app->set.focus == 1);

    DrawText("Gain", (int)panel.x + 28, (int)panel.y + 137, 17,
             (Color){ 166, 188, 201, 255 });
    if (!app->receiver_mode) {
        snprintf(gain, sizeof(gain), "capture (not adjustable)");
    } else if (app->set.gain_choice == 0) {
        snprintf(gain, sizeof(gain), "automatic");
    } else {
        snprintf(gain, sizeof(gain), "%.1f dB",
                 app->supported_gains[app->set.gain_choice - 1] / 10.0);
    }
    if (app->receiver_mode) {
        draw_button(gain_previous, "<", 0);
        draw_button(gain_next, ">", 0);
    }
    DrawText(gain,
             (int)(panel.x + (panel.width - MeasureText(gain, 20)) / 2.0f),
             (int)panel.y + 173, 20, (Color){ 235, 242, 246, 255 });

    bool dc_checked = app->set.remove_dc;
    GuiCheckBox(dc_toggle, "Remove DC spike from spectrum and waterfall",
                &dc_checked);

    bool drift_checked = app->set.auto_drift;
    GuiCheckBox(drift_toggle, "Auto GSM drift check (periodic re-tune)",
                &drift_checked);

    if (app->settings_error[0])
        DrawText(app->settings_error, (int)panel.x + 28, (int)panel.y + 289,
                 16, (Color){ 255, 105, 100, 255 });
    draw_button(cancel, "Cancel", 0);
    draw_button(apply, "Apply", 1);
}

Rectangle settings_button(void) {
    return chrome_layout_now().settings_button;
}

Rectangle calibration_button(void) {
    return chrome_layout_now().calibration_button;
}
