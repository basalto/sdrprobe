#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "core/sdr_dsp.h"
#include "runtime/app.h"
#include "runtime/config.h"
#include "runtime/receiver_runtime.h"
#include "runtime/options.h"
#include "runtime/runtime.h"

/*
 * Opening the Settings panel, and committing what it staged.
 *
 * Neither draws. `open_settings()` seeds the staged set from what is
 * applied, and `settings_apply()` validates the set as a whole and moves the
 * receiver through the one retune transaction. They were in
 * `gui/overlay_settings.c` beside the drawing until a Viewer needed to press
 * Apply, and the server links no window (ADR-0028,
 * `web-visualization/17`).
 */

/*
 * The Settings panel's transaction, without the drawing.
 *
 * Split out of `apply_settings()` (`web-visualization/17`) because a Viewer
 * has to be able to press Apply and the server links no window: everything
 * here decides -- it validates the staged set, moves the receiver through
 * `receiver_runtime_apply()`, persists a transform size -- and the one thing
 * that draws, recreating the waterfall texture, stays with the panel. The
 * comment on that call already identified the seam: "Drawing, and after the
 * transaction rather than inside it."
 *
 * Returns 0, or -1 with the reason in `app->set.error`. `clear_waterfall_out`
 * carries the one fact the caller's drawing needs and cannot recompute --
 * whether a gain changed -- because by the time it runs, `app->applied_gain_*`
 * has been overwritten with what was wanted.
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

int settings_apply(struct app *app, int *clear_waterfall_out) {
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
        *clear_waterfall_out = sdr_dsp_gain_change_clears_waterfall(0, 0, 0, 0);
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
    *clear_waterfall_out = sdr_dsp_gain_change_clears_waterfall(
        had.manual, had.tenths, want.manual, want.tenths);

    app->applied_manual_gain = want.manual;
    app->applied_gain_tenths = want.tenths;
    app->options.frequency = frequency;
    app->options.ppm = ppm;
    app->remove_dc = app->set.remove_dc;
    signal_frame_invalidate(&app->frame);
    return 0;
}

