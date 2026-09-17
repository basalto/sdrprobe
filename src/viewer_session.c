#define _POSIX_C_SOURCE 200809L

#include "viewer_session.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#include "browser.h"
#include "frame_advance.h"
#include "process_cpu.h"
#include "scope_view_model.h"
#include "survey_view_model.h"
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
 * The loop's own clock origin (`viewer_session_run()`'s `started`), so
 * `viewer_session_handle_command()` -- called from inside
 * `viewer_link_poll()`, not from the loop body -- can hand `survey_start()`
 * a `now` on the *same* clock `update_survey()` ticks every iteration
 * (`monotonic_seconds() - started`), rather than the raw, unrelated origin
 * `monotonic_seconds()` alone reads from.
 *
 * This is not the `GetTime()`-before-`InitWindow()` fault the rest of this
 * file's `now`-threading fixed -- `monotonic_seconds()` is always a real
 * clock -- but it is the same *shape* of fault: two callers of one function
 * disagreeing about what `now` means. Found live, not read: a scripted
 * `view survey` retuned once and the sweep never advanced past its first
 * step, because `s->step_started_at` was set from this handler's raw
 * `monotonic_seconds()` (tens of thousands of seconds of host uptime) while
 * every later tick measured elapsed time against the loop's small,
 * relative-to-`started` `now` -- `now - step_started_at` was deeply
 * negative and stayed that way, which `survey_step_phase_at()` reads as
 * "still settling" forever.
 */
static double viewer_session_started_at;

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
    case VIEWER_COMMAND_VIEW:
        /*
         * `set_tab()` is the same function every tab-bar click in the
         * window goes through, not a headless shortcut around it --
         * ticket 07's whole point is one seam, not a second one that
         * happens to agree with the first today. `monotonic_seconds()`
         * rather than a `now` threaded in from the caller: this handler
         * runs from inside `viewer_link_poll()`, at a moment between
         * blocks that frame_advance()'s own `now` does not reach, and
         * unlike raylib's `GetTime()` -- which is what `set_tab()` used to
         * call before ticket 07, and which is exactly `0.0` before
         * `InitWindow()` -- this is a real, always-valid clock read.
         */
        set_tab(app, cmd->screen == VIEWER_SCREEN_SURVEY ? TAB_SURVEY
                                                         : TAB_SCOPE,
               monotonic_seconds() - viewer_session_started_at);
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

    viewer_session_started_at = started;
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

    /*
     * ADR-0027's amendment: the bind address is loopback unless
     * `--serve-bind` said otherwise, and parse_options() has already
     * refused that combination without a token, so nothing here needs to
     * re-check it. `bind_display` is only for the messages below --
     * `INADDR_ANY` (0.0.0.0) is not itself an address a browser can be
     * pointed at, so that case gets its own sentence rather than a URL
     * built from it.
     */
    {
        uint32_t bind_addr_host;
        const char *bind_display;

        switch (app->options.serve_bind_kind) {
        case SERVE_BIND_ANY:
            bind_addr_host = INADDR_ANY;
            bind_display = NULL;
            break;
        case SERVE_BIND_ADDRESS:
            bind_addr_host = app->options.serve_bind_addr;
            bind_display = app->options.serve_bind_text;
            break;
        case SERVE_BIND_LOOPBACK:
        default:
            bind_addr_host = INADDR_LOOPBACK;
            bind_display = "127.0.0.1";
            break;
        }

        if (viewer_link_open(&link, (uint16_t)port, bind_addr_host,
                             app->options.serve_token) < 0) {
            fprintf(stderr, "Cannot open the Viewer link on %s:%d.\n",
                    bind_display ? bind_display : "0.0.0.0 (every interface)",
                    port);
            return -1;
        }
        viewer_link_set_command_handler(&link, viewer_session_handle_command,
                                        app);
        /* `--not-token`: the loud warning parse_options() already refused
           silence about -- printed before the ordinary listening line,
           not folded into it, so it reads as what it is rather than one
           clause among several. */
        if (app->options.serve_bind_kind != SERVE_BIND_LOOPBACK &&
            !app->options.serve_token)
            fprintf(stderr,
                   "WARNING: --not-token -- this Viewer link is reachable "
                   "with NO authentication at all. Anything that can reach "
                   "this port can view and control the receiver.\n");
        if (bind_display) {
            if (app->options.serve_token)
                fprintf(stderr,
                       "Viewer link listening on %s:%d -- open "
                       "http://%s:%d/?token=%s in a browser. Ctrl-C to "
                       "stop.\n",
                       bind_display, port, bind_display, port,
                       app->options.serve_token);
            else
                fprintf(stderr,
                       "Viewer link listening on %s:%d -- open "
                       "http://%s:%d/ in a browser. Ctrl-C to stop.\n",
                       bind_display, port, bind_display, port);
        } else if (app->options.serve_token) {
            /* SERVE_BIND_ANY: every interface, so there is no one address
               to print -- the operator knows which of this machine's own
               addresses the other laptop can reach. */
            fprintf(stderr,
                   "Viewer link listening on port %d, every interface -- "
                   "open http://<this machine's LAN address>:%d/?token=%s "
                   "from another machine, or http://127.0.0.1:%d/?token=%s "
                   "from here. Ctrl-C to stop.\n",
                   port, port, app->options.serve_token, port,
                   app->options.serve_token);
        } else {
            /* SERVE_BIND_ANY with --not-token: no token to fold into
               either URL. */
            fprintf(stderr,
                   "Viewer link listening on port %d, every interface -- "
                   "open http://<this machine's LAN address>:%d/ from "
                   "another machine, or http://127.0.0.1:%d/ from here. "
                   "Ctrl-C to stop.\n",
                   port, port, port);
        }
    }

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
        /* Long enough for "http://127.0.0.1:" + a port + "/?token=" + the
           128-byte cap options.c enforces on a token, with room to
           spare -- sized from that cap rather than guessed, since a
           truncated token here would silently open a browser to a URL
           the token check then refuses. */
        char url[192];

        if (app->options.serve_token)
            snprintf(url, sizeof(url), "http://127.0.0.1:%d/?token=%s", port,
                    app->options.serve_token);
        else
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
        struct survey_view_model survey_svm;
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
            /*
             * Ticket 07's Survey tab, gated on the same signal and for the
             * same reason spectrum/waterfall already are: `publish_*()`
             * queues rather than sends, so publishing on every loop
             * iteration -- rather than on every genuinely new block --
             * is exactly ticket 10's spin, measured again here before this
             * gate existed: **234216 survey_spectrum messages in 10
             * seconds**, an unbounded loop rather than the 15.26/s a live
             * receiver's own block rate would have capped it at. Building
             * the view model inside the same gate, not just publishing it,
             * because there is nothing new to build when no block arrived
             * either.
             */
            survey_view_model_build(app, &survey_svm);
            viewer_link_publish_survey_spectrum(&link, &survey_svm,
                                                svm.tuning_generation, now_ms);
            viewer_link_publish_survey_state(&link, &survey_svm, now_ms);
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
        if (viewer_duration_elapsed(now, app->options.duration_seconds))
            break;
    }

    viewer_link_close(&link);
    return 0;
}
