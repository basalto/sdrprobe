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

/*
 * One row, as both readers draw it: the entry, and the three pieces of text
 * somebody had to *choose* about it.
 *
 * They were chosen inside `draw_log()` and `srd_markers_build()`, in seven
 * and four branches respectively, and the browser could not see either -- so
 * `web/views/srd.js` had grown its own, different, sentence for the same
 * row. Three of the window's seven read protocol fields out of the payload
 * by byte offset, which is the kind of knowledge that must not be spelled
 * out in a drawing: the marker's version of the 2-FSK prefix test checks
 * **two** bytes and ignores the frame kind where `srd_device_type_of()`
 * checks three and requires GENERIC.
 */
struct srd_frame_view {
    struct srd_log_entry entry;
    /* What this row says about itself, beyond its own numbers. */
    char detail[64];
    /* The waterfall marker's label, **empty where a label would say
       nothing** -- an undecoded burst is the commonest thing on this band by
       a wide margin (one live sweep put 35 on screen), every one of them
       said the same word, and the brackets already say a burst was there. */
    char marker_label[24];
    /* What the frame *proves* the device is, already named. `unknown` for
       everything a decode does not establish. */
    char device[24];
    /* The payload as hex, the sixteen bytes both tables show. Text rather
       than bytes because it is what both readers print, and because JSON
       is text: a raw octet is not a JSON string. */
    char hex[56];
};

/* The whole log travels: 64 rows of a few numbers and up to 32 bytes of
   hex, a few kilobytes. No newest-N arithmetic, unlike ADS-B's 256. */
#define SRD_VIEW_MODEL_LOG SRD_LOG_CAPACITY

/* The analysis charts' array lengths, matching `srd_session`'s own
   `last_envelope[1024]` and `last_chips[512]`. Small enough to carry whole,
   so no decimation: the builder asserts they still agree. */
#define SRD_VIEW_MODEL_ENVELOPE 1024
#define SRD_VIEW_MODEL_CHIPS 512

/*
 * Whether this screen can expect to hear anything, and if not, whose
 * problem it is.
 *
 * Two states rather than one, because they are different answers and only
 * one of them is actionable: a receiver pointed elsewhere can be retuned
 * from this screen, and a capture taken somewhere else cannot. The window
 * drew two sentences for these and the browser drew one for both.
 */
enum srd_readiness {
    SRD_READY = 0,              /* tuned inside 430-440 MHz, fast enough */
    SRD_NOT_READY_RECEIVER,     /* a live receiver pointed elsewhere */
    SRD_NOT_READY_CAPTURE       /* a capture that was not taken here */
};

static inline const char *srd_readiness_name(enum srd_readiness r) {
    switch (r) {
    case SRD_READY:              return "ready";
    case SRD_NOT_READY_RECEIVER: return "receiver-elsewhere";
    case SRD_NOT_READY_CAPTURE:  return "capture-elsewhere";
    }
    return "ready";
}

struct srd_view_model {
    /* Where the receiver is pointed, and whether that is inside the
       allocation at all. `srd_receiver_ready()` asks whether the tuning is
       *inside* 430-440 MHz, not whether the whole of it fits -- ten
       megahertz needs 10 MS/s and nothing here samples that fast. */
    int ready;
    enum srd_readiness readiness;
    double centre_hz;

    /* The header's two counters, and where the last carrier was heard --
       as an offset from the tuning, which is how the window says it. */
    int transmissions;
    int frames;
    int have_carrier;
    double last_carrier_offset_hz;

    /*
     * The parameters panel: what the last transmission was made of.
     * `have_parameters` is separate from a zero chip period because
     * "nothing heard yet" is a different answer from a measurement that
     * came back zero.
     */
    int have_parameters;
    char line_code[24];         /* "Manchester (Thomas)" / "(IEEE)" */
    double last_chip_us;
    double last_chip_rate_hz;   /* chips per second, 0 when the period is */
    double last_over_floor_db;

    /* The decoded frames, newest first. */
    int log_count;
    struct srd_frame_view log[SRD_VIEW_MODEL_LOG];

    /* -- The analysis charts behind "Show charts": the last transmission's
          demodulated envelope (the work rate) and its discretised chips
          (preamble, delimiter, data). Both are the session's own last-burst
          arrays, carried whole -- 1024 and 512 are a few KB -- and the browser
          draws what the window's charts read. Empty until a burst. -- */
    int envelope_count;
    float envelope[SRD_VIEW_MODEL_ENVELOPE];
    int chips_count;
    float chips[SRD_VIEW_MODEL_CHIPS];
};

struct srd_view;

/* Fills `out`. Reads plain fields only -- no I/O, no raylib call. */
void srd_view_model_build(const struct srd_view *srd, uint32_t centre_hz,
                          uint32_t sample_rate_hz, int receiver_mode,
                          struct srd_view_model *out);

#endif
