#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "model/srd_view_model.h"
#include "runtime/app.h"

/*
 * The SRD screen's tuning, counters and frame log, gathered once.
 *
 * In `runtime/` and not beside the model it fills: a builder reads the
 * application's state and a contract may not (ADR-0028).
 */

void srd_view_model_build(const struct srd_view *srd, uint32_t centre_hz,
                          uint32_t sample_rate_hz,
                          struct srd_view_model *out) {
    int take, i;

    memset(out, 0, sizeof(*out));

    /* The same predicate the window's "receiver is outside 430-440 MHz"
       notice is drawn from, so the two cannot disagree about what is in
       band. It asks whether the tuning is *inside* the allocation, not
       whether the whole of it fits: ten megahertz would need 10 MS/s. */
    out->ready = srd_receiver_ready(centre_hz, sample_rate_hz);
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

    take = srd->log_count;
    if (take > SRD_VIEW_MODEL_LOG)
        take = SRD_VIEW_MODEL_LOG;
    if (take < 0)
        take = 0;
    out->log_count = take;
    for (i = 0; i < take; i++)
        out->log[i] = srd->log[i];
}
