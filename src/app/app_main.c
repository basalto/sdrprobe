#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "config.h"
#include "debug_log.h"
#include "device_profile.h"
#include "installation.h"
#include "options.h"
#include "runtime.h"
#include "version.h"

/*
 * Everything both binaries do before and after the run: parse the command
 * line, read the environment, load the installation, open the receiver or
 * the capture, install the signal handlers, and shut the worker down.
 *
 * `run_window` is the only difference between them. `sdrprobe` passes
 * `run_gui`; `sdrprobe` passes NULL and is built with no raylib at
 * all (`.scratch/layer-boundaries/issues/04-*`). Same flags, same
 * subcommands, same messages -- so there is no second set of command words
 * to learn, which was the objection to shipping a second binary.
 */

/* Filled in by main before the app is built, and copied into it. */
static struct config loaded_config;

/* getenv() with the const the pure reader wants: it returns `char *`, and a
   lookup that hands out a mutable pointer into the environment invites a
   caller to write through it. */
static const char *environment(const char *name) {
    return getenv(name);
}

int sdrprobe_main(int argc, char **argv,
                  const struct app_window *window) {
    struct app *app;
    int result = 1;
    int gui_result;

    struct options options;
    if (parse_options(argc, argv, &options) < 0) {
        /*
         * Two refusals that can say what the reader meant: every other
         * parse failure is a bad flag or a bad combination, and the usage
         * text is the whole answer there. `serve_bind_error` joined
         * `unknown_command` here rather than getting its own dump-free
         * path, because --serve-bind/--serve-token found the same gap
         * `unknown_command` was written against: a rule specific to one
         * flag (an address that does not parse, a token the wrong length)
         * is not "a bad combination" a reader can find by re-reading the
         * usage text -- it is a rule that text does not state anywhere
         * near the flag it governs.
         */
        if (options.unknown_command)
            fprintf(stderr, "%s: unknown command \"%s\"\n\n", argv[0],
                    options.unknown_command);
        else if (options.serve_bind_error[0])
            fprintf(stderr, "%s: %s\n\n", argv[0], options.serve_bind_error);
        usage(argv[0], window != NULL);
        return 1;
    }
    /*
     * The environment's say, after the flags and before anything reads the
     * result -- a flag beats a variable beats the config file, one rule. It
     * takes getenv as an argument so the parser stays pure and
     * `check-options` can reach every case with a table.
     */
    options_apply_environment(&options, environment);
    if (options.show_version) {
        printf("sdrprobe %s\n%s\n", SDRPROBE_VERSION, SDRPROBE_CONTACT);
        return 0;
    }
    if (options.list_devices)
        return list_devices();

    /*
     * Which binary this is, and what it can do -- asked here, before
     * anything opens a receiver or a file, so a refusal costs nothing and
     * touches nothing.
     *
     * The two builds have *different command surfaces*, which is unusual
     * enough to say why. `sdrprobe` has no window to open: raylib is not
     * linked into it at all, so the refusal is a fact about the build
     * rather than a policy. `sdrprobe-gui` could run `headless` -- it
     * links the same `CORE_SRC` -- and declines anyway, so that each
     * binary does one job and a script cannot land on the wrong one and
     * quietly work. The pair of messages each name the *other* binary,
     * because "wrong build" is only useful to a reader who is told which
     * one is right.
     */
    if (!window && options.command == COMMAND_WINDOW) {
        fprintf(stderr,
                "%s has no window: it is built without raylib.\n"
                "Use `%s headless` or `%s web`, or sdrprobe-gui for the "
                "window.\n\n", argv[0], argv[0], argv[0]);
        usage(argv[0], 0);
        return 1;
    }
    if (window && options.command != COMMAND_WINDOW) {
        fprintf(stderr,
                "%s only opens the window. Use `sdrprobe %s` instead.\n",
                argv[0], options.command == COMMAND_HEADLESS ? "headless"
                                                             : "web");
        return 1;
    }

    /*
     * The installation: antenna and site. Loaded before anything measures,
     * and written back when a flag changes one, so the next run starts where
     * this one left off. A survey that cannot say what it was taken with is a
     * number nobody can compare against.
     */
    {
        struct config config;
        int changed = 0;
        config_load(&config);
        if (options.antenna && strcmp(config.antenna, options.antenna)) {
            snprintf(config.antenna, sizeof(config.antenna), "%s",
                     options.antenna);
            changed = 1;
        }
        if (options.site && strcmp(config.site, options.site)) {
            snprintf(config.site, sizeof(config.site), "%s", options.site);
            changed = 1;
        }
        /* Remembered whether or not they changed: a site or an antenna named
           once should be offered next time, and the very first --site would
           otherwise never reach the list. */
        if (config.site[0] && config_remember_site(&config, config.site))
            changed = 1;
        if (config.antenna[0] &&
            config_remember_antenna(&config, config.antenna))
            changed = 1;
        /*
         * The correction is **not** decided here any more.
         *
         * It follows the site *and the receiver* (ADR-0018), and the receiver
         * is not known until it is open -- this runs before that. Reading a
         * site-only value here is what used to apply a legacy correction to
         * whatever happened to be plugged in, which is the thing the ADR
         * refuses. It is done after `installation_load()` below, where the
         * serial is in hand.
         */
        if (changed && config_save(&config) == 0)
            fprintf(stderr, "Saved: antenna \"%s\"%s%s\n", config.antenna,
                    config.site[0] ? ", site " : "", config.site);
        loaded_config = config;
    }
    /* An ARFCN names a channel; the receiver is tuned 400 kHz below it so the
       carrier sits inside the span rather than on its DC spike, which is what
       the GSM view does when a channel is clicked. */
    if (options.arfcn) {
        uint32_t channel_hz;
        if (!gsm_downlink_hz((unsigned int)options.arfcn, &channel_hz)) {
            fprintf(stderr, "ARFCN %d is not a GSM 900 downlink channel.\n",
                    options.arfcn);
            return 1;
        }
        options.frequency = channel_hz - 400000U;
    }
    /*
     * An EARFCN names a carrier, and the receiver is tuned to its centre --
     * not beside it, as an ARFCN is. LTE never transmits on the middle
     * subcarrier, so the tuner's own DC spike lands where the standard already
     * leaves a hole, and the synchronisation signals a cell search needs sit
     * either side of it.
     */
    if (options.earfcn) {
        uint32_t carrier_hz;
        if (!lte_earfcn_downlink_hz((unsigned int)options.earfcn,
                                    &carrier_hz)) {
            fprintf(stderr, "EARFCN %d is not an LTE downlink channel this "
                            "build knows.\n", options.earfcn);
            return 1;
        }
        options.frequency = carrier_hz;
    }

    app = calloc(1, sizeof(*app));
    if (!app) {
        fprintf(stderr, "Cannot allocate application state.\n");
        return 1;
    }
    app->options = options;
    app->config = loaded_config;
    /* A calloc'd lease is already empty and its generations already start at
       one; this says so rather than leaving it to be rediscovered. */
    receiver_lease_reset(&app->lease);
    app->receiver_mode = options.file_path == NULL;
    app->remove_dc = options.remove_dc;
    view_gsm_defaults(app);
    view_lte_defaults(app);
    view_fm_defaults(app);
    view_scope_defaults(app);
    view_survey_defaults(app);
    view_srd_defaults(app);
    view_adsb_defaults(app);
    if (options.gsm_features_seen) {
        /* The mask straight through: --gsm-features already speaks in
           GSM_OPT_* and the session does too, so nothing unpacks it. */
        app->gsm.session.options = options.gsm_features;
    }
    if (options.arfcn) {
        /* Both the GSM view and a recording's sidecar read this. */
        app->gsm.selected_arfcn = options.arfcn;
        app->gsm.selected_hz = (double)options.frequency + 400000.0;
    }

    const char *log_target = options.debug_log ? options.debug_log : environment("SDRPROBE_DEBUG_LOG");
    if (!log_target && app->receiver_mode)
        log_target = "sdrprobe.log";

    if (log_target && debug_log_open(log_target) == 0) {
        /* From the options rather than from the app: the source has not been
           attached yet here, and the fields it fills in are still zero. */
        debug_log_write("open",
                        "sdrprobe %s, %s, %u S/s, %.6f MHz, %+d ppm",
                        SDRPROBE_VERSION,
                        options.file_path ? options.file_path : "receiver",
                        options.sample_rate, options.frequency / 1e6,
                        options.ppm);
    }

    /* Before any path that can reach cleanup, which touches this. */
    int acq_result = acquisition_init(&app->acq);
    if (acq_result != 0) {
        fprintf(stderr, "Cannot set up acquisition: %s\n",
                strerror(acq_result));
        goto cleanup;
    }

    if (app->receiver_mode) {
        if (configure_receiver(app) < 0)
            goto cleanup;
    } else {
        if (open_capture(app) < 0)
            goto cleanup;
    }

    /*
     * The receiving setup, now that the source has said what it is.
     *
     * It has to be here and not earlier: ADR-0018 keys a correction by the
     * receiver, and the receiver's serial is not known until it is open. A
     * capture reports none, which is right -- a recording's correction is
     * already in its samples.
     */
    installation_load(&app->installation, &app->config, app->device.serial,
                      app->options.receiver_label);
    if (debug_log_active()) {
        /*
         * What this run's measurements belong to.
         *
         * A second line rather than a longer `open` one, because it cannot be
         * written earlier: ADR-0018 keys a correction by the receiver, and the
         * receiver's serial is not known until it is open -- which happens
         * after the log does. Without it a log can say what was tuned and
         * never what installation it was tuned by, which is the one thing
         * every measurement in this program is keyed by (ADR-0018, ADR-0022).
         *
         * The gain goes through the profile's own speller: an AD9361's
         * receive gain is a table index that looks like a decibel, and
         * logging `40 dB` for `index 40` would be a number nobody measured.
         */
        char gain[32];

        device_gain_format(&app->device, app->applied_gain_tenths, gain,
                           sizeof(gain));
        debug_log_write("installation",
                        "receiver %s site \"%s\" antenna \"%s\" gain %s",
                        app->installation.receiver[0]
                            ? app->installation.receiver : "none",
                        app->installation.site, app->installation.antenna,
                        app->applied_manual_gain ? gain : "auto");
    }
    {
        int legacy = 0, profile = 0;

        /*
         * An explicit --ppm outranks everything **for this run** and is
         * written down only when asked. Otherwise a profile for this receiver
         * at this site is restored -- which is what "arriving somewhere the
         * receiver has been calibrated restores that calibration" means once
         * a correction knows whose crystal it is.
         *
         * **It used to record unconditionally, and that destroyed
         * measurements** (`.scratch/device-model/issues/12-*`). A correction
         * is an on-air measurement against whatever reference a place offers;
         * a flag on a command line is not. The two were the same act, so
         * `--ppm 0` -- which every uncorrected sweep needs, because only
         * uncorrected does a clock-coherent tone read its exact nominal, and
         * which `scripts/tone_probe.sh` passes in its documented mode -- wrote
         * 0 over a measured +32 twice in one afternoon. Zeroing it does not
         * merely lose precision: `reading_origin_for()` refuses outright when
         * the crystal error is zero, so every coherence verdict silently
         * becomes `unexplained`, which is the flag the ticket doing the
         * sweeping was about.
         *
         * `--claim-calibration` is the explicit act, and it already meant
         * exactly this for a legacy value: make this correction mine. It is
         * how a headless `--calibrate` result is stored, since the run that
         * measures it only prints it.
         */
        if (app->options.ppm_seen) {
            int stored = 0;
            int had = installation_ppm(&app->installation, &stored);

            if (installation_record_ppm(&app->installation,
                                        app->options.ppm) == 0) {
                if (installation_records_ppm(app->options.ppm_seen,
                                             app->options.claim_calibration)) {
                    if (installation_commit(&app->installation,
                                            &app->config) == 0)
                        fprintf(stderr,
                                "Claimed %+d ppm for %s at \"%s\".\n",
                                app->options.ppm, app->installation.receiver,
                                app->installation.site);
                } else if (had && stored != app->options.ppm) {
                    /* The confusing case, and the only one worth a line: a
                       stored calibration exists and this run is not using
                       it. Silence here is what made the overwrite invisible. */
                    fprintf(stderr,
                            "Using %+d ppm for this run. %s at \"%s\" stays "
                            "calibrated %+d ppm "
                            "(--claim-calibration to replace it).\n",
                            app->options.ppm, app->installation.receiver,
                            app->installation.site, stored);
                }
            }
        } else if (installation_ppm(&app->installation, &profile)) {
            app->options.ppm = profile;
            if (app->receiver_mode &&
                set_frequency_correction(&app->source, profile) >= 0)
                app->applied.ppm = profile;
            fprintf(stderr, "Restored %+d ppm for %s at \"%s\".\n", profile,
                    app->installation.receiver, app->installation.site);
        }

        if (app->installation.site[0] &&
            !installation_ppm(&app->installation, NULL) &&
            installation_legacy_ppm(&app->installation, &legacy)) {
            /*
             * ADR-0018 will not apply it: it cannot say whose crystal it
             * compensates. Saying so beats both applying it silently and
             * saying nothing, which would look like the correction had been
             * lost.
             */
            if (installation_identified(&app->installation))
                fprintf(stderr,
                        "Site \"%s\" has a %+d ppm correction from before "
                        "calibrations named a receiver.\n"
                        "It is not applied. --claim-calibration assigns it "
                        "to this one (%s).\n",
                        app->installation.site, legacy,
                        app->installation.receiver);
            else
                fprintf(stderr,
                        "Site \"%s\" has a %+d ppm correction from before "
                        "calibrations named a receiver.\n"
                        "It is not applied, and this receiver has no "
                        "identity to claim it for -- see "
                        "--receiver-label.\n",
                        app->installation.site, legacy);
        }
        if (app->options.claim_calibration && !app->options.ppm_seen) {
            if (installation_claim_legacy(&app->installation) == 0 &&
                installation_commit(&app->installation, &app->config) == 0)
                fprintf(stderr, "Claimed %+d ppm for %s at \"%s\".\n",
                        legacy, app->installation.receiver,
                        app->installation.site);
            else
                fprintf(stderr, "Nothing to claim.\n");
        }
    }

    if (install_signal_handlers(app) < 0)
        goto cleanup;

    if (options.headless) {
        result = run_headless(app) == 0 ? 0 : 1;
        goto cleanup;
    }
    /*
     * The one place the two binaries differ. `sdrprobe` hands its `run_gui`
     * in; `sdrprobe` hands NULL, because it is built without raylib and
     * has no window to open.
     */
    if (!window) {
        fprintf(stderr,
                "%s is built without a window: use `headless` or `server`.\n",
                argv[0]);
        result = 1;
        goto cleanup;
    }
    gui_result = window->run(app);
    result = gui_result == 0 ? 0 : 1;

cleanup:
    request_worker_stop(&app->acq);
    if (app->acq.worker_started) {
        if (app->receiver_mode) {
            int done = 0;
            int reading = worker_is_reading(app, &done);
            if (reading) {
                int cancel_result = -1;
                for (int attempt = 0; attempt < 100 && !done; attempt++) {
                    cancel_result = device_stop(&app->source);
                    if (cancel_result == 0)
                        break;
                    struct timespec retry = { 0, 1000000L };
                    nanosleep(&retry, NULL);
                    worker_is_reading(app, &done);
                }
                if (cancel_result != 0) {
                    worker_is_reading(app, &done);
                    fprintf(stderr, "Failed to cancel RTL-SDR asynchronous read (%d).\n",
                            cancel_result);
                    result = 1;
                    if (!done) {
                        fprintf(stderr,
                                "Reader may still be active; exiting without releasing shared state.\n");
                        return result;
                    }
                }
            }
        }
        int join_result = pthread_join(app->acq.worker, NULL);
        if (join_result != 0) {
            fprintf(stderr, "Cannot join acquisition worker: %s\n",
                    strerror(join_result));
            fprintf(stderr,
                    "Worker ownership is uncertain; exiting without releasing shared state.\n");
            return 1;
        }
        app->acq.worker_started = 0;
    }

    if (app->capture) {
        if (fclose(app->capture) != 0) {
            fprintf(stderr, "Cannot close capture: %s\n", strerror(errno));
            result = 1;
        }
        app->capture = NULL;
    }
    int destroy_result = acquisition_destroy(&app->acq);
    if (destroy_result != 0) {
        fprintf(stderr, "Cannot tear down acquisition: %s\n",
                strerror(destroy_result));
        result = 1;
    }
    if (device_session_open(&app->source)) {
        device_close(&app->source);
    }
    /* The window's own teardown -- textures, the audio device, the window
       itself -- and then the rows every run allocates whether or not one was
       ever drawn. NULL here is `sdrprobe`, which has neither. */
    if (window && window->release)
        window->release(app);
    scope_release_history(app);
    if (app->signals_ready) {
        if (sigaction(SIGTERM, &app->old_sigterm, NULL) != 0 ||
            sigaction(SIGINT, &app->old_sigint, NULL) != 0) {
            fprintf(stderr, "Cannot restore signal handlers: %s\n",
                    strerror(errno));
            result = 1;
        }
    }
    debug_log_write("close", "result %d", result);
    debug_log_close();
    free(app);
    return result;
}
