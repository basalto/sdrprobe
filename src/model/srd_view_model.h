#ifndef SRD_VIEW_MODEL_H
#define SRD_VIEW_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "tech/srd_dsp.h"
#include "tech/srd_frame.h"

/*
 * What the SRD screen says, as plain data -- no raylib type anywhere
 * (`web-visualization/07`).
 *
 * The screen is a tuning, two counters and a table of decoded frames, and
 * the table's KIND column is the interesting part: `srd_device_type_of()`
 * names a device only where the frame's own structure proves it -- a header
 * and a trailer at known offsets in a Manchester stream decoded without a
 * violation, which repetition cannot manufacture. Everything else reads
 * "unknown", including every burst with no frame whatever its modulation.
 * That line between a measurement and a verdict is the model's to hold, not
 * a drawing's.
 *
 * Takes the struct it reads rather than a `const struct app *`, so
 * `check-srd-view-model` links `-lm` alone.
 */

/*
 * One decoded frame, as the window's table draws a row of it.
 *
 * It was in `runtime/app.h` beside the view that keeps them. The browser
 * renders these rows, so it is a contract, and a contract may not live
 * above the layer that reads it (ADR-0028) -- the same move `site_seen.h`,
 * `survey_tuning.h` and TETRA's log entry already made.
 */
#define SRD_LOG_CAPACITY 64

struct srd_log_entry {
    double at;
    enum srd_frame_kind kind;
    enum srd_modulation modulation;
    uint8_t bytes[32];
    size_t byte_count;
    size_t bit_count;
    double carrier_hz;          /* offset from the tuning that heard it */
    /*
     * Where this actually was, in absolute hertz, fixed when the entry was
     * written.
     *
     * The waterfall used to place a marker at `applied.frequency_hz +
     * carrier_hz` **every frame**, with the *current* tuning -- so retuning
     * dragged every historical label along with it and a burst recorded at
     * 434.42 MHz would be drawn at 435.42 after a one-megahertz step. An
     * offset only means anything beside the tuning it was measured against,
     * and once the receiver can move from this screen it does not stay
     * beside it.
     */
    double absolute_hz;
    double chip_us;
    size_t error_count;
};

/* The whole log travels: 64 rows of a few numbers and up to 32 bytes of
   hex, a few kilobytes. No newest-N arithmetic, unlike ADS-B's 256. */
#define SRD_VIEW_MODEL_LOG SRD_LOG_CAPACITY

struct srd_view_model {
    /* Where the receiver is pointed, and whether that is inside the
       allocation at all. `srd_receiver_ready()` asks whether the tuning is
       *inside* 430-440 MHz, not whether the whole of it fits -- ten
       megahertz needs 10 MS/s and nothing here samples that fast. */
    int ready;
    double centre_hz;

    /* The header's two counters, and where the last carrier was heard --
       as an offset from the tuning, which is how the window says it. */
    int transmissions;
    int frames;
    int have_carrier;
    double last_carrier_offset_hz;

    /* The decoded frames, newest first. */
    int log_count;
    struct srd_log_entry log[SRD_VIEW_MODEL_LOG];
};

struct srd_view;

/* Fills `out`. Reads plain fields only -- no I/O, no raylib call. */
void srd_view_model_build(const struct srd_view *srd, uint32_t centre_hz,
                          uint32_t sample_rate_hz,
                          struct srd_view_model *out);

#endif
