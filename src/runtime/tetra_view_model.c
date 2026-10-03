#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
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
        /*
         * The location area rides the broadcast block, which is scrambled
         * with the network's own colour code and cannot be read until the
         * synchronization block has given that up. Until one has arrived
         * the identity is real and its location area is simply not known
         * yet -- which is a different thing from a location area of zero.
         */
        out->la_read = s->broadcast_total > 0;
        if (out->la_read)
            snprintf(out->marker_label, sizeof(out->marker_label), "LA %d",
                     s->la);
        else
            snprintf(out->marker_label, sizeof(out->marker_label),
                     "LA unread");
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

    /*
     * The analysis charts, computed in tetra_runtime.c so the server has them.
     * The phase steps decimate to the model's cap -- a stride of at least one,
     * so a short block is carried whole rather than skipped.
     */
    if (tetra->point_count > 0) {
        int stride = tetra->point_count / TETRA_VIEW_MODEL_SCATTER;
        if (stride < 1)
            stride = 1;
        out->scatter_count = 0;
        for (i = 0; i * stride < tetra->point_count &&
                    out->scatter_count < TETRA_VIEW_MODEL_SCATTER; i++) {
            out->scatter_x[out->scatter_count] = tetra->point_x[i * stride];
            out->scatter_y[out->scatter_count] = tetra->point_y[i * stride];
            out->scatter_count++;
        }
    }

    out->profile_valid = tetra->profile_valid;
    out->profile_fixed = tetra->profile_fixed;
    if (tetra->profile_valid)
        memcpy(out->profile, tetra->profile, sizeof(out->profile));
}
