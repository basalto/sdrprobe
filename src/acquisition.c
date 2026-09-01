#define _POSIX_C_SOURCE 200809L

#include "acquisition.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Write the capture's companion metadata. A raw .bin says nothing about how it
   was made, and the tuning convention matters as much as the samples: reading
   testfiles/gsm_arfcn_69.bin correctly depends on knowing it was tuned 400 kHz
   below the channel, which lived only in prose. Called with record_mutex held.
   A failure here is reported but does not invalidate the .bin. */
static void record_write_sidecar(struct acquisition *acq, double seconds) {
    char path[sizeof(acq->record_path) + 8];
    size_t n = strlen(acq->record_path);
    if (n > 4 && strcmp(acq->record_path + n - 4, ".bin") == 0)
        snprintf(path, sizeof(path), "%.*s.json", (int)(n - 4),
                 acq->record_path);
    else
        snprintf(path, sizeof(path), "%s.json", acq->record_path);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Cannot write %s: %s\n", path, strerror(errno));
        return;
    }
    fprintf(f, "{\n");
    /* Distinguishes a sidecar written at record time from one reconstructed
       for an older capture, where some fields may be unknown. */
    fprintf(f, "  \"provenance\": \"recorded by sdrprobe\",\n");
    fprintf(f, "  \"format\": \"unsigned 8-bit interleaved I/Q, 127.5 = zero\",\n");
    if (acq->record_technology[0])
        fprintf(f, "  \"technology\": \"%s\",\n", acq->record_technology);
    fprintf(f, "  \"center_frequency_hz\": %u,\n", acq->record_frequency_hz);
    fprintf(f, "  \"sample_rate_hz\": %u,\n", acq->record_sample_rate);
    if (acq->record_manual_gain)
        fprintf(f, "  \"gain_db\": %.1f,\n", acq->record_gain_tenths / 10.0);
    else
        fprintf(f, "  \"gain_db\": \"auto\",\n");
    fprintf(f, "  \"ppm\": %d,\n", acq->record_ppm);
    if (acq->record_arfcn > 0) {
        fprintf(f, "  \"gsm_arfcn\": %d,\n", acq->record_arfcn);
        /* The channel sits this far above the tuned centre; a decoder needs
           it, and it is not recoverable from the samples alone. */
        fprintf(f, "  \"carrier_offset_hz\": %.0f,\n",
                acq->record_carrier_offset_hz);
    }
    fprintf(f, "  \"source\": \"%s\",\n", acq->record_source);
    fprintf(f, "  \"tuner\": \"%s\",\n", acq->record_tuner);
    fprintf(f, "  \"started_at\": \"%s\",\n", acq->record_started_at);
    fprintf(f, "  \"duration_seconds\": %.3f,\n", seconds);
    fprintf(f, "  \"bytes\": %llu,\n",
            (unsigned long long)acq->record_bytes);
    /* Non-zero means the capture is not contiguous: blocks arrived short, so
       samples are missing and any timeline derived from it is suspect. */
    fprintf(f, "  \"short_blocks\": %llu\n",
            (unsigned long long)acq->record_short_blocks);
    fprintf(f, "}\n");
    if (fclose(f) != 0)
        fprintf(stderr, "Cannot close %s: %s\n", path, strerror(errno));
}

static void record_capture(struct acquisition *acq, const unsigned char *data,
                           uint32_t len) {
    pthread_mutex_lock(&acq->record_mutex);
    if (!acq->recording || !acq->record_file) {
        pthread_mutex_unlock(&acq->record_mutex);
        return;
    }
    /* A block that is not the full size is a real gap in the signal, not just
       a short write; the capture is no longer contiguous and must say so. */
    if (len != SAMPLE_BLOCK_BYTES)
        acq->record_short_blocks++;
    size_t written = fwrite(data, 1, len, acq->record_file);
    acq->record_bytes += written;
    if (written != (size_t)len ||
        acq->record_bytes >= acq->record_limit_bytes) {
        int truncated = (written != (size_t)len);
        uint64_t bytes = acq->record_bytes;
        uint64_t shorts = acq->record_short_blocks;
        char path[sizeof(acq->record_path)];
        snprintf(path, sizeof(path), "%s", acq->record_path);
        record_write_sidecar(acq, (double)acq->record_bytes /
                                  ((double)acq->record_sample_rate * 2.0));
        fclose(acq->record_file);
        acq->record_file = NULL;
        acq->recording = 0;
        pthread_mutex_unlock(&acq->record_mutex);
        if (truncated)
            fprintf(stderr, "Recording %s truncated: %s\n", path,
                    strerror(errno));
        else if (shorts)
            fprintf(stderr,
                    "Recorded %.1f MB to %s, but %llu block(s) were short: "
                    "the capture is NOT contiguous.\n",
                    bytes / 1e6, path, (unsigned long long)shorts);
        else
            fprintf(stderr, "Recorded %.1f MB to %s\n", bytes / 1e6, path);
        return;
    }
    pthread_mutex_unlock(&acq->record_mutex);
}

void publish_block(struct acquisition *acq, const unsigned char *data,
                          uint32_t len) {
    struct latest_block *latest = &acq->latest;
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
    /* Wait for the slot to be emptied rather than overwriting it. Only a file
       worker ever does this; request_worker_stop() broadcasts, so a shutdown
       does not leave the worker waiting on a consumer that has stopped. */
    while (latest->lossless && latest->ready && !latest->stop)
        pthread_cond_wait(&latest->drained, &latest->mutex);
    if (latest->stop) {
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

    record_capture(acq, data, valid_len);
}

static void finish_worker(struct acquisition *acq, const char *error) {
    pthread_mutex_lock(&acq->latest.mutex);
    acq->latest.worker_reading = 0;
    acq->latest.worker_done = 1;
    if (error) {
        acq->latest.worker_failed = 1;
        snprintf(acq->latest.worker_error, sizeof(acq->latest.worker_error),
                 "%s", error);
    }
    pthread_mutex_unlock(&acq->latest.mutex);
}

static int begin_worker_read(struct acquisition *acq) {
    int begin;

    pthread_mutex_lock(&acq->latest.mutex);
    begin = !acq->latest.stop;
    if (begin)
        acq->latest.worker_reading = 1;
    else
        acq->latest.worker_done = 1;
    pthread_mutex_unlock(&acq->latest.mutex);
    return begin;
}

void receiver_callback(unsigned char *buffer, uint32_t len, void *ctx) {
    struct acquisition *acq = ctx;

    publish_block(acq, buffer, len);
}

void *receiver_worker(void *arg) {
    struct acquisition *acq = arg;
    int result;
    char error[160];

    if (!begin_worker_read(acq))
        return NULL;
    result = rtlsdr_read_async(acq->dev, receiver_callback, acq, 0,
                               SAMPLE_BLOCK_BYTES);
    pthread_mutex_lock(&acq->latest.mutex);
    int stopped = acq->latest.stop;
    pthread_mutex_unlock(&acq->latest.mutex);
    if (!stopped) {
        if (result < 0)
            snprintf(error, sizeof(error),
                     "RTL-SDR asynchronous read failed (%d)", result);
        else
            snprintf(error, sizeof(error),
                     "RTL-SDR asynchronous acquisition ended unexpectedly");
        finish_worker(acq, error);
    } else {
        finish_worker(acq, NULL);
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

int worker_stop_requested(struct acquisition *acq) {
    int stop;

    pthread_mutex_lock(&acq->latest.mutex);
    stop = acq->latest.stop;
    pthread_mutex_unlock(&acq->latest.mutex);
    return stop;
}

void request_worker_stop(struct acquisition *acq) {
    if (!acq->mutex_ready)
        return;
    pthread_mutex_lock(&acq->latest.mutex);
    acq->latest.stop = 1;
    /* A lossless publisher may be waiting for a consumer that will never come
       back; without this the join below it never returns. */
    pthread_cond_broadcast(&acq->latest.drained);
    pthread_mutex_unlock(&acq->latest.mutex);
}

static int sleep_until(struct acquisition *acq, struct timespec deadline) {
    while (!worker_stop_requested(acq)) {
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
        } while (sleep_result == EINTR && !worker_stop_requested(acq));
        if (sleep_result != 0 && sleep_result != EINTR)
            return sleep_result;
    }
    return 0;
}

void *file_worker(void *arg) {
    struct acquisition *acq = arg;
    unsigned char *block = acq->file_block;
    uint64_t position = 0;
    uint64_t published_pairs = 0;
    struct timespec start;
    char error[160];
    int lossless;

    if (!begin_worker_read(acq))
        return NULL;
    pthread_mutex_lock(&acq->latest.mutex);
    lossless = acq->latest.lossless;
    pthread_mutex_unlock(&acq->latest.mutex);
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        snprintf(error, sizeof(error), "Cannot read monotonic clock: %s",
                 strerror(errno));
        finish_worker(acq, error);
        return NULL;
    }

    while (!worker_stop_requested(acq)) {
        size_t filled = 0;

        while (filled < SAMPLE_BLOCK_BYTES && !worker_stop_requested(acq)) {
            uint64_t available = acq->capture_bytes - position;
            size_t wanted = SAMPLE_BLOCK_BYTES - filled;
            if (available < wanted)
                wanted = (size_t)available;
            size_t got = fread(block + filled, 1, wanted, acq->capture);
            if (got != wanted) {
                if (ferror(acq->capture))
                    snprintf(error, sizeof(error), "Capture read failed: %s",
                             strerror(errno));
                else
                    snprintf(error, sizeof(error),
                             "Capture ended before its measured length");
                finish_worker(acq, error);
                return NULL;
            }
            filled += got;
            position += got;
            if (position == acq->capture_bytes) {
                /* One pass only: publish whatever the last block holds and
                   report a clean end, so a script decoding a capture stops
                   instead of decoding it again. */
                if (!acq->capture_loop) {
                    if (filled > 0)
                        publish_block(acq, block, (uint32_t)filled);
                    finish_worker(acq, NULL);
                    return NULL;
                }
                if (fseeko(acq->capture, 0, SEEK_SET) != 0) {
                    snprintf(error, sizeof(error), "Cannot loop capture: %s",
                             strerror(errno));
                    finish_worker(acq, error);
                    return NULL;
                }
                position = 0;
            }
        }
        if (worker_stop_requested(acq))
            break;

        publish_block(acq, block, SAMPLE_BLOCK_BYTES);
        published_pairs += SAMPLE_BLOCK_PAIRS;
        /* Pacing exists so playback looks like a receiver. A lossless run has
           no display to feed and publish_block already waits for the consumer,
           so pacing would only make the check slower. */
        if (lossless)
            continue;
        struct timespec deadline = playback_deadline(
            start, published_pairs, acq->sample_rate);
        int sleep_result = sleep_until(acq, deadline);
        if (sleep_result != 0) {
            snprintf(error, sizeof(error), "Capture pacing failed: %s",
                     strerror(sleep_result));
            finish_worker(acq, error);
            return NULL;
        }
    }

    finish_worker(acq, NULL);
    return NULL;
}

int consume_latest(struct acquisition *acq, struct slot_snapshot *snapshot) {
    struct latest_block *latest = &acq->latest;
    int have_new = 0;

    pthread_mutex_lock(&latest->mutex);
    if (latest->ready && latest->generation != acq->consumed_generation) {
        memcpy(acq->raw, latest->data, latest->len);
        acq->raw_len = latest->len;
        acq->consumed_generation = latest->generation;
        latest->ready = 0;
        latest->processed_blocks++;
        have_new = 1;
        pthread_cond_signal(&latest->drained);
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


/* Begin recording to `path`. The tuning in `req` is only ever written to the
   sidecar: acquisition does not know how the receiver is tuned, so the caller
   supplies it. Snapshotting it here means the worker never reads live state. */
int acquisition_start_recording(struct acquisition *acq, const char *path,
                                const struct acquisition_record_request *req) {
    pthread_mutex_lock(&acq->record_mutex);
    int busy = acq->recording;
    pthread_mutex_unlock(&acq->record_mutex);
    if (busy)
        return -1;

    FILE *file = fopen(path, "wb");
    if (!file)
        return -1;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char started[32];
    strftime(started, sizeof(started), "%Y-%m-%dT%H:%M:%S", &local);

    pthread_mutex_lock(&acq->record_mutex);
    snprintf(acq->record_path, sizeof(acq->record_path), "%s", path);
    acq->record_frequency_hz = req->frequency_hz;
    acq->record_sample_rate = req->sample_rate;
    acq->record_gain_tenths = req->gain_tenths;
    acq->record_manual_gain = req->manual_gain;
    acq->record_ppm = req->ppm;
    acq->record_arfcn = req->arfcn;
    acq->record_carrier_offset_hz = req->carrier_offset_hz;
    snprintf(acq->record_technology, sizeof(acq->record_technology), "%s",
             req->technology ? req->technology : "");
    snprintf(acq->record_source, sizeof(acq->record_source), "%s",
             req->source ? req->source : "");
    snprintf(acq->record_tuner, sizeof(acq->record_tuner), "%s",
             req->tuner && req->tuner[0] ? req->tuner : "n/a (file playback)");
    snprintf(acq->record_started_at, sizeof(acq->record_started_at), "%s", started);
    acq->record_file = file;
    acq->record_bytes = 0;
    acq->record_short_blocks = 0;
    double seconds = req->seconds > 0.0 ? req->seconds
                                       : ACQUISITION_RECORD_BUTTON_SECONDS;
    acq->record_limit_bytes =
        (uint64_t)(seconds * (double)req->sample_rate * 2.0);
    acq->recording = 1; /* the acquisition thread writes from here on */
    pthread_mutex_unlock(&acq->record_mutex);
    return 0;
}

/* Snapshot for the UI, which does not own this state. */
int acquisition_recording_status(struct acquisition *acq, uint64_t *bytes,
                                 char *path, size_t path_size) {
    pthread_mutex_lock(&acq->record_mutex);
    int active = acq->recording;
    if (bytes)
        *bytes = acq->record_bytes;
    if (path && path_size)
        snprintf(path, path_size, "%s", acq->record_path);
    pthread_mutex_unlock(&acq->record_mutex);
    return active;
}

/* Close an in-progress recording, writing its sidecar. Safe to call when
   nothing is recording. */
void acquisition_stop_recording(struct acquisition *acq) {
    pthread_mutex_lock(&acq->record_mutex);
    if (acq->record_file) {
        fprintf(stderr, "Recording %s stopped early at %.1f MB\n",
                acq->record_path, acq->record_bytes / 1e6);
        record_write_sidecar(acq, (double)acq->record_bytes /
                                  ((double)acq->record_sample_rate * 2.0));
        fclose(acq->record_file);
        acq->record_file = NULL;
        acq->recording = 0;
    }
    pthread_mutex_unlock(&acq->record_mutex);
}


int acquisition_init(struct acquisition *acq) {
    return pthread_mutex_init(&acq->record_mutex, NULL);
}

void acquisition_destroy(struct acquisition *acq) {
    acquisition_stop_recording(acq);
    pthread_mutex_destroy(&acq->record_mutex);
}


void acquisition_set_lossless(struct acquisition *acq, int lossless) {
    pthread_mutex_lock(&acq->latest.mutex);
    acq->latest.lossless = lossless;
    pthread_mutex_unlock(&acq->latest.mutex);
}

void acquisition_attach_source(struct acquisition *acq, rtlsdr_dev_t *dev,
                               FILE *capture, uint32_t sample_rate,
                               const char *capture_path, int capture_loop) {
    acq->dev = dev;
    acq->capture = capture;
    acq->sample_rate = sample_rate;
    acq->capture_path = capture_path;
    acq->capture_loop = capture_loop;
}
