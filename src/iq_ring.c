#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "iq_ring.h"

int iq_ring_init(struct iq_ring *ring, double duration_seconds) {
    if (!ring)
        return -1;
    memset(ring, 0, sizeof(*ring));
    if (pthread_mutex_init(&ring->mutex, NULL) != 0)
        return -1;
    ring->mutex_ready = 1;
    ring->duration_seconds = duration_seconds > 0.0 ? duration_seconds
                                                    : IQ_RING_DEFAULT_SECONDS;
    ring->bytes_per_pair = 2;
    ring->full_scale = 127.5f;
    return 0;
}

void iq_ring_free(struct iq_ring *ring) {
    if (!ring)
        return;
    if (ring->mutex_ready) {
        pthread_mutex_lock(&ring->mutex);
        if (ring->buffer) {
            free(ring->buffer);
            ring->buffer = NULL;
        }
        ring->capacity_bytes = 0;
        ring->head = 0;
        ring->filled_bytes = 0;
        pthread_mutex_unlock(&ring->mutex);
        pthread_mutex_destroy(&ring->mutex);
        ring->mutex_ready = 0;
    }
}

int iq_ring_configure(struct iq_ring *ring, uint32_t sample_rate,
                      enum sample_format format, float full_scale,
                      uint32_t frequency_hz, int gain_tenths,
                      int manual_gain, int ppm,
                      const char *source, const char *tuner) {
    if (!ring || !ring->mutex_ready || sample_rate == 0)
        return -1;

    pthread_mutex_lock(&ring->mutex);
    ring->sample_rate = sample_rate;
    ring->format = format;
    ring->full_scale = full_scale > 0.0f ? full_scale : 127.5f;
    ring->bytes_per_pair = device_format_bytes_per_pair(format);
    if (ring->bytes_per_pair == 0)
        ring->bytes_per_pair = 2;

    ring->frequency_hz = frequency_hz;
    ring->gain_tenths = gain_tenths;
    ring->manual_gain = manual_gain;
    ring->ppm = ppm;
    snprintf(ring->source, sizeof(ring->source), "%s", source ? source : "RTL-SDR");
    snprintf(ring->tuner, sizeof(ring->tuner), "%s", tuner ? tuner : "R820T");

    /* Reconfiguration resets history so old samples are never paired with new tuning/format */
    ring->head = 0;
    ring->filled_bytes = 0;

    size_t needed_bytes = (size_t)(ring->duration_seconds *
                                  (double)sample_rate *
                                  (double)ring->bytes_per_pair);
    /* Align to pair boundary */
    needed_bytes &= ~(size_t)(ring->bytes_per_pair - 1);

    if (needed_bytes != ring->capacity_bytes) {
        unsigned char *new_buf = realloc(ring->buffer, needed_bytes);
        if (!new_buf) {
            pthread_mutex_unlock(&ring->mutex);
            return -1;
        }
        ring->buffer = new_buf;
        ring->capacity_bytes = needed_bytes;
    }
    pthread_mutex_unlock(&ring->mutex);
    return 0;
}

void iq_ring_push(struct iq_ring *ring, const unsigned char *data,
                  size_t len, double now) {
    if (!ring || !ring->mutex_ready || !ring->buffer ||
        ring->capacity_bytes == 0 || !data || len == 0)
        return;

    pthread_mutex_lock(&ring->mutex);
    if (len >= ring->capacity_bytes) {
        /* Data exceeds whole ring: keep only the tail */
        const unsigned char *tail = data + (len - ring->capacity_bytes);
        memcpy(ring->buffer, tail, ring->capacity_bytes);
        ring->head = 0;
        ring->filled_bytes = ring->capacity_bytes;
    } else {
        size_t first = ring->capacity_bytes - ring->head;
        if (len <= first) {
            memcpy(ring->buffer + ring->head, data, len);
            ring->head = (ring->head + len) % ring->capacity_bytes;
        } else {
            memcpy(ring->buffer + ring->head, data, first);
            memcpy(ring->buffer, data + first, len - first);
            ring->head = len - first;
        }
        ring->filled_bytes += len;
        if (ring->filled_bytes > ring->capacity_bytes)
            ring->filled_bytes = ring->capacity_bytes;
    }
    ring->latest_now = now;
    pthread_mutex_unlock(&ring->mutex);
}

double iq_ring_available_seconds(struct iq_ring *ring) {
    double sec = 0.0;
    if (!ring || !ring->mutex_ready)
        return 0.0;
    pthread_mutex_lock(&ring->mutex);
    if (ring->sample_rate > 0 && ring->bytes_per_pair > 0)
        sec = (double)ring->filled_bytes /
              ((double)ring->sample_rate * (double)ring->bytes_per_pair);
    pthread_mutex_unlock(&ring->mutex);
    return sec;
}

int iq_ring_extract_slice(struct iq_ring *ring, double age_seconds,
                          double duration_seconds, unsigned char *out_buf,
                          size_t max_bytes, size_t *actual_bytes_out,
                          uint32_t *sample_rate_out, uint32_t *freq_out,
                          enum sample_format *format_out, float *full_scale_out) {
    if (!ring || !ring->mutex_ready || !out_buf || max_bytes == 0 ||
        duration_seconds <= 0.0)
        return -1;

    pthread_mutex_lock(&ring->mutex);
    if (ring->filled_bytes == 0 || ring->sample_rate == 0 ||
        ring->bytes_per_pair == 0 || !ring->buffer) {
        pthread_mutex_unlock(&ring->mutex);
        return -1;
    }

    /* Start and end ages in the past */
    double half = duration_seconds / 2.0;
    double age_end = age_seconds - half;
    double age_start = age_seconds + half;

    if (age_end < 0.0) {
        age_end = 0.0;
        age_start = duration_seconds;
    }

    size_t bytes_per_sec = (size_t)ring->sample_rate * (size_t)ring->bytes_per_pair;
    size_t offset_back = (size_t)round(age_start * (double)bytes_per_sec);
    /* Align to pair */
    offset_back &= ~(size_t)(ring->bytes_per_pair - 1);

    if (offset_back > ring->filled_bytes)
        offset_back = ring->filled_bytes;

    size_t req_bytes = (size_t)round(duration_seconds * (double)bytes_per_sec);
    req_bytes &= ~(size_t)(ring->bytes_per_pair - 1);

    if (req_bytes > offset_back)
        req_bytes = offset_back;
    if (req_bytes > max_bytes)
        req_bytes = max_bytes & ~(size_t)(ring->bytes_per_pair - 1);

    if (req_bytes == 0) {
        pthread_mutex_unlock(&ring->mutex);
        return -1;
    }

    /*
     * `head` points to the next byte to be written (newest + 1).
     * The start of our slice is `offset_back` bytes behind `head`.
     */
    size_t start_pos = (ring->head + ring->capacity_bytes - offset_back) %
                       ring->capacity_bytes;

    size_t first = ring->capacity_bytes - start_pos;
    if (req_bytes <= first) {
        memcpy(out_buf, ring->buffer + start_pos, req_bytes);
    } else {
        memcpy(out_buf, ring->buffer + start_pos, first);
        memcpy(out_buf + first, ring->buffer, req_bytes - first);
    }

    if (actual_bytes_out)
        *actual_bytes_out = req_bytes;
    if (sample_rate_out)
        *sample_rate_out = ring->sample_rate;
    if (freq_out)
        *freq_out = ring->frequency_hz;
    if (format_out)
        *format_out = ring->format;
    if (full_scale_out)
        *full_scale_out = ring->full_scale;

    pthread_mutex_unlock(&ring->mutex);
    return 0;
}

struct iq_snapshot *iq_ring_extract_snapshot(struct iq_ring *ring,
                                             double age_seconds,
                                             double duration_seconds) {
    if (!ring || !ring->mutex_ready || duration_seconds <= 0.0)
        return NULL;

    pthread_mutex_lock(&ring->mutex);
    if (ring->filled_bytes == 0 || ring->sample_rate == 0 ||
        ring->bytes_per_pair == 0 || !ring->buffer) {
        pthread_mutex_unlock(&ring->mutex);
        return NULL;
    }

    double half = duration_seconds / 2.0;
    double age_end = age_seconds - half;
    double age_start = age_seconds + half;

    if (age_end < 0.0) {
        age_end = 0.0;
        age_start = duration_seconds;
    }

    size_t bytes_per_sec = (size_t)ring->sample_rate * (size_t)ring->bytes_per_pair;
    size_t offset_back = (size_t)round(age_start * (double)bytes_per_sec);
    offset_back &= ~(size_t)(ring->bytes_per_pair - 1);

    if (offset_back > ring->filled_bytes)
        offset_back = ring->filled_bytes;

    size_t req_bytes = (size_t)round(duration_seconds * (double)bytes_per_sec);
    req_bytes &= ~(size_t)(ring->bytes_per_pair - 1);

    if (req_bytes > offset_back)
        req_bytes = offset_back;

    if (req_bytes == 0) {
        pthread_mutex_unlock(&ring->mutex);
        return NULL;
    }

    struct iq_snapshot *snap = calloc(1, sizeof(*snap));
    if (!snap) {
        pthread_mutex_unlock(&ring->mutex);
        return NULL;
    }

    snap->data = malloc(req_bytes);
    if (!snap->data) {
        free(snap);
        pthread_mutex_unlock(&ring->mutex);
        return NULL;
    }

    size_t start_pos = (ring->head + ring->capacity_bytes - offset_back) %
                       ring->capacity_bytes;

    size_t first = ring->capacity_bytes - start_pos;
    if (req_bytes <= first) {
        memcpy(snap->data, ring->buffer + start_pos, req_bytes);
    } else {
        memcpy(snap->data, ring->buffer + start_pos, first);
        memcpy(snap->data + first, ring->buffer, req_bytes - first);
    }

    snap->byte_count = req_bytes;
    snap->pair_count = req_bytes / ring->bytes_per_pair;
    snap->duration_seconds = (double)snap->pair_count / (double)ring->sample_rate;
    snap->age_seconds = age_seconds;
    snap->sample_rate = ring->sample_rate;
    snap->frequency_hz = ring->frequency_hz;
    snap->format = ring->format;
    snap->full_scale = ring->full_scale;
    snap->bytes_per_pair = ring->bytes_per_pair;
    snap->gain_tenths = ring->gain_tenths;
    snap->manual_gain = ring->manual_gain;
    snap->ppm = ring->ppm;
    snprintf(snap->source, sizeof(snap->source), "%s", ring->source);
    snprintf(snap->tuner, sizeof(snap->tuner), "%s", ring->tuner);

    pthread_mutex_unlock(&ring->mutex);
    return snap;
}

void iq_snapshot_free(struct iq_snapshot *snap) {
    if (!snap)
        return;
    if (snap->data)
        free(snap->data);
    free(snap);
}

int iq_snapshot_save(const struct iq_snapshot *snap, const char *path,
                     const char *technology) {
    if (!snap || !snap->data || snap->byte_count == 0 || !path)
        return -1;

    FILE *bin = fopen(path, "wb");
    if (!bin)
        return -1;
    size_t written = fwrite(snap->data, 1, snap->byte_count, bin);
    fclose(bin);
    if (written != snap->byte_count)
        return -1;

    char sidecar[512];
    snprintf(sidecar, sizeof(sidecar), "%s.json", path);
    FILE *json = fopen(sidecar, "w");
    if (!json)
        return -1;

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    char started[32];
    strftime(started, sizeof(started), "%Y-%m-%dT%H:%M:%S", &local);

    fprintf(json, "{\n");
    fprintf(json, "  \"provenance\": \"extracted from sdrprobe ring buffer\",\n");
    fprintf(json, "  \"format\": \"%s\",\n",
            snap->format == SAMPLE_FORMAT_S16
                ? "signed 16-bit interleaved I/Q, host endian"
                : "unsigned 8-bit interleaved I/Q, mid-scale = zero");
    fprintf(json, "  \"full_scale\": %.1f,\n", snap->full_scale);
    fprintf(json, "  \"bytes_per_pair\": %u,\n", snap->bytes_per_pair);
    fprintf(json, "  \"technology\": \"%s\",\n", technology ? technology : "raw");
    fprintf(json, "  \"center_frequency_hz\": %u,\n", snap->frequency_hz);
    fprintf(json, "  \"sample_rate_hz\": %u,\n", snap->sample_rate);
    if (snap->manual_gain)
        fprintf(json, "  \"gain_db\": %.1f,\n", (double)snap->gain_tenths / 10.0);
    else
        fprintf(json, "  \"gain_db\": \"auto\",\n");
    fprintf(json, "  \"ppm\": %d,\n", snap->ppm);
    fprintf(json, "  \"source\": \"%s\",\n", snap->source);
    fprintf(json, "  \"tuner\": \"%s\",\n", snap->tuner);
    fprintf(json, "  \"started_at\": \"%s\",\n", started);
    fprintf(json, "  \"duration_seconds\": %.3f,\n", snap->duration_seconds);
    fprintf(json, "  \"bytes\": %llu,\n", (unsigned long long)snap->byte_count);
    fprintf(json, "  \"short_blocks\": 0\n");
    fprintf(json, "}\n");
    fclose(json);

    return 0;
}

int iq_ring_save_slice(struct iq_ring *ring, double age_seconds,
                       double duration_seconds, const char *path,
                       const char *technology) {
    struct iq_snapshot *snap = iq_ring_extract_snapshot(ring, age_seconds, duration_seconds);
    if (!snap)
        return -1;
    int rc = iq_snapshot_save(snap, path, technology);
    iq_snapshot_free(snap);
    return rc;
}
