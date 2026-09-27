/*
 * The GSM band scan's per-block step, out of the overlay that draws it.
 *
 * `.scratch/layer-boundaries/issues/02-*`: `frame_advance()` calls this on
 * every path, including the two with no window. The overlay keeps the chart,
 * the channel list and the clicks. No raylib here.
 */

#include <stdio.h>
#include <string.h>

#include "runtime/app.h"
#include "runtime/debug_log.h"
#include "runtime/runtime.h"

void update_scan(struct app *app) {
    if (!app->bandscan.running || !app->frame.spectrum_ready)
        return;
    double elapsed = monotonic_seconds() - app->bandscan.step_started_at;
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
        /*
         * What a two-minute walk came to, in one line.
         *
         * `bcch` separately from `chosen` because they can differ and the
         * difference is the whole answer: scan_choose() falls back to the
         * loudest channel when nothing carried a broadcast carrier, so a
         * `chosen` with `bcch 0` beside it is "something to look at" rather
         * than "a GSM channel", and a reader who cannot tell those apart is
         * looking at a chart that can never say anything.
         */
        debug_log_write("gsm-scan", "done, %d steps, chose arfcn %d, "
                        "bcch %d, confidence %.2f",
                        app->bandscan.plan.step_count, chosen,
                        scan_select_bcch(app->bandscan.power,
                                         app->bandscan.bcch_conf),
                        chosen > 0 ? (double)app->bandscan.bcch_conf[chosen]
                                   : 0.0);
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
    app->bandscan.step_started_at = monotonic_seconds();
}

/*
 * Starting a scan, and giving the receiver back, came out of
 * `overlay_scan.c` by ticket 04 of `.scratch/layer-boundaries/`: neither
 * draws anything, both are already called from `gsm_runtime.c`, and a
 * server built without the window could not link because of them.
 *
 * The two `scan_strongest_*` selectors stay where they are deliberately --
 * nothing outside the window asks either, so moving them would be tidying
 * rather than a boundary.
 */
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
