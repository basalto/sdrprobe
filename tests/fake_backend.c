#include "fake_backend.h"

#include <string.h>

static struct fake_device state;

/* One countdown step. Returns 1 when this call should fail. */
static int due(int *countdown) {
    if (*countdown <= 0)
        return 0;
    if (--*countdown == 0)
        return 1;
    return 0;
}

static int fake_open(struct device_session *s, int index,
                     struct device_profile *out) {
    if (index != 0)
        return -1;
    memset(&state, 0, sizeof(state));
    state.frequency_hz = 100000000;
    state.sample_rate_hz = 2000000;
    *out = device_profile_rtlsdr("fake", DEVICE_TUNER_R820T, NULL, 0);
    s->handle = &state;
    return 0;
}

static void fake_close(struct device_session *s) { s->handle = NULL; }

static int fake_set_frequency(struct device_session *s, uint32_t hz) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    if (due(&f->fail_frequency_in))
        return -1;
    f->frequency_hz = hz;
    f->frequency_writes++;
    return 0;
}

static int fake_frequency(struct device_session *s, uint32_t *out) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    if (due(&f->fail_frequency_read_in))
        return -1;          /* the caller reads this as "cannot say" */
    *out = f->frequency_hz;
    return 0;
}

static int fake_set_rate(struct device_session *s, uint32_t hz) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    if (due(&f->fail_rate_in))
        return -1;
    f->sample_rate_hz = hz;
    f->rate_writes++;
    return 0;
}

static int fake_rate(struct device_session *s, uint32_t *out) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    *out = f->sample_rate_hz;
    return 0;
}

static int fake_set_ppm(struct device_session *s, int ppm) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    f->ppm = ppm;
    return 0;
}

static int fake_ppm(struct device_session *s, int *out) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    *out = f->ppm;
    return 0;
}

static int fake_set_gain(struct device_session *s, int manual, int value) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    f->manual_gain = manual;
    f->gain = value;
    return 0;
}

static int fake_gain(struct device_session *s, int *out) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    *out = f->gain;
    return 0;
}

static int fake_flush(struct device_session *s) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    if (due(&f->fail_flush_in))
        return -1;
    f->flushes++;
    return 0;
}

static int fake_stream(struct device_session *s, device_block_fn cb, void *ctx,
                       uint32_t length) {
    struct fake_device *f = s->handle;
    (void)cb; (void)ctx; (void)length;
    if (!f)
        return -1;
    f->blocks_sent++;
    return 0;
}

static int fake_stop(struct device_session *s) {
    struct fake_device *f = s->handle;
    if (!f)
        return -1;
    f->stops++;
    return 0;
}

static const struct device_backend fake = {
    "fake",
    fake_open,
    fake_close,
    fake_set_frequency,
    fake_frequency,
    fake_set_rate,
    fake_rate,
    fake_set_ppm,
    fake_ppm,
    fake_set_gain,
    fake_gain,
    fake_flush,
    fake_stream,
    fake_stop
};

const struct device_backend *fake_backend(void) { return &fake; }
struct fake_device *fake_backend_state(void) { return &state; }
