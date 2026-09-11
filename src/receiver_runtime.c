#include "receiver_runtime.h"

#include <stdarg.h>
#include <stdio.h>

static void say(struct receiver_runtime *rt, const char *fmt, ...) {
    va_list args;

    if (!rt->error || rt->error_size == 0)
        return;
    va_start(args, fmt);
    vsnprintf(rt->error, rt->error_size, fmt, args);
    va_end(args);
}

int receiver_runtime_set_correction(struct device_session *source, int ppm) {
    int current = 0;

    if (device_ppm(source, &current) == 0 && current == ppm)
        return 0;
    return device_set_ppm(source, ppm);
}

static uint32_t read_frequency(struct device_session *source) {
    uint32_t hz = 0;
    return device_frequency_hz(source, &hz) == 0 ? hz : 0;
}

static int read_ppm(struct device_session *source) {
    int ppm = 0;
    return device_ppm(source, &ppm) == 0 ? ppm : 0;
}

static uint32_t read_sample_rate(struct device_session *source) {
    uint32_t hz = 0;
    return device_sample_rate_hz(source, &hz) == 0 ? hz : 0;
}

static int life_stop(struct receiver_runtime *rt) {
    return rt->life.stop ? rt->life.stop(rt->life.ctx) : 0;
}

static int life_start(struct receiver_runtime *rt) {
    return rt->life.start ? rt->life.start(rt->life.ctx) : 0;
}

/* Put the tuning back where it was, flush, and restart. Used by every failure
   path after the frequency has been touched, which is why it is one place. */
static void put_tuning_back(struct receiver_runtime *rt, uint32_t frequency,
                            int ppm) {
    receiver_runtime_set_correction(rt->source, ppm);
    device_set_frequency_hz(rt->source, frequency);
    device_flush(rt->source);
    rt->applied->frequency_hz = frequency;
    rt->applied->ppm = ppm;
}

int receiver_runtime_tune(struct receiver_runtime *rt, uint32_t frequency_hz,
                          int ppm) {
    uint32_t old_frequency, reported;
    int old_ppm;

    if (!rt || !rt->source || !rt->applied)
        return -1;
    if (!rt->live) {
        say(rt, "Tuning requires a live receiver: a capture holds one "
                "frequency");
        return -1;
    }
    old_frequency = rt->applied->frequency_hz;
    old_ppm = rt->applied->ppm;

    if (life_stop(rt) < 0)
        return -1;
    if (receiver_runtime_set_correction(rt->source, ppm) < 0 ||
        device_set_frequency_hz(rt->source, frequency_hz) < 0 ||
        device_flush(rt->source) < 0) {
        say(rt, "Receiver rejected %.6f MHz or %+d ppm",
            frequency_hz / 1e6, ppm);
        put_tuning_back(rt, old_frequency, old_ppm);
        life_start(rt);
        return -1;
    }
    /*
     * Read it back, and treat silence as a refusal. A device that cannot say
     * where it is tuned has not been tuned as far as anything downstream is
     * concerned -- every frequency this program prints, saves or matches a
     * history against comes from here.
     */
    reported = read_frequency(rt->source);
    if (reported == 0) {
        say(rt, "Could not read the tuning back from the receiver");
        put_tuning_back(rt, old_frequency, old_ppm);
        life_start(rt);
        return -1;
    }
    rt->applied->frequency_hz = reported;
    rt->applied->ppm = read_ppm(rt->source);

    if (life_start(rt) < 0) {
        put_tuning_back(rt, old_frequency, old_ppm);
        life_start(rt);
        say(rt, "Acquisition would not restart; put the previous tuning "
                "back");
        return -1;
    }
    return 0;
}

int receiver_runtime_tune_at_rate(struct receiver_runtime *rt,
                                  uint32_t frequency_hz,
                                  uint32_t sample_rate_hz, int ppm) {
    uint32_t old_rate;
    int result;

    if (!rt || !rt->source || !rt->applied)
        return -1;
    if (!rt->live) {
        say(rt, "Changing the sample rate requires a live receiver");
        return -1;
    }
    old_rate = rt->applied->sample_rate_hz;
    if (sample_rate_hz == old_rate)
        return receiver_runtime_tune(rt, frequency_hz, ppm);

    if (life_stop(rt) < 0)
        return -1;
    if (device_set_sample_rate_hz(rt->source, sample_rate_hz) < 0 ||
        device_flush(rt->source) < 0) {
        say(rt, "Receiver refused %.3f MS/s", sample_rate_hz / 1e6);
        device_set_sample_rate_hz(rt->source, old_rate);
        device_flush(rt->source);
        life_start(rt);
        return -1;
    }
    rt->applied->sample_rate_hz = read_sample_rate(rt->source);
    if (rt->applied->sample_rate_hz == 0)
        rt->applied->sample_rate_hz = sample_rate_hz;

    if (life_start(rt) < 0) {
        device_set_sample_rate_hz(rt->source, old_rate);
        device_flush(rt->source);
        rt->applied->sample_rate_hz = old_rate;
        life_start(rt);
        return -1;
    }
    result = receiver_runtime_tune(rt, frequency_hz, ppm);
    if (result < 0) {
        /*
         * The rate took and the tuning did not, so the rate goes back too: a
         * refusal leaves the receiver where it was found, whole. The error is
         * the tuning's and is left alone -- it says which half refused, which
         * is what a reader needs.
         */
        if (life_stop(rt) == 0) {
            device_set_sample_rate_hz(rt->source, old_rate);
            device_flush(rt->source);
            rt->applied->sample_rate_hz = old_rate;
            life_start(rt);
        }
    }
    return result;
}
