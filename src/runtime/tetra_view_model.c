#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "model/tetra_view_model.h"
#include "runtime/app.h"

/*
 * The TETRA screen's identity, funnel and log, gathered once.
 *
 * In `runtime/` and not beside the model it fills: a builder reads the
 * application's state and a contract may not (ADR-0028).
 */

void tetra_view_model_build(const struct tetra_view *tetra,
                            struct tetra_view_model *out) {
    const struct tetra_session *s = &tetra->session;
    int take, i;

    memset(out, 0, sizeof(*out));

    out->rate_supported = !tetra->rate_unsupported;
    out->lock = s->lock;
    out->offset_hz = s->offset_hz;

    /*
     * The identity travels only once a parity check has established it, and
     * is zeroed otherwise rather than left at whatever the session still
     * holds. A colour code shown under a heading that no longer decodes is
     * the fault nobody would question.
     */
    out->have_identity = s->have_identity;
    if (s->have_identity) {
        out->mcc = s->mcc;
        out->mnc = s->mnc;
        out->colour = s->colour;
        out->la = s->la;
    }

    out->bursts = s->bursts;
    out->blocks = s->blocks;
    out->broadcast = s->broadcast;
    out->bursts_total = s->bursts_total;
    out->blocks_total = s->blocks_total;
    out->broadcast_total = s->broadcast_total;
    out->blocks_failed = s->blocks_failed;

    /* Newest first already, and clamped to what the log holds -- which is
       not the capacity on a run that has just started. */
    take = tetra->log_count;
    if (take > TETRA_VIEW_MODEL_LOG)
        take = TETRA_VIEW_MODEL_LOG;
    if (take < 0)
        take = 0;
    out->log_count = take;
    for (i = 0; i < take; i++)
        out->log[i] = tetra->log[i];
}
