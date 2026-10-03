#define _POSIX_C_SOURCE 200809L

#include "server/viewer_session.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#include "server/browser.h"
#include "model/fm_view_model.h"
#include "model/adsb_view_model.h"
#include "model/gsm_view_model.h"
#include "model/lte_view_model.h"
#include "model/calibration_view_model.h"
#include "model/settings_view_model.h"
#include "model/srd_view_model.h"
#include "model/tetra_view_model.h"
#include "runtime/frame_advance.h"
#include "server/process_cpu.h"
#include "model/scope_view_model.h"
#include "model/survey_view_model.h"
#include "runtime/runtime.h"
#include "server/viewer_link.h"

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
         * `set_tab()` and `set_decode()` are the same two functions every
         * tab-bar and option-row click in the window goes through, not a
         * headless shortcut around them -- ticket 07's whole point is one
         * seam, not a second one that happens to agree with the first
         * today. `monotonic_seconds()` rather than a `now` threaded in
         * from the caller: this handler runs from inside
         * `viewer_link_poll()`, at a moment between blocks that
         * frame_advance()'s own `now` does not reach, and unlike raylib's
         * `GetTime()` -- which is what `set_tab()` used to call before
         * ticket 07, and which is exactly `0.0` before `InitWindow()` --
         * this is a real, always-valid clock read.
         *
         * The decode kind is set *before* the tab, which is the order
         * run_gui()'s own startup sequence uses and for the same reason it
         * gives: switching to the Decode tab first enters whichever kind is
         * already recorded and then leaves it again on the way to the one
         * asked for, retuning twice. `set_decode()` off the Decode tab only
         * records the choice, which is exactly what is wanted here.
         */
        {
            double now = monotonic_seconds() - viewer_session_started_at;

            switch (cmd->screen) {
            case VIEWER_SCREEN_SURVEY:
                set_tab(app, TAB_SURVEY, now);
                break;
            case VIEWER_SCREEN_FM:
                set_decode(app, DECODE_FM, now);
                set_tab(app, TAB_DECODE, now);
                break;
            case VIEWER_SCREEN_GSM:
                set_decode(app, DECODE_GSM, now);
                set_tab(app, TAB_DECODE, now);
                break;
            case VIEWER_SCREEN_ADSB:
                set_decode(app, DECODE_ADSB, now);
                set_tab(app, TAB_DECODE, now);
                break;
            case VIEWER_SCREEN_TETRA:
                set_decode(app, DECODE_TETRA, now);
                set_tab(app, TAB_DECODE, now);
                break;
            case VIEWER_SCREEN_SRD:
                set_decode(app, DECODE_SRD, now);
                set_tab(app, TAB_DECODE, now);
                break;
            case VIEWER_SCREEN_LTE:
                set_decode(app, DECODE_LTE, now);
                set_tab(app, TAB_DECODE, now);
                break;
            /*
             * The overlays are opened rather than tabbed to, because that is
             * what they are: full-screen modals orthogonal to the tab bar
             * (ADR-0008). `receiver_state.screen` then reports the overlay
             * rather than the tab underneath, so a browser follows.
             */
            case VIEWER_SCREEN_SETTINGS:
                open_settings(app);
                break;
            case VIEWER_SCREEN_CALIBRATION:
                open_calibration(app);
                break;
            case VIEWER_SCREEN_SCOPE:
            default:
                set_tab(app, TAB_SCOPE, now);
                break;
            }
        }
        return 0;
    case VIEWER_COMMAND_SET:
        /*
         * Staging only -- the same thing `handle_settings_input()` does, and
         * the reason `set` and `apply` are two commands: one step of a
         * stepper must not restart acquisition, and `settings_apply()`
         * validates the staged set *together*, so a rejected PPM must not
         * also lose a transform size the reader had just chosen.
         */
        switch (cmd->setting) {
        case VIEWER_SETTING_PPM:
            snprintf(app->set.ppm, sizeof(app->set.ppm), "%d", cmd->value);
            app->set.ppm_length = (int)strlen(app->set.ppm);
            break;
        case VIEWER_SETTING_FFT: {
            int choice = sdr_dsp_fft_choice_of(cmd->value);

            if (choice < 0) {
                snprintf(error, error_cap,
                         "not a transform size this program offers");
                return -1;
            }
            app->set.fft_choice = choice;
            break;
        }
        case VIEWER_SETTING_GAIN:
            /* The device's list is the bound, and only the device knows it
               -- the command table left the upper end wide for exactly
               this. 0 is automatic, so the count is the last index. */
            if (cmd->value > device_gain_option_count(&app->device)) {
                snprintf(error, error_cap,
                         "this receiver has no such gain step");
                return -1;
            }
            app->set.gain_choice = cmd->value;
            break;
        case VIEWER_SETTING_DC:
            app->set.remove_dc = cmd->value;
            break;
        case VIEWER_SETTING_DRIFT:
            app->set.auto_drift = cmd->value;
            break;
        }
        app->set.error[0] = '\0';
        return 0;
    case VIEWER_COMMAND_CALIBRATE:
        /*
         * Starting a measurement, and nothing more -- it does **not** apply
         * the result. A calibration writes a standing fact about this
         * receiver at this site (ADR-0018, ADR-0022), and applying it is
         * `set ppm` plus `apply`: one more deliberate act, which is what
         * stops a browser silently recalibrating a receiver.
         */
        if (cmd->reference == VIEWER_CALIBRATE_STOP) {
            if (calibration_stop_measuring(app) < 0) {
                snprintf(error, error_cap, "%s",
                         app->cal.status[0] ? app->cal.status
                                            : "could not stop the "
                                              "measurement");
                return -1;
            }
            return 0;
        }
        if (!app->cal.open)
            open_calibration(app);
        calibration_select_technology(app,
                                      cmd->reference == VIEWER_CALIBRATE_LTE);
        if (start_calibration(app) < 0) {
            /* The overlay's own status line, quoted rather than reworded --
               the same sentence a reader at the window would see. */
            snprintf(error, error_cap, "%s",
                     app->cal.status[0] ? app->cal.status
                                        : "the calibration would not start");
            return -1;
        }
        return 0;
    case VIEWER_COMMAND_SCAN: {
        /*
         * A band walk, the window's "Scan band" button -- FM's band II or the
         * LTE band the picker has selected. The browser needs it because in
         * `web` mode there is no window to press it, and the scan lists (and
         * FM's waterfall marks) are empty until one has run. It applies
         * nothing -- a scan only fills a list -- so unlike `calibrate` it
         * needs no second deliberate act.
         *
         * `now` is the serve loop's relative clock, for the reason the VIEW
         * case gives: the scans stamp each step from it, and a 0.0 origin
         * would race the whole plan in one pass.
         */
        double now = monotonic_seconds() - viewer_session_started_at;

        if (cmd->scan == VIEWER_SCAN_STOP) {
            /* The Stop button names no technology, so stop whichever walk is
               running -- both flags are independent and stopping an idle one
               is a no-op. */
            fm_scan_stop(app);
            scan_stop(app);
            return 0;
        }
        if (cmd->scan == VIEWER_SCAN_LTE) {
            /* scan_start() returns -1 without a receiver or without a band
               picked; `app->lte.scan.status` is not a thing, so the message
               is this layer's, matching the window's refusal. */
            if (scan_start(app, now) < 0) {
                snprintf(error, error_cap, "%s",
                         "an LTE band scan needs a live receiver and a band");
                return -1;
            }
            return 0;
        }
        fm_scan_begin(app, now);
        if (!app->fm.scan.running) {
            /* fm_scan_begin() is void and reports its refusal in the scan's
               own status line -- "A band scan needs a live receiver.", or
               the rate being too low -- so that sentence is what a reader
               at the window would see too. */
            snprintf(error, error_cap, "%s",
                     app->fm.scan.status[0] ? app->fm.scan.status
                                            : "the band scan would not start");
            return -1;
        }
        return 0;
    }
    case VIEWER_COMMAND_SELECT: {
        /*
         * A click on a view's own list or chart, the way `tune` is a click on
         * FM's Band II table: GSM inspects a channel, LTE parks on a scan row,
         * the survey inspects a candidate. Each is the window's own click
         * handler reached from the browser, and each runtime function bounds
         * its own index -- an out-of-range value from a command changes
         * nothing rather than being refused here.
         */
        double now = monotonic_seconds() - viewer_session_started_at;

        switch (cmd->select) {
        case VIEWER_SELECT_ARFCN:
            gsm_tune_selected(app, cmd->value);
            break;
        case VIEWER_SELECT_CELL:
            scan_select(app, cmd->value);
            break;
        case VIEWER_SELECT_CANDIDATE:
            survey_select(app, cmd->value, now);
            break;
        }
        return 0;
    }
    case VIEWER_COMMAND_APPLY: {
        int clear_waterfall = 0;

        /*
         * The same transaction the panel's Apply button runs, and the same
         * sentence when it refuses -- `settings_apply()` writes
         * `app->set.error` and this quotes it, so a command's failure and
         * the window's notice cannot come to different wordings.
         *
         * The waterfall flag is discarded here on purpose: there is no
         * texture to recreate without a window, and a Viewer's waterfall is
         * rows it has already been sent.
         */
        if (settings_apply(app, &clear_waterfall) < 0) {
            snprintf(error, error_cap, "%s",
                     app->set.error[0] ? app->set.error
                                       : "the settings were refused");
            return -1;
        }
        return 0;
    }
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
    double settings_published_at = -1.0;
    double cal_published_at = -1.0;
    double state_published_at = -1.0;
    uint32_t state_generation = 0;
    int state_ever_published = 0;
    /* The FM analysis charts are paced on time, so they read the most recent
       FM model on a 4 Hz heartbeat rather than only on the pass that built it.
       The model therefore persists across iterations: zeroed here, rebuilt in
       the on-data block, and its publishers guard on their own counts so a
       zeroed model before the first block sends nothing. */
    double fm_charts_published_at = -1.0;
    struct fm_view_model fm_svm;
    /* The TETRA analysis charts, persisted for the same reason as FM's. */
    double tetra_charts_published_at = -1.0;
    struct tetra_view_model tetra_svm;
    /* The SRD analysis charts, persisted for the same reason. */
    double srd_charts_published_at = -1.0;
    struct srd_view_model srd_svm;
    /* The ADS-B analysis charts, persisted for the same reason. */
    double adsb_charts_published_at = -1.0;
    struct adsb_view_model adsb_svm;
    /* The GSM analysis charts, persisted for the same reason. */
    double gsm_charts_published_at = -1.0;
    struct gsm_view_model gsm_svm;
    /* The LTE analysis charts, persisted for the same reason. */
    double lte_charts_published_at = -1.0;
    struct lte_view_model lte_svm;

    memset(&fm_svm, 0, sizeof(fm_svm));
    memset(&tetra_svm, 0, sizeof(tetra_svm));
    memset(&srd_svm, 0, sizeof(srd_svm));
    memset(&adsb_svm, 0, sizeof(adsb_svm));
    memset(&gsm_svm, 0, sizeof(gsm_svm));
    memset(&lte_svm, 0, sizeof(lte_svm));
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
        /* `--no-token`: the loud warning parse_options() already refused
           silence about -- printed before the ordinary listening line,
           not folded into it, so it reads as what it is rather than one
           clause among several. */
        if (app->options.serve_bind_kind != SERVE_BIND_LOOPBACK &&
            !app->options.serve_token)
            fprintf(stderr,
                   "WARNING: --no-token -- this Viewer link is reachable "
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
            /* SERVE_BIND_ANY with --no-token: no token to fold into
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
        const struct receiver_view_model *rvm;
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

        /*
         * This session sets `app->tab` and `app->view` itself, above -- the
         * Scope with its spectrum view, the only screen ADR-0027 serves --
         * so the Scope does own the spectrum here and the size is whatever
         * `--fft` or the saved config asked for. Said outright rather than
         * routed through a screen query, because there is no screen.
         */
        spectrum_updated = frame_advance(app, &snapshot, now,
                                         app->sv.fft_size);
        {
    struct scope_view_model_input in;

    in.frame = &app->frame;
    in.sv = &app->sv;
    in.applied = &app->applied;
    in.device = &app->device;
    in.tab = (int)app->tab;
    in.decode = (int)app->decode;
    in.settings_open = app->set.open;
    in.calibration_open = app->cal.open;
    scope_view_model_build(&in, &svm);
}
        /* The shell's half of what a Viewer is told -- the screen, the
           tuning and ADR-0027's generation. Named here because the three
           publishes below stamp every message with it. */
        rvm = &svm.receiver;
        now_ms = (uint64_t)(monotonic_seconds() * 1000.0);

        /*
         * The data-paced streams, all eleven of them, through the one
         * predicate rather than a bare `if`.
         *
         * `viewer_publish_due()` reads the pacing table in
         * `viewer_session.h`, which names every value of `enum
         * viewer_stream` and which `check-viewer-session` walks: a stream
         * added without saying what paces it fails the gate. The table is
         * asked with `VIEWER_STREAM_SPECTRUM` because every stream in this
         * block is on-data and the answer is the same for all of them --
         * what the call buys over `if (spectrum_updated)` is that the
         * *reason* has a name and an enumeration behind it.
         *
         * What it cannot buy: nothing here can see the shape of this loop,
         * so a publish written outside this block is still invisible to a
         * check. `make bench-serve` is what catches that, and it is a
         * number rather than an opinion.
         */
        if (viewer_publish_due(VIEWER_STREAM_SPECTRUM, spectrum_updated,
                               now, -1.0, 0.0, 0)) {
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
            {
        struct survey_record_tuning tuning;

        survey_tuning_from(&tuning, app);
        survey_view_model_build(&app->survey.session, &tuning, &survey_svm);
    }
            viewer_link_publish_survey_spectrum(&link, &survey_svm,
                                                rvm->tuning_generation,
                                                now_ms);
            viewer_link_publish_survey_state(&link, &survey_svm, now_ms);
            /* The FM view, on the same gate and for the same reason. */
            fm_view_model_build(&app->fm, &fm_svm);
            viewer_link_publish_fm_spectrum(&link, &fm_svm,
                                            rvm->tuning_generation, now_ms);
            viewer_link_publish_fm_state(&link, &fm_svm, now_ms);
            /* And GSM, on the same gate and for the same reason. */
            gsm_view_model_build(&app->gsm, &app->bandscan,
                                 &app->frame.signal_stats,
                                 app->frame.signal_stats_ready,
                                 acquisition_recording_status(&app->acq, NULL,
                                                              NULL, 0),
                                 app->receiver_mode, &gsm_svm);
            viewer_link_publish_gsm_state(&link, &gsm_svm, now_ms);
            /* And ADS-B, on the same gate. */
            adsb_view_model_build(&app->adsb, app->applied.frequency_hz,
                                  app->applied.sample_rate_hz,
                                  app->receiver_mode,
                                  app->frame.have_samples, &adsb_svm);
            viewer_link_publish_adsb_state(&link, &adsb_svm, now_ms);
            /* And TETRA, on the same gate. */
            tetra_view_model_build(&app->tetra, &tetra_svm);
            viewer_link_publish_tetra_state(&link, &tetra_svm, now_ms);
            /* And SRD, on the same gate. */
            srd_view_model_build(&app->srd, app->applied.frequency_hz,
                                 app->applied.sample_rate_hz,
                                 app->receiver_mode, &srd_svm);
            viewer_link_publish_srd_state(&link, &srd_svm, now, now_ms);
            /* And LTE, on the same gate. The tuning it is handed is the
               applied one, because the crystal error in ppm is that offset
               over *this* carrier and means nothing without it. */
            {
                const struct lte_band *lte_band = selected_band(app);
                struct lte_view_context lte_ctx;

                memset(&lte_ctx, 0, sizeof(lte_ctx));
                const struct lte_band *tuned =
                    lte_band_for_earfcn(app->lte.earfcn);

                lte_ctx.centre_hz = app->applied.frequency_hz;
                lte_ctx.band_number = lte_band ? lte_band->band : 0;
                lte_ctx.tuned_band = tuned ? tuned->band : 0;
                lte_ctx.tuned_band_name = tuned ? tuned->name : NULL;
                if (lte_band) {
                    lte_ctx.scan_channels = lte_scan_count(lte_band);
                    lte_ctx.scan_first_pass_seconds =
                        lte_scan_first_pass_seconds(lte_band);
                    lte_ctx.scan_all_seconds = lte_scan_seconds(lte_band);
                }
                if (lte_band && app->lte.scan.running)
                    lte_earfcn_downlink_hz(
                        lte_scan_candidate(lte_band,
                                           app->lte.scan.candidate),
                        &lte_ctx.scan_candidate_hz);
                lte_ctx.on_grid = lte_on_grid(app);
                lte_ctx.receiver_mode = app->receiver_mode;
                lte_ctx.now = now;
                lte_view_model_build(&app->lte, &lte_ctx, &lte_svm);
            }
            viewer_link_publish_lte_state(&link, &lte_svm, now_ms);
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
        if (viewer_publish_due(VIEWER_STREAM_RECEIVER_STATE, spectrum_updated,
                               now, state_published_at,
                               VIEWER_SESSION_STATE_INTERVAL_SECONDS,
                               state_ever_published &&
                                   rvm->tuning_generation !=
                                       state_generation)) {
            viewer_link_publish_receiver_state(&link, rvm, now_ms);
            state_published_at = now;
            state_generation = rvm->tuning_generation;
            state_ever_published = 1;
        }

        /*
         * The Settings panel, on the same clock as `receiver_state` and for
         * the same reason: it changes when somebody types, not when a block
         * arrives (`viewer_stream_pacing()`).
         */
        if (viewer_publish_due(VIEWER_STREAM_SETTINGS_STATE, spectrum_updated,
                               now, settings_published_at,
                               VIEWER_SESSION_STATE_INTERVAL_SECONDS, 0)) {
            struct settings_view_model set_svm;

            settings_view_model_build(&app->set, &app->device,
                                      app->receiver_mode,
                                      app->applied_manual_gain,
                                      app->applied_gain_tenths,
                                      app->applied.ppm, app->sv.fft_size,
                                      app->remove_dc, app->cal.auto_drift,
                                      app->applied.sample_rate_hz, &set_svm);
            viewer_link_publish_settings_state(&link, &set_svm, now_ms);
            settings_published_at = now;
        }

        /* And the Calibration overlay, on the same clock as the Settings
           panel and for the same reason. */
        if (viewer_publish_due(VIEWER_STREAM_CAL_STATE, spectrum_updated,
                               now, cal_published_at,
                               VIEWER_SESSION_STATE_INTERVAL_SECONDS, 0)) {
            struct calibration_view_model cvm;

            calibration_view_model_build(&app->cal, now, app->applied.ppm,
                                         &cvm);
            viewer_link_publish_cal_state(&link, &cvm, now_ms);
            cal_published_at = now;
        }

        /*
         * The FM analysis charts behind "Show charts", on a 4 Hz heartbeat.
         * One `viewer_publish_due` gates all three because they share a pacing
         * (the on-data block does the same with VIEWER_STREAM_SPECTRUM). They
         * read the persisted `fm_svm`, rebuilt in the on-data block from the
         * last block, and each publisher sends nothing for an array no decode
         * has filled -- so before the first block, and for a chart with no
         * data yet, this costs nothing. Subscribed only while the charts show.
         */
        if (viewer_publish_due(VIEWER_STREAM_FM_AUDIO, spectrum_updated,
                               now, fm_charts_published_at,
                               VIEWER_SESSION_CHART_INTERVAL_SECONDS, 0)) {
            viewer_link_publish_fm_audio(&link, &fm_svm,
                                         rvm->tuning_generation, now_ms);
            viewer_link_publish_fm_audio_spectrum(&link, &fm_svm,
                                                  rvm->tuning_generation,
                                                  now_ms);
            viewer_link_publish_fm_scatter(&link, &fm_svm,
                                           rvm->tuning_generation, now_ms);
            fm_charts_published_at = now;
        }

        /* The TETRA analysis charts, the same 4 Hz heartbeat and the same
           persisted-model reasoning as FM's above. */
        if (viewer_publish_due(VIEWER_STREAM_TETRA_SCATTER, spectrum_updated,
                               now, tetra_charts_published_at,
                               VIEWER_SESSION_CHART_INTERVAL_SECONDS, 0)) {
            viewer_link_publish_tetra_scatter(&link, &tetra_svm,
                                              rvm->tuning_generation, now_ms);
            viewer_link_publish_tetra_profile(&link, &tetra_svm,
                                              rvm->tuning_generation, now_ms);
            tetra_charts_published_at = now;
        }

        /* The SRD analysis charts, the same 4 Hz heartbeat. */
        if (viewer_publish_due(VIEWER_STREAM_SRD_ENVELOPE, spectrum_updated,
                               now, srd_charts_published_at,
                               VIEWER_SESSION_CHART_INTERVAL_SECONDS, 0)) {
            viewer_link_publish_srd_envelope(&link, &srd_svm,
                                             rvm->tuning_generation, now_ms);
            viewer_link_publish_srd_chips(&link, &srd_svm,
                                          rvm->tuning_generation, now_ms);
            srd_charts_published_at = now;
        }

        /* The ADS-B analysis charts, the same 4 Hz heartbeat. */
        if (viewer_publish_due(VIEWER_STREAM_ADSB_LANDSCAPE, spectrum_updated,
                               now, adsb_charts_published_at,
                               VIEWER_SESSION_CHART_INTERVAL_SECONDS, 0)) {
            viewer_link_publish_adsb_landscape(&link, &adsb_svm,
                                               rvm->tuning_generation, now_ms);
            viewer_link_publish_adsb_confidence(&link, &adsb_svm,
                                                rvm->tuning_generation, now_ms);
            viewer_link_publish_adsb_envelope(&link, &adsb_svm,
                                              rvm->tuning_generation, now_ms);
            viewer_link_publish_adsb_scatter(&link, &adsb_svm,
                                             rvm->tuning_generation, now_ms);
            adsb_charts_published_at = now;
        }

        /* The GSM analysis charts, the same 4 Hz heartbeat. */
        if (viewer_publish_due(VIEWER_STREAM_GSM_CORR, spectrum_updated,
                               now, gsm_charts_published_at,
                               VIEWER_SESSION_CHART_INTERVAL_SECONDS, 0)) {
            viewer_link_publish_gsm_corr(&link, &gsm_svm,
                                         rvm->tuning_generation, now_ms);
            viewer_link_publish_gsm_soft(&link, &gsm_svm,
                                         rvm->tuning_generation, now_ms);
            viewer_link_publish_gsm_phase(&link, &gsm_svm,
                                          rvm->tuning_generation, now_ms);
            viewer_link_publish_gsm_scatter(&link, &gsm_svm,
                                            rvm->tuning_generation, now_ms);
            gsm_charts_published_at = now;
        }

        /* The LTE analysis charts, the same 4 Hz heartbeat. */
        if (viewer_publish_due(VIEWER_STREAM_LTE_PSS, spectrum_updated,
                               now, lte_charts_published_at,
                               VIEWER_SESSION_CHART_INTERVAL_SECONDS, 0)) {
            viewer_link_publish_lte_pss(&link, &lte_svm,
                                        rvm->tuning_generation, now_ms);
            viewer_link_publish_lte_sss(&link, &lte_svm,
                                        rvm->tuning_generation, now_ms);
            viewer_link_publish_lte_channel(&link, &lte_svm,
                                            rvm->tuning_generation, now_ms);
            viewer_link_publish_lte_ports(&link, &lte_svm,
                                          rvm->tuning_generation, now_ms);
            viewer_link_publish_lte_scatter(&link, &lte_svm,
                                            rvm->tuning_generation, now_ms);
            lte_charts_published_at = now;
        }

        if (viewer_publish_due(VIEWER_STREAM_LINK_HEALTH, spectrum_updated,
                               now, health_published_at,
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
