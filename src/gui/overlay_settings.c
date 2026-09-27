#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model/settings_view_model.h"
#include "gui/settings_layout.h"
#include "gui/chrome_layout.h"
#include "gui/view.h"
#include "gui/sdrgui.h"
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

/*
 * This panel used to read the device back itself -- its own
 * `settings_frequency()` and `settings_ppm()`, "the same shape sdrprobe.c
 * uses, kept local". That was the tell: two places reading one device back,
 * either of which could come to disagree about what had been applied. The
 * read-back is a step of `receiver_runtime_apply()` now, and what it read is
 * in `app->applied`.
 */

int apply_settings(struct app *app) {
    int clear_waterfall = 0;

    /*
     * The transaction is `settings_apply()`'s, in `runtime/` -- it decides,
     * and a Viewer's `apply` command calls the same function. What is left
     * here is the half that draws: recreating the waterfall texture, after
     * the transaction rather than inside it, because the receiver is where
     * it was asked to be whatever a texture does and a rollback of the
     * tuning because a waterfall could not be allocated would leave the
     * generation advanced with the frequency put back. The panel still says
     * so, because a blank chart with no explanation is worse.
     */
    if (settings_apply(app, &clear_waterfall) < 0)
        return -1;
    if (recreate_waterfall(app, app->gui->plot, clear_waterfall) < 0) {
        snprintf(app->set.error, sizeof(app->set.error),
                 "Could not reset waterfall for the new frequency");
        return -1;
    }
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
    struct settings_view_model m;

    /*
     * The staged set, what is applied, and how each is worded -- decided
     * once (`web-visualization/16`, `/17`). The gain's unit in particular:
     * `device_gain_format()` is what stops a gain-table index being printed
     * as decibels, and it is asked here rather than spelled out twice.
     */
    settings_view_model_build(&app->set, &app->device, app->receiver_mode,
                              app->applied_manual_gain,
                              app->applied_gain_tenths, app->applied.ppm,
                              app->sv.fft_size, app->remove_dc,
                              app->cal.auto_drift,
                              app->applied.sample_rate_hz, &m);

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, 165 });
    DrawRectangleRec(panel, (Color){ 12, 20, 29, 255 });
    DrawRectangleLinesEx(panel, 2.0f, (Color){ 111, 139, 154, 255 });
    DrawText("Acquisition settings", (int)panel.x + 28, (int)panel.y + 22,
             24, (Color){ 235, 242, 246, 255 });
    DrawText("PPM (tuning correction)", (int)l.ppm.x,
             (int)settings_caption_of(l.ppm).y, 17,
             (Color){ 166, 188, 201, 255 });
    sdrgui_text_field(l.ppm, m.staged_ppm, 1);

    DrawText("Gain", (int)l.gain_previous.x,
             (int)settings_caption_of(l.gain_previous).y, 17,
             (Color){ 166, 188, 201, 255 });
    if (m.gain_adjustable) {
        draw_button(l.gain_previous, "<", 0);
        draw_button(l.gain_next, ">", 0);
    }
    DrawText(m.staged_gain,
             (int)(l.gain_value.x +
                   (l.gain_value.width -
                    (float)MeasureText(m.staged_gain, 20)) / 2.0f),
             (int)(l.gain_value.y + 9.0f), 20, (Color){ 235, 242, 246, 255 });

    /*
     * The transform size, and what it costs. Both numbers, because it is a
     * trade rather than an improvement: a longer transform buys resolution
     * and spends averaging, and a reader who picks 16384 should be told they
     * have gone from 64 averages to 8 rather than discover it as a trace that
     * suddenly looks worse.
     */
    {
        DrawText("Scope resolution", (int)l.fft_previous.x,
                 (int)settings_caption_of(l.fft_previous).y, 16,
                 (Color){ 157, 180, 194, 255 });
        draw_button(l.fft_previous, "<", 0);
        draw_button(l.fft_next, ">", 0);
        DrawText(m.staged_fft,
                 (int)(l.fft_value.x +
                       (l.fft_value.width -
                        (float)MeasureText(m.staged_fft, 18)) / 2.0f),
                 (int)(l.fft_value.y + 10.0f), 18,
                 (Color){ 235, 242, 246, 255 });
    }

    bool dc_checked = m.staged_remove_dc;
    GuiCheckBox(l.dc_toggle, "Remove DC spike from spectrum and waterfall",
                &dc_checked);

    bool drift_checked = m.staged_auto_drift;
    GuiCheckBox(l.drift_toggle, "Auto GSM drift check (periodic re-tune)",
                &drift_checked);

    if (m.error[0])
        DrawText(m.error, (int)l.error.x, (int)l.error.y, 16,
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
