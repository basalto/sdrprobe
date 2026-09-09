#ifndef FM_SESSION_H
#define FM_SESSION_H

#include <stddef.h>

#include "fm_dsp.h"
#include "fm_scan.h"   /* fm_rds_chunk_length */
#include "rds.h"

/*
 * An RDS decode, block by block, with no window and no receiver.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`, last of five, and the one
 * whose boundary needed deciding rather than following.
 *
 * `update_fm_flush()` did three things in one pass over the multiplex: decoded
 * RDS, produced **sound**, and built two chart spectra. Only the first is a
 * decode. The sound writes into a raylib `AudioStream` and the spectra exist
 * to be drawn, so both stay in the view -- a session that owned them could not
 * link `-lm` alone, which is the whole point of the split (ADR-0012).
 *
 * What is here is the RDS chain: the front end, the baseband accumulator, the
 * chunking, the soft bits, the bit memory and the station.
 *
 * **The chunking is the subtle part and it is not a sliding window.** Baseband
 * accumulates until there is a whole chunk, then one timing search and one
 * axis are computed over it and its bits are appended; a sliding window would
 * re-derive its timing offset and drop a leading symbol each pass, so which
 * absolute symbol an index means would move underneath the caller. Bits
 * accumulate rather than baseband, because a radio text needs twenty-five
 * seconds of groups and one timing search costs work proportional to its span.
 */

/* Baseband held between blocks, soft bits from one chunk, and the bit memory
   groups are decoded out of. The last is the long one: a radio text is
   twenty-five seconds of groups. */
#define FM_SESSION_BASEBAND 65536   /* about 3.4 s at the pilot rate */
#define FM_SESSION_SOFT_BITS \
    (FM_SESSION_BASEBAND / FM_RDS_SAMPLES_PER_SYMBOL + 8)
#define FM_SESSION_BIT_MEMORY 32768

struct fm_session {
    struct fm_rds_front front;
    int front_rate;                 /* the rate it was built for */

    float bb_i[FM_SESSION_BASEBAND];
    float bb_q[FM_SESSION_BASEBAND];
    size_t bb_count;
    size_t bb_consumed;             /* baseband already turned into bits */

    float soft[FM_SESSION_SOFT_BITS];
    size_t soft_count;
    int timing_offset;
    double axis_radians;

    /* Everything the run has produced, oldest first. */
    float bits[FM_SESSION_BIT_MEMORY];
    size_t bit_count;

    struct rds_station station;

    /* Cumulative across the run, where the station is a window: these say
       whether reception is getting better or worse. */
    long blocks_seen;
    long groups_total;
    double last_group_at;
};

struct fm_session_event {
    int front_rebuilt;      /* the sample rate changed under it */
    int chunk_decoded;      /* a whole chunk became bits this block */
    int groups_advanced;    /* and at least one new group came out */
};

void fm_session_reset(struct fm_session *s);

/*
 * Feed one block's demodulated multiplex.
 *
 * The caller does the discriminator, because it wants the multiplex anyway --
 * for the sound and for the charts -- and doing it twice would be the same
 * work for the same answer.
 *
 * `flush` says there will be no more baseband for this tuning: a band scan is
 * about to move on, so whatever has arrived is decoded as a short chunk
 * instead of being thrown away unread. It is as valid as a full one and simply
 * carries fewer bits -- the fixed size exists so *consecutive* chunks agree
 * about which absolute symbol an index means, and a tuning that is ending has
 * no next chunk to agree with.
 *
 * Returns 1 when a chunk was decoded this block.
 */
int fm_session_feed(struct fm_session *s, const float *multiplex, size_t count,
                    double sample_rate, double now, int flush,
                    struct fm_session_event *out);

#endif /* FM_SESSION_H */
