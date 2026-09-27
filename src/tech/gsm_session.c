#include "gsm_session.h"

#include <string.h>

void gsm_session_reset(struct gsm_session *s) {
    uint32_t options;

    if (!s)
        return;
    options = s->options;   /* a decode preference, not a cell's property */
    memset(s, 0, sizeof(*s));
    s->options = options;
}

/* Fold one message into what the cell has said so far. */
static void gsm_cell_remember(struct gsm_cell *cell, const struct gsm_si *si) {
    cell->blocks++;
    cell->last_type = si->type;
    if (si->have_lai) {
        cell->have_lai = 1;
        cell->mcc = si->mcc;
        cell->mnc = si->mnc;
        cell->mnc_digits = si->mnc_digits;
        cell->lac = si->lac;
    }
    if (si->have_cell_id) {
        cell->have_cell_id = 1;
        cell->cell_id = si->cell_id;
    }
    if (si->neighbour_count > 0) {
        cell->neighbour_count = si->neighbour_count;
        memcpy(cell->neighbours, si->neighbours,
               (size_t)si->neighbour_count * sizeof(*si->neighbours));
    }
}

/*
 * Four normal bursts after the synchronisation burst, into one System
 * Information message.
 *
 * Only one frame in five carries one: the message occupies frames 2 to 5 of
 * the 51-frame multiframe and the other four are paging and access grants,
 * which this does not read. Hence the `% 51 != 1` gate -- the SCH that
 * precedes a broadcast block is the one at frame 1.
 */
static int gsm_read_broadcast(const float *i_samples, const float *q_samples,
                              size_t pair_count, double sample_rate,
                              const struct gsm_sch_result *sch,
                              struct gsm_si *si) {
    float soft[GSM_BCCH_BURSTS * GSM_BURST_DATA_BITS];
    float bursts[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];
    float coded[GSM_BCCH_CODED_BITS];
    struct gsm_bcch_block block;

    if (sch->frame_number % 51 != 1)
        return 0;
    memset(soft, 0, sizeof(soft));
    if (gsm_normal_bursts(i_samples, q_samples, pair_count, sample_rate, sch,
                          GSM_BCCH_BURSTS, soft) < GSM_BCCH_BURSTS)
        return 0; /* the block ran past the end of this sample block */
    for (int b = 0; b < GSM_BCCH_BURSTS; b++)
        memcpy(bursts[b], &soft[b * GSM_BURST_DATA_BITS], sizeof(bursts[b]));
    gsm_bcch_deinterleave((const float (*)[GSM_BURST_DATA_BITS])bursts, coded);
    if (!gsm_bcch_decode_block(coded, &block))
        return 0; /* the Fire code refused it, so it is not a message */
    return gsm_si_parse(block.octets, si);
}

int gsm_session_feed(struct gsm_session *s, const float *i_samples,
                     const float *q_samples, size_t pair_count,
                     double sample_rate, double offset_hz, double now,
                     struct gsm_session_event *out) {
    struct gsm_sch_result result;
    struct gsm_sch_symbols symbols;
    struct gsm_session_event event;

    memset(&event, 0, sizeof(event));
    if (out)
        *out = event;
    if (!s || !i_samples || !q_samples || pair_count == 0)
        return 0;

    if (!gsm_sch_decode(i_samples, q_samples, pair_count, sample_rate,
                        offset_hz, s->options, &result, &symbols))
        return 0;

    s->sch = result;
    s->sch_symbols = symbols;
    s->sch_valid = 1;
    s->sch_time = now;
    gsm_continuity_observe(&s->continuity, result.t1, result.bsic, now);
    event.sch_decoded = 1;

    if (gsm_read_broadcast(i_samples, q_samples, pair_count, sample_rate,
                           &result, &event.si)) {
        gsm_cell_remember(&s->cell, &event.si);
        event.broadcast_read = 1;
    }

    if (out)
        *out = event;
    return 1;
}
