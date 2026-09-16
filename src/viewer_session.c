#define _POSIX_C_SOURCE 200809L

#include "viewer_session.h"

#include <stdio.h>
#include <stdlib.h>

#include "browser.h"
#include "frame_advance.h"
#include "process_cpu.h"
#include "scope_view_model.h"
#include "view.h"
#include "viewer_link.h"

/* How often the link is serviced between blocks. Short enough that a
   65.5 ms block is never delayed by more than a fraction of its own
   period, long enough that an idle session (no client, no new block)
   costs nothing -- select() inside viewer_link_poll() sleeps the
   difference rather than this loop spinning. */
#define VIEWER_SESSION_POLL_MS 20

/* How often the server-CPU half of link_health is re-sampled -- a block
   is 65.5 ms and the counters it feeds barely move in that time, so
   recomputing a percentage every block would be noise wearing the shape
   of a measurement (ticket 08). The sent/dropped counts and high-water
   mark are still published every iteration, same as receiver_state. */

/*
 * Ticket 06's inbound half, wired to the same path every other retune
 * caller uses -- `retune_receiver()` and the transaction in
 * receiver_runtime.c, stop/apply/flush/read-back/restart with rollback
 * at each step. This does not get its own path, and on failure it quotes
 * `app->receiver_error`, the one buffer that already owns the reason a
 * retune failed, rather than inventing a second message for the wire.
 */
static int viewer_session_handle_command(void *ctx, const struct viewer_command *cmd,
                                         char *error, size_t error_cap) {
    struct app *app = ctx;

    switch (cmd->type) {
    case VIEWER_COMMAND_TUNE:
        if (retune_receiver(app, cmd->hz, app->applied.ppm) < 0) {
            snprintf(error, error_cap, "%s", app->receiver_error);
            return -1;
        }
        return 0;
    default:
        snprintf(error, error_cap, "unimplemented command");
        return -1;
    }
}

/* getenv() with the const browser_wanted() wants: it returns `char *`, and a
   lookup handing out a mutable pointer into the environment invites a
   caller to write through it. sdrprobe.c keeps its own copy of this for the
   same reason -- both are one line, and a shared header for one line each
   is not worth the seam. */
static const char *environment(const char *name) {
    return getenv(name);
}

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
    struct process_cpu_sample cpu_previous;
    double cpu_percent = 0.0;
    /* Negative is "never published" -- see viewer_update_due(). A loop timing
       from its own start reaches a real 0.0, so 0.0 cannot mean never. */
    double health_published_at = -1.0;
    double state_published_at = -1.0;
    uint32_t state_generation = 0;
    int state_ever_published = 0;

    process_cpu_sample_now(&cpu_previous);

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
    viewer_link_set_command_handler(&link, viewer_session_handle_command, app);
    fprintf(stderr,
           "Viewer link listening on 127.0.0.1:%d -- open "
           "http://127.0.0.1:%d/ in a browser. Ctrl-C to stop.\n",
           port, port);

    /*
     * The one moment this can happen, and the one time: after the bind
     * above, so a browser's first load never races the listener and reads
     * as the program being broken, and once, so nothing later in the loop
     * -- a reconnect, a retune -- can fire it again.
     *
     * `browser_wanted()` is where `web` and `--no-browser` and the two
     * display variables are actually decided, all of it checkable with no
     * process and no display; this is only the report of what it decided,
     * on the same stream the listening line above is on.
     */
    {
        char url[32];

        snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);
        if (browser_wanted(&app->options, environment)) {
            if (browser_open(url) == 0)
                fprintf(stderr, "Opening it in a browser.\n");
            else
                fprintf(stderr, "Could not start a browser; open the URL "
                                "above yourself.\n");
        } else if (app->options.command == COMMAND_WEB) {
            fprintf(stderr, app->options.no_browser
                                ? "Not opening a browser: --no-browser.\n"
                                : "Not opening a browser: no DISPLAY or "
                                  "WAYLAND_DISPLAY.\n");
        }
    }

    while (!stop_requested()) {
        struct slot_snapshot snapshot;
        double now = monotonic_seconds() - started;
        int spectrum_updated;
        struct scope_view_model svm;
        uint64_t now_ms;

        /*
         * A scripted, one-shot retune for exercising the tuning generation
         * without a Viewer -- see options.h's comment on
         * serve_retune_after_seconds. Independent of ticket 06's `tune`
         * command, which goes through the same retune_receiver() call
         * right below rather than a path of its own.
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
        /*
         * Not gated on spectrum_updated -- the tuning can change (the retune
         * above, or a live receiver's own reconnects) independently of
         * whether a spectrum came with this pass -- and not published every
         * iteration either, which is what used to make this loop spin at
         * ~100 000 iterations a second (viewer_session.h). A retune is
         * immediate, because the tuning generation is what `changed` asks
         * about; everything else is the quarter-second heartbeat.
         */
        if (viewer_update_due(now, state_published_at,
                              VIEWER_SESSION_STATE_INTERVAL_SECONDS,
                              state_ever_published &&
                                  svm.tuning_generation != state_generation)) {
            viewer_link_publish_receiver_state(&link, &svm, now_ms);
            state_published_at = now;
            state_generation = svm.tuning_generation;
            state_ever_published = 1;
        }

        if (viewer_update_due(now, health_published_at,
                              VIEWER_SESSION_HEALTH_INTERVAL_SECONDS, 0)) {
            struct process_cpu_sample cpu_now;

            if (process_cpu_sample_now(&cpu_now) == 0) {
                cpu_percent = process_cpu_percent(&cpu_previous, &cpu_now);
                cpu_previous = cpu_now;
            }
            /* Sampled and published together: the CPU figure cannot be
               fresher than its own sample, and the sent/dropped counts it
               carries alongside are a Health panel's, not a meter's. */
            viewer_link_publish_link_health(&link, cpu_percent, now_ms);
            health_published_at = now;
        }

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
