#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <math.h>
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
#include "capture_sidecar.h"
#include "debug_log.h"
#include "device_profile.h"
#include "frame_advance.h"
#include "options.h"
#include "runtime.h"
#include "view_input.h"
#include "sdr_dsp.h"

/*
 * The application layer: the clock, the receiver, the retune transaction,
 * the lease, the tabs and the per-block step.
 *
 * It was the first eight hundred lines of `sdrprobe.c`, beside `run_gui()`
 * and `InitWindow()`, and that is the whole of why a server could not be
 * built without the window -- fourteen symbols, every one of them reached
 * from a runtime file that draws nothing
 * (`.scratch/layer-boundaries/issues/04-*`, and ticket 02 called this its
 * own remainder). Nothing here draws or reads input; nothing here includes
 * raylib.
 *
 * What is *not* here is as deliberate. `run_gui()` and the tab chrome stay
 * in `sdrprobe.c` with the window; `run_headless()` is `headless_run.c`;
 * and `main()`'s shared setup is `app_main.c`, so the two binaries differ
 * by which `run_window` they hand it and nothing else.
 */

static volatile sig_atomic_t signal_stop_requested = 0;

/* The one global the program has, read through a function so it stays one:
   Ctrl-C during a long headless sweep should stop it, and the sweep lives in
   another file. */
int stop_requested(void) {
    return signal_stop_requested != 0;
}

void set_tab(struct app *app, int new_tab, double now);
void set_decode(struct app *app, int kind, double now);

static void on_signal(int signal_number) {
    (void)signal_number;
    signal_stop_requested = 1;
}



/*
 * What the source currently reports, or 0 when it will not say. The `get_`
 * half of the driver surface used to be called inline; a backend returns a
 * status and writes through a pointer, so these keep the call sites reading
 * the way they did.
 */
static uint32_t source_frequency(struct app *app) {
    uint32_t hz = 0;
    return device_frequency_hz(&app->source, &hz) == 0 ? hz : 0;
}

static uint32_t source_sample_rate(struct app *app) {
    uint32_t hz = 0;
    return device_sample_rate_hz(&app->source, &hz) == 0 ? hz : 0;
}

static int source_ppm(struct app *app) {
    int ppm = 0;
    return device_ppm(&app->source, &ppm) == 0 ? ppm : 0;
}

static int source_gain(struct app *app) {
    int gain = 0;
    return device_gain(&app->source, &gain) == 0 ? gain : 0;
}

int set_frequency_correction(struct device_session *source, int ppm) {
    int current = 0;
    if (device_ppm(source, &current) == 0 && current == ppm)
        return 0;
    return device_set_ppm(source, ppm);
}





/* Every gain the source offers, in the unit the source uses -- tenths of a dB
   from a tuner, dB or a gain-table index from a device with a range. The
   profile decides; nothing here converts (ticket 06). */
static void print_supported_gains(const struct device_profile *profile) {
    int count = device_gain_option_count(profile);
    char text[32];

    fprintf(stderr, "Supported gains:");
    for (int i = 0; i < count; i++) {
        device_gain_format(profile, device_gain_option_value(profile, i), text,
                           sizeof(text));
        fprintf(stderr, "%s%s", i ? ", " : " ", text);
    }
    fputc('\n', stderr);
}

int configure_receiver(struct app *app) {
    const int *gains;
    int gain_count;
    int selected_gain = 0;
    int result = -1;
    uint32_t reported_frequency;
    uint32_t reported_rate;

    if (device_backend_rtlsdr_count() == 0) {
        fprintf(stderr, "No supported RTLSDR devices found.\n");
        return -1;
    }
    /*
     * The backend opens the device and fills in the profile -- the container,
     * the full scale, the tuner's reach, the gain list -- because it is the
     * only thing that knows any of that. What used to be a malloc'd copy of
     * the gain list is the profile's own array now, so there is one of it.
     */
    if (device_backend_rtlsdr()->open(&app->source, app->options.device_index,
                                      &app->device) < 0) {
        fprintf(stderr, "Failed to open RTL-SDR receiver index %d. "
                        "Try --list-devices.\n",
                app->options.device_index);
        return -1;
    }

    gains = app->device.gain_list;
    gain_count = app->device.gain_count;
    if (gain_count <= 0) {
        fprintf(stderr, "Failed to enumerate supported tuner gains.\n");
        goto done;
    }

    if (app->options.gain_kind != GAIN_REQUEST_AUTO) {
        if (app->options.gain_kind == GAIN_REQUEST_MAX) {
            selected_gain = gains[0];
            for (int i = 1; i < gain_count; i++)
                if (gains[i] > selected_gain)
                    selected_gain = gains[i];
        } else if (app->options.gain_kind == GAIN_REQUEST_DEFAULT) {
            /* The gain table is tuner-specific, so the default is a target to
               snap to rather than a value to demand. */
            selected_gain = gains[0];
            for (int i = 1; i < gain_count; i++)
                if (abs(gains[i] - DEFAULT_GAIN_TENTHS) <
                    abs(selected_gain - DEFAULT_GAIN_TENTHS))
                    selected_gain = gains[i];
        } else {
            int supported = 0;
            selected_gain = app->options.gain_tenths;
            for (int i = 0; i < gain_count; i++)
                if (gains[i] == selected_gain)
                    supported = 1;
            if (!supported) {
                fprintf(stderr, "Requested gain %.1f dB is not supported.\n",
                        selected_gain / 10.0);
                print_supported_gains(&app->device);
                goto done;
            }
        }
    }

    app->applied_manual_gain = app->options.gain_kind != GAIN_REQUEST_AUTO;
    if (device_set_gain(&app->source, app->applied_manual_gain,
                        selected_gain) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR tuner gain to %.1f dB.\n",
                selected_gain / 10.0);
        goto done;
    }
    if (set_frequency_correction(&app->source, app->options.ppm) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR frequency correction to %d PPM.\n",
                app->options.ppm);
        goto done;
    }
    if (device_set_frequency_hz(&app->source, app->options.frequency) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR center frequency to %u Hz.\n",
                app->options.frequency);
        goto done;
    }
    if (device_set_sample_rate_hz(&app->source, app->options.sample_rate) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR sample rate to %u S/s.\n",
                app->options.sample_rate);
        goto done;
    }
    if (device_flush(&app->source) < 0) {
        fprintf(stderr, "Failed to reset the RTL-SDR receiver buffer.\n");
        goto done;
    }

    reported_frequency = source_frequency(app);
    if (reported_frequency == 0) {
        fprintf(stderr, "Failed to read back the RTL-SDR center frequency.\n");
        goto done;
    }
    reported_rate = source_sample_rate(app);
    if (reported_rate == 0) {
        fprintf(stderr, "Failed to read back the RTL-SDR sample rate.\n");
        goto done;
    }
    if (reported_rate != app->options.sample_rate) {
        fprintf(stderr, "Sample-rate mismatch: requested %u S/s, reported %u S/s.\n",
                app->options.sample_rate, reported_rate);
        goto done;
    }
    uint32_t frequency_difference = reported_frequency > app->options.frequency
                                        ? reported_frequency - app->options.frequency
                                        : app->options.frequency - reported_frequency;
    if (frequency_difference > 1000U) {
        fprintf(stderr, "Frequency mismatch: requested %u Hz, reported %u Hz.\n",
                app->options.frequency, reported_frequency);
        goto done;
    }

    if (app->applied_manual_gain) {
        int reported_gain = source_gain(app);
        if (selected_gain != 0 && reported_gain != selected_gain) {
            fprintf(stderr,
                    "Gain mismatch: requested %.1f dB, reported %.1f dB.\n",
                    selected_gain / 10.0, reported_gain / 10.0);
            goto done;
        }
        app->applied_gain_tenths = selected_gain == 0 ? 0 : reported_gain;
    }

    app->applied.frequency_hz = reported_frequency;
    app->applied.sample_rate_hz = reported_rate;
    app->applied.ppm = source_ppm(app);
    const char *device_name =
        device_backend_rtlsdr_name(app->options.device_index);
    snprintf(app->source_label, sizeof(app->source_label), "RTL-SDR: %s",
             device_name ? device_name : "receiver 0");
    snprintf(app->tuner_label, sizeof(app->tuner_label), "%s",
             device_backend_rtlsdr_tuner(&app->source));
    /* What this receiver is, in the terms the numbers need: an 8-bit
       container at 127.5 full scale, the tuner's reach, and the gain list it
       just reported (device_profile.h). Everything that would otherwise
       assume eight bits reads it from here. */
    /* The profile is already filled in -- the backend did it at open, which
       is the only place that knows the container and the gain list. It used to
       be rebuilt here from a copy of that list, which after ticket 06 would
       have meant reading the struct being overwritten. */
    result = 0;

done:
    if (result != 0)
        device_close(&app->source);
    return result;
}

int open_capture(struct app *app) {
    off_t size;

    app->capture = fopen(app->options.file_path, "rb");
    if (!app->capture) {
        fprintf(stderr, "Cannot open %s: %s\n", app->options.file_path,
                strerror(errno));
        return -1;
    }
    if (fseeko(app->capture, 0, SEEK_END) != 0 ||
        (size = ftello(app->capture)) < 0 ||
        fseeko(app->capture, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Cannot inspect %s: %s\n", app->options.file_path,
                strerror(errno));
        return -1;
    }
    app->applied.frequency_hz = app->options.frequency;
    app->applied.sample_rate_hz = app->options.sample_rate;
    app->applied.ppm = app->options.ppm;
    snprintf(app->source_label, sizeof(app->source_label), "capture: %s",
             app->options.file_path);

    /*
     * A capture is at one place on the band and cannot be moved, which is what
     * the retune paths already refuse. What its bytes *mean* comes from its
     * own sidecar: a capture that says nothing is the house 8-bit convention,
     * which is what every capture was until build/testfiles16/ existed.
     *
     * This has to happen before the length is rounded below, because how many
     * bytes make a whole I/Q pair is exactly what the sidecar settles.
     */
    struct capture_sidecar sidecar;
    if (capture_sidecar_read(app->options.file_path, &sidecar) < 0) {
        fprintf(stderr,
                "Capture %s names a sample format but no full scale; it "
                "cannot be read in dBFS.\n",
                app->options.file_path);
        return -1;
    }
    app->device = device_profile_capture(
        app->options.file_path, sidecar.format, sidecar.full_scale,
        (double)app->applied.frequency_hz, app->applied.sample_rate_hz);
    unsigned width = app->device.bytes_per_pair;
    if (width == 0) {
        fprintf(stderr, "Capture %s: unsupported sample format.\n",
                app->options.file_path);
        return -1;
    }

    if ((uint64_t)size < width) {
        fprintf(stderr, "Capture %s has no complete I/Q pair.\n",
                app->options.file_path);
        return -1;
    }
    if ((uint64_t)size % width != 0)
        fprintf(stderr,
                "Warning: ignoring %llu unmatched trailing byte(s) in %s.\n",
                (unsigned long long)((uint64_t)size % width),
                app->options.file_path);
    app->acq.capture_bytes = (uint64_t)size - ((uint64_t)size % width);

    if (sidecar.format_stated && sidecar.format != SAMPLE_FORMAT_U8)
        fprintf(stderr, "Capture %s: %u bytes a pair, full scale %.1f\n",
                app->options.file_path, width,
                (double)app->device.full_scale);
    return 0;
}

/* Append one acquired block to an in-progress recording.
 *
 * This runs on the acquisition thread, not the renderer, so the capture holds
 * every block the receiver delivered. The display's latest-block slot
 * deliberately drops blocks the renderer cannot keep up with (ADR-0002), which
 * is right for a display and wrong for a capture: a recording driven off the
 * consumed block loses samples silently, leaving a spliced file that still
 * looks well-formed. A test vector that lies about its own timeline is worse
 * than no test vector.
 */
















/*
 * What size the screen wants the spectrum measured at.
 *
 * The Scope's resolution stepper only applies while the Scope owns the
 * spectrum -- the survey, both band scans and the calibration overlay read
 * the same array and their floors were chosen against 977 Hz bins
 * (CLAUDE.md). That is a question about presentation, so it is asked here,
 * in the layer that has a screen, and the answer is handed to
 * `process_block()` rather than looked up inside it.
 *
 * Asked every block rather than remembered across a screen change, which is
 * the distinction `input_route.h` explains.
 */
/*
 * Which screen is up, as the flags `input_route.h` reads -- and it is here
 * rather than beside the drawing because `scope_requested_fft_size()` asks
 * it every block, on every path, window or not. `view_input_now()` reads
 * `struct app`'s fields and calls nothing; neither touches raylib.
 */
/*
 * What each view is doing, read out of `struct app` and nowhere decided.
 *
 * Every field here is a copy. What any of them *means* -- whether a focus
 * counts as typing, which lists suppress Escape, what the whole of it makes
 * of the routing -- is view_input.h's, where a check with no window can reach
 * it. This function used to compose thirteen fields out of predicates living
 * in raylib-linked files, so neither they nor the composition was reachable
 * and a typing surface left out of one of them went silent rather than wrong
 * (`.scratch/testability/issues/09-*`).
 */
struct view_input view_input_now(const struct app *app) {
    struct view_input v;

    memset(&v, 0, sizeof(v));
    v.help_open = app->help.open;
    v.startup_open = app->startup.open;
    v.settings_open = app->set.open;
    v.calibration_open = app->cal.open;
    v.scan_open = app->bandscan.open;

    v.tab = app->tab;
    v.view = (int)app->view;
    v.decode = (int)app->decode;

    /*
     * What each view holds, in the view's own spelling -- the raw field, not
     * a predicate over it, because deciding what a focus *means* is
     * view_input.h's and that is the whole point of the split.
     *
     * survey_editing() and srd_editing() are gone: they existed to answer
     * this one question, this is where it was asked, and a predicate with no
     * caller is the deletion test answering itself. fm_editing() survives
     * because the FM view asks it of itself.
     */
    v.survey_focused_field = app->survey.focus;
    v.scope_focused_field = app->sv.field_focus;
    v.fm_typing = app->fm.typing;
    v.srd_typing = app->srd.typing;
    v.srd_freq_typing = app->srd.freq_typing;

    v.survey_site_menu_open = app->survey.site_menu_open;
    v.survey_antenna_menu_open = app->survey.antenna_menu_open;
    v.survey_band_menu_open = app->survey.band_menu_open;
    v.startup_site_menu_open = app->startup.site_menu_open;
    v.startup_antenna_menu_open = app->startup.antenna_menu_open;
    v.waterfall_menu_open = app->sv.waterfall_menu_open;
    v.waterfall_report_open = app->sv.waterfall_report_open;

    v.scope_zoomed = app->tab == TAB_SCOPE &&
                     app->view == VIEW_SPECTRUM &&
                     freq_window_zoomed(&app->sv.window.freq);
    return v;
}

/* The flags the precedence chain in input_route.h reads, and nothing else. */
struct input_state input_state_now(const struct app *app) {
    struct view_input v = view_input_now(app);

    return view_input_state(&v);
}

int scope_requested_fft_size(const struct app *app) {
    struct input_state screen = input_state_now(app);

    return input_scope_owns_spectrum(&screen) &&
           sdr_dsp_fft_size_valid(app->sv.fft_size)
               ? app->sv.fft_size : SDR_DSP_FFT_SIZE;
}

int process_block(struct app *app, double now, int fft_size) {
    struct signal_frame_input in;
    int geometry_changed = 0, produced;

    /*
     * The frame does the measuring; this decides what a changed geometry
     * means on screen.
     *
     * The transform size is now a *parameter*. It was worked out here from
     * `input_scope_owns_spectrum()`, which made this shared per-block step
     * ask what was on screen -- on `headless` and `server` runs too, where
     * there is no screen to ask. The caller decides and this obeys, falling
     * back to the default for a size it cannot use (layer-boundaries ticket
     * 02, item 2). `scope_requested_fft_size()` below is where the screen is
     * still asked, in the layer allowed to.
     */
    memset(&in, 0, sizeof(in));
    in.profile = &app->device;
    in.bytes = app->acq.raw;
    in.byte_count = app->acq.raw_len;
    in.remove_dc = app->remove_dc;
    in.now = now;
    in.fft_size = sdr_dsp_fft_size_valid(fft_size) ? fft_size
                                                   : SDR_DSP_FFT_SIZE;

    produced = signal_frame_process(&app->frame, &in, &geometry_changed);
    /*
     * The magnitude chart's reduction is *not* done here any more. Its
     * output -- `sv.magnitude_peaks` and `magnitude_bin_count` -- is read by
     * exactly one function, `draw_magnitude()`, and the reduction is to the
     * plot's pixel width, so it is drawing preparation. Computing it in the
     * step `headless` and `server` share meant reducing every block to a
     * capacity of 1 that nothing read, and it was the last thing here
     * reaching into the window (layer-boundaries ticket 02).
     *
     * `run_gui()` does it now, gated on the same block arriving.
     */
    /*
     * A different number of bins is a different chart. The frame threw its own
     * peak hold away; the waterfall's rows are the Scope's and are thrown away
     * here, because the frame is not allowed to reach into a view to do it.
     */
    if (geometry_changed)
        app->sv.waterfall_rows = 0;
    return produced;
}
















int install_signal_handlers(struct app *app) {
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &app->old_sigint) != 0) {
        fprintf(stderr, "Cannot install SIGINT handler: %s\n", strerror(errno));
        return -1;
    }
    if (sigaction(SIGTERM, &action, &app->old_sigterm) != 0) {
        fprintf(stderr, "Cannot install SIGTERM handler: %s\n", strerror(errno));
        sigaction(SIGINT, &app->old_sigint, NULL);
        return -1;
    }
    app->signals_ready = 1;
    return 0;
}

int worker_is_reading(struct app *app, int *done) {
    int reading;

    pthread_mutex_lock(&app->acq.latest.mutex);
    reading = app->acq.latest.worker_reading;
    *done = app->acq.latest.worker_done;
    pthread_mutex_unlock(&app->acq.latest.mutex);
    return reading;
}

int stop_acquisition(struct app *app) {
    request_worker_stop(&app->acq);
    if (!app->acq.worker_started)
        return 0;

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
            if (cancel_result != 0 && !done) {
                snprintf(app->receiver_error, sizeof(app->receiver_error),
                         "Could not stop receiver acquisition (%d)",
                         cancel_result);
                return -1;
            }
        }
    }

    int join_result = pthread_join(app->acq.worker, NULL);
    if (join_result != 0) {
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Could not join acquisition worker: %s",
                 strerror(join_result));
        return -1;
    }
    app->acq.worker_started = 0;
    return 0;
}

int start_acquisition(struct app *app) {
    /* One worker at a time, and it is worth refusing rather than trusting the
       callers: two workers reading the same receiver deliver no blocks at all
       and deadlock the shutdown join, which is a great deal harder to read
       than an error here. stop_acquisition() clears the flag, so the ordinary
       stop-then-start path is unaffected. */
    if (app->acq.worker_started) {
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Acquisition is already running");
        return -1;
    }
    pthread_mutex_lock(&app->acq.latest.mutex);
    app->acq.latest.stop = 0;
    app->acq.latest.ready = 0;
    app->acq.latest.worker_done = 0;
    app->acq.latest.worker_failed = 0;
    app->acq.latest.worker_reading = 0;
    app->acq.latest.worker_error[0] = '\0';
    pthread_mutex_unlock(&app->acq.latest.mutex);

    sigset_t worker_signals;
    sigset_t original_mask;
    sigemptyset(&worker_signals);
    sigaddset(&worker_signals, SIGINT);
    sigaddset(&worker_signals, SIGTERM);
    int mask_result = pthread_sigmask(SIG_BLOCK, &worker_signals,
                                      &original_mask);
    if (mask_result != 0) {
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Cannot block worker signals: %s", strerror(mask_result));
        return -1;
    }
    if (acquisition_attach_source(&app->acq, &app->source, app->capture,
                                  app->applied.sample_rate_hz,
                                  app->device.bytes_per_pair,
                                  app->options.file_path,
                                  !app->options.play_once) < 0) {
        /* Said rather than left blank: this was the one path here that
           returned -1 with no message, so the headless "Cannot start
           acquisition" fell through to "unknown" -- or to whatever an
           unrelated failure had left in the buffer. */
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Could not attach the source to the acquisition worker");
        return -1;
    }
    iq_ring_configure(&app->acq.ring, app->applied.sample_rate_hz,
                      app->device.format, app->device.full_scale,
                      app->applied.frequency_hz, app->applied_gain_tenths,
                      app->applied_manual_gain, app->applied.ppm,
                      app->source_label, app->tuner_label);
    int thread_result = pthread_create(
        &app->acq.worker, NULL,
        app->receiver_mode ? receiver_worker : file_worker, &app->acq);
    if (thread_result == 0)
        app->acq.worker_started = 1;
    int restore_result = pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
    if (restore_result != 0) {
        request_worker_stop(&app->acq);
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Cannot restore signal mask: %s", strerror(restore_result));
        return -1;
    }
    if (thread_result != 0) {
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Cannot start acquisition worker: %s",
                 strerror(thread_result));
        return -1;
    }
    return 0;
}



/*
 * Retune, and optionally change the sample rate with it.
 *
 * The rate is here rather than in a second function because the two share
 * every line of the rollback: both need the worker stopped, both can be
 * refused by the receiver, and a failure has to put back whichever of them
 * had already changed. Only the LTE view passes a different rate -- see
 * ADR-0014 -- and retune_receiver() below is this with the rate left alone.
 */
/*
 * The runtime, over this application's state.
 *
 * It borrows rather than owns: `app->applied` is the one owner of what the
 * receiver is doing, and the worker's lifecycle is still here with the
 * thread, the signal mask and the choice of worker function. What the runtime
 * owns is the *sequence* -- stop, apply, flush, read back, restart, and the
 * rollback at every step -- which is the half no check could reach while it
 * lived in this file taking `struct app`. `check-receiver-runtime` drives all
 * of it against a fake device and a fake worker.
 */
static int runtime_stop(void *ctx) {
    return stop_acquisition((struct app *)ctx);
}

static int runtime_start(void *ctx) {
    return start_acquisition((struct app *)ctx);
}

struct receiver_runtime runtime_over(struct app *app) {
    struct receiver_runtime rt;

    memset(&rt, 0, sizeof(rt));
    rt.source = &app->source;
    rt.applied = &app->applied;
    rt.error = app->receiver_error;
    rt.error_size = sizeof(app->receiver_error);
    rt.live = app->receiver_mode;
    rt.life.stop = runtime_stop;
    rt.life.start = runtime_start;
    rt.life.ctx = app;
    return rt;
}

/*
 * What the retune actually did.
 *
 * The request is logged before the attempt, deliberately -- a retune that
 * fails is exactly the one worth having a record of -- and for a long time
 * that was the whole of it, so the log showed a request and a reader could
 * not tell a tuning that took from one that was refused and rolled back. The
 * comment promised something the code did not do. This is the other half, and
 * on a failure it quotes `receiver_error`, which by then holds the reason.
 */
static void tune_result_logged(const struct app *app, int result) {
    if (!debug_log_active())
        return;
    if (result == 0)
        debug_log_write("tune", "took, now %.6f MHz, %.3f MS/s, %+d ppm, "
                        "generation %u",
                        app->applied.frequency_hz / 1e6,
                        app->applied.sample_rate_hz / 1e6, app->applied.ppm,
                        app->applied.generation);
    else
        debug_log_write("tune", "refused: %.140s",
                        app->receiver_error[0] ? app->receiver_error
                                               : "no reason given");
}

int retune_receiver_at_rate(struct app *app, uint32_t frequency,
                            uint32_t sample_rate, int ppm) {
    struct receiver_runtime rt = runtime_over(app);
    int result;

    debug_log_write("tune", "%.6f MHz, %+d ppm, %.3f MS/s (from %.6f MHz, "
                    "%.3f MS/s)", frequency / 1e6, ppm, sample_rate / 1e6,
                    app->applied.frequency_hz / 1e6,
                    app->applied.sample_rate_hz / 1e6);
    result = receiver_runtime_tune_at_rate(&rt, frequency, sample_rate, ppm);
    tune_result_logged(app, result);
    if (result == 0) {
        iq_ring_configure(&app->acq.ring, app->applied.sample_rate_hz,
                          app->device.format, app->device.full_scale,
                          app->applied.frequency_hz, app->applied_gain_tenths,
                          app->applied_manual_gain, app->applied.ppm,
                          app->source_label, app->tuner_label);
    }
    if (app->receiver_mode)
        signal_frame_invalidate(&app->frame);
    return result;
}

/*
 * Borrowing the receiver, and giving it back in the order it was taken.
 *
 * The rule and the reason are in receiver_lease.h. These are the half that
 * touches hardware: they call the two retune functions below and nothing
 * else, so rollback behaviour and spectrum invalidation stay exactly where
 * they were.
 *
 * File playback is not a second tuning adapter. A capture holds one tuning,
 * retune_receiver() already refuses to move it, and every one of these
 * reports success without recording anything -- so a view can call them
 * unconditionally instead of wrapping each in `if (app->receiver_mode)`,
 * which is how three of the nine restore sites came to disagree about when
 * they applied.
 */
int receiver_borrow(struct app *app, struct receiver_lease_token *token) {
    struct receiver_tuning here;

    if (!app->receiver_mode)
        return 0;
    here.center_hz = app->applied.frequency_hz;
    here.sample_rate_hz = app->applied.sample_rate_hz;
    if (receiver_lease_acquire(&app->lease, here, token) < 0) {
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Too many screens are borrowing the receiver at once");
        return -1;
    }
    return 0;
}

int receiver_borrow_at(struct app *app, struct receiver_lease_token *token,
                       uint32_t frequency, uint32_t sample_rate) {
    int moved;

    if (!app->receiver_mode)
        return retune_receiver(app, frequency, app->applied.ppm);
    if (receiver_borrow(app, token) < 0)
        return -1;
    /* The rate is only worth the more expensive path when it actually
       differs; retune_receiver_at_rate() says the same and would delegate
       anyway, but saying it here keeps the two callers legible. */
    if (sample_rate != 0 && sample_rate != app->applied.sample_rate_hz)
        moved = retune_receiver_at_rate(app, frequency, sample_rate,
                                        app->applied.ppm);
    else
        moved = retune_receiver(app, frequency, app->applied.ppm);
    if (moved < 0) {
        /* The retune already put the hardware back, so the snapshot describes
           a borrowing that never happened. */
        receiver_lease_cancel(&app->lease, token);
        return -1;
    }
    return 0;
}

int receiver_restore_held(struct app *app,
                          const struct receiver_lease_token *token) {
    struct receiver_tuning back;

    if (!app->receiver_mode)
        return 0;
    if (receiver_lease_begin_return(&app->lease, token, &back) < 0) {
        debug_log_write("lease", "restore out of order, ignored");
        return -1;
    }
    if (back.sample_rate_hz != app->applied.sample_rate_hz)
        return retune_receiver_at_rate(app, back.center_hz,
                                       back.sample_rate_hz, app->applied.ppm);
    return retune_receiver(app, back.center_hz, app->applied.ppm);
}

int receiver_return(struct app *app, struct receiver_lease_token *token) {
    struct receiver_tuning back;
    int restored;

    if (!app->receiver_mode)
        return 0;
    if (!receiver_lease_token_active(token))
        return 0;               /* never borrowed, or already given back */
    if (receiver_lease_begin_return(&app->lease, token, &back) < 0) {
        /* An owner returning out of turn would put back a frequency belonging
           to somebody else. Refuse, change nothing, and say so: this is the
           failure the lease exists to make visible rather than silent. */
        debug_log_write("lease", "return out of order, refused");
        return -1;
    }
    /*
     * The current PPM, not the one in force when the tuning was borrowed. A
     * calibration applied while the receiver was lent out is deliberate
     * persistent state and must survive the return -- which is why the
     * snapshot has no third field to get this wrong with.
     */
    if (back.sample_rate_hz != app->applied.sample_rate_hz)
        restored = retune_receiver_at_rate(app, back.center_hz,
                                           back.sample_rate_hz,
                                           app->applied.ppm);
    else
        restored = retune_receiver(app, back.center_hz, app->applied.ppm);
    if (restored < 0)
        return -1;              /* the token stays live, and retryable */
    receiver_lease_finish_return(&app->lease, token);
    return 0;
}

int receiver_commit(struct app *app, struct receiver_lease_token *token) {
    if (!app->receiver_mode)
        return 0;
    if (!receiver_lease_token_active(token))
        return 0;
    if (receiver_lease_commit(&app->lease, token) < 0) {
        debug_log_write("lease", "commit out of order, refused");
        return -1;
    }
    return 0;
}

int retune_receiver(struct app *app, uint32_t frequency, int ppm) {
    struct receiver_runtime rt = runtime_over(app);
    int result;

    /* Logged before the attempt, not after: a retune that fails is exactly
       the one worth having a record of. */
    debug_log_write("tune", "%.6f MHz, %+d ppm (from %.6f MHz)",
                    frequency / 1e6, ppm, app->applied.frequency_hz / 1e6);
    result = receiver_runtime_tune(&rt, frequency, ppm);
    tune_result_logged(app, result);
    /*
     * Every spectrum was measured across a different span and is now
     * meaningless, whether the move took or was rolled back. The waterfall's
     * history is in the same position, but rebuilding it is drawing and this
     * runs on paths with no window: `view_scope_resize_if_needed()` notices.
     */
    if (app->receiver_mode)
        signal_frame_invalidate(&app->frame);
    return result;
}



int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

/* Switch tabs. The GSM decode view retunes the receiver, so leaving the Decode
   tab (while on the GSM view) restores tuning. Calibration is a separate global
   overlay (a button), not a tab, so tabs do not touch it. */
void set_tab(struct app *app, int new_tab, double now) {
    if (new_tab == (int)app->tab)
        return;
    if (app->tab == TAB_DECODE && app->decode == DECODE_GSM)
        leave_gsm(app);
    if (app->tab == TAB_DECODE && app->decode == DECODE_LTE)
        leave_lte(app);
    if (app->tab == TAB_DECODE && app->decode == DECODE_SRD)
        leave_srd(app);
    if (app->tab == TAB_DECODE && app->decode == DECODE_ADSB)
        leave_adsb(app);
    /* Leaving the survey puts the receiver back where it was before a sweep
       walked it away. */
    if (app->tab == TAB_SURVEY)
        view_survey_leave(app);
    app->set.open = 0;
    app->tab = new_tab;
    if (new_tab == TAB_SURVEY)
        view_survey_enter(app, now);
    if (new_tab == TAB_DECODE && app->decode == DECODE_GSM)
        enter_gsm(app);
    if (new_tab == TAB_DECODE && app->decode == DECODE_FM)
        enter_fm(app, now);
    if (new_tab == TAB_DECODE && app->decode == DECODE_LTE)
        enter_lte(app);
    if (new_tab == TAB_DECODE && app->decode == DECODE_SRD)
        enter_srd(app);
    if (new_tab == TAB_DECODE && app->decode == DECODE_ADSB)
        enter_adsb(app);
}

/*
 * Switch the Decode sub-view. Entering and leaving GSM retunes the receiver,
 * so it happens only when that view is the one on screen: called from the
 * Scope tab -- which is how the startup flags and the survey's handoff both
 * reach it -- this records the choice and leaves the tuning to the set_tab
 * that follows. Doing it here as well would enter the GSM view twice and
 * retune twice on the way in.
 */
void set_decode(struct app *app, int kind, double now) {
    int showing = app->tab == TAB_DECODE;
    if (kind == (int)app->decode)
        return;
    if (showing && app->decode == DECODE_GSM)
        leave_gsm(app);
    if (showing && app->decode == DECODE_LTE)
        leave_lte(app);
    if (showing && app->decode == DECODE_SRD)
        leave_srd(app);
    if (showing && app->decode == DECODE_ADSB)
        leave_adsb(app);
    app->decode = kind;
    if (showing && kind == DECODE_GSM)
        enter_gsm(app);
    if (showing && kind == DECODE_LTE)
        enter_lte(app);
    if (showing && kind == DECODE_FM)
        enter_fm(app, now);
    if (showing && kind == DECODE_SRD)
        enter_srd(app);
    if (showing && kind == DECODE_ADSB)
        enter_adsb(app);
}

/* Start a timestamped 2 s capture in captures/, with a sidecar describing the
   tuning it was taken at. The tuning goes to acquisition only so it can be
   written into that sidecar; the write itself happens on the acquisition
   thread, upstream of the display's lossy block slot, so a capture never
   inherits the frames the renderer dropped. Files are timestamped, so
   re-recording never overwrites one. */
int start_capture_record(struct app *app, const char *basename,
                         const char *technology, int arfcn,
                         double carrier_offset_hz, double seconds) {
    mkdir("captures", 0755); /* ignore EEXIST */
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    char path[256];
    snprintf(path, sizeof(path), "captures/%s_%s.bin", basename, stamp);

    struct acquisition_record_request req = {
        app->applied.frequency_hz, app->applied.sample_rate_hz,
        app->applied_gain_tenths, app->applied_manual_gain, app->applied.ppm,
        arfcn, carrier_offset_hz, technology,
        app->source_label, app->tuner_label, seconds,
        app->device.format, app->device.full_scale
    };
    if (acquisition_start_recording(&app->acq, path, &req) < 0) {
        fprintf(stderr, "Cannot start recording to %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    fprintf(stderr, "Recording %.1f s to %s\n", seconds, path);
    return 0;
}

double monotonic_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}
