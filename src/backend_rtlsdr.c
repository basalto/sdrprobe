/*
 * An RTL-SDR, as a device.
 *
 * The nineteen distinct `rtlsdr_` calls this program made, behind the vtable
 * in `device_backend.h`. **This is the only file in the program that includes
 * `<rtl-sdr.h>`** -- the handle leaves here as a `void *` and `rtlsdr_dev_t *`
 * appears nowhere in `app.h` or `view.h` any more.
 *
 * `docs/absolute-power-reference.md` is why the profile this fills in claims
 * so little: `rtlsdr_get_tuner_gains()` returns a 29-entry array compiled into
 * librtlsdr, identical on every unit ever made and derived from one tuner
 * measured at 928 MHz in 2013, and `rtlsdr_get_tuner_gain()` returns the value
 * last requested rather than a readback. So the gain list is real enough to
 * offer in a panel and worth nothing as an absolute reference.
 */

#include "device_backend.h"

#include <rtl-sdr.h>
#include <stdio.h>
#include <stdlib.h>

/* An R820T reports 29 gains; DEVICE_GAIN_LIST_MAX is 64. */
#define RTLSDR_GAIN_QUERY_MAX DEVICE_GAIN_LIST_MAX

static const char *tuner_name(rtlsdr_dev_t *dev) {
    switch (rtlsdr_get_tuner_type(dev)) {
    case RTLSDR_TUNER_E4000:  return "E4000";
    case RTLSDR_TUNER_FC0012: return "FC0012";
    case RTLSDR_TUNER_FC0013: return "FC0013";
    case RTLSDR_TUNER_FC2580: return "FC2580";
    case RTLSDR_TUNER_R820T:  return "R820T";
    case RTLSDR_TUNER_R828D:  return "R828D";
    default:                  return "unknown";
    }
}

/*
 * The same question, answered for the profile rather than for a label.
 *
 * This file has always asked librtlsdr which tuner it has and used the answer
 * only to spell a name, while `device_profile_rtlsdr()` hardcoded an R820T's
 * reach for every device -- so an E4000 was offered 24-52 MHz it cannot tune
 * and denied 1766-2212 MHz it can (`.scratch/device-model/issues/14-*`).
 * Nothing had to be discovered to fix that; the answer was being thrown away.
 */
static enum device_tuner tuner_kind(rtlsdr_dev_t *dev) {
    switch (rtlsdr_get_tuner_type(dev)) {
    case RTLSDR_TUNER_E4000:  return DEVICE_TUNER_E4000;
    case RTLSDR_TUNER_FC0012: return DEVICE_TUNER_FC0012;
    case RTLSDR_TUNER_FC0013: return DEVICE_TUNER_FC0013;
    case RTLSDR_TUNER_FC2580: return DEVICE_TUNER_FC2580;
    case RTLSDR_TUNER_R820T:  return DEVICE_TUNER_R820T;
    case RTLSDR_TUNER_R828D:  return DEVICE_TUNER_R828D;
    default:                  return DEVICE_TUNER_UNKNOWN;
    }
}

static int rtl_open(struct device_session *s, int index,
                    struct device_profile *out) {
    rtlsdr_dev_t *dev = NULL;
    int gains[RTLSDR_GAIN_QUERY_MAX];
    int gain_count;
    char name[DEVICE_NAME_MAX];

    if (!s || !out)
        return -1;
    if (rtlsdr_open(&dev, (uint32_t)index) < 0 || !dev)
        return -1;

    gain_count = rtlsdr_get_tuner_gains(dev, NULL);
    if (gain_count > RTLSDR_GAIN_QUERY_MAX)
        gain_count = RTLSDR_GAIN_QUERY_MAX;
    if (gain_count > 0)
        gain_count = rtlsdr_get_tuner_gains(dev, gains);
    if (gain_count < 0)
        gain_count = 0;

    snprintf(name, sizeof(name), "RTL-SDR %s", tuner_name(dev));
    *out = device_profile_rtlsdr(name, tuner_kind(dev),
                                 gain_count > 0 ? gains : NULL, gain_count);

    /*
     * The USB serial, for ADR-0018's keying. Many of these dongles ship with
     * "00000001" or nothing at all, so this is offered rather than trusted:
     * `installation.h` decides whether it is an identity.
     */
    {
        char manufacturer[256], product[256], serial[256];

        manufacturer[0] = product[0] = serial[0] = '\0';
        if (rtlsdr_get_device_usb_strings((uint32_t)index, manufacturer,
                                          product, serial) == 0)
            snprintf(out->serial, sizeof(out->serial), "%s", serial);
    }

    s->backend = device_backend_rtlsdr();
    s->handle = dev;
    return 0;
}

static void rtl_close(struct device_session *s) {
    if (s && s->handle) {
        rtlsdr_close(s->handle);
        s->handle = NULL;
    }
}

static int rtl_set_frequency(struct device_session *s, uint32_t hz) {
    return rtlsdr_set_center_freq(s->handle, hz) < 0 ? -1 : 0;
}

static int rtl_frequency(struct device_session *s, uint32_t *out) {
    uint32_t hz;
    if (!out)
        return -1;
    hz = rtlsdr_get_center_freq(s->handle);
    if (hz == 0)
        return -1;
    *out = hz;
    return 0;
}

static int rtl_set_sample_rate(struct device_session *s, uint32_t hz) {
    return rtlsdr_set_sample_rate(s->handle, hz) < 0 ? -1 : 0;
}

static int rtl_sample_rate(struct device_session *s, uint32_t *out) {
    uint32_t hz;
    if (!out)
        return -1;
    hz = rtlsdr_get_sample_rate(s->handle);
    if (hz == 0)
        return -1;
    *out = hz;
    return 0;
}

static int rtl_set_ppm(struct device_session *s, int ppm) {
    /* librtlsdr returns -2 for "already that value", which is success. */
    int result = rtlsdr_set_freq_correction(s->handle, ppm);
    return (result < 0 && result != -2) ? -1 : 0;
}

static int rtl_ppm(struct device_session *s, int *out) {
    if (!out)
        return -1;
    *out = rtlsdr_get_freq_correction(s->handle);
    return 0;
}

static int rtl_set_gain(struct device_session *s, int manual, int value) {
    if (rtlsdr_set_tuner_gain_mode(s->handle, manual ? 1 : 0) < 0)
        return -1;
    if (!manual)
        return 0;
    return rtlsdr_set_tuner_gain(s->handle, value) < 0 ? -1 : 0;
}

static int rtl_gain(struct device_session *s, int *out) {
    if (!out)
        return -1;
    /* The value last requested, not a readback -- librtlsdr has no readback.
       See docs/absolute-power-reference.md. */
    *out = rtlsdr_get_tuner_gain(s->handle);
    return 0;
}

/*
 * The one that matters most and the most driver-specific thing here. After a
 * retune the USB pipeline still holds the previous tuning's samples;
 * measuring those is the fault `survey_measure_settled()` exists to prevent.
 */
static int rtl_flush(struct device_session *s) {
    return rtlsdr_reset_buffer(s->handle) < 0 ? -1 : 0;
}

struct rtl_stream_ctx {
    device_block_fn cb;
    void *ctx;
};

static void rtl_trampoline(unsigned char *buffer, uint32_t len, void *opaque) {
    struct rtl_stream_ctx *c = opaque;
    if (c && c->cb)
        c->cb(c->ctx, buffer, len);
}

static int rtl_stream(struct device_session *s, device_block_fn cb, void *ctx,
                      uint32_t block_bytes) {
    struct rtl_stream_ctx c = { cb, ctx };
    if (!cb)
        return -1;
    return rtlsdr_read_async(s->handle, rtl_trampoline, &c, 0, block_bytes) < 0
               ? -1 : 0;
}

static int rtl_stop(struct device_session *s) {
    return rtlsdr_cancel_async(s->handle) < 0 ? -1 : 0;
}

static const struct device_backend rtlsdr_backend = {
    "rtl-sdr",
    rtl_open,
    rtl_close,
    rtl_set_frequency,
    rtl_frequency,
    rtl_set_sample_rate,
    rtl_sample_rate,
    rtl_set_ppm,
    rtl_ppm,
    rtl_set_gain,
    rtl_gain,
    rtl_flush,
    rtl_stream,
    rtl_stop,
};

const struct device_backend *device_backend_rtlsdr(void) {
    return &rtlsdr_backend;
}

/* The tuner chip, not the USB bridge: it sets the achievable gains and the
   oscillator whose error the PPM correction compensates, so a capture is worth
   labelling with it. */
const char *device_backend_rtlsdr_tuner(const struct device_session *s) {
    if (!s || !s->handle || s->backend != &rtlsdr_backend)
        return "unknown";
    return tuner_name(s->handle);
}

int device_backend_rtlsdr_count(void) {
    return (int)rtlsdr_get_device_count();
}

const char *device_backend_rtlsdr_name(int index) {
    return rtlsdr_get_device_name((uint32_t)index);
}

/*
 * Print the receivers attached, and whether each can actually be opened.
 *
 * The second half is the useful half: "found but busy" is the state that
 * otherwise shows up as a bare failure to start. This lived in sdrprobe.c and
 * moved here with the rest of the driver, because enumerating is as
 * device-specific as tuning is -- UHD enumerates by device args, not by index.
 */
int device_backend_rtlsdr_list(void) {
    uint32_t count = rtlsdr_get_device_count();

    if (count == 0) {
        printf("No RTL-SDR devices found.\n");
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        const char *name = rtlsdr_get_device_name(i);
        rtlsdr_dev_t *dev = NULL;
        int gains[RTLSDR_GAIN_QUERY_MAX];
        int gain_count;

        printf("%u: %s\n", i, name ? name : "unknown");
        if (rtlsdr_open(&dev, i) < 0) {
            printf("   in use by another process, or not accessible\n");
            continue;
        }
        gain_count = rtlsdr_get_tuner_gains(dev, NULL);
        if (gain_count > 0 && gain_count <= RTLSDR_GAIN_QUERY_MAX &&
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
