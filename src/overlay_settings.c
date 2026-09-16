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

/*
 * This panel used to read the device back itself -- its own
 * `settings_frequency()` and `settings_ppm()`, "the same shape sdrprobe.c
 * uses, kept local". That was the tell: two places reading one device back,
 * either of which could come to disagree about what had been applied. The
 * read-back is a step of `receiver_runtime_apply()` now, and what it read is
 * in `app->applied`.
 */

void open_settings(struct app *app) {
    snprintf(app->set.ppm, sizeof(app->set.ppm), "%d",
             app->applied.ppm);
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
    uint32_t frequency = app->applied.frequency_hz;
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
        app->applied.frequency_hz = frequency;
        app->options.frequency = frequency;
        app->options.ppm = ppm;
        app->applied.ppm = ppm;
        app->remove_dc = app->set.remove_dc;
        signal_frame_invalidate(&app->frame);
        /*
         * A capture has no gain to change (the panel shows "capture, not
         * adjustable"), and PPM and DC removal are both cases
         * `sdr_dsp_gain_change_clears_waterfall()` answers "no" to on their
         * own -- so passing it the same value on both sides says exactly
         * that, rather than a bare 0 a reader has to take on faith.
         */
        if (recreate_waterfall(app, app->plot,
                               sdr_dsp_gain_change_clears_waterfall(
                                   0, 0, 0, 0)) < 0) {
            snprintf(app->set.error, sizeof(app->set.error),
                     "Could not reset waterfall for the new frequency");
            return -1;
        }
        return 0;
    }

    /*
     * The receiver half, through the one transaction.
     *
     * This panel used to run its own stop/gain/correction/frequency/flush/
     * read-back/restart with three rollback blocks -- a second copy of
     * `receiver_runtime_apply()` that wrote `app->applied` directly and never
     * advanced the tuning generation ADR-0027 publishes to every Viewer. The
     * refusal text is the runtime's now, and `app->receiver_error` is where it
     * writes: one writer for why a receiver would not do something, which is
     * what that buffer exists for.
     *
     * **One check went with the copy, deliberately.** This panel refused a
     * read-back more than 1 kHz from the request ("Frequency readback
     * mismatch"); the runtime refuses only a read-back of zero, which is what
     * every other retune in the program has always done. The tolerance was
     * worth having while this panel owned the frequency field. It no longer
     * does -- `frequency` here is `app->applied.frequency_hz`, which is
     * itself what the device last reported -- so the case it guarded is a
     * device disagreeing with its own previous answer, and one rule for every
     * retune is worth more than a second one here. Restoring it means
     * tightening the runtime for every caller, which is a change to the
     * survey and both band scans and needs its own measurement.
     */
    struct receiver_runtime rt = runtime_over(app);
    struct receiver_gain want, had;

    want.manual = app->set.gain_choice > 0;
    want.tenths = want.manual
                      ? device_gain_option_value(&app->device,
                                                 app->set.gain_choice - 1)
                      : 0;
    had.manual = app->applied_manual_gain;
    had.tenths = app->applied_gain_tenths;

    if (receiver_runtime_apply(&rt, frequency, ppm, want, had) < 0) {
        snprintf(app->set.error, sizeof(app->set.error), "%s",
                 app->receiver_error[0] ? app->receiver_error
                                        : "Receiver rejected the requested "
                                          "settings");
        return -1;
    }

    /*
     * Whether this Apply clears the waterfall or leaves it standing.
     *
     * Computed from `had`/`want` before they are overwritten below -- of
     * everything this panel can change, only a gain change answers yes
     * (`sdr_dsp_gain_change_clears_waterfall()`). A PPM change moves the true
     * tuning by well under one bin at any setting this panel accepts, which
     * the *existing* per-frame shift already resolves to zero on its own
     * (`app->applied.frequency_hz` itself does not move for a PPM-only
     * apply, so there is nothing here to shift); DC removal touches one bin.
     * This panel used to clear on every Apply, which is what took **7 rows**
     * of waterfall history down to nothing on a plain PPM or DC-removal
     * change, for no reason connected to what actually moved.
     */
    int clear_waterfall = sdr_dsp_gain_change_clears_waterfall(
        had.manual, had.tenths, want.manual, want.tenths);

    app->applied_manual_gain = want.manual;
    app->applied_gain_tenths = want.tenths;
    app->options.frequency = frequency;
    app->options.ppm = ppm;
    app->remove_dc = app->set.remove_dc;
    signal_frame_invalidate(&app->frame);
    /*
     * Drawing, and after the transaction rather than inside it: the receiver
     * is where it was asked to be whatever a texture does, and a rollback of
     * the tuning because a waterfall could not be allocated would leave the
     * generation advanced with the frequency put back. The panel still says
     * so, because a blank chart with no explanation is worse.
     */
    if (recreate_waterfall(app, app->plot, clear_waterfall) < 0) {
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
        double rate = (double)app->applied.sample_rate_hz;
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
