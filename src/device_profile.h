#ifndef DEVICE_PROFILE_H
#define DEVICE_PROFILE_H

#include <stddef.h>
#include <string.h>

/*
 * What a receiver *is*, in the terms this program's numbers depend on.
 *
 * `.scratch/device-model/` is the spec. An RTL-SDR is the only receiver this
 * program has ever seen and a 12-bit device is coming, so the facts that do
 * not transfer between them have to be somewhere a check can construct without
 * a device and a capture can carry. This is that somewhere.
 *
 * It is **data, not a vtable**. Behaviour that genuinely differs -- start
 * streaming, retune, set gain -- gets function pointers when there is a second
 * backend to satisfy them (ticket 07) and not before; the repository's own
 * deletion test says an adapter with one implementation is a pass-through. The
 * data can land first because it is what the checks need.
 *
 * Almost every threshold in this program is *relative* -- a dB over a local
 * floor, a percentile, a fraction, Rayleigh's 0.5227 -- and transfers to any
 * device untouched. Only the short list below does not, which is why this is
 * a header and not a rewrite.
 *
 * No driver header, no GUI header, no `struct app`. A profile is a value: copy
 * it, pass it by pointer, keep one per source. **Nothing here is global** --
 * when a capture and a device are open at once they are two profiles, and that
 * is the case the whole spec exists for, because `testfiles/` is 8-bit and
 * must keep decoding on the day a 12-bit device is plugged in.
 */

/*
 * The discriminator. Nothing else in the program switches on which device this
 * is; a stage that needs to know something asks the profile for that thing.
 */
enum sample_format {
    SAMPLE_FORMAT_U8 = 0,  /* unsigned 8-bit interleaved, 127.5 = zero */
    SAMPLE_FORMAT_S16,     /* signed 16-bit little-endian interleaved */
    SAMPLE_FORMAT_CF32,    /* room for it; nothing produces one today */
};

/* How a device's gain is asked for. An RTL-SDR offers a discrete list its
   tuner supports; an AD9361 has a continuous range. */
enum gain_model {
    GAIN_MODEL_NONE = 0,
    GAIN_MODEL_LIST,
    GAIN_MODEL_RANGE,
};

/* Tenths are librtlsdr's unit and the one `applied_gain_tenths` already
   carries, so the list needs no conversion to reach the settings panel. */
enum gain_unit {
    GAIN_UNIT_TENTHS_DB = 0,
    GAIN_UNIT_DB,
};

#define DEVICE_NAME_MAX 48
/* An R820T offers 29 gains. The cap is generous and refused rather than
   silently truncated, because a list short by one entry is a settings panel
   missing a gain nobody can then select. */
#define DEVICE_GAIN_LIST_MAX 64

struct device_profile {
    char name[DEVICE_NAME_MAX];

    /*
     * The sample container, and the number dBFS is relative to.
     *
     * `full_scale` is carried rather than derived from `format`, and the
     * reason is measured: ticket 01's rescaled corpus is 8-bit data shifted
     * left by four, so its full scale is 127.5 * 16 = **2040.0**, while a real
     * 12-bit part rails at 2047.5. Both are SAMPLE_FORMAT_S16 and they are
     * different numbers. Only the source knows which, so only the source may
     * say. Normalising the corpus by 2047.5 agrees with none of the 256 byte
     * values.
     */
    enum sample_format format;
    float full_scale;
    unsigned bytes_per_pair;

    /*
     * What the front end can reach. `can_retune` is false for file playback,
     * which is not a limitation to work around -- a capture holds one tuning
     * and the code already refuses to move it.
     */
    double tune_lower_hz;
    double tune_upper_hz;
    unsigned rate_min_hz;
    unsigned rate_max_hz;
    int can_retune;

    /* Seconds to wait after a retune before a measurement means anything.
       A sweep throws away every block that arrives inside it. */
    double settle_seconds;

    enum gain_model gain_model;
    enum gain_unit gain_unit;
    int gain_list[DEVICE_GAIN_LIST_MAX]; /* GAIN_MODEL_LIST */
    int gain_count;
    double gain_min;  /* GAIN_MODEL_RANGE */
    double gain_max;
    double gain_step;

    /*
     * Frequency correction, and whether it is worth measuring twice. A crystal
     * drifts and is calibrated per site; a TCXO barely does and a GPSDO does
     * not, which is what would make the whole calibration overlay optional on
     * a later device rather than merely unused.
     */
    int has_ppm_correction;
    int ppm_drifts;

    /* The reference oscillator. Artifact detection is derived from it -- the
       14.4 MHz comb this receiver puts across UHF is its 28.8 MHz reference
       halved -- so a device with a different clock has different spurs, or
       none there. */
    double reference_clock_hz;
};

/*
 * The full scale a format implies, where it implies one.
 *
 * SAMPLE_FORMAT_S16 deliberately returns 0: there is no such thing as *the*
 * full scale of a 16-bit container, and a default here would have quietly
 * supplied 2047.5 to ticket 01's corpus, which is wrong for it. A caller that
 * gets 0 has to go and find out, which is the correct outcome.
 */
static inline float device_default_full_scale(enum sample_format format) {
    switch (format) {
    case SAMPLE_FORMAT_U8:
        return 127.5f;
    case SAMPLE_FORMAT_CF32:
        return 1.0f;
    case SAMPLE_FORMAT_S16:
    default:
        return 0.0f;
    }
}

/* Bytes one I/Q pair occupies in a given container. */
static inline unsigned device_format_bytes_per_pair(enum sample_format format) {
    switch (format) {
    case SAMPLE_FORMAT_U8:
        return 2;
    case SAMPLE_FORMAT_S16:
        return 4;
    case SAMPLE_FORMAT_CF32:
        return 8;
    default:
        return 0;
    }
}

/*
 * Pairs in a block of this many bytes.
 *
 * This is the arithmetic ticket 04 exists for. `SAMPLE_BLOCK_PAIRS` is
 * `SAMPLE_BLOCK_BYTES / 2`, where the 2 means bytes per pair and says nothing
 * about the block; on a four-byte format that formula returns twice the truth,
 * nothing errors, and every timing budget in CLAUDE.md is out by a factor of
 * two with nothing on screen to say so. ADR-0002 has a slow renderer *drop*
 * blocks, so the symptom is lost decodes on a faster device.
 */
static inline size_t device_pairs_per_block(const struct device_profile *p,
                                            size_t block_bytes) {
    if (!p || p->bytes_per_pair == 0)
        return 0;
    return block_bytes / p->bytes_per_pair;
}

/* Seconds of signal a block holds. The other half of the same trap: 65.5 ms
   at two bytes a pair is 32.8 at four, for the same block and the same rate. */
static inline double device_block_seconds(const struct device_profile *p,
                                          size_t block_bytes, double rate_hz) {
    if (!p || rate_hz <= 0.0)
        return 0.0;
    return (double)device_pairs_per_block(p, block_bytes) / rate_hz;
}

/*
 * Whether a profile describes something coherent. Not a substitute for knowing
 * the device -- it cannot tell 2040 from 2047.5 -- but it catches the
 * inconsistencies a hand-filled struct produces: a format that disagrees with
 * its own bytes per pair, a full scale of zero, a range the wrong way round, a
 * gain model with nothing in it.
 */
static inline int device_profile_valid(const struct device_profile *p) {
    if (!p)
        return 0;
    if (p->bytes_per_pair != device_format_bytes_per_pair(p->format))
        return 0;
    if (!(p->full_scale > 0.0f))
        return 0;
    if (p->tune_lower_hz > p->tune_upper_hz)
        return 0;
    if (p->rate_min_hz > p->rate_max_hz)
        return 0;
    if (p->settle_seconds < 0.0)
        return 0;
    if (p->gain_model == GAIN_MODEL_LIST &&
        (p->gain_count <= 0 || p->gain_count > DEVICE_GAIN_LIST_MAX))
        return 0;
    if (p->gain_model == GAIN_MODEL_RANGE && !(p->gain_max > p->gain_min))
        return 0;
    return 1;
}

/* Copy a name in without trusting its length. */
static inline void device_profile_set_name(struct device_profile *p,
                                           const char *name) {
    if (!p)
        return;
    p->name[0] = '\0';
    if (!name)
        return;
    strncpy(p->name, name, DEVICE_NAME_MAX - 1);
    p->name[DEVICE_NAME_MAX - 1] = '\0';
}

/*
 * The gain list a tuner reports. Refused rather than truncated: a list short
 * by one is a gain the settings panel can never select, and nothing downstream
 * would say so. Returns 0 on success, negative if it does not fit.
 */
static inline int device_profile_set_gain_list(struct device_profile *p,
                                               const int *gains, int count) {
    if (!p || !gains || count <= 0 || count > DEVICE_GAIN_LIST_MAX)
        return -1;
    for (int i = 0; i < count; i++)
        p->gain_list[i] = gains[i];
    p->gain_count = count;
    p->gain_model = GAIN_MODEL_LIST;
    p->gain_unit = GAIN_UNIT_TENTHS_DB;
    return 0;
}

/*
 * Today's receiver, with today's constants.
 *
 * Every number here is already compiled into the program somewhere else, and
 * `check-device-profile` pins each against its original so the two cannot
 * drift apart while both exist. Tickets 03 to 06 delete the originals one area
 * at a time.
 *
 * `gains` is what the tuner reported, in tenths of a dB, or NULL when it has
 * not been asked yet -- the profile then carries no gain model rather than an
 * invented one.
 */
static inline struct device_profile device_profile_rtlsdr(const char *name,
                                                          const int *gains,
                                                          int gain_count) {
    struct device_profile p;
    memset(&p, 0, sizeof(p));

    device_profile_set_name(&p, name ? name : "RTL-SDR");

    p.format = SAMPLE_FORMAT_U8;
    p.full_scale = device_default_full_scale(SAMPLE_FORMAT_U8); /* 127.5 */
    p.bytes_per_pair = device_format_bytes_per_pair(SAMPLE_FORMAT_U8);

    /* SURVEY_TUNER_LOWER_HZ / SURVEY_TUNER_UPPER_HZ (survey_bands.h). An
       R820T's reach, and check-survey-bands asserts it in both directions. */
    p.tune_lower_hz = 24000000.0;
    p.tune_upper_hz = 1766000000.0;

    /*
     * librtlsdr's rates. **The real device has a hole in this range** --
     * 225001-300000 and 900001-3200000 Hz, with nothing between -- and this
     * pair of fields cannot say so. Nothing here tunes into the gap: 2.0,
     * 2.048 and 1.92 MS/s are all in the upper span. Left as a known
     * limitation rather than a representation invented for one device;
     * ticket 07 is where a second backend would force the question.
     */
    p.rate_min_hz = 225001;
    p.rate_max_hz = 3200000;
    p.can_retune = 1;

    /* SURVEY_SETTLE_SECONDS (survey_sweep.h). */
    p.settle_seconds = 0.10;

    if (gains && gain_count > 0)
        device_profile_set_gain_list(&p, gains, gain_count);

    /* A crystal, so it drifts, so ppm is measured per site. */
    p.has_ppm_correction = 1;
    p.ppm_drifts = 1;
    /* RECEIVER_REFERENCE_HZ (survey_suspect.h); the comb is this halved. */
    p.reference_clock_hz = 28800000.0;

    return p;
}

/*
 * A capture on disk.
 *
 * `full_scale` is an argument and has no default, for the reason
 * `device_default_full_scale` returns 0 for S16: the sidecar's `full_scale`
 * field is the only thing that knows, and `scripts/rescale_capture.c` already
 * writes it.
 *
 * `can_retune` is false and the tuning range is the single frequency the
 * capture was taken at -- not zero, and not the tuner's range. A capture is at
 * one place on the band and that is a fact about it, not a missing capability.
 * There is no gain model: whatever gain was applied is baked into the samples
 * and cannot now be changed.
 */
static inline struct device_profile
device_profile_capture(const char *name, enum sample_format format,
                       float full_scale, double centre_hz, unsigned rate_hz) {
    struct device_profile p;
    memset(&p, 0, sizeof(p));

    device_profile_set_name(&p, name ? name : "capture");

    p.format = format;
    p.full_scale = full_scale;
    p.bytes_per_pair = device_format_bytes_per_pair(format);

    p.tune_lower_hz = centre_hz;
    p.tune_upper_hz = centre_hz;
    p.rate_min_hz = rate_hz;
    p.rate_max_hz = rate_hz;
    p.can_retune = 0;

    /* Nothing to settle: no tuner moved and none can. */
    p.settle_seconds = 0.0;
    p.gain_model = GAIN_MODEL_NONE;

    /*
     * No ppm correction, and this is the interesting one. A capture's samples
     * already carry whatever error the receiver had when it was recorded, so
     * there is nothing left to correct -- and no reference clock, so nothing
     * downstream may claim a comb belongs to this source. Whichever device
     * recorded it had one; the file does not.
     */
    p.has_ppm_correction = 0;
    p.ppm_drifts = 0;
    p.reference_clock_hz = 0.0;

    return p;
}

#endif /* DEVICE_PROFILE_H */
