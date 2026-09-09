#ifndef LTE_SESSION_H
#define LTE_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "lte_dsp.h"
#include "lte_mib.h"
#include "lte_stats.h"

/*
 * An LTE decode, block by block, with no window and no receiver.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`, third and largest. What
 * moved here is one block's whole answer: the cell search, the reference
 * power, the port coherence, the channel shape, the run's statistics, and the
 * rule that decides when a broadcast message is believed.
 *
 * **That last one is why this is worth a module.** Sixteen bits of parity
 * sound decisive until you count the attempts: four scrambling offsets against
 * three masks, for each of three antenna-port hypotheses, is thirty-six
 * chances a block, and a session that finds a cell nine thousand times over
 * half an hour will see one pass by chance. That is not a rare accident to
 * tolerate -- it is the *expected* number. What a real cell has and chance
 * does not is consistency, so a message counts only when a second one agrees
 * about what a cell does not change between frames: its bandwidth, its
 * acknowledgement channel and its antenna count.
 *
 * A rule like that must have one implementation. `run_headless`'s
 * `--lte-chain` branch still counts differently on purpose -- it is a
 * diagnostic that reports every attempt rather than a decode that latches --
 * and the ticket records that as a question rather than as duplication.
 */

/* The combining hypotheses the broadcast channel is tried under, in order.
   The message is not required to decode under the combining its own mask
   names: the combining is only a way of getting soft bits good enough, and
   which one manages that is a property of the signal. */
#define LTE_SESSION_PORT_HYPOTHESES 3
extern const int lte_session_port_hypotheses[LTE_SESSION_PORT_HYPOTHESES];

/*
 * The rule itself, as a value with one implementation and two users: this
 * session, and `--lte-chain`.
 *
 * Those two had it written out separately and **the two spellings were
 * provably the same rule** -- driven by every sequence of up to eight parity
 * passes over three distinct messages, 9840 of them, they never disagreed, and
 * a live run reads `decoded 140 agreed 139`, the same `n - 1` the session gives
 * on a capture. It is one function now.
 *
 * It is a **memory of the previous pass, not a counter**. An earlier shape
 * here was `pending_mib` plus `pending_mib_hits`, compared against a threshold
 * of two -- which read as state accumulating toward a bar and could never mean
 * more than "did the previous pass agree", because a message was emitted on
 * every pass once the count reached two. The threshold was decoration.
 */
struct lte_mib_repeat {
    struct lte_mib previous;
    int have_previous;
};

static inline void lte_mib_repeat_reset(struct lte_mib_repeat *r) {
    if (r)
        r->have_previous = 0;
}

/*
 * Offer a parity pass. Returns 1 when it agrees with the one before it about
 * what a cell does not change between frames -- its bandwidth, its
 * acknowledgement channel and its antenna count -- and 0 otherwise. The frame
 * number is left out of that comparison because it advances, which is what it
 * is for.
 */
static inline int lte_mib_repeat_observe(struct lte_mib_repeat *r,
                                         const struct lte_mib *mib) {
    int agrees;

    if (!r || !mib)
        return 0;
    agrees = r->have_previous && lte_mib_same_cell(&r->previous, mib);
    r->previous = *mib;
    r->have_previous = 1;
    return agrees;
}

struct lte_session {
    struct lte_cell cell;
    int cell_valid;
    double cell_time;

    /* 36.214's reference-signal measurements for the cell above, filled in the
       same block it was found, so the level always belongs to the identity
       beside it. dBFS rather than dBm -- see struct lte_reference_power. */
    struct lte_reference_power power;
    int power_valid;
    float port_coherence[LTE_PORT_COUNT];
    int port_coherence_valid;
    struct lte_channel_shape shape;
    int shape_valid;

    /* What each measurement has done since this cell was found. Cleared when
       the identity changes -- see lte_stats.h, where the reset is load-bearing
       because a carrier here alternates between two cells block to block. */
    struct lte_cell_stats stats;

    struct lte_mib mib;
    int mib_valid;
    double mib_time;
    int mib_ports_used;         /* the combining the message decoded under */

    /* The previous parity pass, which is all the rule needs. */
    struct lte_mib_repeat repeat;

    uint64_t blocks_seen;
    uint64_t cells_found;
    /* Two different things, and they were both called "messages" somewhere
       until this ticket. `decoded` is a broadcast whose parity passed;
       `confirmed` is one that also agreed with the pass before it. */
    uint64_t mibs_decoded;
    uint64_t mibs_confirmed;

    /*
     * Why the last block produced nothing, when it produced nothing.
     *
     * A sentence rather than a code, and deliberately: both adapters want the
     * same words, and the common answer -- the receiver is not on LTE's
     * sample grid -- is one a reader acts on. `lte_findings.h` sets the
     * precedent for a module that produces prose with its numbers attached.
     */
    char status[160];
};

/* What one block produced. */
struct lte_session_event {
    int off_grid;          /* not at 1.92 MS/s; ADR-0014 */
    int block_too_short;
    int cell_found;
    int parity_passed;     /* a broadcast passed its parity this block */
    int message_confirmed; /* and a second one agreed, so it is believed */
};

void lte_session_reset(struct lte_session *s);

/*
 * Feed one block of centred I/Q at `sample_rate`, which must be
 * LTE_SAMPLE_RATE_HZ. `full_scale` is the converter's, for the dBFS
 * readings. `trace`, when not NULL, collects what the search saw -- it costs a
 * second pass, so a caller that is not drawing passes NULL.
 *
 * Returns 1 when a cell was found, 0 otherwise, and fills `out` either way.
 */
int lte_session_feed(struct lte_session *s, const float *i_samples,
                     const float *q_samples, size_t pair_count,
                     double sample_rate, float full_scale, double now,
                     struct lte_trace *trace, struct lte_session_event *out);

#endif /* LTE_SESSION_H */
