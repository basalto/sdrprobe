#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "model/srd_view_model.h"
#include "runtime/app.h"

/* The model's chart lengths match the session's own last-burst arrays, checked
   here where both types are in hand. At file scope rather than inside the
   builder, so it is not an unused *local* typedef (-Wunused-local-typedefs). */
typedef char srd_envelope_matches
    [(SRD_VIEW_MODEL_ENVELOPE ==
      (int)(sizeof(((struct srd_view *)0)->session.last_envelope) /
            sizeof(float))) ? 1 : -1];
typedef char srd_chips_matches
    [(SRD_VIEW_MODEL_CHIPS ==
      (int)(sizeof(((struct srd_view *)0)->session.last_chips))) ? 1 : -1];

/*
 * The SRD screen's tuning, counters and frame log, gathered once.
 *
 * In `runtime/` and not beside the model it fills: a builder reads the
 * application's state and a contract may not (ADR-0028).
 */

/*
 * What one row says about itself, beyond its own numbers.
 *
 * Seven cases, in the order the window asked them, and three of them read
 * protocol fields out of the payload by byte offset. That is exactly why it
 * is here: a byte offset written into a drawing is protocol knowledge no
 * check can reach, and the browser had already grown a different sentence
 * for the same row.
 */
static void srd_row_detail(const struct srd_log_entry *e, char *out,
                           size_t cap) {
    enum srd_device_type device;

    if (e->kind == SRD_FRAME_FULL && e->byte_count >= 10) {
        snprintf(out, cap, "hdr %02X  tag %02X  trailer %02X",
                 e->bytes[0], e->bytes[1], e->bytes[9]);
        return;
    }
    if (e->kind == SRD_FRAME_REPEAT && e->byte_count >= 3) {
        snprintf(out, cap, "hdr %02X  tag %02X  trailer %02X",
                 e->bytes[0], e->bytes[1], e->bytes[2]);
        return;
    }
    if (e->kind == SRD_FRAME_FSK_DETECTED) {
        if (e->chip_us > 0.0 && e->bit_count > 0)
            snprintf(out, cap, "preamble %zu chips (%.0fus, %.1f kbd)",
                     e->bit_count, e->chip_us, 1e3 / e->chip_us);
        else
            snprintf(out, cap, "preamble / wakeup burst");
        return;
    }
    if (e->kind == SRD_FRAME_UNDECODED) {
        snprintf(out, cap, "detected burst (no frame)");
        return;
    }
    device = srd_device_type_of(e->kind, e->bytes, e->byte_count);
    if (device == SRD_DEVICE_REMOTE_FSK) {
        /* The prefix test is `srd_device_type_of()`'s and is asked rather
           than spelled out -- it was written out here once and that is two
           places to disagree about what the protocol is. */
        uint32_t id = ((uint32_t)e->bytes[4] << 24) |
                      ((uint32_t)e->bytes[5] << 16) |
                      ((uint32_t)e->bytes[6] << 8) | (uint32_t)e->bytes[7];
        uint16_t seq = (uint16_t)(((uint16_t)e->bytes[8] << 8) | e->bytes[9]);

        snprintf(out, cap, "id %08X  seq %04X  flg %02X", id, seq,
                 e->bytes[3]);
        return;
    }
    if (e->byte_count >= 8) {
        uint32_t id = ((uint32_t)e->bytes[0] << 24) |
                      ((uint32_t)e->bytes[1] << 16) |
                      ((uint32_t)e->bytes[2] << 8) | (uint32_t)e->bytes[3];

        snprintf(out, cap, "id %08X  %zu bytes", id, e->byte_count);
        return;
    }
    snprintf(out, cap, "%zu bytes (%zu bits)", e->byte_count,
             e->bit_count ? e->bit_count : e->byte_count * 8);
}

/*
 * And the label the waterfall marker carries, which is **not** the same
 * text: a marker sits over a burst in a picture and has room for a word.
 *
 * An undecoded burst gets no label at all, deliberately. It is the
 * commonest thing on this band by a wide margin -- one live sweep put 35 of
 * them on screen at once -- and every one said the same word, burying the
 * markers that carry a sequence number or a decode. The brackets already
 * say a burst was there and how wide it was.
 */
static void srd_row_marker_label(const struct srd_log_entry *e, char *out,
                                 size_t cap) {
    if (e->kind == SRD_FRAME_FSK_DETECTED) {
        snprintf(out, cap, "WAKEUP");
        return;
    }
    if (e->kind == SRD_FRAME_UNDECODED) {
        out[0] = '\0';
        return;
    }
    if (srd_device_type_of(e->kind, e->bytes, e->byte_count) ==
        SRD_DEVICE_REMOTE_FSK) {
        uint16_t seq = (uint16_t)(((uint16_t)e->bytes[8] << 8) | e->bytes[9]);

        snprintf(out, cap, "seq %04X", seq);
        return;
    }
    snprintf(out, cap, "%s", srd_frame_kind_name(e->kind));
}

void srd_view_model_build(const struct srd_view *srd, uint32_t centre_hz,
                          uint32_t sample_rate_hz, int receiver_mode,
                          struct srd_view_model *out) {
    int take, i;

    memset(out, 0, sizeof(*out));

    /* The same predicate the window's "receiver is outside 430-440 MHz"
       notice is drawn from, so the two cannot disagree about what is in
       band. It asks whether the tuning is *inside* the allocation, not
       whether the whole of it fits: ten megahertz would need 10 MS/s. */
    out->ready = srd_receiver_ready(centre_hz, sample_rate_hz);
    /*
     * And, when it is not ready, whose problem that is -- two answers, only
     * one of them actionable: a receiver can be retuned from this screen
     * and a capture holds the one tuning it was taken at.
     */
    out->readiness = out->ready
        ? SRD_READY
        : (receiver_mode ? SRD_NOT_READY_RECEIVER : SRD_NOT_READY_CAPTURE);
    out->centre_hz = (double)centre_hz;

    out->transmissions = srd->session.transmissions_found;
    out->frames = srd->session.frames_decoded;
    /*
     * The last carrier travels as an **offset**, which is how the window
     * says it -- and an offset only means anything beside the tuning it was
     * measured against. `have_carrier` is separate from a zero offset
     * because a transmitter exactly on the tuning is a real answer and
     * "nothing heard yet" is not.
     */
    out->have_carrier = srd->session.transmissions_found > 0;
    out->last_carrier_offset_hz = srd->session.last_carrier_hz;

    /*
     * The parameters panel. The chip *rate* is carried rather than left to
     * be divided out, because the guard against a zero period is the part
     * worth having once rather than twice.
     */
    out->have_parameters = srd->log_count > 0 ||
                           srd->session.last_chip_us != 0.0;
    snprintf(out->line_code, sizeof(out->line_code), "Manchester (%s)",
             srd->polarity == SRD_MANCHESTER_THOMAS ? "Thomas" : "IEEE");
    out->last_chip_us = srd->session.last_chip_us;
    out->last_chip_rate_hz = srd->session.last_chip_us > 0.0
                                 ? 1e6 / srd->session.last_chip_us : 0.0;
    out->last_over_floor_db = srd->session.last_over_floor_db;

    /*
     * The analysis charts: the last transmission's envelope and chips, carried
     * whole. The model's lengths match the session's own arrays, asserted here
     * where both are in hand rather than mirrored on faith.
     */
    {
        int n = srd->session.last_envelope_count;

        if (n > SRD_VIEW_MODEL_ENVELOPE)
            n = SRD_VIEW_MODEL_ENVELOPE;
        if (n < 0)
            n = 0;
        out->envelope_count = n;
        for (i = 0; i < n; i++)
            out->envelope[i] = srd->session.last_envelope[i];

        n = srd->session.last_chips_count;
        if (n > SRD_VIEW_MODEL_CHIPS)
            n = SRD_VIEW_MODEL_CHIPS;
        if (n < 0)
            n = 0;
        out->chips_count = n;
        for (i = 0; i < n; i++)
            out->chips[i] = (float)srd->session.last_chips[i];
    }

    take = srd->log_count;
    if (take > SRD_VIEW_MODEL_LOG)
        take = SRD_VIEW_MODEL_LOG;
    if (take < 0)
        take = 0;
    out->log_count = take;
    for (i = 0; i < take; i++) {
        struct srd_frame_view *row = &out->log[i];

        row->entry = srd->log[i];
        srd_row_detail(&row->entry, row->detail, sizeof(row->detail));
        srd_row_marker_label(&row->entry, row->marker_label,
                             sizeof(row->marker_label));
        snprintf(row->device, sizeof(row->device), "%s",
                 srd_device_type_name(
                     srd_device_type_of(row->entry.kind, row->entry.bytes,
                                        row->entry.byte_count)));
        {
            /* Sixteen bytes, space separated and no trailing space. The
               window built this with a strncat loop that left one. */
            size_t b, used = 0;

            for (b = 0; b < row->entry.byte_count && b < 16 &&
                        used + 4 < sizeof(row->hex); b++)
                used += (size_t)snprintf(row->hex + used,
                                         sizeof(row->hex) - used, "%s%02X",
                                         b ? " " : "", row->entry.bytes[b]);
        }
    }
}
