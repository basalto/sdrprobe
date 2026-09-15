#ifndef IQ_RING_H
#define IQ_RING_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "device_profile.h"

/*
 * Transversal rolling ring buffer of raw I/Q samples.
 *
 * Maintains a rolling window of historical samples in memory (e.g. 30 seconds),
 * allowing retrospective capture extraction, signal analysis, and auto-saving of
 * detected transmissions.
 *
 * Thread-safe: pushed by the acquisition worker, read by UI / analysis threads.
 * Links -lm only.
 */

#define IQ_RING_DEFAULT_SECONDS 30.0

struct iq_ring {
    pthread_mutex_t mutex;
    int mutex_ready;
    unsigned char *buffer;
    size_t capacity_bytes;
    size_t head;            /* Next write offset in buffer */
    size_t filled_bytes;    /* Number of valid bytes currently in buffer */
    double duration_seconds;/* Desired history span in seconds */

    /* Metadata of current samples in buffer */
    uint32_t sample_rate;
    enum sample_format format;
    float full_scale;
    unsigned bytes_per_pair;
    uint32_t frequency_hz;
    int gain_tenths;
    int manual_gain;
    int ppm;
    char source[128];
    char tuner[32];
    double latest_now;      /* Monotonic time of newest block */
};

struct iq_snapshot {
    unsigned char *data;
    size_t byte_count;
    size_t pair_count;
    double duration_seconds;
    double age_seconds;

    /* Metadata active when these samples were captured */
    uint32_t sample_rate;
    uint32_t frequency_hz;
    enum sample_format format;
    float full_scale;
    unsigned bytes_per_pair;
    int gain_tenths;
    int manual_gain;
    int ppm;
    char source[128];
    char tuner[32];
};

/* Initialize ring buffer with target duration (e.g. 30.0s). */
int iq_ring_init(struct iq_ring *ring, double duration_seconds);

/* Free allocated memory and destroy mutex. */
void iq_ring_free(struct iq_ring *ring);

/* Reconfigure buffer size for new sample rate / format. */
int iq_ring_configure(struct iq_ring *ring, uint32_t sample_rate,
                      enum sample_format format, float full_scale,
                      uint32_t frequency_hz, int gain_tenths,
                      int manual_gain, int ppm,
                      const char *source, const char *tuner);

/* Push a new block of raw I/Q bytes into the ring buffer. */
void iq_ring_push(struct iq_ring *ring, const unsigned char *data,
                  size_t len, double now);

/*
 * Extract an owned sample snapshot carrying its bytes and coherent metadata.
 *
 * Caller owns the returned pointer and must free it with iq_snapshot_free().
 * Returns NULL if the ring has no data or inputs are invalid.
 */
struct iq_snapshot *iq_ring_extract_snapshot(struct iq_ring *ring,
                                             double age_seconds,
                                             double duration_seconds);

/* Free an extracted snapshot and its owned sample buffer. */
void iq_snapshot_free(struct iq_snapshot *snap);

/*
 * Save an extracted snapshot to `path` (.bin) and its JSON sidecar (.json).
 * Returns 0 on success, -1 on failure.
 */
int iq_snapshot_save(const struct iq_snapshot *snap, const char *path,
                     const char *technology);

/*
 * Extract a time slice centered at `age_seconds` ago with span `duration_seconds`.
 *
 * Deprecated caller-allocated form; use iq_ring_extract_snapshot() instead.
 * Returns 0 on success, -1 if no data is available.
 */
int iq_ring_extract_slice(struct iq_ring *ring, double age_seconds,
                          double duration_seconds, unsigned char *out_buf,
                          size_t max_bytes, size_t *actual_bytes_out,
                          uint32_t *sample_rate_out, uint32_t *freq_out,
                          enum sample_format *format_out, float *full_scale_out);

/*
 * Save a time slice centered at `age_seconds` ago with span `duration_seconds`
 * to `path` (.bin) and write a matching JSON sidecar (.json).
 */
int iq_ring_save_slice(struct iq_ring *ring, double age_seconds,
                       double duration_seconds, const char *path,
                       const char *technology);

/* Returns how many seconds of contiguous data are currently stored in the ring. */
double iq_ring_available_seconds(struct iq_ring *ring);

#endif /* IQ_RING_H */
