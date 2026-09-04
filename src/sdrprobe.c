#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <raylib.h>
#include <rlgl.h>        /* rlDrawRenderBatchActive, for --screenshot */
#include <rtl-sdr.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "sdr_dsp.h"
#include "gsm_dsp.h"
#include "adsb_dsp.h"
#include "options.h"
#include "gsm_layout.h"
#include "app.h"
#include "chrome_layout.h"
#include "sdrgui.h"
#include "view.h"
#include "debug_log.h"

/* input_route.h mirrors this so it can stay standalone; if that enum is
   reordered this stops the build rather than misrouting a key. */
typedef char input_route_adsb_matches[
    (DECODE_KIND_ADSB == (int)DECODE_ADSB) ? 1 : -1];
#include "raygui.h"


static volatile sig_atomic_t signal_stop_requested = 0;

/* The one global the program has, read through a function so it stays one:
   Ctrl-C during a long headless sweep should stop it, and the sweep lives in
   another file. */
int stop_requested(void) {
    return signal_stop_requested != 0;
}

void set_tab(struct app *app, int new_tab);
void set_decode(struct app *app, int kind);

static void on_signal(int signal_number) {
    (void)signal_number;
    signal_stop_requested = 1;
}



int set_frequency_correction(rtlsdr_dev_t *dev, int ppm) {
    if (rtlsdr_get_freq_correction(dev) == ppm)
        return 0;
    return rtlsdr_set_freq_correction(dev, ppm);
}





static void print_supported_gains(const int *gains, int count) {
    fprintf(stderr, "Supported gains (dB):");
    for (int i = 0; i < count; i++)
        fprintf(stderr, "%s%.1f", i ? ", " : " ", gains[i] / 10.0);
    fputc('\n', stderr);
}

/* The tuner chip, not the USB bridge: it sets the achievable gains and the
   oscillator whose error the PPM correction compensates, so a capture is worth
   labelling with it, and --list-devices is worth printing it. */
static const char *tuner_name(rtlsdr_dev_t *dev) {
    switch (rtlsdr_get_tuner_type(dev)) {
    case RTLSDR_TUNER_E4000:  return "E4000";
    case RTLSDR_TUNER_FC0012: return "FC0012";
    case RTLSDR_TUNER_FC0013: return "FC0013";
    case RTLSDR_TUNER_FC2580: return "FC2580";
    case RTLSDR_TUNER_R820T:  return "R820T";
    case RTLSDR_TUNER_R828D:  return "R828D";
    default: return "unknown";
    }
}

static int configure_receiver(struct app *app) {
    int *gains = NULL;
    int gain_count = 0;
    int selected_gain = 0;
    int result = -1;
    uint32_t reported_frequency;
    uint32_t reported_rate;

    if (rtlsdr_get_device_count() == 0) {
        fprintf(stderr, "No supported RTLSDR devices found.\n");
        return -1;
    }
    if (rtlsdr_open(&app->dev, (uint32_t)app->options.device_index) < 0) {
        fprintf(stderr, "Failed to open RTL-SDR receiver index %d. "
                        "Try --list-devices.\n",
                app->options.device_index);
        return -1;
    }

    gain_count = rtlsdr_get_tuner_gains(app->dev, NULL);
    if (gain_count <= 0) {
        fprintf(stderr, "Failed to enumerate supported tuner gains.\n");
        goto done;
    }
    gains = malloc((size_t)gain_count * sizeof(*gains));
    if (!gains) {
        fprintf(stderr, "Cannot allocate tuner gain list.\n");
        goto done;
    }
    int returned = rtlsdr_get_tuner_gains(app->dev, gains);
    if (returned <= 0 || returned > gain_count) {
        fprintf(stderr, "Failed to read supported tuner gains.\n");
        goto done;
    }
    gain_count = returned;

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
                print_supported_gains(gains, gain_count);
                goto done;
            }
        }
    }

    app->applied_manual_gain = app->options.gain_kind != GAIN_REQUEST_AUTO;
    if (rtlsdr_set_tuner_gain_mode(app->dev, app->applied_manual_gain) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR tuner gain mode.\n");
        goto done;
    }
    if (app->applied_manual_gain) {
        if (rtlsdr_set_tuner_gain(app->dev, selected_gain) < 0) {
            fprintf(stderr, "Failed to set RTL-SDR tuner gain to %.1f dB.\n",
                    selected_gain / 10.0);
            goto done;
        }
    }
    if (set_frequency_correction(app->dev, app->options.ppm) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR frequency correction to %d PPM.\n",
                app->options.ppm);
        goto done;
    }
    if (rtlsdr_set_center_freq(app->dev, app->options.frequency) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR center frequency to %u Hz.\n",
                app->options.frequency);
        goto done;
    }
    if (rtlsdr_set_sample_rate(app->dev, app->options.sample_rate) < 0) {
        fprintf(stderr, "Failed to set RTL-SDR sample rate to %u S/s.\n",
                app->options.sample_rate);
        goto done;
    }
    if (rtlsdr_reset_buffer(app->dev) < 0) {
        fprintf(stderr, "Failed to reset the RTL-SDR receiver buffer.\n");
        goto done;
    }

    reported_frequency = rtlsdr_get_center_freq(app->dev);
    if (reported_frequency == 0) {
        fprintf(stderr, "Failed to read back the RTL-SDR center frequency.\n");
        goto done;
    }
    reported_rate = rtlsdr_get_sample_rate(app->dev);
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
        int reported_gain = rtlsdr_get_tuner_gain(app->dev);
        if (selected_gain != 0 && reported_gain != selected_gain) {
            fprintf(stderr,
                    "Gain mismatch: requested %.1f dB, reported %.1f dB.\n",
                    selected_gain / 10.0, reported_gain / 10.0);
            goto done;
        }
        app->applied_gain_tenths = selected_gain == 0 ? 0 : reported_gain;
    }

    app->applied_frequency = reported_frequency;
    app->applied_sample_rate = reported_rate;
    app->applied_ppm = rtlsdr_get_freq_correction(app->dev);
    app->supported_gains = gains;
    app->supported_gain_count = gain_count;
    gains = NULL;
    const char *device_name = rtlsdr_get_device_name(0);
    snprintf(app->source_label, sizeof(app->source_label), "RTL-SDR: %s",
             device_name ? device_name : "receiver 0");
    snprintf(app->tuner_label, sizeof(app->tuner_label), "%s",
             tuner_name(app->dev));
    result = 0;

done:
    free(gains);
    return result;
}

static int open_capture(struct app *app) {
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
    if (size < 2) {
        fprintf(stderr, "Capture %s has no complete I/Q pair.\n",
                app->options.file_path);
        return -1;
    }
    if ((size & 1) != 0)
        fprintf(stderr, "Warning: ignoring unmatched trailing byte in %s.\n",
                app->options.file_path);
    app->acq.capture_bytes = (uint64_t)size & ~UINT64_C(1);
    app->applied_frequency = app->options.frequency;
    app->applied_sample_rate = app->options.sample_rate;
    app->applied_ppm = app->options.ppm;
    snprintf(app->source_label, sizeof(app->source_label), "capture: %s",
             app->options.file_path);
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
















int process_block(struct app *app, double now) {
    double sum = 0.0;

    app->pair_count = sdr_dsp_convert_iq(
        app->acq.raw, app->acq.raw_len, app->i_samples, app->q_samples,
        app->magnitudes, SAMPLE_BLOCK_PAIRS);
    if (app->pair_count == 0)
        return 0;
    app->have_samples = 1;
    app->magnitude_min = app->magnitudes[0];
    app->magnitude_max = app->magnitudes[0];
    for (size_t i = 0; i < app->pair_count; i++) {
        float magnitude = app->magnitudes[i];
        if (magnitude < app->magnitude_min)
            app->magnitude_min = magnitude;
        if (magnitude > app->magnitude_max)
            app->magnitude_max = magnitude;
        sum += magnitude;
    }
    app->magnitude_mean = (float)(sum / (double)app->pair_count);
    app->signal_stats_ready = sdr_dsp_signal_stats(
        app->i_samples, app->q_samples, app->magnitudes, app->pair_count,
        app->magnitude_sorted, &app->signal_stats);
    recompute_magnitude_bins(app);

    const float *spectrum_i = app->i_samples;
    const float *spectrum_q = app->q_samples;
    if (app->remove_dc) {
        memcpy(app->spectrum_i, app->i_samples,
               app->pair_count * sizeof(*app->spectrum_i));
        memcpy(app->spectrum_q, app->q_samples,
               app->pair_count * sizeof(*app->spectrum_q));
        sdr_dsp_remove_dc(app->spectrum_i, app->spectrum_q,
                             app->pair_count);
        spectrum_i = app->spectrum_i;
        spectrum_q = app->spectrum_q;
    }
    int windows = sdr_dsp_spectrum(
        &app->dsp, spectrum_i, spectrum_q, app->pair_count,
        SDR_DSP_FFT_SIZE, app->spectrum_average, app->spectrum_candidate);
    if (windows > 0) {
        if (!app->spectrum_peak_ready) {
            memcpy(app->spectrum_peak, app->spectrum_candidate,
                   sizeof(app->spectrum_peak));
            app->spectrum_peak_ready = 1;
        } else {
            for (int i = 0; i < SDR_DSP_FFT_SIZE; i++)
                if (app->spectrum_candidate[i] > app->spectrum_peak[i])
                    app->spectrum_peak[i] = app->spectrum_candidate[i];
        }
        app->spectrum_peak_time = now;
        app->spectrum_windows = windows;
        app->spectrum_ready = 1;
        return 1;
    }
    return 0;
}
















static int install_signal_handlers(struct app *app) {
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

static int worker_is_reading(struct app *app, int *done) {
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
                cancel_result = rtlsdr_cancel_async(app->dev);
                if (cancel_result == 0)
                    break;
                struct timespec retry = { 0, 1000000L };
                nanosleep(&retry, NULL);
                worker_is_reading(app, &done);
            }
            if (cancel_result != 0 && !done) {
                snprintf(app->settings_error, sizeof(app->settings_error),
                         "Could not stop receiver acquisition (%d)",
                         cancel_result);
                return -1;
            }
        }
    }

    int join_result = pthread_join(app->acq.worker, NULL);
    if (join_result != 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
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
        snprintf(app->settings_error, sizeof(app->settings_error),
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
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Cannot block worker signals: %s", strerror(mask_result));
        return -1;
    }
    acquisition_attach_source(&app->acq, app->dev, app->capture,
                              app->applied_sample_rate, app->options.file_path,
                              !app->options.play_once);
    int thread_result = pthread_create(
        &app->acq.worker, NULL,
        app->receiver_mode ? receiver_worker : file_worker, &app->acq);
    if (thread_result == 0)
        app->acq.worker_started = 1;
    int restore_result = pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
    if (restore_result != 0) {
        request_worker_stop(&app->acq);
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Cannot restore signal mask: %s", strerror(restore_result));
        return -1;
    }
    if (thread_result != 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
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
int retune_receiver_at_rate(struct app *app, uint32_t frequency,
                            uint32_t sample_rate, int ppm) {
    uint32_t old_rate = app->applied_sample_rate;
    int changed_rate = sample_rate != old_rate;
    int result;

    if (!app->receiver_mode) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Changing the sample rate requires a live RTL-SDR receiver");
        return -1;
    }
    if (!changed_rate)
        return retune_receiver(app, frequency, ppm);

    if (stop_acquisition(app) < 0)
        return -1;
    if (rtlsdr_set_sample_rate(app->dev, sample_rate) < 0 ||
        rtlsdr_reset_buffer(app->dev) < 0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Receiver refused %.3f MS/s", sample_rate / 1e6);
        rtlsdr_set_sample_rate(app->dev, old_rate);
        rtlsdr_reset_buffer(app->dev);
        start_acquisition(app);
        return -1;
    }
    app->applied_sample_rate = rtlsdr_get_sample_rate(app->dev);
    if (app->applied_sample_rate == 0)
        app->applied_sample_rate = sample_rate;
    /* Every spectrum and waterfall row was measured across a different span
       and is now meaningless; the frame loop rebuilds them. */
    app->spectrum_ready = 0;
    app->spectrum_peak_ready = 0;

    if (start_acquisition(app) < 0) {
        rtlsdr_set_sample_rate(app->dev, old_rate);
        rtlsdr_reset_buffer(app->dev);
        app->applied_sample_rate = old_rate;
        start_acquisition(app);
        return -1;
    }
    result = retune_receiver(app, frequency, ppm);
    if (result < 0) {
        /* The tuning failed but the rate took. Put the rate back too, so a
           refusal leaves the receiver where it was found. */
        if (stop_acquisition(app) == 0) {
            rtlsdr_set_sample_rate(app->dev, old_rate);
            rtlsdr_reset_buffer(app->dev);
            app->applied_sample_rate = old_rate;
            start_acquisition(app);
        }
    }
    return result;
}

int retune_receiver(struct app *app, uint32_t frequency, int ppm) {
    /* Logged before the attempt, not after: a retune that fails is exactly
       the one worth having a record of, and the failure path returns from
       several places. */
    debug_log_write("tune", "%.6f MHz, %+d ppm (from %.6f MHz)",
                    frequency / 1e6, ppm, app->applied_frequency / 1e6);
    if (!app->receiver_mode) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Calibration requires a live RTL-SDR receiver");
        return -1;
    }
    uint32_t old_frequency = app->applied_frequency;
    int old_ppm = app->applied_ppm;
    if (stop_acquisition(app) < 0)
        return -1;
    if (set_frequency_correction(app->dev, ppm) < 0 ||
        rtlsdr_set_center_freq(app->dev, frequency) < 0 ||
        rtlsdr_reset_buffer(app->dev) < 0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Receiver rejected calibration tuning or PPM correction");
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        app->applied_frequency = old_frequency;
        app->applied_ppm = old_ppm;
        if (start_acquisition(app) < 0)
            return -1;
        return -1;
    }
    uint32_t reported = rtlsdr_get_center_freq(app->dev);
    if (reported == 0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Could not read back calibration tuning");
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        app->applied_frequency = old_frequency;
        app->applied_ppm = old_ppm;
        if (start_acquisition(app) < 0)
            return -1;
        return -1;
    }
    app->applied_frequency = reported;
    app->applied_ppm = rtlsdr_get_freq_correction(app->dev);
    app->spectrum_ready = 0;
    app->spectrum_peak_ready = 0;
    /* The waterfall's history now belongs to a frequency the receiver has
       left, but rebuilding it is drawing, and this runs on paths with no
       window: view_scope_resize_if_needed() notices and clears it. */
    if (start_acquisition(app) < 0) {
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        app->applied_frequency = old_frequency;
        app->applied_ppm = old_ppm;
        start_acquisition(app);
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Calibration acquisition failed; restored previous tuning");
        return -1;
    }
    return 0;
}



int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}












int clicked(Rectangle rectangle) {
    return CheckCollisionPointRec(GetMousePosition(), rectangle) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}


static void configure_gui_style(void) {
    GuiSetStyle(DEFAULT, TEXT_SIZE, 17);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,
                ColorToInt((Color){ 10, 18, 28, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,
                ColorToInt((Color){ 29, 43, 54, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,
                ColorToInt((Color){ 44, 62, 75, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,
                ColorToInt((Color){ 44, 62, 75, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL,
                ColorToInt((Color){ 91, 117, 132, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED,
                ColorToInt((Color){ 255, 201, 103, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED,
                ColorToInt((Color){ 255, 201, 103, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,
                ColorToInt((Color){ 235, 242, 246, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED,
                ColorToInt((Color){ 255, 255, 255, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED,
                ColorToInt((Color){ 255, 255, 255, 255 }));
    /*
     * Disabled has to read as *quieter* than normal, and raygui's stock
     * disabled is a pale fill -- which on this dark theme made a dead button
     * the loudest thing on the row. These are the normal colours dimmed
     * rather than a separate grey, so a dim control still looks like the
     * control it is.
     */
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED,
                ColorToInt((Color){ 22, 32, 40, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED,
                ColorToInt((Color){ 55, 72, 84, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED,
                ColorToInt((Color){ 92, 111, 124, 255 }));
    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
}

/* Render-only button: raygui draws it (themed); the click action is dispatched
   from the input phase via clicked(), so heavy actions never run mid-frame. */
/*
 * A button that says whether it can be pressed.
 *
 * Calibration's Back is dim at the top of its own stack, which is the only
 * honest way to draw a control that will do nothing: the alternative is a
 * button that looks live and swallows the click, and an operator who concludes
 * the screen is stuck.
 */
void draw_button_enabled(Rectangle rectangle, const char *label, int enabled) {
    if (!enabled)
        GuiSetState(STATE_DISABLED);
    GuiButton(rectangle, label);
    if (!enabled)
        GuiSetState(STATE_NORMAL);
}

void draw_button(Rectangle rectangle, const char *label, int primary) {
    if (primary) {
        GuiSetStyle(BUTTON, BASE_COLOR_NORMAL,
                    ColorToInt((Color){ 191, 111, 25, 255 }));
        GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED,
                    ColorToInt((Color){ 220, 142, 38, 255 }));
        GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL,
                    ColorToInt((Color){ 255, 201, 103, 255 }));
    }
    GuiButton(rectangle, label);
    if (primary) {
        GuiSetStyle(BUTTON, BASE_COLOR_NORMAL,
                    ColorToInt((Color){ 29, 43, 54, 255 }));
        GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED,
                    ColorToInt((Color){ 44, 62, 75, 255 }));
        GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL,
                    ColorToInt((Color){ 91, 117, 132, 255 }));
    }
}













/* --- Top-level tabs (Scope / Decode) --- */

static const char *tab_labels[TAB_COUNT] = { "Survey", "Scope",
                                             "Decode" };

static Rectangle tab_rect(int index) {
    struct chrome_layout chrome = chrome_layout_now();
    return chrome_tab_rect(&chrome, index);
}

/* A tab button with bright, prominent text and a clear active state. */
static void draw_tab(Rectangle rect, const char *label, int active) {
    Color fill = active ? (Color){ 191, 111, 25, 255 }
                        : (Color){ 27, 41, 52, 255 };
    Color border = active ? (Color){ 255, 201, 103, 255 }
                          : (Color){ 96, 124, 141, 255 };
    Color text = active ? (Color){ 255, 244, 224, 255 }
                        : (Color){ 214, 227, 236, 255 };
    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, active ? 2.0f : 1.0f, border);
    int font = 22;
    int tw = MeasureText(label, font);
    DrawText(label, (int)(rect.x + (rect.width - tw) / 2.0f),
             (int)(rect.y + (rect.height - font) / 2.0f), font, text);
}

static void draw_tab_bar(const struct app *app) {
    for (int i = 0; i < TAB_COUNT; i++)
        draw_tab(tab_rect(i), tab_labels[i], (int)app->tab == i);
}



/* Switch tabs. The GSM decode view retunes the receiver, so leaving the Decode
   tab (while on the GSM view) restores tuning. Calibration is a separate global
   overlay (a button), not a tab, so tabs do not touch it. */
void set_tab(struct app *app, int new_tab) {
    if (new_tab == (int)app->tab)
        return;
    if (app->tab == TAB_DECODE && app->decode == DECODE_GSM)
        leave_gsm(app);
    if (app->tab == TAB_DECODE && app->decode == DECODE_LTE)
        leave_lte(app);
    /* Leaving the survey puts the receiver back where it was before a sweep
       walked it away. */
    if (app->tab == TAB_SURVEY)
        view_survey_leave(app);
    app->settings_open = 0;
    app->tab = new_tab;
    if (new_tab == TAB_SURVEY)
        view_survey_enter(app);
    if (new_tab == TAB_DECODE && app->decode == DECODE_GSM)
        enter_gsm(app);
    if (new_tab == TAB_DECODE && app->decode == DECODE_FM)
        enter_fm(app);
    if (new_tab == TAB_DECODE && app->decode == DECODE_LTE)
        enter_lte(app);
}

/*
 * Switch the Decode sub-view. Entering and leaving GSM retunes the receiver,
 * so it happens only when that view is the one on screen: called from the
 * Scope tab -- which is how the startup flags and the survey's handoff both
 * reach it -- this records the choice and leaves the tuning to the set_tab
 * that follows. Doing it here as well would enter the GSM view twice and
 * retune twice on the way in.
 */
void set_decode(struct app *app, int kind) {
    int showing = app->tab == TAB_DECODE;
    if (kind == (int)app->decode)
        return;
    if (showing && app->decode == DECODE_GSM)
        leave_gsm(app);
    if (showing && app->decode == DECODE_LTE)
        leave_lte(app);
    app->decode = kind;
    if (showing && kind == DECODE_GSM)
        enter_gsm(app);
    if (showing && kind == DECODE_LTE)
        enter_lte(app);
    if (showing && kind == DECODE_FM)
        enter_fm(app);
}

/* One row of numbered mode options, the active one highlighted. */
static void draw_option_row(int active, const char **labels, int count,
                            const char *suffix) {
    struct chrome_layout chrome = chrome_layout_now();
    int x = (int)chrome.option_row_left;
    const int y = (int)chrome.option_row_y;
    for (int i = 0; i < count; i++) {
        Color color = (i == active) ? (Color){ 255, 201, 103, 255 }
                                    : (Color){ 150, 172, 188, 255 };
        DrawText(labels[i], x, y, 18, color);
        x += MeasureText(labels[i], 18) + 26;
    }
    if (suffix && suffix[0])
        DrawText(suffix, x, y, 18, (Color){ 110, 132, 150, 255 });
}

/* The uniform application header, identical on every tab: application name
   (top-left), the current tab's numbered options (below it), and the buttons
   (tabs, Settings, Calibration, health) on the right. The mode display renders
   below it. */
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
        app->applied_frequency, app->applied_sample_rate,
        app->applied_gain_tenths, app->applied_manual_gain, app->applied_ppm,
        arfcn, carrier_offset_hz, technology,
        app->source_label, app->tuner_label, seconds
    };
    if (acquisition_start_recording(&app->acq, path, &req) < 0) {
        fprintf(stderr, "Cannot start recording to %s: %s\n", path,
                strerror(errno));
        return -1;
    }
    fprintf(stderr, "Recording %.1f s to %s\n", seconds, path);
    return 0;
}

static void draw_header(const struct app *app) {
    static const char *scope_opts[4] = {
        "1 magnitude", "2 spectrum", "3 I/Q scatter", "4 waterfall"
    };
    static const char *decode_opts[4] = { "1 FM", "2 ADS-B", "3 GSM",
                                          "4 LTE" };

    DrawText("sdrprobe signal visualizer", 22, 14, 24,
             (Color){ 225, 236, 245, 255 });
    if (app->tab == TAB_SURVEY) {
        /*
         * No numbered options. The survey has one screen, and the four the
         * Scope tab numbers are not alternatives to it -- listing them here
         * offered a reader four keys that would take them somewhere else
         * entirely.
         */
        struct chrome_layout chrome = chrome_layout_now();
        DrawText("+/- zoom   Left/Right pan   Up/Down candidate   h help"
                 "   Esc quit", (int)chrome.option_row_left,
                 (int)chrome.option_row_y, 16, (Color){ 143, 167, 182, 255 });
    } else if (app->tab == TAB_SCOPE) {
        draw_option_row((int)app->view, scope_opts, 4,
                        "Up/Down scale   h help   Esc quit");
    } else {
        draw_option_row((int)app->decode, decode_opts, 4,
                        app->decode == DECODE_ADSB
                            ? "h help   Esc scope"
                            : "Up/Down scale   h help   Esc scope");
    }

    draw_tab_bar(app);
    draw_button(settings_button(), "Settings", 0);
    draw_button(calibration_button(), "Calibration", 0);
    draw_health_indicator(app);
}

/* The flags the precedence chain in input_route.h reads, and nothing else. */
static struct input_state input_state_now(const struct app *app) {
    struct input_state state;

    state.help_open = app->help.open;
    state.settings_open = app->settings_open;
    state.calibration_open = app->calibration_open;
    state.scan_open = app->scan_open;
    state.tab = app->tab;
    state.view = (int)app->view;
    state.decode = (int)app->decode;
    /* Text focus outside the settings panel: the survey's range and dwell
       fields, and the FM view's frequency. Both take digits, and a digit that
       reaches the view switcher instead of the field it was typed into is a
       screen change nobody asked for. */
    state.text_focus = survey_editing(app) || fm_editing(app);
    state.menu_open = app->survey.site_menu_open ||
                      app->survey.antenna_menu_open ||
                      app->survey.band_menu_open;
    return state;
}

/*
 * What is on screen, for the log. Read from the same fields the drawing reads
 * so the two cannot describe different screens.
 */
static struct debug_screen debug_screen_now(const struct app *app) {
    struct debug_screen s;

    memset(&s, 0, sizeof(s));
    s.tab = (int)app->tab;
    s.view = (int)app->view;
    s.decode = (int)app->decode;
    s.settings_open = app->settings_open;
    s.calibration_open = app->calibration_open;
    s.scan_open = app->scan_open;
    s.help_open = app->help.open;
    s.menu_open = app->survey.site_menu_open || app->survey.antenna_menu_open;
    s.analysis = app->adsb.analysis_mode || app->lte.analysis_mode ||
                 app->gsm_analysis_mode;
    return s;
}

/*
 * Every key the window received this frame, and where the router sent it.
 *
 * GetKeyPressed drains a queue raylib fills alongside the state IsKeyPressed
 * reads, so taking from it costs the handlers nothing. It is the only way to
 * see a key the program received and then ignored -- which is the difference
 * between "the key did not arrive" and "the key arrived and nothing was bound
 * to it", and those are two different bugs that look the same.
 */
static void debug_log_frame_keys(const struct app *app,
                                 const struct input_state *input) {
    int key;

    if (!debug_log_active())
        return;
    while ((key = GetKeyPressed()) != 0) {
        struct debug_screen screen = debug_screen_now(app);
        char where[64];

        debug_screen_describe(&screen, where, sizeof(where));
        debug_log_write("key", "%s -> %s on %s",
                        debug_key_name(key),
                        debug_target_name((int)input_route(input)), where);
    }
}

static int handle_tab_input(struct app *app) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (clicked(tab_rect(i))) {
            set_tab(app, i);
            return 1;
        }
    }
    return 0;
}

/* --- Decode tab: numbered sub-views (1 ADS-B, 2 GSM, 3 LTE) --- */

/* --- GSM analysis view (band survey: channel scan + ARFCN waterfall) --- */


/* What a scripted recording is labelled. The frequency is in the sidecar
   either way; the technology is the operator's intent, so it comes from
   --view rather than from a guess about what lives at this frequency. */
static void cli_record_labels(const struct options *options,
                              const char **basename,
                              const char **technology) {
    if (options->arfcn) {
        static char named[32];
        snprintf(named, sizeof(named), "gsm_arfcn%d", options->arfcn);
        *basename = named;
        *technology = "gsm";
    } else if (options->earfcn) {
        static char named[32];
        snprintf(named, sizeof(named), "lte_earfcn%d", options->earfcn);
        *basename = named;
        *technology = "lte";
    } else if (options->technology) {
        *basename = options->technology;
        *technology = options->technology;
    } else if (options->view == START_VIEW_GSM) {
        *basename = "gsm";
        *technology = "gsm";
    } else if (options->view == START_VIEW_ADSB) {
        *basename = "adsb";
        *technology = "adsb";
    } else if (options->view == START_VIEW_LTE) {
        *basename = "lte";
        *technology = "lte";
    } else {
        *basename = "raw";
        *technology = "raw";
    }
}

static int run_gui(struct app *app) {
    struct slot_snapshot snapshot;
    int result = 0;
    int break_requested = 0;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1100, 720, "sdrprobe signal visualizer");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create raylib window.\n");
        return -1;
    }
    app->window_ready = 1;
    SetExitKey(KEY_NULL);
    SetWindowMinSize(1000, 540);
    SetTargetFPS(60);
    configure_gui_style();
    app->plot = calculate_plot();
    if (recreate_scatter(app, app->plot) < 0)
        return -1;
    if (recreate_waterfall(app, app->plot, 1) < 0)
        return -1;

    sdr_dsp_init(&app->dsp);
    /* The survey is where a session starts: "what is out there" comes before
       "what does this one look like", and the Scope views need somebody to
       have tuned the receiver first. TAB_SURVEY is 0, so this is also what
       zero-initialising gives -- said out loud rather than relied on. */
    app->tab = TAB_SURVEY;
    app->view = VIEW_MAGNITUDE;

    sigset_t worker_signals;
    sigset_t original_mask;
    sigemptyset(&worker_signals);
    sigaddset(&worker_signals, SIGINT);
    sigaddset(&worker_signals, SIGTERM);
    int mask_result = pthread_sigmask(SIG_BLOCK, &worker_signals,
                                      &original_mask);
    if (mask_result != 0) {
        fprintf(stderr, "Cannot block worker signals: %s\n",
                strerror(mask_result));
        return -1;
    }
    acquisition_attach_source(&app->acq, app->dev, app->capture,
                              app->applied_sample_rate, app->options.file_path,
                              !app->options.play_once);
    int thread_result = pthread_create(
        &app->acq.worker, NULL,
        app->receiver_mode ? receiver_worker : file_worker, &app->acq);
    if (thread_result == 0)
        app->acq.worker_started = 1;
    int restore_result = pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
    if (restore_result != 0) {
        fprintf(stderr, "Cannot restore main-thread signal mask: %s\n",
                strerror(restore_result));
        request_worker_stop(&app->acq);
        return -1;
    }
    if (thread_result != 0) {
        fprintf(stderr, "Cannot start acquisition worker: %s\n",
                strerror(thread_result));
        return -1;
    }
    /* The starting screen is applied here, with the worker already running,
       because entering the GSM view is not a passive act: it retunes and can
       start a band scan, and retuning stops and restarts acquisition. Done
       before the worker existed, that left one worker started by the retune
       and a second started below, both reading the same device -- no blocks
       ever arrived and the shutdown join hung.

       The decode kind is set before the tab for a related reason: it defaults
       to GSM, so switching to the Decode tab first would enter the GSM view
       and immediately leave it again, retuning twice on the way to ADS-B. */
    switch (app->options.view) {
    case START_VIEW_MAGNITUDE: app->view = VIEW_MAGNITUDE;
                               set_tab(app, TAB_SCOPE); break;
    case START_VIEW_SPECTRUM:  app->view = VIEW_SPECTRUM;
                               set_tab(app, TAB_SCOPE); break;
    case START_VIEW_SCATTER:   app->view = VIEW_SCATTER;
                               set_tab(app, TAB_SCOPE); break;
    case START_VIEW_WATERFALL: app->view = VIEW_WATERFALL;
                               set_tab(app, TAB_SCOPE); break;
    case START_VIEW_SURVEY:
        /*
         * Entered explicitly, not through set_tab.
         *
         * set_tab returns at once when the tab is already current, and since
         * the survey became the default tab it always is -- so --view survey
         * stopped calling view_survey_enter and --survey-range stopped
         * starting a sweep. Nothing failed: the window opened on the survey,
         * which is where it opens anyway, and the sweep simply never began.
         */
        set_tab(app, TAB_SURVEY);
        view_survey_enter(app);
        app->survey.band_menu_open = app->options.survey_bands;
        if (app->options.survey_band > 0)
            survey_choose_band(app, app->options.survey_band);
        break;
    case START_VIEW_GSM:       set_decode(app, DECODE_GSM);
                               set_tab(app, TAB_DECODE); break;
    case START_VIEW_ADSB:      set_decode(app, DECODE_ADSB);
                               set_tab(app, TAB_DECODE); break;
    case START_VIEW_FM:        set_decode(app, DECODE_FM);
                               set_tab(app, TAB_DECODE); break;
    case START_VIEW_LTE:       set_decode(app, DECODE_LTE);
                               set_tab(app, TAB_DECODE); break;
    case START_VIEW_CALIBRATION:
        open_calibration(app);
        /* --calibrate says which technology, so the overlay can be opened on
           the 4G arrangement and looked at. */
        if (app->options.calibrate == 2)
            calibration_select_technology(app, 1);
        break;
    case START_VIEW_SETTINGS:  open_settings(app); break;
    case START_VIEW_HELP:      open_help(app); break;
    default: break;
    }

    /*
     * The charts rather than the data, for whichever decode view was opened.
     * Every screen has to be reachable from the command line for the same
     * reason every decision does -- the LTE calibration panel shipped with
     * three overlapping regions because there was no way to look at it, and
     * three analysis screens had no way either.
     */
    if (app->options.zoom_to_hz > app->options.zoom_from_hz) {
        scope_freq_sync(app);
        app->sv.freq.view_lower_hz = (double)app->options.zoom_from_hz;
        app->sv.freq.view_upper_hz = (double)app->options.zoom_to_hz;
        freq_window_clamp(&app->sv.freq, SCOPE_FREQ_MIN_SPAN_HZ);
    }
    if (app->options.fm_play) {
        set_decode(app, DECODE_FM);
        set_tab(app, TAB_DECODE);
        fm_play(app);
    }
    if (app->options.fm_scan) {
        set_decode(app, DECODE_FM);
        set_tab(app, TAB_DECODE);
        fm_scan_begin(app);
    }
    if (app->options.analysis) {
        app->fm.analysis_mode = 1;
        app->adsb.analysis_mode = 1;
        app->lte.analysis_mode = 1;
        app->gsm_analysis_mode = 1;
    }

    /* A recording asked for on the command line starts as soon as the worker
       is up, exactly as the button's does. */
    if (app->options.record_seconds > 0.0) {
        const char *basename;
        const char *technology;
        cli_record_labels(&app->options, &basename, &technology);
        start_capture_record(app, basename, technology, app->options.arfcn,
                             app->options.arfcn ? 400000.0 : 0.0,
                             app->options.record_seconds);
    }
    double run_started = GetTime();

    while (!signal_stop_requested) {
        /*
         * A run that is ending draws one more frame before it goes, so
         * --screenshot photographs a finished screen rather than whatever was
         * half-composed when the clock ran out.
         */
        int last_frame = 0;
        if (WindowShouldClose())
            last_frame = 1;
        if (app->options.duration_seconds > 0.0 &&
            GetTime() - run_started >= app->options.duration_seconds)
            last_frame = 1;
        if (last_frame && !app->options.screenshot_path)
            break;

        /* Who gets this frame's input. The precedence is in input_route.h,
           where it can be checked; what each target does with the keys is its
           own handler's business. */
        struct input_state input = input_state_now(app);
        int shortcuts = input_shortcuts_live(&input);

        debug_log_frame_keys(app, &input);
        if (debug_log_active() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 at = GetMousePosition();
            struct debug_screen screen = debug_screen_now(app);
            char where[64];

            debug_screen_describe(&screen, where, sizeof(where));
            debug_log_write("click", "%.0f,%.0f on %s", at.x, at.y, where);
        }

        /* Quit is checked before the chain, so it means the same thing from
           every screen -- except while something is taking typed input, where
           losing a half-entered value to a stray letter is worse than having
           to click away first. */
        if (shortcuts && IsKeyPressed(KEY_Q))
            break;

        if (input_help_opens(&input) && IsKeyPressed(KEY_H)) {
            open_help(app);
        } else switch (input_route(&input)) {
        case INPUT_TARGET_HELP:
            handle_help_input(app);
            break;
        case INPUT_TARGET_SETTINGS:
            if (IsKeyPressed(KEY_ESCAPE))
                app->settings_open = 0;
            else
                handle_settings_input(app);
            break;
        case INPUT_TARGET_SCAN:
            handle_scan_input(app);
            break;
        case INPUT_TARGET_CALIBRATION:
            handle_calibration_input(app);
            break;
        case INPUT_TARGET_SURVEY:
            if (handle_tab_input(app)) {
                /* Tab switched this frame. */
            } else if (clicked(settings_button()) ||
                       (shortcuts && IsKeyPressed(KEY_S))) {
                open_settings(app);
            } else if (clicked(calibration_button()) ||
                       (shortcuts && IsKeyPressed(KEY_C))) {
                open_calibration(app);
            } else if (IsKeyPressed(KEY_ESCAPE) &&
                       input_escape(&input) == INPUT_ESCAPE_QUIT) {
                /* The survey is the top of its own stack -- it is the tab the
                   program opens on -- so Escape leaves the program, the same
                   as it does from the Scope views. Anything nearer the
                   surface, a dropdown or a field, is handled below. */
                break_requested = 1;
            } else {
                handle_survey_input(app);
            }
            break;
        case INPUT_TARGET_DECODE:
        case INPUT_TARGET_SCOPE:
            /* A click on a button is unambiguous whatever has focus; only the
               letter that stands for it is suppressed while typing. */
            if (handle_tab_input(app)) {
                /* Tab switched this frame; skip per-tab input. */
            } else if (clicked(settings_button()) ||
                       (shortcuts && IsKeyPressed(KEY_S))) {
                open_settings(app);
            } else if (clicked(calibration_button()) ||
                       (shortcuts && IsKeyPressed(KEY_C))) {
                if (app->tab == TAB_DECODE && app->decode == DECODE_GSM)
                    leave_gsm(app);
                open_calibration(app);
            } else if (input.tab == TAB_DECODE) {
                if (input_decode_keys_live(&input)) {
                    if (IsKeyPressed(KEY_ONE))
                        set_decode(app, DECODE_FM);
                    else if (IsKeyPressed(KEY_TWO))
                        set_decode(app, DECODE_ADSB);
                    else if (IsKeyPressed(KEY_THREE))
                        set_decode(app, DECODE_GSM);
                    else if (IsKeyPressed(KEY_FOUR))
                        set_decode(app, DECODE_LTE);
                }
                if (app->decode == DECODE_GSM)
                    handle_gsm_input(app);
                else if (app->decode == DECODE_ADSB)
                    handle_adsb_input(app);
                else if (app->decode == DECODE_FM)
                    handle_fm_input(app);
                else
                    handle_lte_input(app);
            } else if (IsKeyPressed(KEY_ESCAPE) && !input.text_focus) {
                break_requested = 1;
            }
            break;
        }
        if (break_requested)
            break;

        /*
         * The scale keys, in one place for every screen that draws something
         * they mean anything for.
         *
         * They used to live in each view, and two views that draw a waterfall
         * never got them -- a key binding that is missing is invisible until
         * somebody presses the key, and nothing in a test can see a handler
         * that was not written. input_scale_keys() is the table now, and
         * check-input walks every screen against it.
         */
        {
            int scale = input_scale_keys(&input);
            int up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
            int down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);

            if (scale == INPUT_SCALE_ACTIVE_CHART && (up || down))
                adjust_active_scale(app, up);
            else if (scale == INPUT_SCALE_WATERFALL && (up || down))
                adjust_waterfall_scale(app, up);
        }

        /*
         * The screen, when it changes. After the input phase rather than
         * before, so the line that follows a key is the screen that key
         * produced -- which is the whole question when a key is reported as
         * doing nothing.
         */
        if (debug_log_active()) {
            static struct debug_screen previous;
            static int seen;
            struct debug_screen screen = debug_screen_now(app);

            if (!seen || debug_screen_differs(&screen, &previous)) {
                char was[64], is[64];

                debug_screen_describe(&previous, was, sizeof(was));
                debug_screen_describe(&screen, is, sizeof(is));
                debug_log_write("screen", "%s%s%s", seen ? was : "",
                                seen ? " -> " : "", is);
                previous = screen;
                seen = 1;
            }
        }

        double now = GetTime();

        if (view_scope_resize_if_needed(app, calculate_plot()) < 0) {
            result = -1;
            break;
        }

        if (input_route(&input) == INPUT_TARGET_SCOPE) {
            /* While a survey range field has focus the digits belong to it,
               not to the view switcher. */
            if (input_view_keys_live(&input)) {
                enum view_kind selected = app->view;
                if (IsKeyPressed(KEY_ONE))
                    selected = VIEW_MAGNITUDE;
                if (IsKeyPressed(KEY_TWO))
                    selected = VIEW_SPECTRUM;
                if (IsKeyPressed(KEY_THREE))
                    selected = VIEW_SCATTER;
                if (IsKeyPressed(KEY_FOUR))
                    selected = VIEW_WATERFALL;
                if (selected != app->view) {
                    app->view = selected;
                    if (selected == VIEW_SCATTER)
                        clear_scatter(app);
                }
                /*
                 * The frequency window the spectrum and the waterfall share:
                 * drag to zoom, Left and Right to pan, 0 to put it back. It
                 * retunes only when a pan has run out of received span.
                 */
                if (app->view == VIEW_SPECTRUM ||
                    app->view == VIEW_WATERFALL) {
                    if (IsKeyPressed(KEY_ZERO))
                        scope_freq_reset(app);
                    else
                        scope_freq_input(app, app->plot);
                }
                /* The scale keys are applied once, below, for every screen
                   that has a scale -- not here, and not per view. */
            }
        }

        decay_spectrum_peak(app, now);
        int have_new = consume_latest(&app->acq, &snapshot);
        int spectrum_updated = have_new ? process_block(app, now) : 0;
        if (spectrum_updated) {
            update_waterfall(app);
            update_scan(app);
            update_calibration_measurement(app);
            if (app->tab == TAB_SURVEY && !app->calibration_open)
                update_survey(app, now, spectrum_updated);
        }
        if (have_new && app->tab == TAB_DECODE &&
            app->decode == DECODE_ADSB && !app->calibration_open)
            update_adsb(app, now);
        if (have_new && app->tab == TAB_DECODE &&
            app->decode == DECODE_GSM && !app->calibration_open)
            update_gsm_sch(app, now);
        if (app->tab == TAB_DECODE && app->decode == DECODE_LTE &&
            !app->calibration_open) {
            /* The scan drives the tuning, so it runs every frame and not only
               when a block arrives: most of its time is spent waiting for the
               tuner to settle, and nothing arrives worth having then. */
            update_lte_scan(app, now, have_new);
            if (have_new && !app->lte.scan.running)
                update_lte(app, now);
        }
        if (app->tab == TAB_DECODE && app->decode == DECODE_FM) {
            /* Every block, and only when one arrived: the pilot loop is a
               continuous thing and a block skipped is a quarter second of
               its lock thrown away. While the scan is walking the band it
               owns the receiver and feeds the chain itself. */
            update_fm_scan(app, now, have_new);
            if (have_new && !app->fm.scan.running)
                update_fm(app, now);
            /* Every frame, not every block: the sound card asks on its own
               schedule and a block is several of its buffers. */
            update_fm_audio(app);
        }
        update_drift_check(app, spectrum_updated);
        update_scatter(app, now,
                       have_new && app->tab == TAB_SCOPE &&
                           !app->calibration_open &&
                           app->view == VIEW_SCATTER);

        BeginDrawing();
        ClearBackground((Color){ 12, 19, 28, 255 });
        if (app->calibration_open) {
            /* Calibration is a global full-screen overlay reached by a button,
               independent of the active tab. */
            if (app->scan_open)
                draw_scan(app);
            else
                draw_calibration(app);
        } else {
            if (app->tab == TAB_DECODE) {
                if (app->decode == DECODE_GSM)
                    draw_gsm(app);
                else if (app->decode == DECODE_ADSB)
                    draw_adsb(app);
                else if (app->decode == DECODE_FM)
                    draw_fm(app);
                else
                    draw_lte(app);
            } else if (app->tab == TAB_SURVEY) {
                draw_survey(app);
            } else {
                draw_base_hud(app, &snapshot);
                if (app->view == VIEW_MAGNITUDE)
                    draw_magnitude(app);
                else if (app->view == VIEW_SPECTRUM)
                    draw_spectrum(app);
                else if (app->view == VIEW_SCATTER)
                    draw_scatter(app);
                else
                    draw_waterfall(app);
            }
            draw_header(app);
            if (app->settings_open)
                draw_settings(app);
        }
        if (app->help.open)
            draw_help(app);

        if (last_frame) {
            /*
             * Flush the batch, then read, then swap.
             *
             * All three parts matter and the order is the whole of it. raylib
             * accumulates geometry in a render batch and does not touch the
             * framebuffer until something flushes it, which EndDrawing does on
             * its way to swapping the buffers. So a read placed after
             * EndDrawing sees a swapped back buffer -- undefined, in practice
             * usually the frame just presented, which is why it appeared to
             * work for months -- and a read placed before it sees a
             * framebuffer holding the cleared background and nothing else.
             *
             * Both failures look identical from the outside: a PNG of a screen
             * the compositor can be photographed drawing perfectly well. The
             * second was the more honest of the two, because it failed for
             * every view at once instead of only for the one whose overlay
             * happened to overrun the batch and flush it by accident.
             *
             * rlDrawRenderBatchActive is the flush on its own, without the
             * swap.
             *
             * Exported rather than handed to TakeScreenshot, which prefixes
             * the working directory to whatever it is given, so an absolute
             * path silently becomes nonsense and no file appears.
             */
            rlDrawRenderBatchActive();
            Image frame = LoadImageFromScreen();
            if (ExportImage(frame, app->options.screenshot_path))
                fprintf(stderr, "Wrote %s (%dx%d)\n",
                        app->options.screenshot_path, frame.width,
                        frame.height);
            else
                fprintf(stderr, "Could not write %s\n",
                        app->options.screenshot_path);
            UnloadImage(frame);
            EndDrawing();
            break;
        }
        EndDrawing();
        if (snapshot.worker_failed) {
            result = -1;
            break;
        }
    }
    return result;
}

/* Print the receivers attached, and whether each can actually be opened. The
   second half is the useful half: "found but busy" is the state that otherwise
   shows up as a bare failure to start. */
static int list_devices(void) {
    uint32_t count = rtlsdr_get_device_count();

    if (count == 0) {
        printf("No RTL-SDR devices found.\n");
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        const char *name = rtlsdr_get_device_name(i);
        rtlsdr_dev_t *dev = NULL;
        printf("%u: %s\n", i, name ? name : "unknown");
        if (rtlsdr_open(&dev, i) < 0) {
            printf("   in use by another process, or not accessible\n");
            continue;
        }
        int gain_count = rtlsdr_get_tuner_gains(dev, NULL);
        int gains[64];
        if (gain_count > 0 && gain_count <= (int)(sizeof(gains) / sizeof(*gains)) &&
            rtlsdr_get_tuner_gains(dev, gains) == gain_count)
            printf("   tuner %s   gains %.1f..%.1f dB (%d steps)\n",
                   tuner_name(dev), gains[0] / 10.0,
                   gains[gain_count - 1] / 10.0, gain_count);
        else
            printf("   tuner %s\n", tuner_name(dev));
        rtlsdr_close(dev);
    }
    return 0;
}

double monotonic_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Print what one block's worth of decoding produced. The views keep the same
   results on screen; this is the same data as lines, so a capture can be
   checked from a script without a display or a person. */
/*
 * What the cell is saying, when this SCH is the one a broadcast block follows.
 *
 * The BCCH occupies frames 2 to 5 of the 51-multiframe, so only the SCH at
 * frame 1 is followed by one -- one in five of them. The other four are
 * followed by paging and access grants, which this does not read.
 */
static void print_broadcast(struct app *app, const struct gsm_sch_result *sch)
{
    float soft[GSM_BCCH_BURSTS * GSM_BURST_DATA_BITS];
    float bursts[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];
    float coded[GSM_BCCH_CODED_BITS];
    struct gsm_bcch_block block;
    struct gsm_si si;

    if (sch->frame_number % 51 != 1)
        return;
    memset(soft, 0, sizeof(soft));
    if (gsm_normal_bursts(app->i_samples, app->q_samples, app->pair_count,
                          (double)app->applied_sample_rate, sch,
                          GSM_BCCH_BURSTS, soft) < GSM_BCCH_BURSTS)
        return; /* the block ran past the end of this sample block */
    for (int b = 0; b < GSM_BCCH_BURSTS; b++)
        memcpy(bursts[b], &soft[b * GSM_BURST_DATA_BITS], sizeof(bursts[b]));
    gsm_bcch_deinterleave((const float (*)[GSM_BURST_DATA_BITS])bursts, coded);
    if (!gsm_bcch_decode_block(coded, &block))
        return; /* the Fire code refused it, so it is not a message */
    if (!gsm_si_parse(block.octets, &si))
        return; /* a broadcast this does not read */

    printf("BCCH %s", gsm_si_type_name(si.type));
    if (si.have_lai)
        printf("  MCC %d MNC %0*d  LAC %d", si.mcc, si.mnc_digits, si.mnc,
               si.lac);
    if (si.have_cell_id)
        printf("  CI %d", si.cell_id);
    if (si.neighbour_count) {
        printf("  ARFCN");
        for (int i = 0; i < si.neighbour_count; i++)
            printf(" %d", si.neighbours[i]);
    }
    printf("\n");
}

/*
 * What an LTE block yielded, printed only when it says something new.
 *
 * A cell is announced once and then only when it changes, because the search
 * finds the same one in every block and a line per block would bury the
 * message. The message itself is printed whenever it decodes: its frame number
 * advances, so consecutive lines are the cell's clock ticking rather than
 * repetition.
 */
static void print_lte(struct app *app, double now)
{
    uint64_t cells_before = app->lte.cells_found;
    uint64_t messages_before = app->lte.mibs_decoded;
    const struct lte_cell *cell = &app->lte.cell;
    const struct lte_mib *mib = &app->lte.mib;

    update_lte(app, now);

    if (app->lte.cells_found > cells_before &&
        cell->pci != app->lte.announced_pci) {
        printf("LTE  cell %d (N_ID_1 %d, N_ID_2 %d)  %s CP"
               "  offset %+.1f kHz (%+d subcarriers)  PSS %.2f  SSS %.2f\n",
               cell->pci, cell->n_id_1, cell->n_id_2,
               cell->extended_cp ? "extended" : "normal",
               cell->frequency_offset_hz / 1e3, cell->integer_offset,
               (double)cell->pss_correlation, (double)cell->sss_correlation);
        app->lte.announced_pci = cell->pci;
    }
    /* Only a message that repeated counts, so this prints at most once per
       cell rather than once per lucky parity. */
    if (app->lte.mibs_decoded > messages_before)
        printf("MIB  %d blocks (%.2f MHz)  PHICH %s %s  SFN %d"
               "  %d antenna port%s\n",
               mib->bandwidth_prb,
               lte_mib_occupied_hz(mib->bandwidth_prb) / 1e6,
               mib->phich_extended ? "extended" : "normal",
               lte_phich_resource_name(mib->phich_resource_sixths),
               mib->system_frame_number, mib->antenna_ports,
               mib->antenna_ports == 1 ? "" : "s");
}

static void print_new_decodes(struct app *app, double now,
                              enum decode_kind decoder)
{
    if (decoder == DECODE_LTE) {
        print_lte(app, now);
        fflush(stdout);
        return;
    }
    if (decoder == DECODE_FM) {
        /*
         * One line whenever the station's account of itself changes, rather
         * than one per block: FM is continuous and a block adds a couple of
         * groups, so per-block output would be four hundred lines saying the
         * same thing. What changes is worth printing; what repeats is not.
         */
        static uint16_t announced_pi;
        static int announced_valid;
        static char announced_ps[9];
        const struct rds_station *s = &app->fm.station;

        update_fm(app, now);
        if (s->pi_valid && (!announced_valid || s->pi != announced_pi)) {
            printf("FM   station 0x%04X  %.3f MHz\n", s->pi,
                   app->applied_frequency / 1e6);
            announced_pi = s->pi;
            announced_valid = 1;
            announced_ps[0] = '\0';
        }
        {
            static char announced_rt[65];
            if (s->rt_valid && strcmp(s->rt, announced_rt) != 0) {
                printf("RT   \"%s\"\n", s->rt);
                snprintf(announced_rt, sizeof(announced_rt), "%s", s->rt);
            }
        }
        if (s->ps_valid && strcmp(s->ps, announced_ps) != 0) {
            printf("RDS  \"%s\"  %s, %s  identification 0x%04X "
                   "(%d agreeing)\n", s->ps,
                   rds_pty_name(s->pty) ? rds_pty_name(s->pty) : "?",
                   rds_traffic_name(s->tp, s->ta), s->pi, s->pi_repeats);
            snprintf(announced_ps, sizeof(announced_ps), "%s", s->ps);
        }
        fflush(stdout);
        return;
    }
    if (decoder == DECODE_GSM) {
        int had = app->gsm.sch_valid;
        double before = app->gsm.sch_time;
        update_gsm_sch(app, now);
        if (!app->gsm.sch_valid || (had && app->gsm.sch_time == before))
            return;
        const struct gsm_sch_result *sch = &app->gsm.sch;
        printf("SCH  BSIC %d (NCC %d, BCC %d)  frame %d (T1/T2/T3 %d/%d/%d)"
               "  match %.2f%s\n",
               sch->bsic, sch->ncc, sch->bcc, sch->frame_number, sch->t1,
               sch->t2, sch->t3, (double)sch->confidence,
               app->gsm.continuity.implausible ? "  [T1 JUMPED]" : "");
        print_broadcast(app, sch);
    } else {
        int before = app->adsb.log_count;
        uint64_t frames = app->adsb.frames_total;
        update_adsb(app, now);
        int added = (int)(app->adsb.frames_total - frames);
        if (added <= 0)
            return;
        (void)before;
        /* The log is newest-first, so walk the new rows back to front to
           print them in the order they arrived. */
        for (int i = added - 1; i >= 0; i--) {
            const struct adsb_log_entry *e = &app->adsb.log[i];
            printf("%s  %s  %-3s  %-42s  %s\n", e->stamp, e->icao, e->label,
                   e->detail, e->raw);
        }
    }
    fflush(stdout);
}

/* Acquire with no window: start the worker, optionally record, and stop when
   the recording finishes, the duration elapses, or a signal arrives. Nothing
   here touches raylib, and nothing needs the frame loop -- recording tees off
   inside the acquisition thread, upstream of the display's block slot. */
static int run_headless(struct app *app) {
    /* raylib writes its own notices to stdout, and stdout here is a data
       stream someone is parsing. Nothing should reach raylib on this path,
       but a stray line would corrupt a survey rather than merely clutter it. */
    SetTraceLogLevel(LOG_NONE);

    const char *basename;
    const char *technology;
    int recording_started = 0;
    int result = 0;
    double started;

    /* Nothing is watching a headless run, so playing a capture at the speed of
       a receiver buys nothing and costs blocks: the idle poll below is longer
       than the 65.5 ms a block covers, so the slot overwrites and the same
       capture decodes a different number of messages each run. Read it whole
       instead, as fast as this loop can take it. A live receiver keeps the
       overwriteable slot -- it cannot be asked to wait. */
    if (app->options.file_path)
        acquisition_set_lossless(&app->acq, 1);

    if (start_acquisition(app) < 0) {
        fprintf(stderr, "Cannot start acquisition: %s\n",
                app->settings_error[0] ? app->settings_error : "unknown");
        return -1;
    }
    started = monotonic_seconds();
    if (app->options.record_seconds > 0.0) {
        cli_record_labels(&app->options, &basename, &technology);
        if (start_capture_record(app, basename, technology,
                                 app->options.arfcn,
                                 app->options.arfcn ? 400000.0 : 0.0,
                                 app->options.record_seconds) < 0) {
            stop_acquisition(app);
            return -1;
        }
        recording_started = 1;
    } else if (app->options.duration_seconds <= 0.0 &&
               !app->options.play_once && !app->options.survey_report) {
        fprintf(stderr, "Acquiring headless; Ctrl-C to stop.\n");
    }

    /*
     * A headless band scan is its own run, like the survey: it retunes its
     * way across a band and prints what it found. It exists for the same
     * reason the headless survey does -- the scan is otherwise a button, and
     * a button is not something a script or an agent can press (ADR-0012).
     */
    if (app->options.lte_scan_band) {
        const struct lte_band *band = NULL;
        double began = monotonic_seconds();
        int i;

        sdr_dsp_init(&app->dsp);
        for (i = 0; i < lte_band_count(); i++)
            if (lte_band_at(i)->band == app->options.lte_scan_band)
                band = lte_band_at(i);
        if (!band || lte_scan_begin(app, app->options.lte_scan_band,
                                    monotonic_seconds() - began) < 0) {
            fprintf(stderr, "Cannot scan band %d.\n",
                    app->options.lte_scan_band);
            stop_acquisition(app);
            return -1;
        }
        fprintf(stderr, "Scanning band %d (%s): %d channels, about %.0f s.\n",
                band->band, band->name, lte_scan_count(band),
                lte_scan_seconds(band));
        printf("# cell <earfcn> <frequency_hz> <pci> <n_id_1> <n_id_2>"
               " <pss> <sss_margin>\n");
        while (lte_scan_running(app) && !signal_stop_requested) {
            struct timespec tick = { 0, 5 * 1000000L };
            struct slot_snapshot snapshot;
            double now = monotonic_seconds() - began;
            int have_new = consume_latest(&app->acq, &snapshot);
            if (have_new)
                process_block(app, now);
            if (snapshot.worker_failed) {
                fprintf(stderr, "Acquisition failed: %s\n",
                        snapshot.worker_error);
                break;
            }
            update_lte_scan(app, now, have_new);
            if (!have_new)
                nanosleep(&tick, NULL);
        }
        for (i = 0; i < app->lte.scan.found_count; i++) {
            const struct lte_found_cell *found = &app->lte.scan.found[i];
            printf("cell %u %u %d %d %d %.3f %.3f\n", found->earfcn,
                   found->frequency_hz, found->pci, found->pci / 3,
                   found->pci % 3, (double)found->pss,
                   (double)found->sss_margin);
        }
        /* `found` is what survived; `dropped` is what the confirmation pass
           took away. Reporting only the survivors would hide the difference
           between a quiet band and a noisy one that argued and lost. */
        printf("lte-scan band %d channels %d searched %d found %d "
               "dropped %d\n",
               band->band, lte_scan_count(band), app->lte.scan.candidate,
               app->lte.scan.found_count, app->lte.scan.confirm_dropped);
        fflush(stdout);
        if (stop_acquisition(app) < 0)
            return -1;
        return 0;
    }

    /*
     * Walking the LTE chain over a live cell.
     *
     * probe-lte-chain does this for a capture, and a capture is two seconds of
     * one afternoon. What the chain does over a live cell for a minute is a
     * different question and the one that matters when it is not working: a
     * cell that decodes in half its blocks and a cell that never decodes look
     * identical in a single block, and completely different in sixty.
     *
     * A stage line per block rather than a verdict. Which stage stops is the
     * whole diagnosis -- no PSS is a tuning or a band problem, PSS without SSS
     * was the conjugated-sequence bug, SSS without parity is the broadcast
     * channel, and parity without a repeat is chance.
     */
    if (app->options.lte_chain) {
        static const int port_hypotheses[3] = { 1, 2, 4 };
        double began, limit = app->options.lte_chain_seconds > 0.0
                                  ? app->options.lte_chain_seconds : 30.0;
        unsigned long blocks = 0, cells = 0, parity = 0, messages = 0;
        struct lte_mib last;
        int have_last = 0;
        uint32_t carrier = 0;
        int earfcn = app->options.earfcn;

        sdr_dsp_init(&app->dsp);
        if (app->options.lte_chain_band) {
            printf("lte-chain scanning band %d\n", app->options.lte_chain_band);
            fflush(stdout);
            if (retune_receiver_at_rate(app, app->applied_frequency,
                                        LTE_SAMPLE_RATE_HZ,
                                        app->applied_ppm) < 0 ||
                lte_scan_begin(app, app->options.lte_chain_band,
                               monotonic_seconds()) != 0) {
                fprintf(stderr, "Could not start the band scan\n");
                return -1;
            }
            while (lte_scan_running(app) && !signal_stop_requested) {
                struct timespec tick = { 0, 5 * 1000000L };
                struct slot_snapshot snapshot;
                int have_new = consume_latest(&app->acq, &snapshot);
                if (have_new)
                    process_block(app, monotonic_seconds());
                if (snapshot.worker_failed) {
                    fprintf(stderr, "Acquisition failed: %s\n",
                            snapshot.worker_error);
                    return -1;
                }
                update_lte_scan(app, monotonic_seconds(), have_new);
                if (!have_new)
                    nanosleep(&tick, NULL);
            }
            if (app->lte.scan.found_count < 1) {
                printf("lte-chain-summary blocks 0 cells 0 parity 0 "
                       "messages 0 reason no-cell\n");
                fflush(stdout);
                return stop_acquisition(app) < 0 ? -1 : 0;
            }
            earfcn = (int)app->lte.scan.found[0].earfcn;
        }
        if (!lte_earfcn_downlink_hz((unsigned int)earfcn, &carrier)) {
            fprintf(stderr, "EARFCN %d is not a downlink channel\n", earfcn);
            return -1;
        }
        if (retune_receiver_at_rate(app, carrier, LTE_SAMPLE_RATE_HZ,
                                    app->applied_ppm) < 0)
            return -1;
        printf("lte-chain earfcn %d carrier_hz %u rate %u ppm %d\n", earfcn,
               carrier, app->applied_sample_rate, app->applied_ppm);
        printf("# chain <block> pss <corr> <runner_up> n_id_2 <n> timing <sample> "
               "offset_hz <hz> integer <subcarriers>\n");
        printf("# chain <block> sss <corr> <runner_up> n_id_1 <n> pci <n> cp "
               "<normal|extended> half_frame <0|1>\n");
        printf("# chain <block> mib ports <n> prb <n> phich <duration> "
               "<resource> sfn <n> quarter <n> combining <ports>\n");
        fflush(stdout);
        began = monotonic_seconds();

        while (!signal_stop_requested &&
               monotonic_seconds() - began < limit) {
            struct timespec tick = { 0, 5 * 1000000L };
            struct slot_snapshot snapshot;
            struct lte_cell cell;
            int have_new = consume_latest(&app->acq, &snapshot);
            int h;

            if (!have_new) {
                nanosleep(&tick, NULL);
                continue;
            }
            process_block(app, monotonic_seconds());
            if (snapshot.worker_failed) {
                fprintf(stderr, "Acquisition failed: %s\n",
                        snapshot.worker_error);
                return -1;
            }
            if (app->pair_count < LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE)
                continue;
            blocks++;
            if (lte_cell_search(app->i_samples, app->q_samples,
                                app->pair_count,
                                (double)app->applied_sample_rate, &cell,
                                NULL) != 1) {
                printf("chain %lu pss %.3f %.3f n_id_2 %d timing - "
                       "offset_hz - integer - no-cell\n", blocks,
                       (double)cell.pss_correlation,
                       (double)cell.pss_runner_up, cell.n_id_2);
                fflush(stdout);
                continue;
            }
            cells++;
            printf("chain %lu pss %.3f %.3f n_id_2 %d timing %zu offset_hz "
                   "%.0f integer %d\n", blocks, (double)cell.pss_correlation,
                   (double)cell.pss_runner_up, cell.n_id_2,
                   cell.subframe0_start, cell.frequency_offset_hz,
                   cell.integer_offset);
            printf("chain %lu sss %.3f %.3f n_id_1 %d pci %d cp %s "
                   "half_frame %d\n", blocks, (double)cell.sss_correlation,
                   (double)cell.sss_runner_up, cell.n_id_1, cell.pci,
                   cell.extended_cp ? "extended" : "normal", cell.half_frame);

            for (h = 0; h < 3; h++) {
                float soft[LTE_PBCH_SOFT_BITS];
                struct lte_mib mib;
                if (lte_pbch_soft_bits(app->i_samples, app->q_samples,
                                       app->pair_count,
                                       (double)app->applied_sample_rate, &cell,
                                       cell.subframe0_start,
                                       port_hypotheses[h], soft,
                                       NULL) != LTE_PBCH_SOFT_BITS)
                    continue;
                if (!lte_mib_decode(soft, cell.pci, &mib))
                    continue;
                parity++;
                /* A parity that passes is not yet a message: sixteen bits
                   accept one block in 65536 and this tries thirty-six a
                   block. What separates them is a repeat that agrees. */
                if (have_last && lte_mib_same_cell(&last, &mib))
                    messages++;
                last = mib;
                have_last = 1;
                {
                    /* The resource as the standard names it -- 1/6, 1/2, 1,
                       2 -- not the raw count of sixths, which reads as a
                       different number entirely. */
                    const char *res =
                        lte_phich_resource_name(mib.phich_resource_sixths);
                    printf("chain %lu mib ports %d prb %d phich %s %s sfn %d "
                           "quarter %d combining %d\n", blocks,
                           mib.antenna_ports, mib.bandwidth_prb,
                           mib.phich_extended ? "extended" : "normal",
                           res ? res : "?", mib.system_frame_number,
                           mib.quarter, port_hypotheses[h]);
                }
                break;
            }
            fflush(stdout);
        }
        printf("lte-chain-summary blocks %lu cells %lu parity %lu messages "
               "%lu\n", blocks, cells, parity, messages);
        fflush(stdout);
        if (stop_acquisition(app) < 0)
            return -1;
        return 0;
    }

    /*
     * A headless calibration. Its own run, like the survey and the band scan,
     * and for the same reason: the lock gate decides whether a correction may
     * be applied, and a decision reachable only by somebody clicking Start is
     * a decision no check can reach (ADR-0012).
     *
     * It prints every measurement rather than only the verdict. The verdict is
     * one bit; the sequence is what shows *why* -- whether the residuals are
     * converging, and if they are not, whether the scatter is in the estimator
     * or in the crystal.
     */
    if (app->options.calibrate) {
        double began = monotonic_seconds();
        double limit = app->options.calibrate_seconds > 0.0
                           ? app->options.calibrate_seconds : 90.0;
        int reported = 0, locked = 0;
        const char *why = "timeout";

        sdr_dsp_init(&app->dsp);
        app->calibration_open = 1;
        app->calibration_technology = app->options.calibrate == 1 ? 0 : 1;
        if (app->options.calibrate == 1) {
            snprintf(app->cal.channel, sizeof(app->cal.channel), "%d",
                     app->options.arfcn);
        } else if (app->options.earfcn) {
            snprintf(app->cal.channel, sizeof(app->cal.channel), "%d",
                     app->options.earfcn);
        }
        app->cal.channel_length = (int)strlen(app->cal.channel);
        app->cal.return_frequency = app->applied_frequency;

        if (app->options.calibrate == 2 && app->options.calibrate_band) {
            /* Find something to calibrate against rather than being told. */
            printf("calibrate scanning band %d\n", app->options.calibrate_band);
            fflush(stdout);
            if (retune_receiver_at_rate(app, app->applied_frequency,
                                        LTE_SAMPLE_RATE_HZ,
                                        app->applied_ppm) < 0 ||
                lte_scan_begin(app, app->options.calibrate_band,
                               monotonic_seconds()) != 0) {
                fprintf(stderr, "Could not start the band scan\n");
                return -1;
            }
            while (lte_scan_running(app) && !signal_stop_requested) {
                struct timespec tick = { 0, 5 * 1000000L };
                struct slot_snapshot snapshot;
                int have_new = consume_latest(&app->acq, &snapshot);
                if (have_new)
                    process_block(app, monotonic_seconds() - began);
                if (snapshot.worker_failed) {
                    fprintf(stderr, "Acquisition failed: %s\n",
                            snapshot.worker_error);
                    return -1;
                }
                update_lte_scan(app, monotonic_seconds(), have_new);
                if (!have_new)
                    nanosleep(&tick, NULL);
            }
            if (app->lte.scan.found_count < 1) {
                printf("calibrate-result locked 0 reason no-cell\n");
                fflush(stdout);
                return stop_acquisition(app) < 0 ? -1 : 0;
            }
            /* Strongest first, so the head of the list is the best reference
               the band has to offer. */
            snprintf(app->cal.channel, sizeof(app->cal.channel), "%u",
                     app->lte.scan.found[0].earfcn);
            app->cal.channel_length = (int)strlen(app->cal.channel);
            printf("calibrate chose earfcn %u cell %d pss %.2f\n",
                   app->lte.scan.found[0].earfcn, app->lte.scan.found[0].pci,
                   (double)app->lte.scan.found[0].pss);
            fflush(stdout);
            /* The budget is for the calibration, not for finding something to
               calibrate against: a band scan runs for minutes and would eat
               the whole of it before a single residual was measured. */
            began = monotonic_seconds();
        }

        if (start_calibration(app) < 0) {
            fprintf(stderr, "%s\n", app->calibration_status);
            return -1;
        }
        printf("calibrate technology %s channel %s expected_hz %u "
               "applied_ppm %d\n",
               app->options.calibrate == 1 ? "gsm" : "lte", app->cal.channel,
               app->calibration_expected_hz, app->applied_ppm);
        fflush(stdout);

        while (!signal_stop_requested) {
            struct timespec tick = { 0, 5 * 1000000L };
            struct slot_snapshot snapshot;
            int have_new = consume_latest(&app->acq, &snapshot);
            double now = monotonic_seconds();

            if (have_new)
                process_block(app, now - began);
            if (snapshot.worker_failed) {
                fprintf(stderr, "Acquisition failed: %s\n",
                        snapshot.worker_error);
                return -1;
            }
            update_calibration_measurement(app);
            if (app->cal.track.measurements > reported) {
                reported = app->cal.track.measurements;
                /* Every measurement, not a summary: the sequence is what shows
                   whether the scatter is the estimator or the crystal. */
                printf("cal-measure %d observed_ppm %.2f centre_ppm %.2f "
                       "sem_ppm %.2f spread_ppm %.2f source %s quality %.2f\n",
                       reported, app->cal.offset_hz /
                           (double)app->calibration_expected_hz * 1e6,
                       app->cal.track.recent_center, app->cal.track.recent_sem,
                       app->cal.track.recent_spread,
                       app->cal.track.source == CALIBRATION_SOURCE_FCCH
                           ? "fcch"
                           : app->cal.track.source == CALIBRATION_SOURCE_LTE
                                 ? "lte" : "centroid",
                       (double)app->cal.fcch_confidence);
                fflush(stdout);
            }
            if (app->cal.track.stable) {
                locked = 1;
                why = "locked";
                break;
            }
            if (now - began > limit) {
                /* Say which clause is still unsatisfied, so a calibration that
                   will not lock is a diagnosis rather than a shrug. */
                if (app->cal.track.measurements < CALIBRATION_MIN_MEASUREMENTS)
                    why = "too-few-measurements";
                else if (app->cal.track.recent_sem > CALIBRATION_MAX_SEM_PPM)
                    why = "sem-too-wide";
                else
                    why = "timeout";
                break;
            }
            if (!have_new)
                nanosleep(&tick, NULL);
        }
        printf("calibrate-result locked %d measurements %d centre_ppm %.2f "
               "sem_ppm %.2f spread_ppm %.2f suggested_ppm %d reason %s\n",
               locked, app->cal.track.measurements, app->cal.track.recent_center,
               app->cal.track.recent_sem, app->cal.track.recent_spread,
               app->cal.suggested_ppm, why);
        fflush(stdout);
        if (stop_acquisition(app) < 0)
            return -1;
        return 0;
    }

    /* A headless survey is its own run: it sweeps, prints, and returns, rather
       than sharing the block loop below with a decode. */
    if (app->options.survey_report) {
        int survey_result;

        sdr_dsp_init(&app->dsp);
        survey_result = survey_report_run(app);
        if (stop_acquisition(app) < 0)
            survey_result = -1;
        return survey_result;
    }

    enum decode_kind decoder = DECODE_ADSB;
    if (app->options.technology &&
        strcmp(app->options.technology, "gsm") == 0)
        decoder = DECODE_GSM;
    else if (app->options.technology &&
             strcmp(app->options.technology, "lte") == 0)
        decoder = DECODE_LTE;
    else if (app->options.technology &&
             strcmp(app->options.technology, "fm") == 0)
        decoder = DECODE_FM;
    if (app->options.decode)
        sdr_dsp_init(&app->dsp);

    while (!signal_stop_requested) {
        struct timespec tick = { 0, 100 * 1000000L };
        struct slot_snapshot snapshot;
        uint64_t bytes = 0;
        char path[ACQUISITION_PATH_MAX];
        int recording = acquisition_recording_status(&app->acq, &bytes, path,
                                                     sizeof(path));

        int have_new = consume_latest(&app->acq, &snapshot);
        if (have_new && app->options.decode) {
            double now = monotonic_seconds() - started;
            /* The spectrum is not needed to decode -- process_block's return
               only says whether it updated -- but the magnitudes and centred
               I/Q it fills in are. */
            process_block(app, now);
            if (app->pair_count > 0)
                print_new_decodes(app, now, decoder);
        }
        if (snapshot.worker_failed) {
            fprintf(stderr, "Acquisition failed: %s\n", snapshot.worker_error);
            result = -1;
            break;
        }
        if (snapshot.worker_done && !recording) {
            if (app->options.decode)
                fprintf(stderr, "End of capture.\n");
            break;
        }
        if (recording_started && !recording) {
            printf("%s\n", path);
            break;
        }
        if (app->options.duration_seconds > 0.0 &&
            monotonic_seconds() - started >= app->options.duration_seconds)
            break;
        /* Only idle when there was nothing to do: a capture being decoded
           delivers blocks as fast as the pacer allows, and sleeping through
           them would drop the ones the slot overwrites. */
        if (!have_new)
            nanosleep(&tick, NULL);
    }
    if (recording_started && signal_stop_requested)
        fprintf(stderr, "Stopped early; the capture is short but its sidecar "
                        "records what was written.\n");
    if (stop_acquisition(app) < 0)
        result = -1;
    return result;
}

/* Filled in by main before the app is built, and copied into it. */
static struct config loaded_config;

int main(int argc, char **argv) {
    struct app *app;
    int result = 1;
    int gui_result;

    struct options options;
    if (parse_options(argc, argv, &options) < 0) {
        usage(argv[0]);
        return 1;
    }
    if (options.list_devices)
        return list_devices();
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
         * The correction follows the site. Arriving somewhere the receiver has
         * been calibrated should restore that calibration, not carry the last
         * one measured somewhere else -- but an explicit --ppm outranks it and
         * is recorded against wherever we now are.
         */
        if (config.site[0]) {
            if (options.ppm_seen) {
                if (config_set_site_ppm(&config, config.site, options.ppm))
                    changed = 1;
            } else {
                options.ppm = config_site_ppm(&config, config.site);
            }
        }
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
    app->receiver_mode = options.file_path == NULL;
    app->remove_dc = options.remove_dc;
    view_gsm_defaults(app);
    view_lte_defaults(app);
    view_fm_defaults(app);
    view_scope_defaults(app);
    view_survey_defaults(app);
    if (options.gsm_features_seen) {
        app->gsm.opt_filter = (options.gsm_features & GSM_OPT_FILTER) != 0;
        app->gsm.opt_finecfo = (options.gsm_features & GSM_OPT_FINECFO) != 0;
        app->gsm.opt_trellis = (options.gsm_features & GSM_OPT_TRELLIS) != 0;
    }
    if (options.arfcn) {
        /* Both the GSM view and a recording's sidecar read this. */
        app->scan_selected_arfcn = options.arfcn;
        app->gsm.selected_hz = (double)options.frequency + 400000.0;
    }

    if (options.debug_log && debug_log_open(options.debug_log) == 0) {
        /* From the options rather than from the app: the source has not been
           attached yet here, and the fields it fills in are still zero. */
        debug_log_write("open", "sdrprobe, %s, %u S/s, %.6f MHz, %+d ppm",
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

    if (install_signal_handlers(app) < 0)
        goto cleanup;

    if (options.headless) {
        result = run_headless(app) == 0 ? 0 : 1;
        goto cleanup;
    }
    gui_result = run_gui(app);
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
                    cancel_result = rtlsdr_cancel_async(app->dev);
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
    if (app->dev) {
        int close_result = rtlsdr_close(app->dev);
        if (close_result != 0) {
            fprintf(stderr, "Failed to close RTL-SDR receiver (%d).\n",
                    close_result);
            result = 1;
        }
        app->dev = NULL;
    }
    view_scope_release(app);
    free(app->supported_gains);
    app->supported_gains = NULL;
    if (app->window_ready) {
        CloseWindow();
        app->window_ready = 0;
    }
    if (app->signals_ready) {
        if (sigaction(SIGTERM, &app->old_sigterm, NULL) != 0 ||
            sigaction(SIGINT, &app->old_sigint, NULL) != 0) {
            fprintf(stderr, "Cannot restore signal handlers: %s\n",
                    strerror(errno));
            result = 1;
        }
    }
    fm_audio_close(app);
    debug_log_write("close", "result %d", result);
    debug_log_close();
    free(app);
    return result;
}
