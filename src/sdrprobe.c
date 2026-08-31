#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <raylib.h>
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
#include "raygui.h"


static volatile sig_atomic_t signal_stop_requested = 0;

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
















static int process_block(struct app *app, double now) {
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
        app->spectrum_average, app->spectrum_candidate);
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



int retune_receiver(struct app *app, uint32_t frequency, int ppm) {
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
    if (recreate_waterfall(app, app->plot, 1) < 0) {
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        app->applied_frequency = old_frequency;
        app->applied_ppm = old_ppm;
        if (start_acquisition(app) < 0)
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "Waterfall failed and acquisition could not restart");
        return -1;
    }
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
    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
}

/* Render-only button: raygui draws it (themed); the click action is dispatched
   from the input phase via clicked(), so heavy actions never run mid-frame. */
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

static const char *tab_labels[TAB_COUNT] = { "Scope", "Decode" };

static Rectangle tab_rect(int index) {
    return chrome_layout_now().tab[index & 1];
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
    app->settings_open = 0;
    app->tab = new_tab;
    if (new_tab == TAB_DECODE && app->decode == DECODE_GSM)
        enter_gsm(app);
}

/* Switch the Decode sub-view. Entering/leaving GSM manages its tuning. */
void set_decode(struct app *app, int kind) {
    if (kind == (int)app->decode)
        return;
    if (app->decode == DECODE_GSM)
        leave_gsm(app);
    app->decode = kind;
    if (kind == DECODE_GSM)
        enter_gsm(app);
}

/* One row of numbered mode options, the active one highlighted. */
static void draw_option_row(int active, const char **labels, int count,
                            const char *suffix) {
    int x = 22;
    const int y = 50;
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
    static const char *scope_opts[5] = {
        "1 magnitude", "2 spectrum", "3 I/Q scatter", "4 waterfall",
        "5 survey"
    };
    static const char *decode_opts[2] = { "1 GSM", "2 ADS-B" };

    DrawText("sdrprobe signal visualizer", 22, 14, 24,
             (Color){ 225, 236, 245, 255 });
    if (app->tab == TAB_SCOPE)
        draw_option_row((int)app->view, scope_opts, 5,
                        "Up/Down scale   h help   Esc quit");
    else
        draw_option_row((int)app->decode, decode_opts, 2,
                        "h help   Esc scope");

    draw_tab_bar(app);
    draw_button(settings_button(), "Settings", 0);
    draw_button(calibration_button(), "Calibration", 0);
    draw_health_indicator(app);
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

/* --- Decode tab: numbered sub-views (1 GSM, 2 ADS-B) --- */

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
    } else if (options->technology) {
        *basename = options->technology;
        *technology = options->technology;
    } else if (options->view == START_VIEW_GSM) {
        *basename = "gsm";
        *technology = "gsm";
    } else if (options->view == START_VIEW_ADSB) {
        *basename = "adsb";
        *technology = "adsb";
    } else {
        *basename = "raw";
        *technology = "raw";
    }
}

static int run_gui(struct app *app) {
    struct slot_snapshot snapshot;
    int result = 0;

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
    case START_VIEW_SPECTRUM:  app->view = VIEW_SPECTRUM; break;
    case START_VIEW_SCATTER:   app->view = VIEW_SCATTER; break;
    case START_VIEW_WATERFALL: app->view = VIEW_WATERFALL; break;
    case START_VIEW_SURVEY:    app->view = VIEW_SURVEY;
                               view_survey_enter(app); break;
    case START_VIEW_GSM:       set_decode(app, DECODE_GSM);
                               set_tab(app, TAB_DECODE); break;
    case START_VIEW_ADSB:      set_decode(app, DECODE_ADSB);
                               set_tab(app, TAB_DECODE); break;
    default: break;
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
        if (WindowShouldClose())
            break;
        if (app->options.duration_seconds > 0.0 &&
            GetTime() - run_started >= app->options.duration_seconds)
            break;

        /* Quit is checked before the precedence chain, so it means the same
           thing from every screen. The exception is the Settings panel, which
           is taking typed input: losing half-entered settings to a stray
           letter is worse than having to leave the panel first. */
        if (IsKeyPressed(KEY_Q) && !app->settings_open)
            break;

        if (app->help.open) {
            /* Help is the outermost overlay: it can be raised over a view or
               over calibration, and takes every key while it is up. */
            handle_help_input(app);
        } else if (app->settings_open) {
            if (IsKeyPressed(KEY_ESCAPE))
                app->settings_open = 0;
            else
                handle_settings_input(app);
        } else if (IsKeyPressed(KEY_H)) {
            open_help(app);
        } else if (app->calibration_open) {
            if (app->scan_open)
                handle_scan_input(app);
            else
                handle_calibration_input(app);
        } else if (handle_tab_input(app)) {
            /* Tab switched this frame; skip per-tab input. */
        } else if (clicked(settings_button()) || IsKeyPressed(KEY_S)) {
            open_settings(app);
        } else if (clicked(calibration_button()) || IsKeyPressed(KEY_C)) {
            if (app->tab == TAB_DECODE && app->decode == DECODE_GSM)
                leave_gsm(app);
            open_calibration(app);
        } else if (app->tab == TAB_DECODE) {
            if (IsKeyPressed(KEY_ONE))
                set_decode(app, DECODE_GSM);
            else if (IsKeyPressed(KEY_TWO))
                set_decode(app, DECODE_ADSB);
            if (app->decode == DECODE_GSM)
                handle_gsm_input(app);
            else
                handle_adsb_input(app);
        } else if (IsKeyPressed(KEY_ESCAPE) && !survey_editing(app)) {
            break;
        }

        double now = GetTime();

        if (view_scope_resize_if_needed(app, calculate_plot()) < 0) {
            result = -1;
            break;
        }

        if (app->tab == TAB_SCOPE && !app->settings_open &&
            !app->calibration_open) {
            /* While a survey range field has focus the digits belong to it,
               not to the view switcher. */
            if (!survey_editing(app)) {
                enum view_kind selected = app->view;
                if (IsKeyPressed(KEY_ONE))
                    selected = VIEW_MAGNITUDE;
                if (IsKeyPressed(KEY_TWO))
                    selected = VIEW_SPECTRUM;
                if (IsKeyPressed(KEY_THREE))
                    selected = VIEW_SCATTER;
                if (IsKeyPressed(KEY_FOUR))
                    selected = VIEW_WATERFALL;
                if (IsKeyPressed(KEY_FIVE))
                    selected = VIEW_SURVEY;
                if (selected != app->view) {
                    /* Leaving the survey puts the receiver back where it was
                       before the sweep walked it away. */
                    if (app->view == VIEW_SURVEY)
                        view_survey_leave(app);
                    app->view = selected;
                    if (selected == VIEW_SCATTER)
                        clear_scatter(app);
                    if (selected == VIEW_SURVEY)
                        view_survey_enter(app);
                }
                if (app->view != VIEW_SURVEY) {
                    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
                        adjust_active_scale(app, 1);
                    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
                        adjust_active_scale(app, 0);
                }
            }
            if (app->view == VIEW_SURVEY)
                handle_survey_input(app);
        }

        decay_spectrum_peak(app, now);
        int have_new = consume_latest(&app->acq, &snapshot);
        int spectrum_updated = have_new ? process_block(app, now) : 0;
        if (spectrum_updated) {
            update_waterfall(app);
            update_scan(app);
            update_calibration_measurement(app);
            if (app->tab == TAB_SCOPE && app->view == VIEW_SURVEY &&
                !app->calibration_open)
                update_survey(app, now);
        }
        if (have_new && app->tab == TAB_DECODE &&
            app->decode == DECODE_ADSB && !app->calibration_open)
            update_adsb(app, now);
        if (have_new && app->tab == TAB_DECODE &&
            app->decode == DECODE_GSM && !app->calibration_open)
            update_gsm_sch(app, now);
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
                else
                    draw_adsb(app);
            } else {
                if (app->view == VIEW_SURVEY) {
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
                        draw_waterfall(app, 0);
                }
            }
            draw_header(app);
            if (app->settings_open)
                draw_settings(app);
        }
        if (app->help.open)
            draw_help(app);
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

static double monotonic_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

/* Print what one block's worth of decoding produced. The views keep the same
   results on screen; this is the same data as lines, so a capture can be
   checked from a script without a display or a person. */
static void print_new_decodes(struct app *app, double now, int gsm)
{
    if (gsm) {
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
    const char *basename;
    const char *technology;
    int recording_started = 0;
    int result = 0;
    double started;

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
               !app->options.play_once) {
        fprintf(stderr, "Acquiring headless; Ctrl-C to stop.\n");
    }

    int decode_gsm = app->options.technology &&
                     strcmp(app->options.technology, "gsm") == 0;
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
                print_new_decodes(app, now, decode_gsm);
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

    app = calloc(1, sizeof(*app));
    if (!app) {
        fprintf(stderr, "Cannot allocate application state.\n");
        return 1;
    }
    app->options = options;
    app->receiver_mode = options.file_path == NULL;
    app->remove_dc = options.remove_dc;
    view_gsm_defaults(app);
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

    /* Before any path that can reach cleanup, which touches this. */
    int record_mutex_result = acquisition_init(&app->acq);
    if (record_mutex_result != 0) {
        fprintf(stderr, "Cannot create recording mutex: %s\n",
                strerror(record_mutex_result));
        goto cleanup;
    }
    app->record_mutex_ready = 1;

    if (app->receiver_mode) {
        if (configure_receiver(app) < 0)
            goto cleanup;
    } else {
        if (open_capture(app) < 0)
            goto cleanup;
    }

    int mutex_result = pthread_mutex_init(&app->acq.latest.mutex, NULL);
    if (mutex_result != 0) {
        fprintf(stderr, "Cannot initialize acquisition mutex: %s\n",
                strerror(mutex_result));
        goto cleanup;
    }
    app->acq.mutex_ready = 1;
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

    if (app->acq.mutex_ready) {
        int destroy_result = pthread_mutex_destroy(&app->acq.latest.mutex);
        if (destroy_result != 0) {
            fprintf(stderr, "Cannot destroy acquisition mutex: %s\n",
                    strerror(destroy_result));
            result = 1;
        }
        app->acq.mutex_ready = 0;
    }
    if (app->capture) {
        if (fclose(app->capture) != 0) {
            fprintf(stderr, "Cannot close capture: %s\n", strerror(errno));
            result = 1;
        }
        app->capture = NULL;
    }
    if (app->record_mutex_ready) {
        acquisition_destroy(&app->acq);
        app->record_mutex_ready = 0;
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
    free(app);
    return result;
}
