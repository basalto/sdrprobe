#include "fm_session.h"

#include <string.h>

void fm_session_reset(struct fm_session *s) {
    if (s)
        memset(s, 0, sizeof(*s));
}

/* Append `count` bits, dropping the oldest when the memory is full. */
static void fm_session_append(struct fm_session *s, const float *from,
                              size_t count) {
    size_t k;

    if (s->bit_count + count > FM_SESSION_BIT_MEMORY) {
        size_t drop = s->bit_count + count - FM_SESSION_BIT_MEMORY;

        if (drop > s->bit_count)
            drop = s->bit_count;
        memmove(s->bits, s->bits + drop,
                (s->bit_count - drop) * sizeof(s->bits[0]));
        s->bit_count -= drop;
    }
    for (k = 0; k < count && s->bit_count < FM_SESSION_BIT_MEMORY; k++)
        s->bits[s->bit_count++] = from[k];
}

int fm_session_feed(struct fm_session *s, const float *multiplex, size_t count,
                    double sample_rate, double now, int flush,
                    struct fm_session_event *out) {
    static float fresh_i[FM_SESSION_BASEBAND];
    static float fresh_q[FM_SESSION_BASEBAND];
    struct fm_session_event event;
    size_t bb, chunk;
    long before;

    memset(&event, 0, sizeof(event));
    if (out)
        *out = event;
    if (!s || !multiplex || count == 0)
        return 0;

    if (s->front_rate != (int)sample_rate) {
        if (fm_rds_front_init(&s->front, sample_rate) < 0)
            return 0;
        s->front_rate = (int)sample_rate;
        s->bb_count = 0;
        s->soft_count = 0;
        event.front_rebuilt = 1;
    }

    bb = fm_rds_front_feed(&s->front, multiplex, count, fresh_i, fresh_q,
                           FM_SESSION_BASEBAND);
    if (bb == 0) {
        if (out)
            *out = event;
        return 0;
    }

    /* Accumulate baseband until there is a whole chunk to decode. */
    if (bb > FM_SESSION_BASEBAND - s->bb_count)
        bb = FM_SESSION_BASEBAND - s->bb_count;
    memcpy(s->bb_i + s->bb_count, fresh_i, bb * sizeof(fresh_i[0]));
    memcpy(s->bb_q + s->bb_count, fresh_q, bb * sizeof(fresh_q[0]));
    s->bb_count += bb;
    s->blocks_seen++;

    chunk = fm_rds_chunk_length(s->bb_count, flush);
    if (chunk == 0) {
        if (out)
            *out = event;
        return 0;
    }

    s->soft_count = fm_rds_soft_bits(s->bb_i, s->bb_q, chunk, s->soft,
                                     FM_SESSION_SOFT_BITS, &s->timing_offset,
                                     &s->axis_radians);
    fm_session_append(s, s->soft, s->soft_count);

    /* The chunk is spent; keep whatever arrived past its end. */
    s->bb_count -= chunk;
    if (s->bb_count > 0) {
        memmove(s->bb_i, s->bb_i + chunk, s->bb_count * sizeof(s->bb_i[0]));
        memmove(s->bb_q, s->bb_q + chunk, s->bb_count * sizeof(s->bb_q[0]));
    }
    if (s->soft_count == 0) {
        if (out)
            *out = event;
        return 0;
    }
    event.chunk_decoded = 1;

    /*
     * Append only what is new.
     *
     * The window slides by whole symbols, so symbol k of this decode is symbol
     * k + dropped of the last one and the newest few are the ones that have
     * not been seen. Re-appending the whole window every block would count
     * each group twenty times over.
     */
    {
        size_t fresh_bb = s->bb_count > s->bb_consumed
                              ? s->bb_count - s->bb_consumed : 0;
        size_t fresh = fresh_bb / FM_RDS_SAMPLES_PER_SYMBOL;

        if (fresh > s->soft_count)
            fresh = s->soft_count;
        fm_session_append(s, s->soft + s->soft_count - fresh, fresh);
        s->bb_consumed = s->bb_count;
    }

    /* Over everything remembered, not just the window: the window is three
       seconds and a radio text is twenty-five. */
    before = s->station.funnel.groups;
    rds_decode(s->bits, s->bit_count, &s->station, NULL, 0);
    if (s->station.funnel.groups > 0 && s->station.funnel.groups != before) {
        s->last_group_at = now;
        event.groups_advanced = 1;
    }
    s->groups_total = s->station.funnel.groups;

    if (out)
        *out = event;
    return 1;
}
