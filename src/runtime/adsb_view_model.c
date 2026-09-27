#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "model/adsb_view_model.h"
#include "runtime/app.h"

/*
 * The ADS-B screen's numbers and its log, gathered once.
 *
 * In `runtime/` and not beside the model it fills: a builder reads the
 * application's state and a contract may not (ADR-0028).
 */

void adsb_view_model_build(const struct adsb_view *adsb,
                           uint32_t frequency_hz, uint32_t sample_rate_hz,
                           struct adsb_view_model *out) {
    int take, i;

    memset(out, 0, sizeof(*out));

    /* The same predicate and the same constant `adsb_tuned()` uses, so the
       window's "retune to 1090" affordance and this field cannot come to
       disagree about what "ready" means. */
    out->ready = adsb_receiver_ready(frequency_hz, sample_rate_hz,
                                     DEFAULT_FREQUENCY);

    out->frames_total = adsb->session.frames_total;
    out->positions_total = adsb->session.positions_total;
    out->totals = adsb->session.totals;
    out->block = adsb->session.block_stats;

    /*
     * The newest `ADSB_VIEW_MODEL_LOG` of them. `adsb->log` is already
     * newest-first, so this is a prefix rather than a walk backwards -- and
     * clamped to what the log actually holds, which is not the same as the
     * capacity on a run that has just started.
     */
    take = adsb->log_count;
    if (take > ADSB_VIEW_MODEL_LOG)
        take = ADSB_VIEW_MODEL_LOG;
    if (take < 0)
        take = 0;
    out->log_count = take;
    for (i = 0; i < take; i++)
        out->log[i] = adsb->log[i];
}
