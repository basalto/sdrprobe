#define _POSIX_C_SOURCE 200809L

#include "viewer_session.h"

#include <stdio.h>

#include "frame_advance.h"
#include "scope_view_model.h"
#include "view.h"
#include "viewer_link.h"

/* How often the link is serviced between blocks. Short enough that a
   65.5 ms block is never delayed by more than a fraction of its own
   period, long enough that an idle session (no client, no new block)
   costs nothing -- select() inside viewer_link_poll() sleeps the
   difference rather than this loop spinning. */
#define VIEWER_SESSION_POLL_MS 20

int viewer_session_run(struct app *app) {
    /* struct viewer_link is ~12 MB (VIEWER_LINK_MAX_CLIENTS clients, each
       carrying a slot per stream sized to the largest message this link
       ever sends) -- as large a lesson as the one that already sits in
       tests/frame_advance_test.c and tests/scope_view_model_test.c: never
       a stack local. */
    static struct viewer_link link;
    double started = monotonic_seconds();
    int port = app->options.serve_port > 0 ? app->options.serve_port
                                           : VIEWER_SESSION_DEFAULT_PORT;
    int retuned = 0;

    sdr_dsp_init(&app->frame.dsp);
    /* Headless has no tabs to choose from; the Scope is the only screen
       this ticket serves (ticket 05's own "Not in scope: any decode
       view"), so frame_advance()'s dispatch is told exactly that rather
       than left to whatever app->tab happened to default to. */
    app->tab = TAB_SCOPE;
    app->view = VIEW_SPECTRUM;
    /*
     * run_gui() reads --fft (and a saved config) into app->sv.fft_size
     * itself, in the windowed startup sequence this session never runs --
     * so headless serving silently stayed at the SDR_DSP_FFT_SIZE default
     * (2048) whatever --fft asked for, found while measuring bytes/sec at
     * 2048 and at 16384 bins for this ticket and getting the same number
     * twice. Same two lines run_gui() has, so the two paths agree on the
     * one place this value comes from.
     */
    if (app->config.fft_size > 0)
        app->sv.fft_size = app->config.fft_size;
    if (app->options.fft_size)
        app->sv.fft_size = app->options.fft_size;
    /*
     * The waterfall ring, allocated the raylib-free way: there is no plot
     * rectangle and no texture here, only the float history
     * advance_waterfall_row() writes into and this session reads the
     * front row of. A handful of rows is plenty -- nothing server-side
     * ever reads past row 0, since the Viewer builds its own history from
     * the rows it is sent (ADR-0027) and never asks for this program's.
     */
    if (allocate_waterfall_history(app, 8) < 0) {
        fprintf(stderr, "Cannot allocate the waterfall history.\n");
        return -1;
    }
    app->sv.waterfall_ready = 1;

    if (viewer_link_open(&link, (uint16_t)port) < 0) {
        fprintf(stderr, "Cannot open the Viewer link on 127.0.0.1:%d.\n",
                port);
        return -1;
    }
    fprintf(stderr,
           "Viewer link listening on 127.0.0.1:%d -- open "
           "http://127.0.0.1:%d/ in a browser. Ctrl-C to stop.\n",
           port, port);

    while (!stop_requested()) {
        struct slot_snapshot snapshot;
        double now = monotonic_seconds() - started;
        int spectrum_updated;
        struct scope_view_model svm;
        uint64_t now_ms;

        /*
         * A scripted, one-shot retune for exercising the tuning generation
         * -- see options.h's comment on serve_retune_after_seconds. Not a
         * Viewer command: nothing on the wire can retune this receiver
         * until ticket 06.
         */
        if (!retuned && app->options.serve_retune_after_seconds > 0.0 &&
            now >= app->options.serve_retune_after_seconds) {
            retune_receiver(app, app->options.serve_retune_to_hz,
                           app->applied.ppm);
            retuned = 1;
        }

        spectrum_updated = frame_advance(app, &snapshot, now);
        scope_view_model_build(app, &svm);
        now_ms = (uint64_t)(monotonic_seconds() * 1000.0);

        if (spectrum_updated) {
            viewer_link_publish_spectrum(&link, &svm, now_ms);
            viewer_link_publish_waterfall_row(&link, &svm, now_ms);
        }
        /* Every iteration, not gated on spectrum_updated: the tuning can
           change (the retune above, or a live receiver's own reconnects)
           independently of whether a spectrum came with this pass, and
           the replaceable-stream rule means a redundant one costs nothing
           a client keeps. */
        viewer_link_publish_receiver_state(&link, &svm, now_ms);

        viewer_link_poll(&link, VIEWER_SESSION_POLL_MS);

        if (snapshot.worker_failed) {
            fprintf(stderr, "Acquisition failed: %s\n",
                   snapshot.worker_error);
            viewer_link_close(&link);
            return -1;
        }
        if (snapshot.worker_done && !app->receiver_mode) {
            fprintf(stderr, "End of capture.\n");
            break;
        }
    }

    viewer_link_close(&link);
    return 0;
}
