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
#include "sdrgui.h"
#include "view.h"
#include "raygui.h"


static volatile sig_atomic_t signal_stop_requested = 0;

void set_tab(struct app *app, int new_tab);

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
    if (rtlsdr_open(&app->dev, 0) < 0) {
        fprintf(stderr, "Failed to open RTL-SDR receiver index 0.\n");
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
    /* The tuner chip, not the USB bridge: it sets the achievable gains and the
       oscillator whose error the PPM correction compensates, so a capture is
       worth labelling with it. */
    const char *tuner = "unknown";
    switch (rtlsdr_get_tuner_type(app->dev)) {
    case RTLSDR_TUNER_E4000:  tuner = "E4000"; break;
    case RTLSDR_TUNER_FC0012: tuner = "FC0012"; break;
    case RTLSDR_TUNER_FC0013: tuner = "FC0013"; break;
    case RTLSDR_TUNER_FC2580: tuner = "FC2580"; break;
    case RTLSDR_TUNER_R820T:  tuner = "R820T"; break;
    case RTLSDR_TUNER_R828D:  tuner = "R828D"; break;
    default: break;
    }
    snprintf(app->tuner_label, sizeof(app->tuner_label), "%s", tuner);
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














static void recompute_magnitude_bins(struct app *app) {
    size_t capacity;

    if (!app->have_samples || app->pair_count == 0) {
        app->magnitude_bin_count = 0;
        return;
    }
    capacity = app->plot.width > 1.0f ? (size_t)app->plot.width : 1;
    if (capacity > SAMPLE_BLOCK_PAIRS)
        capacity = SAMPLE_BLOCK_PAIRS;
    app->magnitude_bin_count = sdr_dsp_peak_bins(
        app->magnitudes, app->pair_count, app->magnitude_peaks, capacity);
}

static void decay_spectrum_peak(struct app *app, double now) {
    if (!app->spectrum_peak_ready) {
        app->spectrum_peak_time = now;
        return;
    }
    double elapsed = now - app->spectrum_peak_time;
    if (elapsed <= 0.0)
        return;
    float decay = (float)elapsed * PEAK_DECAY_DB_PER_SECOND;
    for (int i = 0; i < SDR_DSP_FFT_SIZE; i++)
        app->spectrum_peak[i] = fmaxf(SDR_DSP_DBFS_FLOOR,
                                     app->spectrum_peak[i] - decay);
    app->spectrum_peak_time = now;
}

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
                              app->applied_sample_rate, app->options.file_path);
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











static void adjust_active_scale(struct app *app, int zoom_in) {
    if (app->view == VIEW_MAGNITUDE) {
        app->magnitude_upper *= zoom_in ? SCALE_FACTOR : 1.0f / SCALE_FACTOR;
        app->magnitude_upper = fmaxf(1.0f,
                                     fminf(app->magnitude_upper,
                                           PHYSICAL_MAGNITUDE_MAX));
    } else if (app->view == VIEW_SPECTRUM) {
        app->spectrum_lower_dbfs += zoom_in ? DB_SCALE_STEP : -DB_SCALE_STEP;
        app->spectrum_lower_dbfs = fmaxf(
            SDR_DSP_DBFS_FLOOR,
            fminf(app->spectrum_lower_dbfs, SPECTRUM_TOP_DBFS - 20.0f));
    } else if (app->view == VIEW_SCATTER) {
        app->scatter_axis_limit *= zoom_in ? SCALE_FACTOR : 1.0f / SCALE_FACTOR;
        app->scatter_axis_limit = fmaxf(0.01f,
                                        fminf(app->scatter_axis_limit, 1.0f));
    } else {
        adjust_waterfall_scale(app, zoom_in);
    }
}


/* --- Top-level tabs (Scope / Decode) --- */

static const char *tab_labels[TAB_COUNT] = { "Scope", "Decode" };

static Rectangle tab_rect(int index) {
    float width = (float)GetScreenWidth();
    return (Rectangle){ width - 512.0f + (float)index * 128.0f, 14.0f,
                        118.0f, 36.0f };
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

/* Enter the GSM decode view: pick the default channel and show it in the
   waterfall above. Priority: the channel a calibration is using, else the last
   channel the user selected, else run a band scan and auto-pick the strongest
   BCCH when it finishes. */
void enter_gsm(struct app *app) {
    if (app->receiver_mode && !app->gsm_return_valid) {
        app->gsm_return_frequency = app->applied_frequency;
        app->gsm_return_valid = 1;
    }
    int arfcn = 0;
    if (app->gsm_cal_arfcn > 0)
        arfcn = app->gsm_cal_arfcn;
    else if (app->scan_selected_arfcn > 0)
        arfcn = app->scan_selected_arfcn;
    if (arfcn > 0) {
        gsm_tune_selected(app, arfcn);
        app->gsm_analysis_mode = 1; /* Default to Burst mode when inspecting */
    } else if (app->receiver_mode) {
        if (start_scan(app) == 0) {
            app->scan_open = 0;
            app->gsm_autoselect_pending = 1;
        }
    }
}

/* Leave the GSM decode view: stop any scan and restore the entry tuning. */
void leave_gsm(struct app *app) {
    app->scan_running = 0;
    app->scan_open = 0;
    app->gsm_autoselect_pending = 0;
    if (app->receiver_mode && app->gsm_return_valid)
        retune_receiver(app, app->gsm_return_frequency, app->applied_ppm);
    app->gsm_return_valid = 0;
    app->gsm_selected_hz = 0.0;
    app->gsm_sch_valid = 0;
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
static void set_decode(struct app *app, int kind) {
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
static void draw_header(const struct app *app) {
    static const char *scope_opts[4] = {
        "1 magnitude", "2 spectrum", "3 I/Q scatter", "4 waterfall"
    };
    static const char *decode_opts[2] = { "1 GSM", "2 ADS-B" };

    DrawText("sdrprobe signal visualizer", 22, 14, 24,
             (Color){ 225, 236, 245, 255 });
    if (app->tab == TAB_SCOPE)
        draw_option_row((int)app->view, scope_opts, 4,
                        "Up/Down scale   Esc quit");
    else
        draw_option_row((int)app->decode, decode_opts, 2, "Esc scope");

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
                              app->applied_sample_rate, app->options.file_path);
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
    while (!signal_stop_requested) {
        if (WindowShouldClose())
            break;

        if (app->settings_open) {
            if (IsKeyPressed(KEY_ESCAPE))
                app->settings_open = 0;
            else
                handle_settings_input(app);
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
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            break;
        }

        double now = GetTime();

        Rectangle new_plot = calculate_plot();
        int resized = IsWindowResized() ||
                      (int)new_plot.width != app->scatter.texture.width ||
                      (int)new_plot.height != app->scatter.texture.height ||
                      (int)new_plot.width != app->waterfall_width ||
                      (int)new_plot.height != app->waterfall_height;
        if (resized) {
            if (recreate_scatter(app, new_plot) < 0) {
                result = -1;
                break;
            }
            if (recreate_waterfall(app, new_plot, 0) < 0) {
                result = -1;
                break;
            }
            recompute_magnitude_bins(app);
        } else {
            app->plot = new_plot;
        }

        if (app->tab == TAB_SCOPE && !app->settings_open &&
            !app->calibration_open) {
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
            if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
                adjust_active_scale(app, 1);
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
                adjust_active_scale(app, 0);
        }

        decay_spectrum_peak(app, now);
        int have_new = consume_latest(&app->acq, &snapshot);
        int spectrum_updated = have_new ? process_block(app, now) : 0;
        if (spectrum_updated) {
            update_waterfall(app);
            update_scan(app);
            update_calibration_measurement(app);
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
            draw_header(app);
            if (app->settings_open)
                draw_settings(app);
        }
        EndDrawing();

        if (snapshot.worker_failed) {
            result = -1;
            break;
        }
    }
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

    app = calloc(1, sizeof(*app));
    if (!app) {
        fprintf(stderr, "Cannot allocate application state.\n");
        return 1;
    }
    app->options = options;
    app->receiver_mode = options.file_path == NULL;
    app->remove_dc = 1;
    app->gsm_const_amplitude = 1; /* constellation shows amplitude by default */
    app->gsm_opt_filter = 1;
    app->gsm_opt_finecfo = 1;
    app->gsm_opt_trellis = 1;
    app->magnitude_lower = 0.0f;
    app->magnitude_upper = 64.0f;
    app->spectrum_lower_dbfs = SDR_DSP_DBFS_FLOOR;
    app->scatter_axis_limit = 0.5f;
    app->waterfall_lower_dbfs = SDR_DSP_DBFS_FLOOR;

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
    if (app->scatter_ready) {
        UnloadRenderTexture(app->scatter);
        app->scatter_ready = 0;
    }
    if (app->waterfall_ready) {
        UnloadTexture(app->waterfall);
        app->waterfall_ready = 0;
    }
    free(app->waterfall_pixels);
    app->waterfall_pixels = NULL;
    free(app->waterfall_dbfs);
    app->waterfall_dbfs = NULL;
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
