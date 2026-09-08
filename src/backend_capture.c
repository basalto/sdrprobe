/*
 * A capture on disk, as a device.
 *
 * File playback was never a backend -- it was a `FILE *` in `struct app` and
 * an `if (!app->receiver_mode)` in front of every retune. This makes it the
 * second implementation of `device_backend`, which is what stops that
 * interface being a pass-through before the hardware arrives.
 *
 * Nearly every operation is a refusal, and that is the content rather than a
 * shortfall: a capture holds one tuning at one rate with one gain baked into
 * its samples, and the value of saying so here is that the refusals stop being
 * scattered through the callers. `profile.can_retune` is false, so a caller
 * that asks first never gets a refusal it did not expect.
 */

#define _POSIX_C_SOURCE 200809L

#include "device_backend.h"
#include "capture_sidecar.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct capture_handle {
    FILE *file;
    char path[1024];
    uint64_t bytes;      /* whole pairs only */
    uint32_t frequency_hz;
    uint32_t sample_rate_hz;
    int ppm;
    int loop;
    volatile int stop;
};

/*
 * Opening takes a path rather than an index, and there is no enumeration, so
 * this backend is filled in by `device_backend_capture_open()` below rather
 * than through the vtable's `open`. The vtable entry refuses, which is
 * honest: there is no such thing as "capture number 2".
 */
static int capture_open_by_index(struct device_session *s, int index,
                                 struct device_profile *out) {
    (void)s;
    (void)index;
    (void)out;
    return -1;
}

static void capture_close(struct device_session *s) {
    struct capture_handle *h = s ? s->handle : NULL;
    if (!h)
        return;
    if (h->file)
        fclose(h->file);
    free(h);
    s->handle = NULL;
}

/* One place on the band, one rate, one gain. All three are facts about the
   recording and none of them can be changed by asking. */
static int capture_refuse_u32(struct device_session *s, uint32_t hz) {
    (void)s;
    (void)hz;
    return -1;
}

static int capture_refuse_gain(struct device_session *s, int manual,
                               int value) {
    (void)s;
    (void)manual;
    (void)value;
    return -1;
}

static int capture_frequency(struct device_session *s, uint32_t *out) {
    struct capture_handle *h = s ? s->handle : NULL;
    if (!h || !out)
        return -1;
    *out = h->frequency_hz;
    return 0;
}

static int capture_sample_rate(struct device_session *s, uint32_t *out) {
    struct capture_handle *h = s ? s->handle : NULL;
    if (!h || !out)
        return -1;
    *out = h->sample_rate_hz;
    return 0;
}

static int capture_set_ppm(struct device_session *s, int ppm) {
    (void)s;
    (void)ppm;
    /* Whatever error the receiver had is already in the samples. There is
       nothing left to correct, and pretending to correct it would move a
       number without moving the signal. */
    return -1;
}

static int capture_ppm(struct device_session *s, int *out) {
    struct capture_handle *h = s ? s->handle : NULL;
    if (!h || !out)
        return -1;
    *out = h->ppm;
    return 0;
}

static int capture_gain(struct device_session *s, int *out) {
    (void)s;
    (void)out;
    return -1;
}

/*
 * Nothing is in flight, so there is nothing to throw away, and **0 is the
 * truthful answer rather than a stub**. A caller retunes and flushes; on a
 * source that cannot retune the flush has nothing to do and saying so lets
 * the caller keep one code path.
 */
static int capture_flush(struct device_session *s) {
    (void)s;
    return 0;
}

static int capture_stop(struct device_session *s) {
    struct capture_handle *h = s ? s->handle : NULL;
    if (!h)
        return -1;
    h->stop = 1;
    return 0;
}

/*
 * Read the file in blocks until it ends or `stop` is set.
 *
 * Deliberately **not paced**: pacing is the acquisition layer's business,
 * because it is what makes playback look like a receiver and what a headless
 * run turns off to get the same answer twice (`acquisition_set_lossless`).
 * A backend that paced would take that choice away from the layer that owns
 * it.
 */
static int capture_stream(struct device_session *s, device_block_fn cb,
                          void *ctx, uint32_t block_bytes) {
    struct capture_handle *h = s ? s->handle : NULL;
    uint8_t *block;
    uint64_t position = 0;

    if (!h || !h->file || !cb || block_bytes == 0)
        return -1;
    block = malloc(block_bytes);
    if (!block)
        return -1;

    h->stop = 0;
    while (!h->stop) {
        size_t filled = 0;

        while (filled < block_bytes && !h->stop) {
            uint64_t available = h->bytes - position;
            size_t wanted = block_bytes - filled;
            size_t got;

            if (available == 0) {
                if (!h->loop)
                    break;
                if (fseeko(h->file, 0, SEEK_SET) != 0) {
                    free(block);
                    return -1;
                }
                position = 0;
                continue;
            }
            if (available < wanted)
                wanted = (size_t)available;
            got = fread(block + filled, 1, wanted, h->file);
            if (got != wanted) {
                free(block);
                return -1;
            }
            filled += got;
            position += got;
        }
        if (filled == 0)
            break;
        cb(ctx, block, (uint32_t)filled);
        if (filled < block_bytes && !h->loop)
            break;
    }
    free(block);
    return 0;
}

static const struct device_backend capture_backend = {
    "capture",
    capture_open_by_index,
    capture_close,
    capture_refuse_u32,   /* set_frequency_hz */
    capture_frequency,
    capture_refuse_u32,   /* set_sample_rate_hz */
    capture_sample_rate,
    capture_set_ppm,
    capture_ppm,
    capture_refuse_gain,
    capture_gain,
    capture_flush,
    capture_stream,
    capture_stop,
};

const struct device_backend *device_backend_capture(void) {
    return &capture_backend;
}

/*
 * Open a capture by path, which is the way a capture is actually named.
 *
 * The sidecar decides the container and the full scale, exactly as
 * `open_capture()` in sdrprobe.c does -- a capture that says nothing is the
 * house 8-bit convention. The length is rounded down to whole pairs *of this
 * container*, which is why the sidecar has to be read first: rounding to an
 * even byte count is a two-byte format's answer.
 */
int device_backend_capture_open(struct device_session *s, const char *path,
                                uint32_t frequency_hz, uint32_t sample_rate_hz,
                                int ppm, int loop,
                                struct device_profile *out) {
    struct capture_sidecar sidecar;
    struct capture_handle *h;
    off_t size;

    if (!s || !path || !out)
        return -1;
    if (capture_sidecar_read(path, &sidecar) < 0) {
        fprintf(stderr,
                "Capture %s names a sample format but no full scale; it "
                "cannot be read in dBFS.\n",
                path);
        return -1;
    }

    *out = device_profile_capture(path, sidecar.format, sidecar.full_scale,
                                  (double)frequency_hz, sample_rate_hz);
    if (out->bytes_per_pair == 0) {
        fprintf(stderr, "Capture %s: unsupported sample format.\n", path);
        return -1;
    }

    h = calloc(1, sizeof(*h));
    if (!h)
        return -1;
    h->file = fopen(path, "rb");
    if (!h->file) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        free(h);
        return -1;
    }
    if (fseeko(h->file, 0, SEEK_END) != 0 || (size = ftello(h->file)) < 0 ||
        fseeko(h->file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Cannot inspect %s: %s\n", path, strerror(errno));
        fclose(h->file);
        free(h);
        return -1;
    }
    if ((uint64_t)size < out->bytes_per_pair) {
        fprintf(stderr, "Capture %s has no complete I/Q pair.\n", path);
        fclose(h->file);
        free(h);
        return -1;
    }

    snprintf(h->path, sizeof(h->path), "%s", path);
    h->bytes = (uint64_t)size - ((uint64_t)size % out->bytes_per_pair);
    h->frequency_hz = frequency_hz;
    h->sample_rate_hz = sample_rate_hz;
    h->ppm = ppm;
    h->loop = loop;

    s->backend = &capture_backend;
    s->handle = h;
    return 0;
}

uint64_t device_backend_capture_bytes(const struct device_session *s) {
    const struct capture_handle *h = s ? s->handle : NULL;
    return h ? h->bytes : 0;
}
