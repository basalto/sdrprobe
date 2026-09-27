#ifndef TETRA_SESSION_H
#define TETRA_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "tetra_dsp.h"
#include "tetra_sync.h"

/*
 * A TETRA decode, block by block, with no window and no receiver.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`, second of three. This was
 * the clearest duplication in the program: `update_tetra()` in `view_tetra.c`
 * and `print_tetra()` in `sdrprobe.c` each ran the same chain -- coarse offset,
 * channel filter, demodulate, walk the symbols for the synchronisation word,
 * decode the block, pull MCC/MNC/colour out of it, then the broadcast channel
 * scrambled with the colour code that block just gave up.
 *
 * They were not quite the same, which is the argument for this module rather
 * than against it. One pulled its fields with a `field(bits, at, count)`
 * helper; the other unrolled four bit loops inline. Two spellings of one
 * transcription from ETSI EN 300 392-2, either of which could have been fixed
 * without the other.
 *
 * **The view draws a session; the headless report prints one.**
 */

/* How many samples the channel filter may write. Scratch, and the session's
   own, so neither adapter has to size it. */
#define TETRA_SESSION_WORK TETRA_MAX_WORK

struct tetra_session {
    /*
     * The last block's symbols, kept because the analysis charts draw them --
     * the phase steps as points on a circle, and how much of a 255-symbol slot
     * repeats. A caller that only prints ignores them.
     */
    struct tetra_symbols symbols;
    int symbols_valid;
    struct tetra_burst_sync sync;
    int sync_valid;

    float lock;
    double offset_hz;

    /* The network, once a parity check has established it. */
    int have_identity;
    int mcc, mnc, colour, la;

    /* This block. */
    int bursts, blocks, broadcast;
    /* And the run. `blocks_failed` is the funnel's middle: a burst whose
       synchronisation word matched and whose parity did not. */
    uint64_t bursts_total, blocks_total, broadcast_total, blocks_failed;
};

/*
 * What one block produced.
 *
 * `rate_unsupported` is its own answer rather than a silent nothing: the
 * channel filter needs a sample rate that is a whole multiple of
 * TETRA_WORK_RATE_HZ, and a run at the wrong rate decodes nothing for a reason
 * that has nothing to do with the signal. Both adapters said so; now they say
 * it from the same flag.
 */
struct tetra_session_event {
    int rate_unsupported;
    int demodulated;
    int identity_changed;   /* colour code or location area moved */
};

void tetra_session_reset(struct tetra_session *s);

/*
 * Feed one block of centred I/Q. Returns the number of synchronisation blocks
 * that passed their parity check -- 0 is the ordinary answer off air and not
 * an error.
 */
int tetra_session_feed(struct tetra_session *s, const float *i_samples,
                       const float *q_samples, size_t pair_count,
                       double sample_rate, struct tetra_session_event *out);

#endif /* TETRA_SESSION_H */
