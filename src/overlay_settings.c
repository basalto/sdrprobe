#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings_layout.h"
#include "chrome_layout.h"
#include "view.h"
#include "sdrgui.h"
#include "raygui.h"

/*
 * The Settings panel: PPM, gain, the DC-spike filter and the Scope's
 * transform size -- plus the two buttons that open this panel and the
 * calibration overlay.
 *
 * The centre frequency was the first field here and is not any more. It lived
 * in this panel because nothing else had a place for it; the Scope header
 * does now, on the screen the frequency actually moves.
 *
 * Applying a change can retune or restart acquisition, which is why
 * apply_settings reaches back into the application rather than only writing
 * fields.
 */

/* What the source reports, or 0 when it will not say -- the same shape
   sdrprobe.c uses, kept local because the settings panel is the only other
   place that reads the device back. */
static uint32_t settings_frequency(struct app *app) {
    uint32_t hz = 0;
    return device_frequency_hz(&app->source, &hz) == 0 ? hz : 0;
}

static int settings_ppm(struct app *app) {
    int ppm = 0;
    return device_ppm(&app->source, &ppm) == 0 ? ppm : 0;
}

void open_settings(struct app *app) {
    snprintf(app->set.ppm, sizeof(app->set.ppm), "%d",
             app->applied_ppm);
    app->set.ppm_length = (int)strlen(app->set.ppm);
    {
        int choice = sdr_dsp_fft_choice_of(app->sv.fft_size);
        app->set.fft_choice = choice >= 0
                                  ? choice
                                  : sdr_dsp_fft_choice_of(SDR_DSP_FFT_SIZE);
    }
    app->set.gain_choice = 0;
    if (app->receiver_mode && app->applied_manual_gain) {
        int count = device_gain_option_count(&app->device);
        for (int i = 0; i < count; i++)
            if (device_gain_option_value(&app->device, i) ==
                app->applied_gain_tenths)
                app->set.gain_choice = i + 1;
    }
    app->set.error[0] = '\0';
    app->set.remove_dc = app->remove_dc;
    app->set.auto_drift = app->cal.auto_drift;
    app->set.open = 1;
}

int apply_settings(struct app *app) {
    /*
     * Where the receiver already is. This panel no longer moves it -- the
     * Scope header does -- but the retune below still has to name a
     * frequency, because a PPM or gain change restarts acquisition and the
     * device comes back untuned.
     */
    uint32_t frequency = app->applied_frequency;
    int ppm;
    if (parse_int(app->set.ppm, &ppm) < 0 || ppm < -1000 || ppm > 1000) {
        snprintf(app->set.error, sizeof(app->set.error),
                 "PPM must be a signed integer from -1000 to 1000");
        return -1;
    }

    app->cal.auto_drift = app->set.auto_drift;
    /*
     * The transform size, which needs no receiver and so is applied before
     * anything that can fail: a rejected frequency should not also lose a
     * resolution the reader had just chosen.
     */
    {
        int size = sdr_dsp_fft_choice(app->set.fft_choice);
        if (size > 0) {
            app->sv.fft_size = size;
            /*
             * The only place a resolution is remembered between runs. It
             * describes how somebody likes to work rather than one run, which
             * is the test config.h applies -- and this panel is where that
             * intent is expressed, because reaching it takes opening an
             * overlay and pressing Apply.
             *
             * The Scope header has a stepper for the same value and it does
             * not persist: one click, easily an accidental one, should not
             * decide what the program opens with tomorrow. Applying here
             * keeps whatever that stepper last chose, which is why this no
             * longer skips when the size already matches.
             */
            if (config_set_fft_size(&app->config, size))
                config_save(&app->config);
        }
    }
    /* A manual PPM change is no longer FCCH-backed: drop to grey. */
    if (app->cal.gsm_valid && ppm != app->cal.gsm_ppm) {
        app->cal.gsm_valid = 0;
        app->cal.drift_health = CAL_HEALTH_UNKNOWN;
        app->cal.drift_notice[0] = '\0';
        app->cal.drift_phase = DRIFT_IDLE;
    }

    if (!app->receiver_mode) {
        app->applied_frequency = frequency;
        app->options.frequency = frequency;
        app->options.ppm = ppm;
        app->applied_ppm = ppm;
        app->remove_dc = app->set.remove_dc;
        signal_frame_invalidate(&app->frame);
        if (recreate_waterfall(app, app->plot, 1) < 0) {
            snprintf(app->set.error, sizeof(app->set.error),
                     "Could not reset waterfall for the new frequency");
            return -1;
        }
        return 0;
    }

    int manual = app->set.gain_choice > 0;
    int gain = manual ? device_gain_option_value(&app->device,
                                                 app->set.gain_choice - 1)
                      : 0;
    int old_manual = app->applied_manual_gain;
    int old_gain = app->applied_gain_tenths;
    int old_ppm = app->applied_ppm;
    uint32_t old_frequency = app->applied_frequency;
    if (stop_acquisition(app) < 0)
        return -1;
    if (device_set_gain(&app->source, manual, gain) < 0 ||
        set_frequency_correction(&app->source, ppm) < 0 ||
        device_set_frequency_hz(&app->source, frequency) < 0 ||
        device_flush(&app->source) < 0) {
        snprintf(app->set.error, sizeof(app->set.error),
                 "Receiver rejected the requested settings");
        device_set_gain(&app->source, old_manual, old_gain);
        set_frequency_correction(&app->source, old_ppm);
        device_set_frequency_hz(&app->source, old_frequency);
        device_flush(&app->source);
        if (start_acquisition(app) < 0)
            snprintf(app->set.error, sizeof(app->set.error),
                     "Settings failed and acquisition could not restart");
        return -1;
    }
    uint32_t reported_frequency = settings_frequency(app);
    uint32_t difference = reported_frequency > frequency
                              ? reported_frequency - frequency
                              : frequency - reported_frequency;
    if (reported_frequency == 0 || difference > 1000U) {
        snprintf(app->set.error, sizeof(app->set.error),
                 "Frequency readback mismatch: requested %u, got %u",
                 frequency, reported_frequency);
        device_set_gain(&app->source, old_manual, old_gain);
        set_frequency_correction(&app->source, old_ppm);
        device_set_frequency_hz(&app->source, old_frequency);
        device_flush(&app->source);
        if (start_acquisition(app) < 0)
            snprintf(app->set.error, sizeof(app->set.error),
                     "Readback failed and acquisition could not restart");
        return -1;
    }

    app->applied_frequency = reported_frequency;
    app->applied_manual_gain = manual;
    app->applied_gain_tenths = gain;
    app->options.frequency = frequency;
    app->options.ppm = ppm;
    app->applied_ppm = settings_ppm(app);
    app->remove_dc = app->set.remove_dc;
    signal_frame_invalidate(&app->frame);
    if (recreate_waterfall(app, app->plot, 1) < 0) {
        snprintf(app->set.error, sizeof(app->set.error),
                 "Could not reset waterfall for the new frequency");
        device_set_gain(&app->source, old_manual, old_gain);
        set_frequency_correction(&app->source, old_ppm);
        device_set_frequency_hz(&app->source, old_frequency);
        device_flush(&app->source);
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

void handle_settings_input(struct app *app) {
    struct settings_layout l = settings_layout_for(
        (float)GetScreenWidth(), (float)GetScreenHeight());

    int character;

    /* PPM is the only field left here: the centre frequency moved to the
       Scope header, where the chart it moves can be seen. */
    while ((character = GetCharPressed()) != 0) {
        int valid = (character >= '0' && character <= '9') ||
                    (character == '-' && app->set.ppm_length == 0);
        if (valid && app->set.ppm_length < (int)sizeof(app->set.ppm) - 1) {
            app->set.ppm[app->set.ppm_length++] = (char)character;
            app->set.ppm[app->set.ppm_length] = '\0';
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && app->set.ppm_length > 0)
        app->set.ppm[--app->set.ppm_length] = '\0';

    if (app->receiver_mode && clicked(l.gain_previous)) {
        app->set.gain_choice--;
        if (app->set.gain_choice < 0)
            app->set.gain_choice = device_gain_option_count(&app->device);
    }
    if (app->receiver_mode && clicked(l.gain_next)) {
        app->set.gain_choice++;
        if (app->set.gain_choice > device_gain_option_count(&app->device))
            app->set.gain_choice = 0;
    }
    /* Wraps at both ends, the way the gain stepper does: seven choices and
       two arrows, and a reader who has gone one too far should not have to
       walk back through all of them. */
    if (clicked(l.fft_previous)) {
        app->set.fft_choice--;
        if (app->set.fft_choice < 0)
            app->set.fft_choice = SDR_DSP_FFT_CHOICES - 1;
    }
    if (clicked(l.fft_next)) {
        app->set.fft_choice++;
        if (app->set.fft_choice >= SDR_DSP_FFT_CHOICES)
            app->set.fft_choice = 0;
    }
    if (clicked(l.dc_toggle))
        app->set.remove_dc = !app->set.remove_dc;
    if (clicked(l.drift_toggle))
        app->set.auto_drift = !app->set.auto_drift;
    if (clicked(l.cancel)) {
        app->set.open = 0;
        return;
    }
    if (clicked(l.apply) || IsKeyPressed(KEY_ENTER)) {
        if (apply_settings(app) == 0)
            app->set.open = 0;
    }

}

void draw_settings(const struct app *app) {
    struct settings_layout l = settings_layout_for(
        (float)GetScreenWidth(), (float)GetScreenHeight());
    Rectangle panel = l.panel;
    char gain[64];

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, 165 });
    DrawRectangleRec(panel, (Color){ 12, 20, 29, 255 });
    DrawRectangleLinesEx(panel, 2.0f, (Color){ 111, 139, 154, 255 });
    DrawText("Acquisition settings", (int)panel.x + 28, (int)panel.y + 22,
             24, (Color){ 235, 242, 246, 255 });
    DrawText("PPM (tuning correction)", (int)l.ppm.x,
             (int)settings_caption_of(l.ppm).y, 17,
             (Color){ 166, 188, 201, 255 });
    sdrgui_text_field(l.ppm, app->set.ppm, 1);

    DrawText("Gain", (int)l.gain_previous.x,
             (int)settings_caption_of(l.gain_previous).y, 17,
             (Color){ 166, 188, 201, 255 });
    if (!app->receiver_mode) {
        snprintf(gain, sizeof(gain), "capture (not adjustable)");
    } else if (app->set.gain_choice == 0) {
        snprintf(gain, sizeof(gain), "automatic");
    } else {
        /* In the profile's own unit, so a gain-table index says so rather
           than pretending to be decibels (ticket 06). */
        device_gain_format(&app->device,
                           device_gain_option_value(&app->device,
                                                    app->set.gain_choice - 1),
                           gain, sizeof(gain));
    }
    if (app->receiver_mode) {
        draw_button(l.gain_previous, "<", 0);
        draw_button(l.gain_next, ">", 0);
    }
    DrawText(gain,
             (int)(l.gain_value.x +
                   (l.gain_value.width - (float)MeasureText(gain, 20)) / 2.0f),
             (int)(l.gain_value.y + 9.0f), 20, (Color){ 235, 242, 246, 255 });

    /*
     * The transform size, and what it costs. Both numbers, because it is a
     * trade rather than an improvement: a longer transform buys resolution
     * and spends averaging, and a reader who picks 16384 should be told they
     * have gone from 64 averages to 8 rather than discover it as a trace that
     * suddenly looks worse.
     */
    {
        char fft[96];
        int size = sdr_dsp_fft_choice(app->set.fft_choice);
        double rate = (double)app->applied_sample_rate;
        int windows = size > 0 ? (int)(SAMPLE_BLOCK_PAIRS / (size_t)size) : 0;

        DrawText("Scope resolution", (int)l.fft_previous.x,
                 (int)settings_caption_of(l.fft_previous).y, 16,
                 (Color){ 157, 180, 194, 255 });
        if (size > 0 && rate > 0.0)
            snprintf(fft, sizeof(fft), "%d points   %.0f Hz bins   %d averaged",
                     size, rate / size, windows);
        else
            snprintf(fft, sizeof(fft), "%d points", size);
        draw_button(l.fft_previous, "<", 0);
        draw_button(l.fft_next, ">", 0);
        DrawText(fft,
                 (int)(l.fft_value.x +
                       (l.fft_value.width - (float)MeasureText(fft, 18)) /
                           2.0f),
                 (int)(l.fft_value.y + 10.0f), 18,
                 (Color){ 235, 242, 246, 255 });
    }

    bool dc_checked = app->set.remove_dc;
    GuiCheckBox(l.dc_toggle, "Remove DC spike from spectrum and waterfall",
                &dc_checked);

    bool drift_checked = app->set.auto_drift;
    GuiCheckBox(l.drift_toggle, "Auto GSM drift check (periodic re-tune)",
                &drift_checked);

    if (app->set.error[0])
        DrawText(app->set.error, (int)l.error.x, (int)l.error.y, 16,
                 (Color){ 255, 105, 100, 255 });
    draw_button(l.cancel, "Cancel", 0);
    draw_button(l.apply, "Apply", 1);
}

Rectangle settings_button(void) {
    return chrome_layout_now().settings_button;
}

Rectangle calibration_button(void) {
    return chrome_layout_now().calibration_button;
}
