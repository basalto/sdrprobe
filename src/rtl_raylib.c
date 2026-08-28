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
#include <time.h>

#include "sdr_dsp.h"
#include "gsm_dsp.h"

#define SAMPLE_BLOCK_BYTES (16 * 16384)
#define SAMPLE_BLOCK_PAIRS (SAMPLE_BLOCK_BYTES / 2)
#define DEFAULT_FREQUENCY 1090000000U
#define DEFAULT_SAMPLE_RATE 2000000U
#define SCATTER_SAMPLES 4096
#define SCATTER_HISTORY_BLOCKS 64
#define SCATTER_HISTORY_SECONDS 1.0
#define PEAK_DECAY_DB_PER_SECOND 20.0f
#define PHYSICAL_MAGNITUDE_MAX 180.31223f
#define SPECTRUM_TOP_DBFS 6.0f
#define SCALE_FACTOR 0.8f
#define DB_SCALE_STEP 10.0f
#define CALIBRATION_RECENT 64
#define CALIBRATION_SETTLE_SECONDS 2.0
#define CALIBRATION_MIN_SECONDS 8.0
#define CALIBRATION_MAX_SEM_PPM 1.0
#define CALIBRATION_VIEW_HALF_WIDTH_HZ 250000.0
#define CALIBRATION_SOURCE_CENTROID 0
#define CALIBRATION_SOURCE_FCCH 1
#define CALIBRATION_FCCH_MISS_LIMIT 12
#define SCAN_BAND_LOWER_HZ 935100000.0
#define SCAN_BAND_UPPER_HZ 959900000.0
#define SCAN_EDGE_MARGIN_HZ 200000.0
#define SCAN_STEP_SETTLE_SECONDS 0.35
#define SCAN_STEP_PROBE_SECONDS 0.45
#define SCAN_SENTINEL_DBFS (-300.0f)
#define SCAN_BCCH_MIN_CONF 0.85f

enum gain_request_kind {
    GAIN_REQUEST_MAX,
    GAIN_REQUEST_AUTO,
    GAIN_REQUEST_NUMERIC
};

enum view_kind {
    VIEW_MAGNITUDE,
    VIEW_SPECTRUM,
    VIEW_SCATTER,
    VIEW_WATERFALL
};

struct options {
    uint32_t frequency;
    uint32_t sample_rate;
    enum gain_request_kind gain_kind;
    int gain_tenths;
    const char *file_path;
    int gain_seen;
    int ppm;
    int ppm_seen;
};

struct latest_block {
    unsigned char data[SAMPLE_BLOCK_BYTES];
    uint32_t len;
    uint64_t generation;
    uint64_t published_blocks;
    uint64_t processed_blocks;
    uint64_t overwritten_blocks;
    uint64_t malformed_blocks;
    int ready;
    int worker_done;
    int worker_failed;
    int worker_reading;
    int stop;
    char worker_error[160];
    pthread_mutex_t mutex;
};

struct slot_snapshot {
    uint64_t published_blocks;
    uint64_t processed_blocks;
    uint64_t overwritten_blocks;
    uint64_t malformed_blocks;
    int worker_done;
    int worker_failed;
    char worker_error[160];
};

struct scatter_block {
    float i[SCATTER_SAMPLES];
    float q[SCATTER_SAMPLES];
    size_t count;
    double time;
};

struct app {
    struct options options;
    struct latest_block latest;
    struct sdr_dsp dsp;
    rtlsdr_dev_t *dev;
    FILE *capture;
    uint64_t capture_bytes;
    pthread_t worker;
    int mutex_ready;
    int worker_started;
    int window_ready;
    int scatter_ready;
    int waterfall_ready;
    int signals_ready;
    int receiver_mode;
    int applied_manual_gain;
    int applied_gain_tenths;
    int applied_ppm;
    int *supported_gains;
    int supported_gain_count;
    uint32_t applied_frequency;
    uint32_t applied_sample_rate;
    char source_label[320];
    struct sigaction old_sigint;
    struct sigaction old_sigterm;

    unsigned char raw[SAMPLE_BLOCK_BYTES];
    unsigned char file_block[SAMPLE_BLOCK_BYTES];
    uint32_t raw_len;
    uint64_t consumed_generation;
    float i_samples[SAMPLE_BLOCK_PAIRS];
    float q_samples[SAMPLE_BLOCK_PAIRS];
    float spectrum_i[SAMPLE_BLOCK_PAIRS];
    float spectrum_q[SAMPLE_BLOCK_PAIRS];
    float magnitudes[SAMPLE_BLOCK_PAIRS];
    float magnitude_peaks[SAMPLE_BLOCK_PAIRS];
    float magnitude_sorted[SAMPLE_BLOCK_PAIRS];
    size_t pair_count;
    size_t magnitude_bin_count;
    float magnitude_min;
    float magnitude_mean;
    float magnitude_max;
    float magnitude_lower;
    float magnitude_upper;
    struct sdr_signal_stats signal_stats;
    int signal_stats_ready;

    float spectrum_average[SDR_DSP_FFT_SIZE];
    float spectrum_candidate[SDR_DSP_FFT_SIZE];
    float spectrum_peak[SDR_DSP_FFT_SIZE];
    float calibration_workspace[SDR_DSP_FFT_SIZE];
    float spectrum_lower_dbfs;
    int spectrum_windows;
    int spectrum_ready;
    int spectrum_peak_ready;
    double spectrum_peak_time;
    size_t scatter_inserted;
    struct scatter_block scatter_history[SCATTER_HISTORY_BLOCKS];
    size_t scatter_history_head;
    size_t scatter_history_count;
    float scatter_axis_limit;
    int have_samples;

    enum view_kind view;
    Rectangle plot;
    RenderTexture2D scatter;
    Texture2D waterfall;
    Color *waterfall_pixels;
    float *waterfall_dbfs;
    int waterfall_capacity;
    int waterfall_width;
    int waterfall_height;
    int waterfall_rows;
    float waterfall_lower_dbfs;

    int settings_open;
    char settings_frequency[32];
    int settings_frequency_length;
    char settings_ppm[16];
    int settings_ppm_length;
    int settings_focus;
    int settings_gain_choice;
    int remove_dc;
    int settings_remove_dc;
    char settings_error[160];

    int calibration_open;
    int calibration_running;
    int calibration_technology;
    int calibration_band;
    char calibration_channel[16];
    int calibration_channel_length;
    uint32_t calibration_expected_hz;
    uint32_t calibration_tune_hz;
    double calibration_measured_hz;
    double calibration_offset_hz;
    int calibration_measurements;
    float calibration_peak_dbfs;
    float calibration_floor_dbfs;
    float calibration_prominence_db;
    double calibration_peak_hz;
    double calibration_started_at;
    double calibration_recent_ppm[CALIBRATION_RECENT];
    int calibration_recent_count;
    int calibration_recent_head;
    double calibration_recent_center;
    double calibration_recent_spread;
    double calibration_recent_sem;
    int calibration_fcch_locked;
    float calibration_fcch_confidence;
    int calibration_source;
    int calibration_fcch_miss;
    int calibration_fcch_hits;
    int calibration_stable;
    uint32_t calibration_return_frequency;
    int calibration_suggested_ppm;
    char calibration_status[160];

    int scan_open;
    int scan_running;
    int scan_step;
    int scan_step_count;
    double scan_step_started_at;
    double scan_first_center_hz;
    double scan_step_hz;
    double scan_accept_half_hz;
    uint32_t scan_return_frequency;
    float scan_power[125];
    float scan_bcch_conf[125];
    int scan_selected_arfcn;
};

static volatile sig_atomic_t signal_stop_requested = 0;

static void on_signal(int signal_number) {
    (void)signal_number;
    signal_stop_requested = 1;
}

static void usage(const char *program) {
    fprintf(stderr,
            "Usage: %s [--frequency Hz|K|M|G] [--sample-rate samples_per_second]\n"
            "          [--gain max|auto|dB] [--ppm signed_integer]\n"
            "          [--file capture.bin]\n",
            program);
}

static int parse_int(const char *text, int *value) {
    char *end = NULL;
    long parsed;
    if (!text || !text[0])
        return -1;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || !end || *end || parsed < INT_MIN || parsed > INT_MAX)
        return -1;
    *value = (int)parsed;
    return 0;
}

static int set_frequency_correction(rtlsdr_dev_t *dev, int ppm) {
    if (rtlsdr_get_freq_correction(dev) == ppm)
        return 0;
    return rtlsdr_set_freq_correction(dev, ppm);
}

static int parse_u32(const char *text, uint32_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    if (!text || !text[0] || text[0] == '-' || text[0] == '+')
        return -1;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || !end || *end || parsed == 0 || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

static int parse_frequency(const char *text, uint32_t *value) {
    char *end = NULL;
    double parsed;
    double multiplier = 1.0;
    double hz;

    if (!text || !text[0] || text[0] == '-' || text[0] == '+')
        return -1;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno || !end || end == text || !isfinite(parsed) || parsed <= 0.0)
        return -1;
    if (*end) {
        if (end[1])
            return -1;
        switch (*end) {
        case 'k':
        case 'K':
            multiplier = 1000.0;
            break;
        case 'm':
        case 'M':
            multiplier = 1000000.0;
            break;
        case 'g':
        case 'G':
            multiplier = 1000000000.0;
            break;
        default:
            return -1;
        }
    }
    hz = parsed * multiplier;
    if (!isfinite(hz) || hz < 1.0 || hz > UINT32_MAX ||
        fabs(hz - round(hz)) > 1e-6)
        return -1;
    *value = (uint32_t)llround(hz);
    return 0;
}

static int parse_numeric_gain(const char *text, int *tenths) {
    const char *p = text;
    char *end = NULL;
    double value;
    double scaled;
    long long rounded;
    int digits = 0;
    int dots = 0;

    if (!p || !*p)
        return -1;
    if (*p == '+' || *p == '-')
        p++;
    for (; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            digits++;
        } else if (*p == '.' && !dots) {
            dots++;
        } else {
            return -1;
        }
    }
    if (!digits)
        return -1;

    errno = 0;
    value = strtod(text, &end);
    if (errno || !end || *end || !isfinite(value))
        return -1;
    scaled = value * 10.0;
    if (scaled < (double)INT_MIN || scaled > (double)INT_MAX)
        return -1;
    rounded = llround(scaled);
    if (fabs(scaled - (double)rounded) > 1e-7)
        return -1;
    *tenths = (int)rounded;
    return 0;
}

static int parse_options(int argc, char **argv, struct options *options) {
    int frequency_seen = 0;
    int sample_rate_seen = 0;
    int file_seen = 0;

    memset(options, 0, sizeof(*options));
    options->frequency = DEFAULT_FREQUENCY;
    options->sample_rate = DEFAULT_SAMPLE_RATE;
    options->gain_kind = GAIN_REQUEST_MAX;

    for (int i = 1; i < argc; i++) {
        const char *option = argv[i];
        const char *argument;

        if (strcmp(option, "--frequency") == 0) {
            if (frequency_seen || i + 1 >= argc)
                return -1;
            argument = argv[++i];
            if (parse_frequency(argument, &options->frequency) < 0)
                return -1;
            frequency_seen = 1;
        } else if (strcmp(option, "--sample-rate") == 0) {
            if (sample_rate_seen || i + 1 >= argc)
                return -1;
            argument = argv[++i];
            if (parse_u32(argument, &options->sample_rate) < 0)
                return -1;
            sample_rate_seen = 1;
        } else if (strcmp(option, "--gain") == 0) {
            if (options->gain_seen || i + 1 >= argc)
                return -1;
            argument = argv[++i];
            if (strcmp(argument, "max") == 0) {
                options->gain_kind = GAIN_REQUEST_MAX;
            } else if (strcmp(argument, "auto") == 0) {
                options->gain_kind = GAIN_REQUEST_AUTO;
            } else {
                if (parse_numeric_gain(argument, &options->gain_tenths) < 0)
                    return -1;
                options->gain_kind = GAIN_REQUEST_NUMERIC;
            }
            options->gain_seen = 1;
        } else if (strcmp(option, "--file") == 0) {
            if (file_seen || i + 1 >= argc)
                return -1;
            options->file_path = argv[++i];
            if (!options->file_path[0])
                return -1;
            file_seen = 1;
        } else if (strcmp(option, "--ppm") == 0) {
            if (options->ppm_seen || i + 1 >= argc ||
                parse_int(argv[++i], &options->ppm) < 0 ||
                options->ppm < -1000 || options->ppm > 1000)
                return -1;
            options->ppm_seen = 1;
        } else {
            return -1;
        }
    }

    if (options->file_path && options->gain_seen)
        return -1;
    return 0;
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
    app->capture_bytes = (uint64_t)size & ~UINT64_C(1);
    app->applied_frequency = app->options.frequency;
    app->applied_sample_rate = app->options.sample_rate;
    app->applied_ppm = app->options.ppm;
    snprintf(app->source_label, sizeof(app->source_label), "capture: %s",
             app->options.file_path);
    return 0;
}

static void publish_block(struct app *app, const unsigned char *data,
                          uint32_t len) {
    struct latest_block *latest = &app->latest;
    uint32_t valid_len;

    pthread_mutex_lock(&latest->mutex);
    if (latest->stop) {
        pthread_mutex_unlock(&latest->mutex);
        return;
    }
    if (len == 0 || len > SAMPLE_BLOCK_BYTES) {
        latest->malformed_blocks++;
        pthread_mutex_unlock(&latest->mutex);
        return;
    }
    valid_len = len & ~UINT32_C(1);
    if (valid_len != len)
        latest->malformed_blocks++;
    if (valid_len == 0) {
        pthread_mutex_unlock(&latest->mutex);
        return;
    }
    if (latest->ready)
        latest->overwritten_blocks++;
    memcpy(latest->data, data, valid_len);
    latest->len = valid_len;
    latest->generation++;
    latest->published_blocks++;
    latest->ready = 1;
    pthread_mutex_unlock(&latest->mutex);
}

static void finish_worker(struct app *app, const char *error) {
    pthread_mutex_lock(&app->latest.mutex);
    app->latest.worker_reading = 0;
    app->latest.worker_done = 1;
    if (error) {
        app->latest.worker_failed = 1;
        snprintf(app->latest.worker_error, sizeof(app->latest.worker_error),
                 "%s", error);
    }
    pthread_mutex_unlock(&app->latest.mutex);
}

static int begin_worker_read(struct app *app) {
    int begin;

    pthread_mutex_lock(&app->latest.mutex);
    begin = !app->latest.stop;
    if (begin)
        app->latest.worker_reading = 1;
    else
        app->latest.worker_done = 1;
    pthread_mutex_unlock(&app->latest.mutex);
    return begin;
}

static void receiver_callback(unsigned char *buffer, uint32_t len, void *ctx) {
    struct app *app = ctx;

    publish_block(app, buffer, len);
}

static void *receiver_worker(void *arg) {
    struct app *app = arg;
    int result;
    char error[160];

    if (!begin_worker_read(app))
        return NULL;
    result = rtlsdr_read_async(app->dev, receiver_callback, app, 0,
                               SAMPLE_BLOCK_BYTES);
    pthread_mutex_lock(&app->latest.mutex);
    int stopped = app->latest.stop;
    pthread_mutex_unlock(&app->latest.mutex);
    if (!stopped) {
        if (result < 0)
            snprintf(error, sizeof(error),
                     "RTL-SDR asynchronous read failed (%d)", result);
        else
            snprintf(error, sizeof(error),
                     "RTL-SDR asynchronous acquisition ended unexpectedly");
        finish_worker(app, error);
    } else {
        finish_worker(app, NULL);
    }
    return NULL;
}

static struct timespec playback_deadline(struct timespec start,
                                         uint64_t pairs, uint32_t rate) {
    struct timespec deadline = start;
    uint64_t seconds = pairs / rate;
    uint64_t remainder = pairs % rate;
    uint64_t nanoseconds = remainder * UINT64_C(1000000000) / rate;

    deadline.tv_sec += (time_t)seconds;
    deadline.tv_nsec += (long)nanoseconds;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

static int compare_timespec(struct timespec left, struct timespec right) {
    if (left.tv_sec != right.tv_sec)
        return left.tv_sec < right.tv_sec ? -1 : 1;
    return (left.tv_nsec > right.tv_nsec) - (left.tv_nsec < right.tv_nsec);
}

static int worker_stop_requested(struct app *app) {
    int stop;

    pthread_mutex_lock(&app->latest.mutex);
    stop = app->latest.stop;
    pthread_mutex_unlock(&app->latest.mutex);
    return stop;
}

static void request_worker_stop(struct app *app) {
    if (!app->mutex_ready)
        return;
    pthread_mutex_lock(&app->latest.mutex);
    app->latest.stop = 1;
    pthread_mutex_unlock(&app->latest.mutex);
}

static int sleep_until(struct app *app, struct timespec deadline) {
    while (!worker_stop_requested(app)) {
        struct timespec now;
        struct timespec wake;
        int sleep_result;

        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return errno;
        if (compare_timespec(now, deadline) >= 0)
            return 0;
        wake = now;
        wake.tv_nsec += 20000000L;
        if (wake.tv_nsec >= 1000000000L) {
            wake.tv_sec++;
            wake.tv_nsec -= 1000000000L;
        }
        if (compare_timespec(deadline, wake) < 0)
            wake = deadline;
        do {
            sleep_result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                           &wake, NULL);
        } while (sleep_result == EINTR && !worker_stop_requested(app));
        if (sleep_result != 0 && sleep_result != EINTR)
            return sleep_result;
    }
    return 0;
}

static void *file_worker(void *arg) {
    struct app *app = arg;
    unsigned char *block = app->file_block;
    uint64_t position = 0;
    uint64_t published_pairs = 0;
    struct timespec start;
    char error[160];

    if (!begin_worker_read(app))
        return NULL;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        snprintf(error, sizeof(error), "Cannot read monotonic clock: %s",
                 strerror(errno));
        finish_worker(app, error);
        return NULL;
    }

    while (!worker_stop_requested(app)) {
        size_t filled = 0;

        while (filled < SAMPLE_BLOCK_BYTES && !worker_stop_requested(app)) {
            uint64_t available = app->capture_bytes - position;
            size_t wanted = SAMPLE_BLOCK_BYTES - filled;
            if (available < wanted)
                wanted = (size_t)available;
            size_t got = fread(block + filled, 1, wanted, app->capture);
            if (got != wanted) {
                if (ferror(app->capture))
                    snprintf(error, sizeof(error), "Capture read failed: %s",
                             strerror(errno));
                else
                    snprintf(error, sizeof(error),
                             "Capture ended before its measured length");
                finish_worker(app, error);
                return NULL;
            }
            filled += got;
            position += got;
            if (position == app->capture_bytes) {
                if (fseeko(app->capture, 0, SEEK_SET) != 0) {
                    snprintf(error, sizeof(error), "Cannot loop capture: %s",
                             strerror(errno));
                    finish_worker(app, error);
                    return NULL;
                }
                position = 0;
            }
        }
        if (worker_stop_requested(app))
            break;

        publish_block(app, block, SAMPLE_BLOCK_BYTES);
        published_pairs += SAMPLE_BLOCK_PAIRS;
        struct timespec deadline = playback_deadline(
            start, published_pairs, app->applied_sample_rate);
        int sleep_result = sleep_until(app, deadline);
        if (sleep_result != 0) {
            snprintf(error, sizeof(error), "Capture pacing failed: %s",
                     strerror(sleep_result));
            finish_worker(app, error);
            return NULL;
        }
    }

    finish_worker(app, NULL);
    return NULL;
}

static int consume_latest(struct app *app, struct slot_snapshot *snapshot) {
    struct latest_block *latest = &app->latest;
    int have_new = 0;

    pthread_mutex_lock(&latest->mutex);
    if (latest->ready && latest->generation != app->consumed_generation) {
        memcpy(app->raw, latest->data, latest->len);
        app->raw_len = latest->len;
        app->consumed_generation = latest->generation;
        latest->ready = 0;
        latest->processed_blocks++;
        have_new = 1;
    }
    snapshot->published_blocks = latest->published_blocks;
    snapshot->processed_blocks = latest->processed_blocks;
    snapshot->overwritten_blocks = latest->overwritten_blocks;
    snapshot->malformed_blocks = latest->malformed_blocks;
    snapshot->worker_done = latest->worker_done;
    snapshot->worker_failed = latest->worker_failed;
    snprintf(snapshot->worker_error, sizeof(snapshot->worker_error), "%s",
             latest->worker_error);
    pthread_mutex_unlock(&latest->mutex);
    return have_new;
}

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
        app->raw, app->raw_len, app->i_samples, app->q_samples,
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

static Rectangle calculate_plot(void) {
    float width = (float)GetScreenWidth();
    float height = (float)GetScreenHeight();
    Rectangle plot = { 82.0f, 210.0f, width - 112.0f, height - 278.0f };

    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
    return plot;
}

static void clear_scatter(struct app *app) {
    BeginTextureMode(app->scatter);
    ClearBackground(BLANK);
    EndTextureMode();
    app->scatter_inserted = 0;
    app->scatter_history_head = 0;
    app->scatter_history_count = 0;
}

static int recreate_scatter(struct app *app, Rectangle plot) {
    int width = (int)plot.width;
    int height = (int)plot.height;
    RenderTexture2D replacement = LoadRenderTexture(width, height);

    if (replacement.id == 0 || replacement.texture.id == 0) {
        fprintf(stderr, "Failed to create %dx%d I/Q render texture.\n",
                width, height);
        if (replacement.id != 0 || replacement.texture.id != 0)
            UnloadRenderTexture(replacement);
        return -1;
    }
    if (app->scatter_ready)
        UnloadRenderTexture(app->scatter);
    app->scatter = replacement;
    app->scatter_ready = 1;
    app->plot = plot;
    clear_scatter(app);
    return 0;
}

static Color waterfall_color(const struct app *app, float dbfs);

static void render_waterfall(struct app *app) {
    if (!app->waterfall_ready)
        return;
    size_t pixel_count = (size_t)app->waterfall_width *
                         (size_t)app->waterfall_height;
    for (size_t n = 0; n < pixel_count; n++)
        app->waterfall_pixels[n] = (Color){ 6, 10, 17, 255 };

    int rows = app->waterfall_rows < app->waterfall_height
                   ? app->waterfall_rows
                   : app->waterfall_height;
    for (int y = 0; y < rows; y++) {
        const float *row = app->waterfall_dbfs +
                           (size_t)y * SDR_DSP_FFT_SIZE;
        for (int x = 0; x < app->waterfall_width; x++) {
            float position = app->waterfall_width == 1
                                 ? 0.0f
                                 : (float)x * (SDR_DSP_FFT_SIZE - 1) /
                                       (float)(app->waterfall_width - 1);
            int lower = (int)position;
            int upper = lower < SDR_DSP_FFT_SIZE - 1 ? lower + 1 : lower;
            float fraction = position - lower;
            float dbfs = row[lower] * (1.0f - fraction) +
                         row[upper] * fraction;
            app->waterfall_pixels[(size_t)y * app->waterfall_width + x] =
                waterfall_color(app, dbfs);
        }
    }
    UpdateTexture(app->waterfall, app->waterfall_pixels);
}

static int recreate_waterfall(struct app *app, Rectangle plot,
                              int clear_history) {
    int width = (int)plot.width;
    int height = (int)plot.height;
    Color *pixels = calloc((size_t)width * (size_t)height, sizeof(*pixels));
    if (!pixels) {
        fprintf(stderr, "Failed to allocate %dx%d waterfall pixels.\n",
                width, height);
        return -1;
    }

    Image image = GenImageColor(width, height, (Color){ 6, 10, 17, 255 });
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0) {
        fprintf(stderr, "Failed to create %dx%d waterfall texture.\n",
                width, height);
        free(pixels);
        return -1;
    }

    if (!app->waterfall_dbfs || height > app->waterfall_capacity) {
        float *history = realloc(
            app->waterfall_dbfs,
            (size_t)height * SDR_DSP_FFT_SIZE * sizeof(*history));
        if (!history) {
            fprintf(stderr, "Failed to allocate %d waterfall history rows.\n",
                    height);
            UnloadTexture(texture);
            free(pixels);
            return -1;
        }
        app->waterfall_dbfs = history;
        app->waterfall_capacity = height;
    }
    if (app->waterfall_ready)
        UnloadTexture(app->waterfall);
    free(app->waterfall_pixels);
    app->waterfall = texture;
    app->waterfall_pixels = pixels;
    app->waterfall_width = width;
    app->waterfall_height = height;
    if (clear_history)
        app->waterfall_rows = 0;
    else if (app->waterfall_rows > height)
        app->waterfall_rows = height;
    app->waterfall_ready = 1;
    render_waterfall(app);
    return 0;
}

static Color waterfall_color(const struct app *app, float dbfs) {
    float level = (dbfs - app->waterfall_lower_dbfs) /
                  (SPECTRUM_TOP_DBFS - app->waterfall_lower_dbfs);
    if (level < 0.0f)
        level = 0.0f;
    if (level > 1.0f)
        level = 1.0f;

    if (level < 0.35f) {
        float t = level / 0.35f;
        return (Color){ (unsigned char)(5 + 20 * t),
                        (unsigned char)(8 + 45 * t),
                        (unsigned char)(20 + 95 * t), 255 };
    }
    if (level < 0.65f) {
        float t = (level - 0.35f) / 0.30f;
        return (Color){ (unsigned char)(25 + 210 * t),
                        (unsigned char)(53 + 37 * t),
                        (unsigned char)(115 - 90 * t), 255 };
    }
    float t = (level - 0.65f) / 0.35f;
    return (Color){ (unsigned char)(235 + 20 * t),
                    (unsigned char)(90 + 165 * t),
                    (unsigned char)(25 + 205 * t), 255 };
}

static void update_waterfall(struct app *app) {
    if (!app->waterfall_ready || !app->spectrum_ready)
        return;

    int retained = app->waterfall_rows < app->waterfall_capacity
                       ? app->waterfall_rows
                       : app->waterfall_capacity - 1;
    if (retained > 0)
        memmove(app->waterfall_dbfs + SDR_DSP_FFT_SIZE,
                app->waterfall_dbfs,
                (size_t)retained * SDR_DSP_FFT_SIZE *
                    sizeof(*app->waterfall_dbfs));
    memcpy(app->waterfall_dbfs, app->spectrum_average,
           SDR_DSP_FFT_SIZE * sizeof(*app->waterfall_dbfs));
    if (app->waterfall_rows < app->waterfall_height)
        app->waterfall_rows++;
    render_waterfall(app);
}

static int plot_cursor(Rectangle plot, float *x_fraction, float *y_fraction,
                       Vector2 *mouse) {
    *mouse = GetMousePosition();
    if (!CheckCollisionPointRec(*mouse, plot))
        return 0;
    *x_fraction = (mouse->x - plot.x) / plot.width;
    *y_fraction = (mouse->y - plot.y) / plot.height;
    return 1;
}

static void draw_cursor_readout(Rectangle plot, Vector2 mouse,
                                const char *text) {
    const int font_size = 16;
    const int padding = 7;
    int text_width = MeasureText(text, font_size);
    int box_width = text_width + padding * 2;
    int box_height = font_size + padding * 2;
    int box_x = (int)mouse.x + 14;
    int box_y = (int)mouse.y - box_height - 10;

    if (box_x + box_width > (int)(plot.x + plot.width))
        box_x = (int)mouse.x - box_width - 14;
    if (box_y < (int)plot.y)
        box_y = (int)mouse.y + 10;

    DrawLine((int)plot.x, (int)mouse.y, (int)(plot.x + plot.width),
             (int)mouse.y, (Color){ 255, 206, 92, 150 });
    DrawLine((int)mouse.x, (int)plot.y, (int)mouse.x,
             (int)(plot.y + plot.height), (Color){ 255, 206, 92, 150 });
    DrawCircleV(mouse, 3.0f, (Color){ 255, 225, 130, 255 });
    DrawRectangle(box_x, box_y, box_width, box_height,
                  (Color){ 7, 12, 19, 235 });
    DrawRectangleLines(box_x, box_y, box_width, box_height,
                       (Color){ 255, 190, 65, 255 });
    DrawText(text, box_x + padding, box_y + padding, font_size,
             (Color){ 255, 231, 170, 255 });
}

static void format_frequency_offset(char *text, size_t size, double offset) {
    double absolute = fabs(offset);
    if (absolute >= 1000000.0)
        snprintf(text, size, "%+.3f MHz", offset / 1000000.0);
    else if (absolute >= 1000.0)
        snprintf(text, size, "%+.3f kHz", offset / 1000.0);
    else
        snprintf(text, size, "%+.0f Hz", offset);
}

#define GSM900_BASE_HZ 935000000.0
#define GSM900_ARFCN_SPACING_HZ 200000.0

static void draw_waterfall(const struct app *app, int calibration_mode) {
    char text[256];
    int channel_axis = calibration_mode && app->calibration_technology == 0;
    Rectangle plot = app->plot;
    Rectangle source = { 0.0f, 0.0f, (float)app->waterfall.width,
                         (float)app->waterfall.height };
    double full_lower = (double)app->applied_frequency -
                        (double)app->applied_sample_rate / 2.0;
    double full_upper = (double)app->applied_frequency +
                        (double)app->applied_sample_rate / 2.0;
    double lower_frequency = full_lower;
    double upper_frequency = full_upper;
    if (calibration_mode && app->calibration_expected_hz > 0 &&
        full_upper > full_lower) {
        double zoom_lower = (double)app->calibration_expected_hz -
                            CALIBRATION_VIEW_HALF_WIDTH_HZ;
        double zoom_upper = (double)app->calibration_expected_hz +
                            CALIBRATION_VIEW_HALF_WIDTH_HZ;
        if (zoom_lower < full_lower)
            zoom_lower = full_lower;
        if (zoom_upper > full_upper)
            zoom_upper = full_upper;
        if (zoom_upper - zoom_lower > 1.0) {
            lower_frequency = zoom_lower;
            upper_frequency = zoom_upper;
            source.x = (float)((zoom_lower - full_lower) /
                               (full_upper - full_lower)) *
                       (float)app->waterfall.width;
            source.width = (float)((zoom_upper - zoom_lower) /
                                   (full_upper - full_lower)) *
                           (float)app->waterfall.width;
        }
    }
    const double frequency_steps[] = {
        1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0,
        100000.0, 200000.0, 500000.0, 1000000.0, 2000000.0,
        5000000.0, 10000000.0, 20000000.0, 50000000.0
    };
    double frequency_step = frequency_steps[
        sizeof(frequency_steps) / sizeof(frequency_steps[0]) - 1];
    double minimum_frequency_step = app->applied_sample_rate * 105.0 /
                                    fmaxf(plot.width, 1.0f);
    for (size_t i = 0;
         i < sizeof(frequency_steps) / sizeof(frequency_steps[0]); i++) {
        if (frequency_steps[i] >= minimum_frequency_step) {
            frequency_step = frequency_steps[i];
            break;
        }
    }

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawTexturePro(app->waterfall, source, plot, (Vector2){ 0.0f, 0.0f },
                   0.0f, WHITE);

    double first_frequency = ceil(lower_frequency / frequency_step) *
                             frequency_step;
    if (!channel_axis) {
    for (double frequency = first_frequency;
         frequency <= upper_frequency + frequency_step * 1e-6;
         frequency += frequency_step) {
        float x = plot.x + (float)((frequency - lower_frequency) /
                                   (upper_frequency - lower_frequency)) *
                                   plot.width;
        DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                 (Color){ 170, 190, 200, 70 });
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + 7,
                 (Color){ 126, 151, 166, 255 });
        snprintf(text, sizeof(text), "%.3f", frequency / 1000000.0);
        DrawText(text, (int)x - MeasureText(text, 16) / 2,
                 (int)(plot.y + plot.height) + 11, 16,
                 (Color){ 151, 174, 188, 255 });

        double minor_step = frequency_step / 5.0;
        for (int minor = 1; minor < 5; minor++) {
            double minor_frequency = frequency + minor * minor_step;
            if (minor_frequency >= upper_frequency)
                break;
            float minor_x = plot.x +
                            (float)((minor_frequency - lower_frequency) /
                                    (upper_frequency - lower_frequency)) *
                                plot.width;
            if (minor_x <= plot.x || minor_x >= plot.x + plot.width)
                continue;
            DrawLine((int)minor_x, (int)plot.y, (int)minor_x,
                     (int)(plot.y + plot.height),
                     (Color){ 100, 125, 140, 32 });
            DrawLine((int)minor_x, (int)(plot.y + plot.height),
                     (int)minor_x, (int)(plot.y + plot.height) + 4,
                     (Color){ 70, 91, 105, 255 });
        }
    }
    } else {
        double channel_px = (double)plot.width * GSM900_ARFCN_SPACING_HZ /
                            (upper_frequency - lower_frequency);
        int label_stride = 1;
        while (channel_px * label_stride < 42.0)
            label_stride++;
        int first_arfcn = (int)ceil((lower_frequency - GSM900_BASE_HZ) /
                                    GSM900_ARFCN_SPACING_HZ);
        if (first_arfcn < 1)
            first_arfcn = 1;
        for (int arfcn = first_arfcn; arfcn <= 124; arfcn++) {
            double frequency = GSM900_BASE_HZ +
                               arfcn * GSM900_ARFCN_SPACING_HZ;
            if (frequency > upper_frequency)
                break;
            float x = plot.x + (float)((frequency - lower_frequency) /
                                       (upper_frequency - lower_frequency)) *
                                       plot.width;
            int labeled = ((arfcn - first_arfcn) % label_stride) == 0;
            DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                     labeled ? (Color){ 170, 190, 200, 70 }
                             : (Color){ 100, 125, 140, 32 });
            DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                     (int)(plot.y + plot.height) + (labeled ? 7 : 4),
                     labeled ? (Color){ 126, 151, 166, 255 }
                             : (Color){ 70, 91, 105, 255 });
            if (labeled) {
                snprintf(text, sizeof(text), "%d", arfcn);
                DrawText(text, (int)x - MeasureText(text, 16) / 2,
                         (int)(plot.y + plot.height) + 11, 16,
                         (Color){ 151, 174, 188, 255 });
            }
        }
    }

    double row_seconds = app->pair_count > 0
                             ? (double)app->pair_count /
                                   app->applied_sample_rate
                             : (double)SAMPLE_BLOCK_PAIRS /
                                   app->applied_sample_rate;
    double visible_seconds = app->waterfall_rows * row_seconds;
    int time_divisions = plot.height >= 400.0f ? 4 : 2;
    for (int division = 0; division <= time_divisions; division++) {
        float y = plot.y + plot.height * division / (float)time_divisions;
        double age = visible_seconds * division / (double)time_divisions;
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 (Color){ 170, 190, 200, division == 0 ? 100 : 45 });
        snprintf(text, sizeof(text), division == 0 ? "now" : "-%.1f s", age);
        DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
                 (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
    }

    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });
    if (!calibration_mode)
        DrawText("time (newest at top)", (int)plot.x, (int)plot.y - 25, 16,
                 (Color){ 151, 174, 188, 255 });
    if (channel_axis)
        snprintf(text, sizeof(text),
                 "GSM 900 ARFCN (200 kHz spacing)   visible history %.1f s   color %.0f..%.0f dBFS",
                 visible_seconds, app->waterfall_lower_dbfs,
                 SPECTRUM_TOP_DBFS);
    else
        snprintf(text, sizeof(text),
                 "frequency (MHz), major %.3f MHz   visible history %.1f s   color %.0f..%.0f dBFS",
                 frequency_step / 1000000.0, visible_seconds,
                 app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
             (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        double frequency = lower_frequency +
                           x_fraction * (upper_frequency - lower_frequency);
        double age = y_fraction * app->waterfall_height * row_seconds;
        char offset[40];
        format_frequency_offset(offset, sizeof(offset),
                                frequency - app->applied_frequency);
        if (channel_axis) {
            double channel = (frequency - GSM900_BASE_HZ) /
                             GSM900_ARFCN_SPACING_HZ;
            long arfcn = lround(channel);
            if (arfcn >= 1 && arfcn <= 124)
                snprintf(text, sizeof(text),
                         "ARFCN %ld   frequency %.6f MHz   age %.2f s",
                         arfcn, frequency / 1000000.0, age);
            else
                snprintf(text, sizeof(text),
                         "outside GSM 900   frequency %.6f MHz   age %.2f s",
                         frequency / 1000000.0, age);
        } else {
            snprintf(text, sizeof(text),
                     "frequency %.6f MHz   offset %s   age %.2f s",
                     frequency / 1000000.0, offset, age);
        }
        draw_cursor_readout(plot, mouse, text);
    }
}

static void update_scatter(struct app *app, double now, int insert) {
    if (insert && app->pair_count > 0) {
        struct scatter_block *block =
            &app->scatter_history[app->scatter_history_head];
        block->count = app->pair_count < SCATTER_SAMPLES
                           ? app->pair_count
                           : SCATTER_SAMPLES;
        block->time = now;
        for (size_t n = 0; n < block->count; n++) {
            size_t index = block->count == 1
                               ? 0
                               : n * (app->pair_count - 1) /
                                     (block->count - 1);
            block->i[n] = app->i_samples[index] / 127.5f;
            block->q[n] = app->q_samples[index] / 127.5f;
        }
        app->scatter_inserted = block->count;
        app->scatter_history_head =
            (app->scatter_history_head + 1) % SCATTER_HISTORY_BLOCKS;
        if (app->scatter_history_count < SCATTER_HISTORY_BLOCKS)
            app->scatter_history_count++;
    }

    while (app->scatter_history_count > 0) {
        size_t oldest = (app->scatter_history_head + SCATTER_HISTORY_BLOCKS -
                         app->scatter_history_count) %
                        SCATTER_HISTORY_BLOCKS;
        if (now - app->scatter_history[oldest].time <=
            SCATTER_HISTORY_SECONDS)
            break;
        app->scatter_history_count--;
    }

    size_t oldest = (app->scatter_history_head + SCATTER_HISTORY_BLOCKS -
                     app->scatter_history_count) %
                    SCATTER_HISTORY_BLOCKS;

    BeginTextureMode(app->scatter);
    ClearBackground(BLANK);
    for (size_t b = 0; b < app->scatter_history_count; b++) {
        const struct scatter_block *block =
            &app->scatter_history[(oldest + b) % SCATTER_HISTORY_BLOCKS];
        float age = (float)(now - block->time);
        float age_alpha = 1.0f - age / (float)SCATTER_HISTORY_SECONDS;
        if (age_alpha < 0.0f)
            age_alpha = 0.0f;
        for (size_t n = 0; n < block->count; n++) {
            float radial = hypotf(block->i[n], block->q[n]) /
                           app->scatter_axis_limit;
            if (radial > 1.0f)
                radial = 1.0f;
            float emphasis = sqrtf(radial);
            float x = (block->i[n] / app->scatter_axis_limit + 1.0f) *
                      0.5f * (float)(app->scatter.texture.width - 1);
            float y = (1.0f - block->q[n] / app->scatter_axis_limit) *
                      0.5f * (float)(app->scatter.texture.height - 1);
            float persistence = 0.30f + 0.70f * age_alpha;
            int alpha = (int)((135.0f + 120.0f * emphasis) * persistence);
            if (alpha < 1)
                continue;
            DrawRectangle((int)x - 1, (int)y - 1, 3, 3,
                          (Color){ 255,
                                   (unsigned char)(145 + 100 * emphasis),
                                   (unsigned char)(35 + 45 * emphasis),
                                   (unsigned char)alpha });
        }
    }
    EndTextureMode();
}

static const char *view_name(enum view_kind view) {
    if (view == VIEW_SPECTRUM)
        return "spectrum";
    if (view == VIEW_SCATTER)
        return "I/Q scatter";
    if (view == VIEW_WATERFALL)
        return "waterfall";
    return "magnitude";
}

static void draw_base_hud(const struct app *app,
                           const struct slot_snapshot *snapshot) {
    char text[640];
    const char *state = snapshot->worker_failed
                            ? "failed"
                            : snapshot->worker_done ? "done" : "running";
    char gain[64];

    if (!app->receiver_mode) {
        snprintf(gain, sizeof(gain), "capture");
    } else if (!app->applied_manual_gain) {
        snprintf(gain, sizeof(gain), "auto");
    } else {
        snprintf(gain, sizeof(gain), "%.1f dB",
                 app->applied_gain_tenths / 10.0);
    }

    DrawText("rtl_raylib signal visualizer", 22, 16, 24,
             (Color){ 225, 236, 245, 255 });
    DrawText("1 magnitude   2 spectrum   3 I/Q scatter   4 waterfall   Up/Down scale   Esc quit",
             22, 47, 17, (Color){ 127, 151, 170, 255 });
    snprintf(text, sizeof(text), "source: %s   state: %s", app->source_label,
             state);
    DrawText(text, 22, 78, 17, (Color){ 187, 205, 216, 255 });
    snprintf(text, sizeof(text),
              "center: %.6f MHz   rate: %u S/s   gain: %s   PPM: %+d   DC filter: %s   view: %s   FPS: %d",
              app->applied_frequency / 1000000.0, app->applied_sample_rate,
              gain, app->applied_ppm, app->remove_dc ? "on" : "off",
              view_name(app->view), GetFPS());
    DrawText(text, 22, 103, 17, (Color){ 187, 205, 216, 255 });
    snprintf(text, sizeof(text),
             "blocks published: %llu   processed: %llu   overwritten: %llu   malformed: %llu   worker: %s",
             (unsigned long long)snapshot->published_blocks,
             (unsigned long long)snapshot->processed_blocks,
             (unsigned long long)snapshot->overwritten_blocks,
             (unsigned long long)snapshot->malformed_blocks, state);
    DrawText(text, 22, 128, 17, (Color){ 187, 205, 216, 255 });

    if (snapshot->worker_failed) {
        snprintf(text, sizeof(text), "acquisition error: %s",
                 snapshot->worker_error);
        DrawText(text, 22, 154, 17, (Color){ 255, 104, 104, 255 });
    } else if (!app->have_samples) {
        DrawText("waiting for samples", 22, 154, 17,
                  (Color){ 250, 190, 74, 255 });
    } else if (app->signal_stats_ready) {
        const struct sdr_signal_stats *stats = &app->signal_stats;
        const char *quality = "healthy";
        Color quality_color = (Color){ 90, 220, 164, 255 };
        if (stats->clipping_percent >= 0.1f || stats->headroom_db < 1.0f) {
            quality = "clipping";
            quality_color = (Color){ 255, 102, 94, 255 };
        } else if (stats->clipping_percent > 0.0f ||
                   stats->headroom_db < 3.0f) {
            quality = "low headroom";
            quality_color = (Color){ 250, 190, 74, 255 };
        }
        snprintf(text, sizeof(text),
                 "noise (p10) %.2f   signal (p99.5) %.2f   estimated SNR %.1f dB   clipping %.4f%%   headroom %.1f dB   %s",
                 stats->noise_magnitude, stats->signal_magnitude,
                 stats->snr_db, stats->clipping_percent,
                 stats->headroom_db, quality);
        DrawText(text, 22, 154, 17, quality_color);
    }
}

static void draw_plot_frame(Rectangle plot, int quarter_grid) {
    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    if (quarter_grid) {
        for (int i = 1; i < 4; i++) {
            int y = (int)(plot.y + plot.height * (float)i / 4.0f);
            DrawLine((int)plot.x, y, (int)(plot.x + plot.width), y,
                     (Color){ 31, 47, 59, 255 });
        }
    }
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });
}

static float plot_y(Rectangle plot, float value, float lower, float upper) {
    float fraction = (value - lower) / (upper - lower);
    if (fraction < 0.0f)
        fraction = 0.0f;
    if (fraction > 1.0f)
        fraction = 1.0f;
    return plot.y + plot.height * (1.0f - fraction);
}

static void draw_magnitude(const struct app *app) {
    char text[256];
    Rectangle plot = app->plot;

    draw_plot_frame(plot, 1);
    snprintf(text, sizeof(text), "%.2f", app->have_samples
                                              ? app->magnitude_upper
                                              : PHYSICAL_MAGNITUDE_MAX);
    DrawText(text, 12, (int)plot.y - 8, 16, (Color){ 151, 174, 188, 255 });
    snprintf(text, sizeof(text), "%.2f", app->have_samples
                                              ? app->magnitude_lower
                                              : 0.0f);
    DrawText(text, 12, (int)(plot.y + plot.height) - 8, 16,
             (Color){ 151, 174, 188, 255 });
    DrawText("magnitude (sample units)", (int)plot.x, (int)plot.y - 25, 16,
             (Color){ 151, 174, 188, 255 });

    if (app->magnitude_bin_count > 0) {
        if (app->magnitude_bin_count == 1) {
            DrawCircleV((Vector2){ plot.x,
                                   plot_y(plot, app->magnitude_peaks[0],
                                          app->magnitude_lower,
                                          app->magnitude_upper) },
                        2.0f, (Color){ 72, 221, 189, 255 });
        }
        for (size_t i = 1; i < app->magnitude_bin_count; i++) {
            float x0 = plot.x + plot.width * (float)(i - 1) /
                                  (float)(app->magnitude_bin_count - 1);
            float x1 = plot.x + plot.width * (float)i /
                                  (float)(app->magnitude_bin_count - 1);
            float y0 = plot_y(plot, app->magnitude_peaks[i - 1],
                              app->magnitude_lower, app->magnitude_upper);
            float y1 = plot_y(plot, app->magnitude_peaks[i],
                              app->magnitude_lower, app->magnitude_upper);
            DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 1.5f,
                       (Color){ 72, 221, 189, 255 });
        }
    }

    DrawText("0 ms", (int)plot.x, (int)(plot.y + plot.height + 11), 16,
             (Color){ 151, 174, 188, 255 });
    double duration_ms = app->have_samples
                             ? (double)app->pair_count * 1000.0 /
                                   app->applied_sample_rate
                             : 0.0;
    snprintf(text, sizeof(text), "%.3f ms", duration_ms);
    DrawText(text, (int)(plot.x + plot.width) - MeasureText(text, 16),
             (int)(plot.y + plot.height + 11), 16,
             (Color){ 151, 174, 188, 255 });
    snprintf(text, sizeof(text),
                 "absolute min %.2f   mean %.2f   max %.2f   manual axis %.2f..%.2f sample units   Up/Down scale",
             app->magnitude_min, app->magnitude_mean, app->magnitude_max,
             app->magnitude_lower, app->magnitude_upper);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
             (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        double time_ms = duration_ms * x_fraction;
        float lower = app->have_samples ? app->magnitude_lower : 0.0f;
        float upper = app->have_samples ? app->magnitude_upper
                                        : PHYSICAL_MAGNITUDE_MAX;
        float magnitude = upper - y_fraction * (upper - lower);
        snprintf(text, sizeof(text), "time %.3f ms   magnitude %.2f",
                 time_ms, magnitude);
        draw_cursor_readout(plot, mouse, text);
    }
}

static void draw_spectrum(const struct app *app) {
    char text[256];
    Rectangle plot = app->plot;
    double lower_frequency = (double)app->applied_frequency -
                             (double)app->applied_sample_rate / 2.0;
    double upper_frequency = (double)app->applied_frequency +
                             (double)app->applied_sample_rate / 2.0;
    const double frequency_steps[] = {
        1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0,
        100000.0, 200000.0, 500000.0, 1000000.0, 2000000.0,
        5000000.0, 10000000.0, 20000000.0, 50000000.0
    };
    double frequency_step = frequency_steps[
        sizeof(frequency_steps) / sizeof(frequency_steps[0]) - 1];
    double minimum_frequency_step = app->applied_sample_rate * 105.0 /
                                    fmaxf(plot.width, 1.0f);
    for (size_t i = 0;
         i < sizeof(frequency_steps) / sizeof(frequency_steps[0]); i++) {
        if (frequency_steps[i] >= minimum_frequency_step) {
            frequency_step = frequency_steps[i];
            break;
        }
    }
    int db_step = 10;
    while ((float)db_step * plot.height /
               (SPECTRUM_TOP_DBFS - app->spectrum_lower_dbfs) < 25.0f)
        db_step += 10;

    draw_plot_frame(plot, 0);
    int minor_db_step = db_step / 2;
    int first_minor_db = (int)ceilf(app->spectrum_lower_dbfs /
                                    (float)minor_db_step) * minor_db_step;
    for (int db = first_minor_db; db <= (int)SPECTRUM_TOP_DBFS;
         db += minor_db_step) {
        float y = plot_y(plot, (float)db, app->spectrum_lower_dbfs,
                          SPECTRUM_TOP_DBFS);
        int major = db % db_step == 0;
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 major ? (Color){ 42, 61, 74, 255 }
                       : (Color){ 24, 37, 48, 255 });
        DrawLine((int)plot.x - (major ? 7 : 4), (int)y, (int)plot.x, (int)y,
                 major ? (Color){ 126, 151, 166, 255 }
                       : (Color){ 70, 91, 105, 255 });
        if (major) {
            snprintf(text, sizeof(text), "%d", db);
            DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
                     (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
        }
    }
    DrawText("dBFS", (int)plot.x, (int)plot.y - 25, 16,
              (Color){ 151, 174, 188, 255 });

    double first_frequency = ceil(lower_frequency / frequency_step) *
                             frequency_step;
    for (double frequency = first_frequency;
         frequency <= upper_frequency + frequency_step * 1e-6;
         frequency += frequency_step) {
        float x = plot.x + (float)((frequency - lower_frequency) /
                                   (upper_frequency - lower_frequency)) *
                                   plot.width;
        DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                 (Color){ 42, 61, 74, 255 });
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + 7,
                 (Color){ 126, 151, 166, 255 });
        snprintf(text, sizeof(text), "%.3f", frequency / 1000000.0);
        int label_width = MeasureText(text, 16);
        DrawText(text, (int)x - label_width / 2,
                 (int)(plot.y + plot.height) + 11, 16,
                 (Color){ 151, 174, 188, 255 });

        double minor_step = frequency_step / 5.0;
        for (int minor = 1; minor < 5; minor++) {
            double minor_frequency = frequency + minor * minor_step;
            if (minor_frequency >= upper_frequency)
                break;
            float minor_x = plot.x +
                            (float)((minor_frequency - lower_frequency) /
                                    (upper_frequency - lower_frequency)) *
                                plot.width;
            if (minor_x <= plot.x || minor_x >= plot.x + plot.width)
                continue;
            DrawLine((int)minor_x, (int)plot.y, (int)minor_x,
                     (int)(plot.y + plot.height),
                     (Color){ 24, 37, 48, 255 });
            DrawLine((int)minor_x, (int)(plot.y + plot.height),
                     (int)minor_x, (int)(plot.y + plot.height) + 4,
                     (Color){ 70, 91, 105, 255 });
        }
    }

    if (app->spectrum_ready) {
        for (int i = 1; i < SDR_DSP_FFT_SIZE; i++) {
            float x0 = plot.x + plot.width * (float)(i - 1) /
                                  (SDR_DSP_FFT_SIZE - 1);
            float x1 = plot.x + plot.width * (float)i /
                                  (SDR_DSP_FFT_SIZE - 1);
            float average_y0 = plot_y(plot, app->spectrum_average[i - 1],
                                      app->spectrum_lower_dbfs,
                                      SPECTRUM_TOP_DBFS);
            float average_y1 = plot_y(plot, app->spectrum_average[i],
                                      app->spectrum_lower_dbfs,
                                      SPECTRUM_TOP_DBFS);
            float peak_y0 = plot_y(plot, app->spectrum_peak[i - 1],
                                   app->spectrum_lower_dbfs,
                                   SPECTRUM_TOP_DBFS);
            float peak_y1 = plot_y(plot, app->spectrum_peak[i],
                                   app->spectrum_lower_dbfs,
                                   SPECTRUM_TOP_DBFS);
            DrawLineEx((Vector2){ x0, peak_y0 }, (Vector2){ x1, peak_y1 },
                       1.0f, (Color){ 251, 176, 64, 210 });
            DrawLineEx((Vector2){ x0, average_y0 },
                       (Vector2){ x1, average_y1 }, 1.4f,
                       (Color){ 65, 202, 240, 255 });
        }
    }

    snprintf(text, sizeof(text),
              "frequency (MHz), major %.3f MHz   axis %.1f..%.1f dBFS   %d x %d-pair windows   bin %.3f Hz   average   peak hold",
              frequency_step / 1000000.0,
              app->spectrum_lower_dbfs, SPECTRUM_TOP_DBFS,
              app->spectrum_windows, SDR_DSP_FFT_SIZE,
             app->applied_sample_rate / (double)SDR_DSP_FFT_SIZE);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
             (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        double frequency = lower_frequency +
                           x_fraction * (upper_frequency - lower_frequency);
        float dbfs = SPECTRUM_TOP_DBFS -
                     y_fraction * (SPECTRUM_TOP_DBFS -
                                   app->spectrum_lower_dbfs);
        char offset[40];
        format_frequency_offset(offset, sizeof(offset),
                                frequency - app->applied_frequency);
        snprintf(text, sizeof(text),
                 "frequency %.6f MHz   offset %s   power %.1f dBFS",
                 frequency / 1000000.0, offset, dbfs);
        draw_cursor_readout(plot, mouse, text);
    }
}

static void draw_scatter(const struct app *app) {
    char text[256];
    Rectangle plot = app->plot;
    Rectangle source = { 0.0f, 0.0f, (float)app->scatter.texture.width,
                          -(float)app->scatter.texture.height };
    float major_step = app->scatter_axis_limit /
                       (plot.width >= 700.0f && plot.height >= 350.0f
                            ? 4.0f
                            : 2.0f);
    float minor_step = major_step / 5.0f;

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawTexturePro(app->scatter.texture, source, plot, (Vector2){ 0.0f, 0.0f },
                   0.0f, WHITE);

    float limit = app->scatter_axis_limit;
    for (float value = -limit; value <= limit + minor_step * 0.01f;
         value += minor_step) {
        int major_index = (int)lroundf(value / major_step);
        int major = fabsf(value - major_index * major_step) < 0.0001f;
        float x = plot.x + (value / limit + 1.0f) * 0.5f * plot.width;
        float y = plot.y + (1.0f - (value / limit + 1.0f) * 0.5f) *
                           plot.height;
        Color grid = major ? (Color){ 42, 61, 74, 255 }
                           : (Color){ 24, 37, 48, 255 };
        Color tick = major ? (Color){ 126, 151, 166, 255 }
                           : (Color){ 70, 91, 105, 255 };
        int tick_size = major ? 7 : 4;

        DrawLine((int)x, (int)plot.y, (int)x,
                 (int)(plot.y + plot.height), grid);
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 grid);
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + tick_size, tick);
        DrawLine((int)plot.x - tick_size, (int)y, (int)plot.x, (int)y,
                 tick);

        if (major) {
            float label_value = fabsf(value) < 0.0001f ? 0.0f : value;
            int precision = limit < 0.1f ? 3 : 2;
            snprintf(text, sizeof(text), "%+.*f", precision, label_value);
            DrawText(text, (int)x - MeasureText(text, 16) / 2,
                     (int)(plot.y + plot.height) + 11, 16,
                     (Color){ 151, 174, 188, 255 });
            DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
                     (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
        }
    }

    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });
    DrawText("Q (normalized full scale)", (int)plot.x, (int)plot.y - 25, 16,
             (Color){ 151, 174, 188, 255 });
    snprintf(text, sizeof(text),
              "I (normalized full scale)   manual range +/-%.3f   major %.3f   latest block: %zu points   Up/Down scale",
              limit, major_step,
              app->scatter_inserted);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
              (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        float i_value = (x_fraction * 2.0f - 1.0f) * limit;
        float q_value = (1.0f - y_fraction * 2.0f) * limit;
        snprintf(text, sizeof(text), "I %+.4f   Q %+.4f full scale",
                 i_value, q_value);
        draw_cursor_readout(plot, mouse, text);
    }
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

    pthread_mutex_lock(&app->latest.mutex);
    reading = app->latest.worker_reading;
    *done = app->latest.worker_done;
    pthread_mutex_unlock(&app->latest.mutex);
    return reading;
}

static int stop_acquisition(struct app *app) {
    request_worker_stop(app);
    if (!app->worker_started)
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

    int join_result = pthread_join(app->worker, NULL);
    if (join_result != 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Could not join acquisition worker: %s",
                 strerror(join_result));
        return -1;
    }
    app->worker_started = 0;
    return 0;
}

static int start_acquisition(struct app *app) {
    pthread_mutex_lock(&app->latest.mutex);
    app->latest.stop = 0;
    app->latest.ready = 0;
    app->latest.worker_done = 0;
    app->latest.worker_failed = 0;
    app->latest.worker_reading = 0;
    app->latest.worker_error[0] = '\0';
    pthread_mutex_unlock(&app->latest.mutex);

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
    int thread_result = pthread_create(
        &app->worker, NULL,
        app->receiver_mode ? receiver_worker : file_worker, app);
    if (thread_result == 0)
        app->worker_started = 1;
    int restore_result = pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
    if (restore_result != 0) {
        request_worker_stop(app);
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

static void open_settings(struct app *app) {
    snprintf(app->settings_frequency, sizeof(app->settings_frequency), "%u",
             app->applied_frequency);
    app->settings_frequency_length = (int)strlen(app->settings_frequency);
    snprintf(app->settings_ppm, sizeof(app->settings_ppm), "%d",
             app->applied_ppm);
    app->settings_ppm_length = (int)strlen(app->settings_ppm);
    app->settings_focus = 0;
    app->settings_gain_choice = 0;
    if (app->receiver_mode && app->applied_manual_gain) {
        for (int i = 0; i < app->supported_gain_count; i++)
            if (app->supported_gains[i] == app->applied_gain_tenths)
                app->settings_gain_choice = i + 1;
    }
    app->settings_error[0] = '\0';
    app->settings_remove_dc = app->remove_dc;
    app->settings_open = 1;
}

static int apply_settings(struct app *app) {
    uint32_t frequency;
    int ppm;
    if (parse_frequency(app->settings_frequency, &frequency) < 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Use Hz or a K/M/G value, for example 1090M");
        return -1;
    }
    if (parse_int(app->settings_ppm, &ppm) < 0 || ppm < -1000 || ppm > 1000) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "PPM must be a signed integer from -1000 to 1000");
        return -1;
    }

    if (!app->receiver_mode) {
        app->applied_frequency = frequency;
        app->options.frequency = frequency;
        app->options.ppm = ppm;
        app->applied_ppm = ppm;
        app->remove_dc = app->settings_remove_dc;
        app->spectrum_ready = 0;
        app->spectrum_peak_ready = 0;
        if (recreate_waterfall(app, app->plot, 1) < 0) {
            snprintf(app->settings_error, sizeof(app->settings_error),
                     "Could not reset waterfall for the new frequency");
            return -1;
        }
        return 0;
    }

    int manual = app->settings_gain_choice > 0;
    int gain = manual ? app->supported_gains[app->settings_gain_choice - 1] : 0;
    int old_manual = app->applied_manual_gain;
    int old_gain = app->applied_gain_tenths;
    int old_ppm = app->applied_ppm;
    uint32_t old_frequency = app->applied_frequency;
    if (stop_acquisition(app) < 0)
        return -1;
    if (rtlsdr_set_tuner_gain_mode(app->dev, manual) < 0 ||
        (manual && rtlsdr_set_tuner_gain(app->dev, gain) < 0) ||
        set_frequency_correction(app->dev, ppm) < 0 ||
        rtlsdr_set_center_freq(app->dev, frequency) < 0 ||
        rtlsdr_reset_buffer(app->dev) < 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Receiver rejected the requested settings");
        rtlsdr_set_tuner_gain_mode(app->dev, old_manual);
        if (old_manual)
            rtlsdr_set_tuner_gain(app->dev, old_gain);
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        if (start_acquisition(app) < 0)
            snprintf(app->settings_error, sizeof(app->settings_error),
                     "Settings failed and acquisition could not restart");
        return -1;
    }
    uint32_t reported_frequency = rtlsdr_get_center_freq(app->dev);
    uint32_t difference = reported_frequency > frequency
                              ? reported_frequency - frequency
                              : frequency - reported_frequency;
    if (reported_frequency == 0 || difference > 1000U) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Frequency readback mismatch: requested %u, got %u",
                 frequency, reported_frequency);
        rtlsdr_set_tuner_gain_mode(app->dev, old_manual);
        if (old_manual)
            rtlsdr_set_tuner_gain(app->dev, old_gain);
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        if (start_acquisition(app) < 0)
            snprintf(app->settings_error, sizeof(app->settings_error),
                     "Readback failed and acquisition could not restart");
        return -1;
    }

    app->applied_frequency = reported_frequency;
    app->applied_manual_gain = manual;
    app->applied_gain_tenths = gain;
    app->options.frequency = frequency;
    app->options.ppm = ppm;
    app->applied_ppm = rtlsdr_get_freq_correction(app->dev);
    app->remove_dc = app->settings_remove_dc;
    app->spectrum_ready = 0;
    app->spectrum_peak_ready = 0;
    if (recreate_waterfall(app, app->plot, 1) < 0) {
        snprintf(app->settings_error, sizeof(app->settings_error),
                 "Could not reset waterfall for the new frequency");
        rtlsdr_set_tuner_gain_mode(app->dev, old_manual);
        if (old_manual)
            rtlsdr_set_tuner_gain(app->dev, old_gain);
        set_frequency_correction(app->dev, old_ppm);
        rtlsdr_set_center_freq(app->dev, old_frequency);
        rtlsdr_reset_buffer(app->dev);
        app->applied_manual_gain = old_manual;
        app->applied_gain_tenths = old_gain;
        app->applied_ppm = old_ppm;
        app->applied_frequency = old_frequency;
        start_acquisition(app);
        return -1;
    }
    if (start_acquisition(app) < 0)
        return -1;
    return 0;
}

static int retune_receiver(struct app *app, uint32_t frequency, int ppm) {
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

static void open_calibration(struct app *app) {
    app->calibration_open = 1;
    app->calibration_running = 0;
    app->calibration_technology = 0;
    app->calibration_band = 0;
    snprintf(app->calibration_channel, sizeof(app->calibration_channel),
             "113");
    app->calibration_channel_length = 3;
    app->calibration_expected_hz = 0;
    app->calibration_measurements = 0;
    app->calibration_recent_count = 0;
    app->calibration_recent_head = 0;
    app->calibration_recent_center = 0.0;
    app->calibration_recent_spread = 0.0;
    app->calibration_recent_sem = 0.0;
    app->calibration_fcch_locked = 0;
    app->calibration_fcch_confidence = 0.0f;
    app->calibration_source = CALIBRATION_SOURCE_CENTROID;
    app->calibration_fcch_miss = 0;
    app->calibration_fcch_hits = 0;
    app->scan_open = 0;
    app->scan_running = 0;
    app->calibration_stable = 0;
    app->calibration_measured_hz = 0.0;
    app->calibration_offset_hz = 0.0;
    app->calibration_return_frequency = app->applied_frequency;
    app->calibration_suggested_ppm = app->applied_ppm;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "Select GSM 900 ARFCN 1-124, then press Start");
}

static int start_calibration(struct app *app) {
    int arfcn;
    uint32_t expected;
    if (app->calibration_technology != 0 || app->calibration_band != 0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Only 2G GSM 900 is supported in this version");
        return -1;
    }
    if (app->applied_sample_rate < 1000000U) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "GSM calibration requires a sample rate of at least 1 MS/s");
        return -1;
    }
    if (parse_int(app->calibration_channel, &arfcn) < 0 ||
        arfcn < 1 || arfcn > 124 ||
        !gsm_downlink_hz((unsigned int)arfcn, &expected)) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "GSM 900 ARFCN must be between 1 and 124");
        return -1;
    }

    app->calibration_expected_hz = expected;
    app->calibration_tune_hz = expected - 400000U;
    app->calibration_measurements = 0;
    app->calibration_measured_hz = 0.0;
    app->calibration_offset_hz = 0.0;
    app->calibration_recent_count = 0;
    app->calibration_recent_head = 0;
    app->calibration_recent_center = 0.0;
    app->calibration_recent_spread = 0.0;
    app->calibration_recent_sem = 0.0;
    app->calibration_source = CALIBRATION_SOURCE_CENTROID;
    app->calibration_fcch_miss = 0;
    app->calibration_fcch_hits = 0;
    app->calibration_fcch_locked = 0;
    app->calibration_stable = 0;
    if (retune_receiver(app, app->calibration_tune_hz, app->applied_ppm) < 0)
        return -1;
    app->calibration_started_at = GetTime();
    app->calibration_running = 1;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "Measuring GSM 900 ARFCN %d at %.3f MHz", arfcn,
             expected / 1000000.0);
    return 0;
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

/* Robust center and spread of the recent PPM residuals. The center is the
   median and the spread is a normal-consistent scale estimate (1.4826 x MAD),
   both resistant to a peak that momentarily hops to an adjacent feature. */
static void robust_center_spread(const double *values, int count,
                                 double *center, double *spread) {
    double sorted[CALIBRATION_RECENT];
    double deviations[CALIBRATION_RECENT];
    int i;

    if (count <= 0) {
        *center = 0.0;
        *spread = 0.0;
        return;
    }
    for (i = 0; i < count; i++)
        sorted[i] = values[i];
    qsort(sorted, (size_t)count, sizeof(*sorted), compare_double);
    double median = (count % 2)
                        ? sorted[count / 2]
                        : 0.5 * (sorted[count / 2 - 1] + sorted[count / 2]);
    for (i = 0; i < count; i++)
        deviations[i] = fabs(values[i] - median);
    qsort(deviations, (size_t)count, sizeof(*deviations), compare_double);
    double mad = (count % 2)
                     ? deviations[count / 2]
                     : 0.5 * (deviations[count / 2 - 1] +
                              deviations[count / 2]);
    *center = median;
    *spread = 1.4826 * mad;
}

static void reset_calibration_stats(struct app *app) {
    app->calibration_measurements = 0;
    app->calibration_recent_count = 0;
    app->calibration_recent_head = 0;
    app->calibration_recent_center = 0.0;
    app->calibration_recent_spread = 0.0;
    app->calibration_recent_sem = 0.0;
    app->calibration_stable = 0;
}

static void calibration_set_status(struct app *app) {
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "%s (%s): %d meas, +/- %.2f PPM (spread %.2f), FCCH hits %d miss %d conf %.2f, suggested %+d PPM",
             app->calibration_stable ? "Stable lock" : "Acquiring",
             app->calibration_source == CALIBRATION_SOURCE_FCCH
                 ? "FCCH tone"
                 : "centroid",
             app->calibration_measurements,
             app->calibration_recent_sem,
             app->calibration_recent_spread,
             app->calibration_fcch_hits,
             app->calibration_fcch_miss,
             app->calibration_fcch_confidence,
             app->calibration_suggested_ppm);
}

static void update_calibration_measurement(struct app *app) {
    if (!app->calibration_open || !app->calibration_running ||
        app->scan_open || !app->spectrum_ready)
        return;

    double lower = (double)app->applied_frequency -
                   app->applied_sample_rate / 2.0;
    double upper = (double)app->applied_frequency +
                   app->applied_sample_rate / 2.0;
    double elapsed = GetTime() - app->calibration_started_at;
    if (elapsed < CALIBRATION_SETTLE_SECONDS) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Settling receiver... %.1f s", elapsed);
        return;
    }

    /* FCCH detection is independent of the centroid: a dip in centroid
       prominence must not wipe an FCCH accumulation. */
    struct gsm_fcch_result fcch;
    double fcch_target = (double)app->calibration_expected_hz -
                         (double)app->applied_frequency +
                         GSM_FCCH_TONE_HZ;
    int have_fcch = gsm_fcch_detect(app->i_samples, app->q_samples,
                                           app->pair_count,
                                           app->applied_sample_rate,
                                           fcch_target, 50000.0, &fcch);

    /* The centroid supplies the peak/floor/prominence metrics and the
       carrier estimate used in centroid mode. */
    struct sdr_channel_estimate estimate;
    int have_centroid = sdr_dsp_estimate_channel_center(
        app->spectrum_average, SDR_DSP_FFT_SIZE, lower, upper,
        app->calibration_expected_hz, 100000.0, 50000.0,
        app->calibration_workspace, &estimate);
    if (have_centroid) {
        app->calibration_peak_hz = estimate.peak_frequency_hz;
        app->calibration_peak_dbfs = estimate.peak_dbfs;
        app->calibration_floor_dbfs = estimate.floor_dbfs;
        app->calibration_prominence_db = estimate.prominence_db;
    }

    /* FCCH and centroid residuals differ by many PPM, so the recent-residual
       buffer must never mix them: switching source resets it, and while locked
       to the tone a burst-free block is skipped rather than recorded. */
    double measured_hz;
    if (have_fcch) {
        if (app->calibration_source != CALIBRATION_SOURCE_FCCH) {
            app->calibration_source = CALIBRATION_SOURCE_FCCH;
            reset_calibration_stats(app);
            app->calibration_fcch_hits = 0;
        }
        app->calibration_fcch_miss = 0;
        app->calibration_fcch_locked = 1;
        app->calibration_fcch_confidence = fcch.confidence;
        app->calibration_fcch_hits++;
        measured_hz = (double)app->applied_frequency +
                      fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
    } else if (app->calibration_source == CALIBRATION_SOURCE_FCCH) {
        app->calibration_fcch_miss++;
        if (app->calibration_fcch_miss < CALIBRATION_FCCH_MISS_LIMIT) {
            calibration_set_status(app); /* hold the tone lock */
            return;
        }
        app->calibration_source = CALIBRATION_SOURCE_CENTROID;
        app->calibration_fcch_locked = 0;
        reset_calibration_stats(app);
        app->calibration_fcch_hits = 0;
        if (!have_centroid) {
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "No isolated GSM carrier at least 8 dB above guard-band floor");
            return;
        }
        measured_hz = estimate.measured_frequency_hz;
    } else {
        app->calibration_fcch_locked = 0;
        if (!have_centroid) {
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "No isolated GSM carrier at least 8 dB above guard-band floor");
            reset_calibration_stats(app);
            return;
        }
        measured_hz = estimate.measured_frequency_hz;
    }

    app->calibration_measured_hz = measured_hz;
    app->calibration_offset_hz = measured_hz - app->calibration_expected_hz;
    double observed_ppm = app->calibration_offset_hz /
                          app->calibration_expected_hz * 1000000.0;
    app->calibration_measurements++;
    app->calibration_recent_ppm[app->calibration_recent_head] = observed_ppm;
    app->calibration_recent_head =
        (app->calibration_recent_head + 1) % CALIBRATION_RECENT;
    if (app->calibration_recent_count < CALIBRATION_RECENT)
        app->calibration_recent_count++;

    /* Individual 65 ms blocks scatter a lot on a modulated GSM channel, but the
       correction we apply is the center of the recent residuals, whose
       uncertainty is the standard error of that center, not the per-block
       spread. Gate on the standard error so the lock reflects how well the
       correction is known. Median/MAD keep a hopping peak from biasing it. */
    robust_center_spread(app->calibration_recent_ppm,
                         app->calibration_recent_count,
                         &app->calibration_recent_center,
                         &app->calibration_recent_spread);
    app->calibration_recent_sem =
        app->calibration_recent_spread /
        sqrt((double)app->calibration_recent_count);

    app->calibration_suggested_ppm = sdr_dsp_corrected_ppm(
        app->applied_ppm, app->calibration_expected_hz *
                              (1.0 + app->calibration_recent_center / 1000000.0),
        app->calibration_expected_hz);
    if (app->calibration_suggested_ppm < -1000)
        app->calibration_suggested_ppm = -1000;
    if (app->calibration_suggested_ppm > 1000)
        app->calibration_suggested_ppm = 1000;

    /* Prominence only gates centroid mode; an FCCH tone is its own quality
       proof and its prominence metric may momentarily dip. */
    int quality_ok = (app->calibration_source == CALIBRATION_SOURCE_FCCH)
                         ? 1
                         : (app->calibration_prominence_db >= 8.0f);
    app->calibration_stable = elapsed >= CALIBRATION_MIN_SECONDS &&
                              app->calibration_measurements >= 32 &&
                              app->calibration_recent_count >= 32 &&
                              app->calibration_recent_sem <=
                                  CALIBRATION_MAX_SEM_PPM &&
                              quality_ok;
    calibration_set_status(app);
}

static int scan_strongest_arfcn(const struct app *app) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        if (app->scan_power[arfcn] > best_power) {
            best_power = app->scan_power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

static int scan_strongest_bcch(const struct app *app) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        if (app->scan_bcch_conf[arfcn] >= SCAN_BCCH_MIN_CONF &&
            app->scan_power[arfcn] > best_power) {
            best_power = app->scan_power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

static int start_scan(struct app *app) {
    if (!app->receiver_mode) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Channel scan requires a live RTL-SDR receiver");
        return -1;
    }
    double accept_half = app->applied_sample_rate / 2.0 - SCAN_EDGE_MARGIN_HZ;
    if (accept_half < 100000.0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Channel scan requires a sample rate of at least 1 MS/s");
        return -1;
    }
    app->scan_accept_half_hz = accept_half;
    app->scan_step_hz = 2.0 * accept_half;
    app->scan_first_center_hz = SCAN_BAND_LOWER_HZ + accept_half;
    int count = 0;
    double center = app->scan_first_center_hz;
    while (center - accept_half < SCAN_BAND_UPPER_HZ) {
        count++;
        center += app->scan_step_hz;
    }
    app->scan_step_count = count;
    for (int arfcn = 0; arfcn < 125; arfcn++) {
        app->scan_power[arfcn] = SCAN_SENTINEL_DBFS;
        app->scan_bcch_conf[arfcn] = 0.0f;
    }
    app->scan_selected_arfcn = 0;
    app->scan_return_frequency = app->applied_frequency;
    app->scan_step = 0;
    if (retune_receiver(app, (uint32_t)llround(app->scan_first_center_hz),
                        app->applied_ppm) < 0)
        return -1;
    app->scan_step_started_at = GetTime();
    app->scan_running = 1;
    app->scan_open = 1;
    return 0;
}

static void update_scan(struct app *app) {
    if (!app->scan_running || !app->spectrum_ready)
        return;
    double elapsed = GetTime() - app->scan_step_started_at;
    if (elapsed < SCAN_STEP_SETTLE_SECONDS)
        return;

    double center = (double)app->applied_frequency;
    double lower = center - app->applied_sample_rate / 2.0;
    double upper = center + app->applied_sample_rate / 2.0;
    sdr_dsp_channel_powers(app->spectrum_average, SDR_DSP_FFT_SIZE,
                              lower, upper,
                              center - app->scan_accept_half_hz,
                              center + app->scan_accept_half_hz,
                              GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ,
                              1, 124, app->scan_power);

    /* Flag BCCH channels: probe each channel in this step's window for its
       FCCH tone (carrier + 67.708 kHz). FCCH is intermittent and a strong
       neighbour can lower its coherence, so keep the peak coherence seen at
       the tone offset across the step's blocks (never clearing it) and treat a
       channel as BCCH at a slightly relaxed scan threshold. */
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        double channel = GSM900_BASE_HZ +
                         (double)arfcn * GSM900_ARFCN_SPACING_HZ;
        if (channel < center - app->scan_accept_half_hz ||
            channel > center + app->scan_accept_half_hz)
            continue;
        struct gsm_fcch_result fcch;
        double target = channel - center + GSM_FCCH_TONE_HZ;
        gsm_fcch_detect(app->i_samples, app->q_samples,
                               app->pair_count, app->applied_sample_rate,
                               target, 50000.0, &fcch);
        if (fcch.confidence > app->scan_bcch_conf[arfcn])
            app->scan_bcch_conf[arfcn] = fcch.confidence;
    }

    if (elapsed < SCAN_STEP_SETTLE_SECONDS + SCAN_STEP_PROBE_SECONDS)
        return; /* keep probing this step for more FCCH bursts */

    app->scan_step++;
    if (app->scan_step >= app->scan_step_count) {
        app->scan_running = 0;
        int bcch = scan_strongest_bcch(app);
        app->scan_selected_arfcn = bcch > 0 ? bcch : scan_strongest_arfcn(app);
        return;
    }
    double next = app->scan_first_center_hz +
                  (double)app->scan_step * app->scan_step_hz;
    if (retune_receiver(app, (uint32_t)llround(next), app->applied_ppm) < 0) {
        app->scan_running = 0;
        return;
    }
    app->scan_step_started_at = GetTime();
}

static Rectangle settings_panel(void) {
    float width = 520.0f;
    float height = 330.0f;
    return (Rectangle){ ((float)GetScreenWidth() - width) * 0.5f,
                        ((float)GetScreenHeight() - height) * 0.5f,
                        width, height };
}

static int clicked(Rectangle rectangle) {
    return CheckCollisionPointRec(GetMousePosition(), rectangle) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void handle_settings_input(struct app *app) {
    Rectangle panel = settings_panel();
    Rectangle frequency = { panel.x + 28.0f, panel.y + 83.0f,
                            300.0f, 38.0f };
    Rectangle ppm = { panel.x + 342.0f, panel.y + 83.0f,
                      panel.width - 370.0f, 38.0f };
    Rectangle gain_previous = { panel.x + 28.0f, panel.y + 164.0f,
                                42.0f, 38.0f };
    Rectangle gain_next = { panel.x + panel.width - 70.0f,
                            panel.y + 164.0f, 42.0f, 38.0f };
    Rectangle dc_toggle = { panel.x + 28.0f, panel.y + 218.0f,
                            22.0f, 22.0f };
    Rectangle cancel = { panel.x + panel.width - 224.0f,
                         panel.y + panel.height - 55.0f, 92.0f, 34.0f };
    Rectangle apply = { panel.x + panel.width - 120.0f,
                        panel.y + panel.height - 55.0f, 92.0f, 34.0f };

    int character;
    while ((character = GetCharPressed()) != 0) {
        if (app->settings_focus == 0) {
            int valid = (character >= '0' && character <= '9') ||
                        character == '.' || character == 'k' ||
                        character == 'K' || character == 'm' ||
                        character == 'M' || character == 'g' ||
                        character == 'G';
            if (valid && app->settings_frequency_length <
                             (int)sizeof(app->settings_frequency) - 1) {
                app->settings_frequency[app->settings_frequency_length++] =
                    (char)character;
                app->settings_frequency[app->settings_frequency_length] = '\0';
            }
        } else {
            int valid = (character >= '0' && character <= '9') ||
                        (character == '-' && app->settings_ppm_length == 0);
            if (valid && app->settings_ppm_length <
                             (int)sizeof(app->settings_ppm) - 1) {
                app->settings_ppm[app->settings_ppm_length++] =
                    (char)character;
                app->settings_ppm[app->settings_ppm_length] = '\0';
            }
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (app->settings_focus == 0 && app->settings_frequency_length > 0)
            app->settings_frequency[--app->settings_frequency_length] = '\0';
        if (app->settings_focus == 1 && app->settings_ppm_length > 0)
            app->settings_ppm[--app->settings_ppm_length] = '\0';
    }
    if (clicked(frequency))
        app->settings_focus = 0;
    if (clicked(ppm))
        app->settings_focus = 1;

    if (app->receiver_mode && clicked(gain_previous)) {
        app->settings_gain_choice--;
        if (app->settings_gain_choice < 0)
            app->settings_gain_choice = app->supported_gain_count;
    }
    if (app->receiver_mode && clicked(gain_next)) {
        app->settings_gain_choice++;
        if (app->settings_gain_choice > app->supported_gain_count)
            app->settings_gain_choice = 0;
    }
    if (clicked(dc_toggle))
        app->settings_remove_dc = !app->settings_remove_dc;
    if (clicked(cancel)) {
        app->settings_open = 0;
        return;
    }
    if (clicked(apply) || IsKeyPressed(KEY_ENTER)) {
        if (apply_settings(app) == 0)
            app->settings_open = 0;
    }

    (void)frequency;
}

static void draw_button(Rectangle rectangle, const char *label, int primary) {
    int hover = CheckCollisionPointRec(GetMousePosition(), rectangle);
    Color fill = primary ? (hover ? (Color){ 220, 142, 38, 255 }
                                  : (Color){ 191, 111, 25, 255 })
                         : (hover ? (Color){ 44, 62, 75, 255 }
                                  : (Color){ 29, 43, 54, 255 });
    DrawRectangleRec(rectangle, fill);
    DrawRectangleLinesEx(rectangle, 1.0f,
                         primary ? (Color){ 255, 201, 103, 255 }
                                 : (Color){ 91, 117, 132, 255 });
    int font_size = 17;
    DrawText(label,
             (int)(rectangle.x + (rectangle.width - MeasureText(label,
                                                                 font_size)) /
                                      2.0f),
             (int)(rectangle.y + (rectangle.height - font_size) / 2.0f),
             font_size, (Color){ 235, 242, 246, 255 });
}

static void draw_settings(const struct app *app) {
    Rectangle panel = settings_panel();
    Rectangle frequency = { panel.x + 28.0f, panel.y + 83.0f,
                            300.0f, 38.0f };
    Rectangle ppm = { panel.x + 342.0f, panel.y + 83.0f,
                      panel.width - 370.0f, 38.0f };
    Rectangle gain_previous = { panel.x + 28.0f, panel.y + 164.0f,
                                42.0f, 38.0f };
    Rectangle gain_next = { panel.x + panel.width - 70.0f,
                            panel.y + 164.0f, 42.0f, 38.0f };
    Rectangle dc_toggle = { panel.x + 28.0f, panel.y + 218.0f,
                            22.0f, 22.0f };
    Rectangle cancel = { panel.x + panel.width - 224.0f,
                         panel.y + panel.height - 55.0f, 92.0f, 34.0f };
    Rectangle apply = { panel.x + panel.width - 120.0f,
                        panel.y + panel.height - 55.0f, 92.0f, 34.0f };
    char gain[64];

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 0, 0, 0, 165 });
    DrawRectangleRec(panel, (Color){ 12, 20, 29, 255 });
    DrawRectangleLinesEx(panel, 2.0f, (Color){ 111, 139, 154, 255 });
    DrawText("Acquisition settings", (int)panel.x + 28, (int)panel.y + 22,
             24, (Color){ 235, 242, 246, 255 });
    DrawText("Center frequency (Hz or K/M/G)", (int)frequency.x,
             (int)frequency.y - 23,
             17, (Color){ 166, 188, 201, 255 });
    DrawRectangleRec(frequency, (Color){ 5, 10, 16, 255 });
    DrawRectangleLinesEx(frequency, 1.0f, (Color){ 255, 174, 62, 255 });
    DrawText(app->settings_frequency, (int)frequency.x + 10,
             (int)frequency.y + 9, 19, (Color){ 255, 225, 161, 255 });
    DrawText("PPM", (int)ppm.x, (int)ppm.y - 23, 17,
             (Color){ 166, 188, 201, 255 });
    DrawRectangleRec(ppm, (Color){ 5, 10, 16, 255 });
    DrawRectangleLinesEx(ppm, 1.0f,
                         app->settings_focus == 1
                             ? (Color){ 255, 174, 62, 255 }
                             : (Color){ 91, 117, 132, 255 });
    DrawText(app->settings_ppm, (int)ppm.x + 9, (int)ppm.y + 9, 19,
             (Color){ 255, 225, 161, 255 });

    DrawText("Gain", (int)panel.x + 28, (int)panel.y + 137, 17,
             (Color){ 166, 188, 201, 255 });
    if (!app->receiver_mode) {
        snprintf(gain, sizeof(gain), "capture (not adjustable)");
    } else if (app->settings_gain_choice == 0) {
        snprintf(gain, sizeof(gain), "automatic");
    } else {
        snprintf(gain, sizeof(gain), "%.1f dB",
                 app->supported_gains[app->settings_gain_choice - 1] / 10.0);
    }
    if (app->receiver_mode) {
        draw_button(gain_previous, "<", 0);
        draw_button(gain_next, ">", 0);
    }
    DrawText(gain,
             (int)(panel.x + (panel.width - MeasureText(gain, 20)) / 2.0f),
             (int)panel.y + 173, 20, (Color){ 235, 242, 246, 255 });

    DrawRectangleRec(dc_toggle, (Color){ 5, 10, 16, 255 });
    DrawRectangleLinesEx(dc_toggle, 1.0f, (Color){ 255, 174, 62, 255 });
    if (app->settings_remove_dc) {
        DrawLineEx((Vector2){ dc_toggle.x + 4.0f, dc_toggle.y + 11.0f },
                   (Vector2){ dc_toggle.x + 9.0f, dc_toggle.y + 17.0f },
                   2.0f, (Color){ 255, 205, 91, 255 });
        DrawLineEx((Vector2){ dc_toggle.x + 9.0f, dc_toggle.y + 17.0f },
                   (Vector2){ dc_toggle.x + 19.0f, dc_toggle.y + 5.0f },
                   2.0f, (Color){ 255, 205, 91, 255 });
    }
    DrawText("Remove DC spike from spectrum and waterfall",
             (int)dc_toggle.x + 32, (int)dc_toggle.y + 2, 17,
             (Color){ 205, 218, 226, 255 });

    if (app->settings_error[0])
        DrawText(app->settings_error, (int)panel.x + 28, (int)panel.y + 247,
                 16, (Color){ 255, 105, 100, 255 });
    draw_button(cancel, "Cancel", 0);
    draw_button(apply, "Apply", 1);
}

static Rectangle settings_button(void) {
    return (Rectangle){ (float)GetScreenWidth() - 130.0f, 16.0f,
                        108.0f, 34.0f };
}

static Rectangle calibration_button(void) {
    return (Rectangle){ (float)GetScreenWidth() - 130.0f, 58.0f,
                        108.0f, 34.0f };
}

static void close_calibration(struct app *app) {
    if (app->calibration_running) {
        if (retune_receiver(app, app->calibration_return_frequency,
                            app->applied_ppm) < 0)
            return;
    }
    app->calibration_running = 0;
    app->calibration_open = 0;
}

static void adjust_waterfall_scale(struct app *app, int zoom_in) {
    app->waterfall_lower_dbfs += zoom_in ? DB_SCALE_STEP : -DB_SCALE_STEP;
    app->waterfall_lower_dbfs = fmaxf(
        SDR_DSP_DBFS_FLOOR,
        fminf(app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS - 20.0f));
    render_waterfall(app);
}

static void handle_calibration_input(struct app *app) {
    Rectangle tech_2g = { 24, 72, 74, 34 };
    Rectangle tech_4g = { 106, 72, 74, 34 };
    Rectangle tech_5g = { 188, 72, 74, 34 };
    float right = (float)GetScreenWidth() - 24.0f;
    Rectangle scan = { 470, 72, 90, 34 };
    Rectangle start = { right - 248.0f, 72, 88, 34 };
    Rectangle apply_ppm = { right - 148.0f, 72, 126, 34 };
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };

    int inputs_changed = 0;
    if (!app->calibration_running && clicked(tech_2g)) {
        app->calibration_technology = 0;
        inputs_changed = 1;
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Select GSM 900 ARFCN 1-124, then press Start");
    }
    if (!app->calibration_running && clicked(tech_4g)) {
        app->calibration_technology = 1;
        inputs_changed = 1;
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "4G channel tables are not implemented yet");
    }
    if (!app->calibration_running && clicked(tech_5g)) {
        app->calibration_technology = 2;
        inputs_changed = 1;
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "5G channel tables are not implemented yet");
    }

    int character;
    while (app->calibration_technology == 0 &&
           (character = GetCharPressed()) != 0) {
        if (character >= '0' && character <= '9' &&
            app->calibration_channel_length <
                (int)sizeof(app->calibration_channel) - 1) {
            app->calibration_channel[app->calibration_channel_length++] =
                (char)character;
            app->calibration_channel[app->calibration_channel_length] = '\0';
            inputs_changed = 1;
        }
    }
    if (app->calibration_technology == 0 &&
        IsKeyPressed(KEY_BACKSPACE) &&
        app->calibration_channel_length > 0) {
        app->calibration_channel[--app->calibration_channel_length] = '\0';
        inputs_changed = 1;
    }
    if (inputs_changed) {
        app->calibration_stable = 0;
        app->calibration_measurements = 0;
        app->calibration_recent_count = 0;
        app->calibration_recent_head = 0;
        app->calibration_recent_center = 0.0;
        app->calibration_recent_spread = 0.0;
        app->calibration_recent_sem = 0.0;
        if (app->calibration_running)
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "Editing target ARFCN; press Start to retune");
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
        adjust_waterfall_scale(app, 1);
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
        adjust_waterfall_scale(app, 0);
    if ((clicked(start) || IsKeyPressed(KEY_ENTER)) &&
        app->calibration_technology == 0)
        start_calibration(app);
    if (clicked(scan) && app->calibration_technology == 0)
        start_scan(app);
    if (clicked(apply_ppm) && app->calibration_stable) {
        if (retune_receiver(app, app->calibration_tune_hz,
                            app->calibration_suggested_ppm) == 0) {
            app->options.ppm = app->calibration_suggested_ppm;
            app->calibration_measurements = 0;
            app->calibration_recent_count = 0;
            app->calibration_recent_head = 0;
            app->calibration_stable = 0;
            app->calibration_started_at = GetTime();
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "Applied %+d PPM; measuring residual error",
                     app->applied_ppm);
        }
    }
    if (clicked(back) || IsKeyPressed(KEY_ESCAPE))
        close_calibration(app);
}

static void draw_calibration(struct app *app) {
    char text[256];
    Rectangle tech_2g = { 24, 72, 74, 34 };
    Rectangle tech_4g = { 106, 72, 74, 34 };
    Rectangle tech_5g = { 188, 72, 74, 34 };
    float right = (float)GetScreenWidth() - 24.0f;
    Rectangle scan = { 470, 72, 90, 34 };
    Rectangle channel = { right - 370.0f, 72, 110, 34 };
    Rectangle start = { right - 248.0f, 72, 88, 34 };
    Rectangle apply_ppm = { right - 148.0f, 72, 126, 34 };
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };

    DrawText("Cellular frequency calibration", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    DrawText("Technology", 24, 50, 16, (Color){ 157, 180, 194, 255 });
    draw_button(tech_2g, "2G", app->calibration_technology == 0);
    draw_button(tech_4g, "4G", app->calibration_technology == 1);
    draw_button(tech_5g, "5G", app->calibration_technology == 2);
    DrawText(app->calibration_technology == 0
                 ? "Band: GSM 900"
                 : "Band: unavailable",
             274, 80, 18,
             (Color){ 209, 221, 228, 255 });
    DrawText("ARFCN", (int)channel.x, 50, 16,
             (Color){ 157, 180, 194, 255 });
    DrawRectangleRec(channel, (Color){ 5, 10, 16, 255 });
    DrawRectangleLinesEx(channel, 1.0f, (Color){ 255, 174, 62, 255 });
    DrawText(app->calibration_technology == 0
                 ? app->calibration_channel
                 : "N/A",
             (int)channel.x + 9,
             (int)channel.y + 8, 18, (Color){ 255, 225, 161, 255 });
    draw_button(start, app->calibration_running ? "Retune" : "Start",
                app->calibration_technology == 0);
    draw_button(apply_ppm, "Apply PPM", app->calibration_stable);
    draw_button(scan, "Scan", app->calibration_technology == 0);
    draw_button(back, "Back", 0);

    snprintf(text, sizeof(text),
             "expected: %.6f MHz   tuned center: %.6f MHz   current correction: %+d PPM",
             app->calibration_expected_hz / 1000000.0,
             app->applied_frequency / 1000000.0, app->applied_ppm);
    DrawText(text, 24, 118, 17, (Color){ 190, 208, 218, 255 });
    if (app->calibration_measurements > 0) {
        snprintf(text, sizeof(text),
                 "measured: %.6f MHz   offset: %+.1f kHz   observed: %+.2f PPM   center: %+.2f +/- %.2f PPM (SEM %.2f)",
                 app->calibration_measured_hz / 1000000.0,
                 app->calibration_offset_hz / 1000.0,
                 app->calibration_offset_hz /
                     app->calibration_expected_hz * 1000000.0,
                 app->calibration_recent_center,
                 app->calibration_recent_spread,
                 app->calibration_recent_sem);
        DrawText(text, 24, 142, 17, (Color){ 255, 205, 91, 255 });
        snprintf(text, sizeof(text),
                 "peak: %.1f dBFS   guard floor: %.1f dBFS   prominence: %.1f dB   suggested correction: %+d PPM",
                 app->calibration_peak_dbfs, app->calibration_floor_dbfs,
                 app->calibration_prominence_db,
                 app->calibration_suggested_ppm);
        DrawText(text, 24, 164, 17,
                 app->calibration_stable ? (Color){ 99, 228, 170, 255 }
                                         : (Color){ 250, 190, 74, 255 });
    }
    DrawText(app->calibration_status, 24, 186, 17,
             (Color){ 158, 204, 230, 255 });

    draw_waterfall(app, 1);
    if (app->calibration_expected_hz > 0) {
        double full_lower = (double)app->applied_frequency -
                            app->applied_sample_rate / 2.0;
        double full_upper = (double)app->applied_frequency +
                            app->applied_sample_rate / 2.0;
        double lower = (double)app->calibration_expected_hz -
                       CALIBRATION_VIEW_HALF_WIDTH_HZ;
        double upper = (double)app->calibration_expected_hz +
                       CALIBRATION_VIEW_HALF_WIDTH_HZ;
        if (lower < full_lower)
            lower = full_lower;
        if (upper > full_upper)
            upper = full_upper;
        if (upper - lower <= 1.0) {
            lower = full_lower;
            upper = full_upper;
        }
        float expected_x = app->plot.x +
                           (float)((app->calibration_expected_hz - lower) /
                                   (upper - lower)) * app->plot.width;
        DrawLine((int)expected_x, (int)app->plot.y, (int)expected_x,
                 (int)(app->plot.y + app->plot.height),
                 (Color){ 87, 229, 173, 230 });
        DrawText("expected", (int)expected_x + 5, (int)app->plot.y + 5, 16,
                 (Color){ 111, 244, 191, 255 });
        if (app->calibration_measurements > 0) {
            float measured_x = app->plot.x +
                               (float)((app->calibration_measured_hz - lower) /
                                       (upper - lower)) * app->plot.width;
            DrawLine((int)measured_x, (int)app->plot.y, (int)measured_x,
                     (int)(app->plot.y + app->plot.height),
                     (Color){ 255, 181, 59, 240 });
            DrawText("measured", (int)measured_x + 5,
                      (int)app->plot.y + 25, 16,
                      (Color){ 255, 202, 105, 255 });
        }
    }
}

static int scan_arfcn_at(const struct app *app, Vector2 point) {
    if (!CheckCollisionPointRec(point, app->plot))
        return 0;
    double fraction = (point.x - app->plot.x) / app->plot.width;
    int arfcn = 1 + (int)(fraction * 124.0);
    if (arfcn < 1)
        arfcn = 1;
    if (arfcn > 124)
        arfcn = 124;
    return arfcn;
}

static void draw_scan(struct app *app) {
    char text[160];
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };
    Rectangle rescan = { (float)GetScreenWidth() - 212.0f, 18, 88, 34 };
    Rectangle plot = app->plot;
    int strongest = scan_strongest_arfcn(app);
    int strongest_bcch = scan_strongest_bcch(app);

    DrawText("GSM 900 channel power scan", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    draw_button(rescan, "Rescan", !app->scan_running);
    draw_button(back, "Back", 0);

    if (app->scan_running)
        snprintf(text, sizeof(text),
                 "Scanning ARFCN band... step %d / %d",
                 app->scan_step + 1, app->scan_step_count);
    else if (strongest_bcch > 0)
        snprintf(text, sizeof(text),
                 "Strongest BCCH ARFCN %d at %.1f dBFS (conf %.2f)   click a green channel to calibrate it",
                 strongest_bcch, app->scan_power[strongest_bcch],
                 app->scan_bcch_conf[strongest_bcch]);
    else if (strongest > 0)
        snprintf(text, sizeof(text),
                 "No BCCH detected; strongest channel ARFCN %d at %.1f dBFS   click a channel to try it",
                 strongest, app->scan_power[strongest]);
    else
        snprintf(text, sizeof(text),
                 "No channels measured; press Rescan");
    DrawText(text, 24, 54, 18, (Color){ 190, 208, 218, 255 });

    /* Power range for the vertical axis. */
    float minimum = 0.0f;
    float maximum = -300.0f;
    int measured = 0;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        float power = app->scan_power[arfcn];
        if (power <= SCAN_SENTINEL_DBFS)
            continue;
        if (!measured || power < minimum)
            minimum = power;
        if (!measured || power > maximum)
            maximum = power;
        measured = 1;
    }
    if (!measured) {
        minimum = -100.0f;
        maximum = -30.0f;
    }
    minimum -= 3.0f;
    maximum += 3.0f;
    if (maximum - minimum < 10.0f)
        maximum = minimum + 10.0f;

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    /* Vertical dBFS gridlines. */
    for (int division = 0; division <= 4; division++) {
        float y = plot.y + plot.height * division / 4.0f;
        float value = maximum - (maximum - minimum) * division / 4.0f;
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 (Color){ 170, 190, 200, division == 4 ? 100 : 40 });
        snprintf(text, sizeof(text), "%.0f", value);
        DrawText(text, (int)plot.x - MeasureText(text, 16) - 10,
                 (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
    }

    float bar_width = plot.width / 124.0f;
    Vector2 mouse = GetMousePosition();
    int hover = (!app->scan_running) ? scan_arfcn_at(app, mouse) : 0;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        float power = app->scan_power[arfcn];
        if (power <= SCAN_SENTINEL_DBFS)
            continue;
        float level = (power - minimum) / (maximum - minimum);
        if (level < 0.0f)
            level = 0.0f;
        if (level > 1.0f)
            level = 1.0f;
        float x = plot.x + (float)(arfcn - 1) * bar_width;
        float height = level * plot.height;
        int is_bcch = app->scan_bcch_conf[arfcn] >= SCAN_BCCH_MIN_CONF;
        Color color = is_bcch ? (Color){ 99, 228, 170, 255 }
                              : (Color){ 90, 140, 210, 220 };
        if (arfcn == hover)
            color = (Color){ 255, 202, 105, 255 };
        int width = (int)(bar_width > 1.0f ? bar_width - 1.0f : 1.0f);
        DrawRectangle((int)x, (int)(plot.y + plot.height - height),
                      width, (int)height, color);
        /* A cap marker keeps a short BCCH bar visible. */
        if (is_bcch)
            DrawRectangle((int)x, (int)(plot.y + plot.height - height) - 4,
                          width, 4, (Color){ 139, 255, 205, 255 });
    }

    /* ARFCN axis labels every 10 channels. */
    for (int arfcn = 10; arfcn <= 120; arfcn += 10) {
        float x = plot.x + ((float)(arfcn - 1) + 0.5f) * bar_width;
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + 6,
                 (Color){ 126, 151, 166, 255 });
        snprintf(text, sizeof(text), "%d", arfcn);
        DrawText(text, (int)x - MeasureText(text, 16) / 2,
                 (int)(plot.y + plot.height) + 10, 16,
                 (Color){ 151, 174, 188, 255 });
    }
    DrawText("ARFCN (GSM 900 downlink, 200 kHz spacing)   green = BCCH (FCCH tone present)",
             (int)plot.x, (int)(plot.y + plot.height + 34), 16,
             (Color){ 187, 205, 216, 255 });

    if (hover > 0 && app->scan_power[hover] > SCAN_SENTINEL_DBFS) {
        uint32_t frequency = 935000000U + (uint32_t)hover * 200000U;
        if (app->scan_bcch_conf[hover] >= SCAN_BCCH_MIN_CONF)
            snprintf(text, sizeof(text),
                     "ARFCN %d   %.3f MHz   %.1f dBFS   BCCH conf %.2f",
                     hover, frequency / 1000000.0, app->scan_power[hover],
                     app->scan_bcch_conf[hover]);
        else
            snprintf(text, sizeof(text),
                     "ARFCN %d   %.3f MHz   %.1f dBFS   FCCH coh %.2f",
                     hover, frequency / 1000000.0, app->scan_power[hover],
                     app->scan_bcch_conf[hover]);
        DrawText(text, (int)mouse.x + 12, (int)mouse.y - 24, 16,
                 (Color){ 235, 242, 246, 255 });
    }
}

static void handle_scan_input(struct app *app) {
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };
    Rectangle rescan = { (float)GetScreenWidth() - 212.0f, 18, 88, 34 };

    if (clicked(back) || IsKeyPressed(KEY_ESCAPE)) {
        if (app->receiver_mode)
            retune_receiver(app, app->scan_return_frequency, app->applied_ppm);
        app->scan_running = 0;
        app->scan_open = 0;
        return;
    }
    if (app->scan_running)
        return;
    if (clicked(rescan)) {
        start_scan(app);
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int arfcn = scan_arfcn_at(app, GetMousePosition());
        if (arfcn > 0 && app->scan_power[arfcn] > SCAN_SENTINEL_DBFS) {
            snprintf(app->calibration_channel,
                     sizeof(app->calibration_channel), "%d", arfcn);
            app->calibration_channel_length =
                (int)strlen(app->calibration_channel);
            app->scan_open = 0;
            start_calibration(app);
        }
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

static int run_gui(struct app *app) {
    struct slot_snapshot snapshot;
    int result = 0;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1100, 720, "rtl_raylib signal visualizer");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create raylib window.\n");
        return -1;
    }
    app->window_ready = 1;
    SetExitKey(KEY_NULL);
    SetWindowMinSize(1000, 540);
    SetTargetFPS(60);
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
    int thread_result = pthread_create(
        &app->worker, NULL,
        app->receiver_mode ? receiver_worker : file_worker, app);
    if (thread_result == 0)
        app->worker_started = 1;
    int restore_result = pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
    if (restore_result != 0) {
        fprintf(stderr, "Cannot restore main-thread signal mask: %s\n",
                strerror(restore_result));
        request_worker_stop(app);
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

        if (app->scan_open) {
            handle_scan_input(app);
        } else if (app->calibration_open) {
            handle_calibration_input(app);
        } else if (app->settings_open) {
            if (IsKeyPressed(KEY_ESCAPE))
                app->settings_open = 0;
            else
                handle_settings_input(app);
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            break;
        } else if (clicked(settings_button()) || IsKeyPressed(KEY_S)) {
            open_settings(app);
        } else if (clicked(calibration_button()) || IsKeyPressed(KEY_C)) {
            open_calibration(app);
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

        if (!app->settings_open && !app->calibration_open) {
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
        int have_new = consume_latest(app, &snapshot);
        int spectrum_updated = have_new ? process_block(app, now) : 0;
        if (spectrum_updated) {
            update_waterfall(app);
            update_scan(app);
            update_calibration_measurement(app);
        }
        update_scatter(app, now,
                       have_new && app->view == VIEW_SCATTER);

        BeginDrawing();
        ClearBackground((Color){ 12, 19, 28, 255 });
        if (app->scan_open) {
            draw_scan(app);
        } else if (app->calibration_open) {
            draw_calibration(app);
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
            draw_button(settings_button(), "Settings", 0);
            draw_button(calibration_button(), "Calibration", 0);
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
    app->magnitude_lower = 0.0f;
    app->magnitude_upper = 64.0f;
    app->spectrum_lower_dbfs = SDR_DSP_DBFS_FLOOR;
    app->scatter_axis_limit = 0.5f;
    app->waterfall_lower_dbfs = SDR_DSP_DBFS_FLOOR;

    if (app->receiver_mode) {
        if (configure_receiver(app) < 0)
            goto cleanup;
    } else {
        if (open_capture(app) < 0)
            goto cleanup;
    }

    int mutex_result = pthread_mutex_init(&app->latest.mutex, NULL);
    if (mutex_result != 0) {
        fprintf(stderr, "Cannot initialize acquisition mutex: %s\n",
                strerror(mutex_result));
        goto cleanup;
    }
    app->mutex_ready = 1;
    if (install_signal_handlers(app) < 0)
        goto cleanup;

    gui_result = run_gui(app);
    result = gui_result == 0 ? 0 : 1;

cleanup:
    request_worker_stop(app);
    if (app->worker_started) {
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
        int join_result = pthread_join(app->worker, NULL);
        if (join_result != 0) {
            fprintf(stderr, "Cannot join acquisition worker: %s\n",
                    strerror(join_result));
            fprintf(stderr,
                    "Worker ownership is uncertain; exiting without releasing shared state.\n");
            return 1;
        }
        app->worker_started = 0;
    }

    if (app->mutex_ready) {
        int destroy_result = pthread_mutex_destroy(&app->latest.mutex);
        if (destroy_result != 0) {
            fprintf(stderr, "Cannot destroy acquisition mutex: %s\n",
                    strerror(destroy_result));
            result = 1;
        }
        app->mutex_ready = 0;
    }
    if (app->capture) {
        if (fclose(app->capture) != 0) {
            fprintf(stderr, "Cannot close capture: %s\n", strerror(errno));
            result = 1;
        }
        app->capture = NULL;
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
