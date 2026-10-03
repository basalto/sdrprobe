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
                           int receiver_mode, int have_samples,
                           struct adsb_view_model *out) {
    int take, i;

    memset(out, 0, sizeof(*out));

    /* The same predicate and the same constant `adsb_tuned()` uses, so the
       window's "retune to 1090" affordance and this field cannot come to
       disagree about what "ready" means. */
    out->ready = adsb_receiver_ready(frequency_hz, sample_rate_hz,
                                     DEFAULT_FREQUENCY);
    /* And, when it is not, whose problem that is: a receiver can be retuned
       from this screen and a capture holds one tuning. */
    out->readiness = out->ready
        ? ADSB_READY
        : (receiver_mode ? ADSB_NOT_READY_RECEIVER : ADSB_NOT_READY_CAPTURE);
    out->have_samples = have_samples;

    out->frames_total = adsb->session.frames_total;
    out->positions_total = adsb->session.positions_total;
    out->totals = adsb->session.totals;
    out->block = adsb->session.block_stats;
    /*
     * Frames arriving and none of them decoding. It reads `attempts` rather
     * than `preambles` deliberately: a preamble is a correlation peak and
     * noise produces those, where an attempt is a preamble that survived
     * shaping and so is a frame that was really there.
     */
    out->funnel_warn = out->totals.attempts > 0 && out->totals.decoded == 0;

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

    /*
     * The analysis charts' frame trace, the shown one -- latest attempt, or
     * the last good frame while "Hold" is on -- selected the same way the
     * window selects it. Computed by the decode, so the server has it.
     */
    {
        const struct adsb_frame_trace *t =
            adsb_trace_shown(&adsb->session.trace, &adsb->session.good_trace,
                             adsb->hold_last_good);
        out->trace_valid = t->valid;
        if (t->valid) {
            int bits = t->bit_count;

            if (bits > ADSB_LONG_BITS)
                bits = ADSB_LONG_BITS;
            if (bits < 0)
                bits = 0;
            out->trace_bits = bits;
            out->landscape_count = ADSB_TRACE_LANDSCAPE;
            memcpy(out->landscape, t->landscape, sizeof(out->landscape));
            memcpy(out->confidence, t->confidence, sizeof(out->confidence));
            out->envelope_count =
                ADSB_PREAMBLE_SAMPLES + bits * ADSB_SAMPLES_PER_BIT;
            if (out->envelope_count > ADSB_TRACE_SAMPLES)
                out->envelope_count = ADSB_TRACE_SAMPLES;
            memcpy(out->envelope, t->envelope, sizeof(out->envelope));
            for (i = 0; i < bits; i++) {
                float a = t->amplitude[i] * 2.0f - 1.0f;

                if (a > 1.4f)
                    a = 1.4f;
                if (a < -1.4f)
                    a = -1.4f;
                out->scatter_x[i] = t->margin[i];
                out->scatter_y[i] = a;
            }
        }
    }
}
