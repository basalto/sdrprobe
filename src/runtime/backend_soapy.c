/*
 * SoapySDR, when there is a SoapySDR to talk to.
 *
 * `.scratch/device-model/issues/07-a-second-backend.md`. This file is always
 * compiled; what it contains depends on `HAVE_SOAPY`, which the Makefile
 * defaults to 0 so the gate stays runnable on a machine with no SoapySDR.
 * `make HAVE_SOAPY=1` opts in, and that is how the attached USRP B210 clone
 * (AD9361 + Kintex-7 XC7K325T) is reached: SoapySDR's C API over SoapyUHD over
 * libuhd, which is ticket 07's "one C API over every device library" hedge.
 *
 * Why SoapySDR and not libuhd's own C API: SoapyUHD is a pure C surface that
 * drops into this seam with no C++ in the build, and the same backend will
 * later drive other devices. Two SoapyUHD quirks were measured with this clone
 * and shape the code below:
 *
 *   - **Never query hardware time.** `getHardwareTime`/`get_time` times out
 *     because the clone's FPGA time core does not answer; plain RX streaming
 *     never needs it, so nothing here calls it.
 *   - **Open with `driver=uhd` and no `fpga=` arg.** The vendor Kintex-7 image
 *     is installed as UHD's default `usrp_b210_fpga.bin`, so UHD loads it with
 *     no argument; passing `fpga=` as a device arg trips SoapySDR's matcher.
 *
 * The profile it fills in is `device_profile_b210()`, not the RTL's -- S16, a
 * provisional full scale, 50-6000 MHz, a gain-table *index* 0..76. See that
 * builder and `docs/absolute-power-reference.md` for what is measured and what
 * is still provisional.
 */

#include "runtime/device_backend.h"

#ifdef HAVE_SOAPY

#include "core/device_profile.h"

#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * The backend's own handle. The device is SoapySDR's; the RX stream and the
 * stop flag are ours. `stop` is set from another thread (the caller's `stop`)
 * while `stream` loops on the worker thread, exactly as the capture backend's
 * flag is -- a single word polled, no queue (ADR-0002).
 */
struct soapy_handle {
    SoapySDRDevice *dev;
    SoapySDRStream *rx;
    int ppm;            /* last requested; SoapyUHD has no reliable readback */
    volatile int stop;
};

/* RX channel 0. MIMO is out of scope for this ticket (07, "Not in scope"). */
#define SOAPY_RX_CHANNEL 0

static int soapy_open(struct device_session *s, int index,
                      struct device_profile *out) {
    SoapySDRKwargs *found;
    size_t count = 0;
    SoapySDRDevice *dev;
    struct soapy_handle *h;
    char name[DEVICE_NAME_MAX];
    char serial[DEVICE_SERIAL_MAX];

    if (!s || !out || index < 0)
        return -1;

    /* Enumerate, then open the index-th. A bare make() would open whatever is
       first; this keeps `--device-index` meaning the same thing it does for
       the RTL backend. */
    found = SoapySDRDevice_enumerate(NULL, &count);
    if (!found || (size_t)index >= count) {
        if (found)
            SoapySDRKwargsList_clear(found, count);
        return -1;
    }

    /* The serial is in the enumeration kwargs -- `getHardwareInfo` on an open
       SoapyUHD device does not carry it. It is the UHD EEPROM serial, which
       installation.h decides whether to trust (a batch can share one). */
    name[0] = serial[0] = '\0';
    {
        const char *ser = SoapySDRKwargs_get(&found[index], "serial");
        if (ser)
            snprintf(serial, sizeof(serial), "%s", ser);
    }

    dev = SoapySDRDevice_make(&found[index]);
    SoapySDRKwargsList_clear(found, count);
    if (!dev)
        return -1;

    {
        char *key = SoapySDRDevice_getHardwareKey(dev);
        if (key) {
            snprintf(name, sizeof(name), "%s", key);
            free(key);
        }
    }
    if (name[0] == '\0')
        snprintf(name, sizeof(name), "SoapySDR");

    h = calloc(1, sizeof(*h));
    if (!h) {
        SoapySDRDevice_unmake(dev);
        return -1;
    }
    h->dev = dev;

    *out = device_profile_b210(name, serial[0] ? serial : NULL);

    s->backend = device_backend_soapy();
    s->handle = h;
    return 0;
}

static void soapy_close(struct device_session *s) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h)
        return;
    if (h->rx) {
        SoapySDRDevice_deactivateStream(h->dev, h->rx, 0, 0);
        SoapySDRDevice_closeStream(h->dev, h->rx);
        h->rx = NULL;
    }
    if (h->dev)
        SoapySDRDevice_unmake(h->dev);
    free(h);
    s->handle = NULL;
}

static int soapy_set_frequency(struct device_session *s, uint32_t hz) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h)
        return -1;
    return SoapySDRDevice_setFrequency(h->dev, SOAPY_SDR_RX, SOAPY_RX_CHANNEL,
                                       (double)hz, NULL) != 0 ? -1 : 0;
}

static int soapy_frequency(struct device_session *s, uint32_t *out) {
    struct soapy_handle *h = s ? s->handle : NULL;
    double hz;
    if (!h || !out)
        return -1;
    hz = SoapySDRDevice_getFrequency(h->dev, SOAPY_SDR_RX, SOAPY_RX_CHANNEL);
    if (!(hz > 0.0))
        return -1;
    *out = (uint32_t)(hz + 0.5);
    return 0;
}

static int soapy_set_sample_rate(struct device_session *s, uint32_t hz) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h)
        return -1;
    return SoapySDRDevice_setSampleRate(h->dev, SOAPY_SDR_RX, SOAPY_RX_CHANNEL,
                                        (double)hz) != 0 ? -1 : 0;
}

static int soapy_sample_rate(struct device_session *s, uint32_t *out) {
    struct soapy_handle *h = s ? s->handle : NULL;
    double hz;
    if (!h || !out)
        return -1;
    hz = SoapySDRDevice_getSampleRate(h->dev, SOAPY_SDR_RX, SOAPY_RX_CHANNEL);
    if (!(hz > 0.0))
        return -1;
    *out = (uint32_t)(hz + 0.5);
    return 0;
}

/*
 * Frequency correction. SoapySDR expresses it as a ppm double on the RX
 * channel. SoapyUHD may not advertise the component on every device, so the
 * requested value is always stored for readback and only applied when the
 * device says it has the control -- a device without it is not an error here,
 * the same way the capture backend accepts and remembers a ppm it cannot act
 * on.
 */
static int soapy_set_ppm(struct device_session *s, int ppm) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h)
        return -1;
    h->ppm = ppm;
    if (SoapySDRDevice_hasFrequencyCorrection(h->dev, SOAPY_SDR_RX,
                                              SOAPY_RX_CHANNEL))
        SoapySDRDevice_setFrequencyCorrection(h->dev, SOAPY_SDR_RX,
                                              SOAPY_RX_CHANNEL, (double)ppm);
    return 0;
}

static int soapy_ppm(struct device_session *s, int *out) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h || !out)
        return -1;
    *out = h->ppm;
    return 0;
}

/*
 * Gain. `manual` 0 hands gain to the device's AGC; otherwise `value` is the
 * AD9361 gain-table index (GAIN_UNIT_INDEX), which SoapySDR takes as the
 * overall gain in its own units.
 */
static int soapy_set_gain(struct device_session *s, int manual, int value) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h)
        return -1;
    if (SoapySDRDevice_setGainMode(h->dev, SOAPY_SDR_RX, SOAPY_RX_CHANNEL,
                                   manual ? false : true) != 0) {
        /* A device with no AGC toggle is fine as long as we set manual gain. */
        if (!manual)
            return -1;
    }
    if (!manual)
        return 0;
    return SoapySDRDevice_setGain(h->dev, SOAPY_SDR_RX, SOAPY_RX_CHANNEL,
                                  (double)value) != 0 ? -1 : 0;
}

static int soapy_gain(struct device_session *s, int *out) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h || !out)
        return -1;
    *out = (int)(SoapySDRDevice_getGain(h->dev, SOAPY_SDR_RX,
                                        SOAPY_RX_CHANNEL) + 0.5);
    return 0;
}

/*
 * No analogue to `rtlsdr_reset_buffer()`. A retune's stale samples are handled
 * by `settle_seconds` (the sweep throws away blocks that arrive inside it), so
 * this is the same truthful no-op the capture and UHD backends give rather than
 * a stub.
 */
static int soapy_flush(struct device_session *s) {
    return s && s->handle ? 0 : -1;
}

/*
 * Stream until `stop` is called, handing fixed `block_bytes` blocks to `cb`.
 * SoapySDR's readStream is a pull that returns however many elements it has, so
 * -- like the capture backend -- we accumulate to a whole block before each
 * callback, which is what the rest of the program expects a block to be.
 *
 * CS16 is requested because `sdr_dsp_convert_iq()` decodes S16 and refuses
 * CF32. Overflows are UHD's `SOAPY_SDR_OVERFLOW`; the slot is overwriteable by
 * design (ADR-0002), so an overflow is dropped and streaming continues rather
 * than queued. The read timeout is short so `stop` is noticed promptly.
 */
static int soapy_stream(struct device_session *s, device_block_fn cb, void *ctx,
                        uint32_t block_bytes) {
    struct soapy_handle *h = s ? s->handle : NULL;
    uint8_t *block;
    size_t elems_per_block;

    if (!h || !cb || block_bytes == 0 || (block_bytes % 4) != 0)
        return -1;
    elems_per_block = block_bytes / 4;   /* S16 I/Q = 4 bytes per pair */

    if (!h->rx) {
        size_t chan = SOAPY_RX_CHANNEL;
        h->rx = SoapySDRDevice_setupStream(h->dev, SOAPY_SDR_RX, SOAPY_SDR_CS16,
                                           &chan, 1, NULL);
        if (!h->rx)
            return -1;
    }
    if (SoapySDRDevice_activateStream(h->dev, h->rx, 0, 0, 0) != 0)
        return -1;

    block = malloc(block_bytes);
    if (!block) {
        SoapySDRDevice_deactivateStream(h->dev, h->rx, 0, 0);
        return -1;
    }

    h->stop = 0;
    while (!h->stop) {
        size_t filled = 0;   /* pairs */
        while (filled < elems_per_block && !h->stop) {
            void *buffs[1];
            int flags = 0;
            long long time_ns = 0;
            int n;

            buffs[0] = block + filled * 4;
            n = SoapySDRDevice_readStream(h->dev, h->rx, buffs,
                                          elems_per_block - filled, &flags,
                                          &time_ns, 100000);
            if (n > 0)
                filled += (size_t)n;
            else if (n == SOAPY_SDR_TIMEOUT || n == SOAPY_SDR_OVERFLOW)
                continue;   /* keep the slot current; do not queue (ADR-0002) */
            else
                break;      /* a real error ends the stream */
        }
        if (filled == 0)
            break;
        cb(ctx, block, (uint32_t)(filled * 4));
    }

    free(block);
    SoapySDRDevice_deactivateStream(h->dev, h->rx, 0, 0);
    return 0;
}

static int soapy_stop(struct device_session *s) {
    struct soapy_handle *h = s ? s->handle : NULL;
    if (!h)
        return -1;
    h->stop = 1;
    return 0;
}

static const struct device_backend soapy_backend = {
    "soapy",
    soapy_open,
    soapy_close,
    soapy_set_frequency,
    soapy_frequency,
    soapy_set_sample_rate,
    soapy_sample_rate,
    soapy_set_ppm,
    soapy_ppm,
    soapy_set_gain,
    soapy_gain,
    soapy_flush,
    soapy_stream,
    soapy_stop,
};

const struct device_backend *device_backend_soapy(void) {
    return &soapy_backend;
}

int device_backend_soapy_count(void) {
    size_t count = 0;
    SoapySDRKwargs *found = SoapySDRDevice_enumerate(NULL, &count);
    if (found)
        SoapySDRKwargsList_clear(found, count);
    return (int)count;
}

/* Print the SoapySDR devices and what each advertises, indexed the way
   `--driver soapy --device N` selects them. Enumerating a UHD device loads its
   firmware, so this can take a moment. */
int device_backend_soapy_list(void) {
    size_t count = 0;
    SoapySDRKwargs *found = SoapySDRDevice_enumerate(NULL, &count);

    if (!found || count == 0) {
        printf("No SoapySDR devices found.\n");
        if (found)
            SoapySDRKwargsList_clear(found, count);
        return 0;
    }
    printf("SoapySDR devices (use --driver soapy):\n");
    for (size_t i = 0; i < count; i++) {
        const char *label = SoapySDRKwargs_get(&found[i], "label");
        const char *driver = SoapySDRKwargs_get(&found[i], "driver");
        const char *serial = SoapySDRKwargs_get(&found[i], "serial");

        printf("%zu: %s\n", i, label ? label : "unknown");
        printf("   driver %s", driver ? driver : "unknown");
        if (serial && serial[0])
            printf("   serial %s", serial);
        printf("\n");
    }
    SoapySDRKwargsList_clear(found, count);
    return (int)count;
}

#else /* !HAVE_SOAPY */

/*
 * Without SoapySDR there is no backend, and saying so with NULL is what lets a
 * caller ask instead of testing a macro -- a binary built without it refuses a
 * `--device soapy` with a sentence rather than failing to link, and the refusal
 * lives in one place.
 */
const struct device_backend *device_backend_soapy(void) {
    return NULL;
}

int device_backend_soapy_count(void) {
    return 0;
}

/* Nothing to list, and nothing to say: a build without SoapySDR leaves
   `--list-devices` showing exactly the RTL devices it always has. */
int device_backend_soapy_list(void) {
    return 0;
}

#endif /* HAVE_SOAPY */
