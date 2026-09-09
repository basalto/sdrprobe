#ifndef ADSB_SESSION_H
#define ADSB_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "adsb_analysis.h"
#include "adsb_dsp.h"

/*
 * A Mode S decode, block by block, with no window and no receiver.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`, fourth. Like GSM, ADS-B
 * was already shared -- `run_headless` calls `update_adsb()` -- so this is an
 * extraction rather than a de-duplication, and the same standard applies: if
 * it changes an answer, the extraction is wrong.
 *
 * What it owns is the decoder's own state, which is more than it looks. The
 * **even/odd pairing cache** lives in `struct adsb_decoder`: a global position
 * needs two frames of opposite parity from the same aircraft, so a session
 * that forgot between blocks would resolve nothing. `adsb_cpr_pair.bin`
 * exercises exactly that and is the only capture that does.
 */

/* Messages one block may produce. A 65.5 ms block at 2 MS/s holds a few
   squitters at most; sixty-four is generous and bounded. */
#define ADSB_SESSION_MESSAGES 64

struct adsb_session {
    /* Carries the even/odd pairing cache across blocks, which is what makes a
       global position possible at all. */
    struct adsb_decoder decoder;

    /* This block's messages, in the order they were demodulated. */
    struct adsb_message messages[ADSB_SESSION_MESSAGES];
    int message_count;

    uint64_t frames_total;
    uint64_t positions_total;

    /*
     * The funnel. `block_stats` is the latest block alone and `totals` the
     * run, because "nothing decoded" has several causes and which stage
     * stopped is the diagnosis: no preambles at all is tuning or antenna,
     * preambles with failing parity is marginal bits, and clipping mangles
     * the pulse amplitudes the demodulator compares.
     */
    struct adsb_demod_stats block_stats;
    struct adsb_demod_stats totals;

    /*
     * `trace` is the most recent attempt whatever its outcome, because a frame
     * that failed its parity is the one worth looking at; `good_trace` is the
     * last that passed, for a reader to pin.
     */
    struct adsb_frame_trace trace;
    struct adsb_frame_trace good_trace;
};

void adsb_session_reset(struct adsb_session *s);

/*
 * Feed one block's magnitudes -- Mode S is demodulated from the envelope, not
 * from I and Q, which is why this takes magnitudes where the other sessions
 * take both.
 *
 * Returns how many messages the block produced, and leaves them in
 * `messages`. `now` timestamps them and drives the pairing cache's staleness,
 * so a replay passing a block index gets the same answers twice.
 */
int adsb_session_feed(struct adsb_session *s, const float *magnitudes,
                      size_t pair_count, double now);

#endif /* ADSB_SESSION_H */
