#ifndef DEVICE_BACKEND_H
#define DEVICE_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "device_profile.h"

/*
 * How a receiver is driven, as opposed to what it is.
 *
 * `device_profile.h` carries the **facts** -- the container, full scale, the
 * tuner's reach, the reference clock -- and deliberately carried no function
 * pointers, because `.scratch/device-model/spec.md` said behaviour that
 * genuinely differs gets them "*later*, when there is a second backend to
 * satisfy them". A B210-class device is on order, so later is now.
 *
 * There are three implementations, which is what keeps this from being the
 * pass-through the repository's deletion test warns about: librtlsdr, a
 * capture on disk, and UHD when the hardware lands. The check adds a fourth,
 * a fake, because a contract nobody can exercise without hardware is not a
 * contract (ADR-0012).
 *
 * **This header knows no driver.** It includes `<stdint.h>` and the profile,
 * and nothing else. `rtlsdr_dev_t *` used to be in `app.h` and `view.h`; the
 * handle is `void *` here and only the backend that made it knows what it is.
 *
 * Every entry point returns 0 for success and negative for failure, the house
 * convention, and a backend that cannot do a thing says so rather than
 * pretending: a capture refuses to retune, which is what the retune paths
 * already do by hand with `if (!app->receiver_mode)`.
 */

/* What a worker is handed when samples arrive. `bytes` is what the source
   produced, which is `SAMPLE_BLOCK_PAIRS * profile.bytes_per_pair` in the
   ordinary case and less at the end of a capture. */
typedef void (*device_block_fn)(void *ctx, const uint8_t *data, uint32_t bytes);

struct device_session;

struct device_backend {
    const char *name;

    /*
     * Open source `index` and fill in `out`. The profile is the backend's to
     * populate -- it is the only thing that knows the container, the reach and
     * the gain model -- and a caller must not assume it afterwards.
     */
    int (*open)(struct device_session *s, int index,
                struct device_profile *out);
    void (*close)(struct device_session *s);

    /* Tuning. A source that cannot move returns negative and changes nothing;
       `profile.can_retune` says so in advance. */
    int (*set_frequency_hz)(struct device_session *s, uint32_t hz);
    int (*frequency_hz)(struct device_session *s, uint32_t *out);
    int (*set_sample_rate_hz)(struct device_session *s, uint32_t hz);
    int (*sample_rate_hz)(struct device_session *s, uint32_t *out);
    int (*set_ppm)(struct device_session *s, int ppm);
    int (*ppm)(struct device_session *s, int *out);

    /* Gain. `manual` 0 means let the device decide; the value's unit is
       `profile.gain_unit`, which is why the panel needs no conversion. */
    int (*set_gain)(struct device_session *s, int manual, int value);
    int (*gain)(struct device_session *s, int *out);

    /*
     * Throw away whatever is in flight.
     *
     * This is `rtlsdr_reset_buffer()`, which appears thirteen times in this
     * program and is the most driver-specific thing in it -- after a retune
     * the USB pipeline still holds the previous tuning's samples, and
     * measuring them is the bug `survey_measure_settled()` exists for. A
     * backend with no such pipeline implements it as a no-op returning 0,
     * which is a truthful answer and not a stub.
     */
    int (*flush)(struct device_session *s);

    /* Stream until `stop` is called, handing each block to `cb`. Blocking:
       the caller runs it on its own thread. */
    int (*stream)(struct device_session *s, device_block_fn cb, void *ctx,
                  uint32_t block_bytes);
    int (*stop)(struct device_session *s);
};

/*
 * One open source. `handle` is the backend's own -- an `rtlsdr_dev_t *`, a
 * `FILE *`, a UHD object -- and nothing outside that backend may look at it.
 */
struct device_session {
    const struct device_backend *backend;
    void *handle;
};

/* Whether a session is open at all. A closed one answers every operation with
   a refusal rather than a crash, which is what makes the headless paths safe
   to write against. */
static inline int device_session_open(const struct device_session *s) {
    return s && s->backend && s->handle;
}

/*
 * The operations, as functions rather than as reached-through pointers.
 *
 * These exist so a caller writes `device_set_frequency_hz(&app->source, hz)`
 * instead of `app->source.backend->set_frequency_hz(&app->source, hz)`, and so
 * that a NULL entry -- a backend that does not implement an optional
 * operation -- is a refusal in one place instead of a crash in ninety.
 */
#define DEVICE_CALL(session, method, ...)                                      \
    (device_session_open(session) && (session)->backend->method               \
         ? (session)->backend->method(session, ##__VA_ARGS__)                  \
         : -1)

static inline void device_close(struct device_session *s) {
    if (device_session_open(s) && s->backend->close)
        s->backend->close(s);
    if (s) {
        s->handle = NULL;
        s->backend = NULL;
    }
}

static inline int device_set_frequency_hz(struct device_session *s,
                                          uint32_t hz) {
    return DEVICE_CALL(s, set_frequency_hz, hz);
}
static inline int device_frequency_hz(struct device_session *s,
                                      uint32_t *out) {
    return DEVICE_CALL(s, frequency_hz, out);
}
static inline int device_set_sample_rate_hz(struct device_session *s,
                                            uint32_t hz) {
    return DEVICE_CALL(s, set_sample_rate_hz, hz);
}
static inline int device_sample_rate_hz(struct device_session *s,
                                        uint32_t *out) {
    return DEVICE_CALL(s, sample_rate_hz, out);
}
static inline int device_set_ppm(struct device_session *s, int ppm) {
    return DEVICE_CALL(s, set_ppm, ppm);
}
static inline int device_ppm(struct device_session *s, int *out) {
    return DEVICE_CALL(s, ppm, out);
}
static inline int device_set_gain(struct device_session *s, int manual,
                                  int value) {
    return DEVICE_CALL(s, set_gain, manual, value);
}
static inline int device_gain(struct device_session *s, int *out) {
    return DEVICE_CALL(s, gain, out);
}
static inline int device_flush(struct device_session *s) {
    return DEVICE_CALL(s, flush);
}
static inline int device_stream(struct device_session *s, device_block_fn cb,
                                void *ctx, uint32_t block_bytes) {
    return DEVICE_CALL(s, stream, cb, ctx, block_bytes);
}
static inline int device_stop(struct device_session *s) {
    return DEVICE_CALL(s, stop);
}

/*
 * The backends. Each is a `const struct device_backend *` from its own
 * translation unit, so linking one does not drag in the others -- which is
 * what lets `check-device-backend` link the capture backend and a fake
 * without librtlsdr, and what will let a build without UHD omit that one
 * entirely.
 */
const struct device_backend *device_backend_rtlsdr(void);
int device_backend_rtlsdr_count(void);
const char *device_backend_rtlsdr_name(int index);
/* Print what is attached, and whether each opens. Enumeration is as
   device-specific as tuning: UHD enumerates by device args, not by index. */
int device_backend_rtlsdr_list(void);
/* The tuner chip behind an open RTL session, for a capture's sidecar. */
const char *device_backend_rtlsdr_tuner(const struct device_session *s);
const struct device_backend *device_backend_capture(void);

/*
 * A capture is opened by path rather than by index -- there is no such thing
 * as "capture number 2" -- so it has its own opener and the vtable's `open`
 * refuses. It reads the sidecar to decide the container and the full scale,
 * and rounds the file's length down to whole pairs *of that container*.
 */
int device_backend_capture_open(struct device_session *s, const char *path,
                                uint32_t frequency_hz, uint32_t sample_rate_hz,
                                int ppm, int loop, struct device_profile *out);
uint64_t device_backend_capture_bytes(const struct device_session *s);

/*
 * UHD, when the build found it. NULL otherwise, so a caller asks rather than
 * testing a macro, and a binary built without UHD refuses a `--device uhd`
 * with a sentence instead of failing to link.
 */
const struct device_backend *device_backend_uhd(void);

#endif /* DEVICE_BACKEND_H */
