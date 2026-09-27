/*
 * GSM's runtime: tuning a channel, feeding the synchronization decoder,
 * entering and leaving the view, and starting a recording.
 *
 * Out of `view_gsm.c` by `.scratch/layer-boundaries/issues/02-*`, which is
 * moving every technology's share of `frame_advance()` out of the files
 * that draw. What stayed behind is the drawing and the input handling.
 *
 * No raylib header here, and it must not gain one.
 */

#include <stdio.h>
#include <string.h>

#include "runtime/app.h"
#include "runtime/chart_window.h"
#include "runtime/debug_log.h"
#include "tech/gsm_continuity.h"
#include "runtime/runtime.h"

void gsm_tune_selected(struct app *app, int arfcn) {
    uint32_t expected;
    if (arfcn < 1 || arfcn > 124 ||
        !gsm_downlink_hz((unsigned int)arfcn, &expected))
        return;
    app->gsm.selected_arfcn = arfcn;
    app->gsm.selected_hz = (double)expected;
    if (app->receiver_mode)
        retune_receiver(app, expected - 400000U, app->applied.ppm);

    /* Open on the channel that was chosen. A default, not a lock: 0 puts the
       whole span back and a drag goes anywhere. */
    chart_window_sync(&app->gsm.window, app->applied.frequency_hz,
                      app->applied.sample_rate_hz,
                      chart_min_span(GSM900_ARFCN_SPACING_HZ));
    chart_window_centre_on(&app->gsm.window, (double)expected,
                           CALIBRATION_VIEW_HALF_WIDTH_HZ,
                           chart_min_span(GSM900_ARFCN_SPACING_HZ));
    app->gsm.session.sch_valid = 0;
    gsm_continuity_reset(&app->gsm.session.continuity);
    memset(&app->gsm.session.cell, 0, sizeof(app->gsm.session.cell)); /* a different cell */
}

/*
 * Feed the inspected channel's latest block to the decode.
 *
 * The channel carrier sits at +400 kHz, because gsm_tune_selected() tunes that
 * far below it. Everything past this line is `gsm_session.h`'s -- this is the
 * adapter, and the headless report has its own.
 */
void update_gsm_sch(struct app *app, double now) {
    struct gsm_session_event event;

    if (app->gsm.selected_hz <= 0.0 || app->bandscan.running ||
        app->frame.pair_count == 0)
        return;
    gsm_session_feed(&app->gsm.session, app->frame.i_samples, app->frame.q_samples,
                     app->frame.pair_count, (double)app->applied.sample_rate_hz,
                     app->gsm.selected_hz - (double)app->applied.frequency_hz,
                     now, &event);

    if (event.sch_decoded) {
        debug_log_write("gsm-sch", "bsic %d (ncc %d bcc %d) fn %u",
                        app->gsm.session.sch.bsic,
                        app->gsm.session.sch.ncc,
                        app->gsm.session.sch.bcc,
                        app->gsm.session.sch.frame_number);
    }
    if (event.broadcast_read) {
        debug_log_write("gsm-bcch", "mcc %d mnc %d lac %d ci %d",
                        event.si.mcc, event.si.mnc, event.si.lac, event.si.cell_id);
    }
}

void start_record(struct app *app) {
    char basename[64];
    int arfcn = app->gsm.selected_arfcn > 0 ? app->gsm.selected_arfcn : 0;

    snprintf(basename, sizeof(basename), "gsm_arfcn%d", arfcn);
    start_capture_record(app, basename, "gsm", arfcn,
                         arfcn > 0 ? 400000.0 : 0.0,
                         ACQUISITION_RECORD_BUTTON_SECONDS);
}

void view_gsm_defaults(struct app *app) {
    app->gsm.const_amplitude = 1;
    app->gsm.session.options = GSM_OPT_FILTER | GSM_OPT_FINECFO |
                               GSM_OPT_TRELLIS;
    /*
     * No channel has been measured yet, and the sentinel is how the chart is
     * told so. Zero is not the sentinel: left at it, the scan chart drew all
     * 124 channels pinned at 0 dBFS -- a full band of signal, out of an array
     * nothing had written to. Tuning straight to a channel with --arfcn skips
     * the scan, so this is the state the view opens in.
     */
    for (int arfcn = 0; arfcn <= SCAN_ARFCN_LAST; arfcn++) {
        app->bandscan.power[arfcn] = SCAN_SENTINEL_DBFS;
        app->bandscan.bcch_conf[arfcn] = 0.0f;
    }
}

void enter_gsm(struct app *app) {
    /* Borrow where the operator had it, before anything below moves it. */
    receiver_borrow(app, &app->gsm.lease_token);
    int arfcn = 0;
    if (app->cal.gsm_arfcn > 0)
        arfcn = app->cal.gsm_arfcn;
    else if (app->gsm.selected_arfcn > 0)
        arfcn = app->gsm.selected_arfcn;
    if (arfcn > 0) {
        gsm_tune_selected(app, arfcn);
        app->gsm.analysis_mode = 1; /* Default to Burst mode when inspecting */
    } else if (app->receiver_mode) {
        if (start_scan(app) == 0) {
            app->bandscan.open = 0;
            app->bandscan.autoselect = 1;
        }
    }
}

/* Leave the GSM decode view: stop any scan and restore the entry tuning. */
void leave_gsm(struct app *app) {
    app->bandscan.running = 0;
    app->bandscan.open = 0;
    app->bandscan.autoselect = 0;
    /* Inside out: a scan borrowed the receiver from this view, and the lease
       refuses to let this view return while somebody else still holds it. */
    scan_release_receiver(app);
    receiver_return(app, &app->gsm.lease_token);
    app->gsm.selected_hz = 0.0;
    app->gsm.session.sch_valid = 0;
}
