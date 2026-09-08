#ifndef GSM_SESSION_H
#define GSM_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "gsm_bcch.h"
#include "gsm_continuity.h"
#include "gsm_dsp.h"

/*
 * A GSM decode, block by block, with no window and no receiver.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`. Per-block orchestration --
 * feed the block, latch what was read, fold a message into what the cell has
 * said so far, keep the continuity -- was written once for the window and once
 * for `run_headless` in every technology **except** GSM, where
 * `update_gsm_sch()` was already called from both. That made GSM the shape to
 * copy and the safest one to move first: if this changes an answer, the
 * extraction is wrong rather than the design.
 *
 * It was still not a session, though. It took `struct app` and read
 * `i_samples`, `pair_count`, `applied_sample_rate` and six fields of
 * `gsm_view` out of it, so nothing could drive it but the program. Now it
 * takes samples and gives back events, links `-lm`, and
 * `check-gsm-session` replays a capture through it.
 *
 * **The view draws a session; the headless report prints one.** Both are
 * adapters, and neither owns the decode.
 */

/* What one cell has said, folded across every message read from it. Messages
   arrive one type at a time -- identity in System Information 3, neighbours in
   1 and 2 -- so a reader wants the union rather than the latest. */
struct gsm_cell {
    int blocks;                 /* System Information messages read */
    enum gsm_si_type last_type;
    int have_lai;
    int mcc;
    int mnc;
    int mnc_digits;
    int lac;
    int have_cell_id;
    int cell_id;
    int neighbour_count;
    int neighbours[GSM_SI_MAX_NEIGHBOURS];
};

/*
 * The decode's own state: what was last read, and everything accumulated
 * across blocks. Nothing here is about drawing.
 */
struct gsm_session {
    /* GSM_OPT_FILTER, _FINECFO, _TRELLIS -- the front-end refinements, which
       the view offers as buttons and a headless run sets from a flag. They
       are decode options, so they live with the decode. */
    uint32_t options;

    struct gsm_sch_result sch;
    struct gsm_sch_symbols sch_symbols;
    int sch_valid;
    double sch_time;

    struct gsm_sch_continuity continuity;
    struct gsm_cell cell;
};

/*
 * What one block produced. `sch_decoded` without `broadcast_read` is the
 * ordinary case -- a synchronisation burst arrives far more often than a
 * System Information message, which needs four normal bursts after it inside
 * the same block.
 */
struct gsm_session_event {
    int sch_decoded;
    int broadcast_read;
    struct gsm_si si;           /* meaningful when broadcast_read */
};

/*
 * The front-end refinements, as bits rather than as three ints.
 *
 * `gsm_sch_decode()` has always taken them as a GSM_OPT_* mask; the view kept
 * three separate flags and rebuilt the mask every block, and `--gsm-features`
 * unpacked the mask into those three to do it. One representation, and the
 * unpacking goes away.
 */
static inline int gsm_session_option(const struct gsm_session *s,
                                     uint32_t bit) {
    return s && (s->options & bit) ? 1 : 0;
}

static inline void gsm_session_set_option(struct gsm_session *s, uint32_t bit,
                                          int on) {
    if (!s)
        return;
    if (on)
        s->options |= bit;
    else
        s->options &= ~bit;
}

static inline void gsm_session_toggle_option(struct gsm_session *s,
                                             uint32_t bit) {
    if (s)
        s->options ^= bit;
}

/* Start again: a different channel is a different cell, and carrying one
   cell's identity into another is how a reader is told a lie quietly. */
void gsm_session_reset(struct gsm_session *s);

/*
 * Feed one block of centred I/Q. `offset_hz` is where the channel sits
 * relative to the tuning -- the GSM view tunes 400 kHz below it, so this is
 * normally +400 kHz.
 *
 * Returns 1 when anything was read, 0 otherwise, and fills `out` either way.
 * `now` is only carried into `sch_time` and the continuity, so a replay can
 * pass a block index and get the same answers.
 */
int gsm_session_feed(struct gsm_session *s, const float *i_samples,
                     const float *q_samples, size_t pair_count,
                     double sample_rate, double offset_hz, double now,
                     struct gsm_session_event *out);

#endif /* GSM_SESSION_H */
