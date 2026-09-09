#include "lte_session.h"

#include <stdio.h>
#include <string.h>

const int lte_session_port_hypotheses[LTE_SESSION_PORT_HYPOTHESES] = {1, 2, 4};

void lte_session_reset(struct lte_session *s) {
    if (s)
        memset(s, 0, sizeof(*s));
}

/* Every measurement of this block into the run's statistics. */
static void lte_session_accumulate(struct lte_session *s) {
    lte_stats_for_cell(&s->stats, s->cell.pci);
    lte_stat_add(&s->stats.frequency_khz,
                 (float)(s->cell.frequency_offset_hz / 1e3));
    lte_stat_add(&s->stats.pss, s->cell.pss_correlation);
    lte_stat_add(&s->stats.sss, s->cell.sss_correlation);
    if (s->power_valid) {
        lte_stat_add(&s->stats.rsrp_dbfs, s->power.rsrp_dbfs);
        lte_stat_add(&s->stats.rsrq_db, s->power.rsrq_db);
        lte_stat_add(&s->stats.sinr_db, s->power.sinr_db);
    }
    if (s->shape_valid) {
        lte_stat_add(&s->stats.delay_ns, s->shape.delay_ns);
        lte_stat_add(&s->stats.spread_ns, s->shape.delay_spread_ns);
        lte_stat_add(&s->stats.drift_hz, s->shape.drift_hz);
    }
    if (s->port_coherence_valid) {
        int ports = 0, p;
        for (p = 0; p < LTE_PORT_COUNT; p++)
            if (s->port_coherence[p] >= LTE_PORT_COHERENCE_PRESENT)
                ports++;
        lte_stat_add(&s->stats.ports, (float)ports);
    }
}

int lte_session_feed(struct lte_session *s, const float *i_samples,
                     const float *q_samples, size_t pair_count,
                     double sample_rate, float full_scale, double now,
                     struct lte_trace *trace, struct lte_session_event *out) {
    struct lte_session_event event;
    struct lte_cell cell;
    int h;

    memset(&event, 0, sizeof(event));
    if (out)
        *out = event;
    if (!s || !i_samples || !q_samples)
        return 0;

    s->blocks_seen++;

    /* ADR-0014: the arithmetic is the 1.92 MS/s grid and nothing else will
       do. Saying so beats an empty screen. */
    if (sample_rate != LTE_SAMPLE_RATE_HZ) {
        snprintf(s->status, sizeof(s->status),
                 "Receiver is at %.3f MS/s; LTE's grid is 1.920.",
                 sample_rate / 1e6);
        event.off_grid = 1;
        if (out)
            *out = event;
        return 0;
    }
    /* The search needs a whole half-frame plus a symbol to be sure of holding
       one synchronisation signal, and a whole subframe after it. */
    if (pair_count < (size_t)(LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE)) {
        snprintf(s->status, sizeof(s->status),
                 "Block holds %zu samples; a cell search needs %d.",
                 pair_count, LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE);
        event.block_too_short = 1;
        if (out)
            *out = event;
        return 0;
    }

    if (lte_cell_search(i_samples, q_samples, pair_count, sample_rate, &cell,
                        trace) != 1) {
        /*
         * The search fills in what it measured even when it refuses, and the
         * two cases are worth telling apart: an empty channel, or a carrier
         * whose primary sequence locked and whose secondary one did not.
         */
        if (cell.pss_correlation > 0.5f)
            snprintf(s->status, sizeof(s->status),
                     "A primary sequence at %.2f, but the secondary one only "
                     "reached %.2f against %.2f -- no identity.",
                     (double)cell.pss_correlation,
                     (double)cell.sss_correlation,
                     (double)cell.sss_runner_up);
        else
            snprintf(s->status, sizeof(s->status),
                     "No synchronisation signal here. Scan the band to find "
                     "one.");
        if (out)
            *out = event;
        return 0;
    }

    /* A different identity is a different cell: what was waiting to be
       believed belonged to the old one. */
    if (s->cell_valid && cell.pci != s->cell.pci)
        s->pending_mib_hits = 0;
    s->cell = cell;
    s->cell_valid = 1;
    s->cell_time = now;
    s->cells_found++;
    event.cell_found = 1;

    s->power_valid = lte_reference_power(i_samples, q_samples, pair_count,
                                         sample_rate, full_scale, &cell,
                                         &s->power);
    s->port_coherence_valid = lte_port_coherence(i_samples, q_samples,
                                                 pair_count, sample_rate,
                                                 &cell, s->port_coherence);
    s->shape_valid = lte_channel_shape(i_samples, q_samples, pair_count,
                                       sample_rate, full_scale, &cell,
                                       &s->shape);
    lte_session_accumulate(s);

    snprintf(s->status, sizeof(s->status),
             "Cell %d found; its broadcast has not decoded yet.", cell.pci);

    for (h = 0; h < LTE_SESSION_PORT_HYPOTHESES; h++) {
        float soft[LTE_PBCH_SOFT_BITS];
        struct lte_mib mib;

        if (lte_pbch_soft_bits(i_samples, q_samples, pair_count, sample_rate,
                               &cell, cell.subframe0_start,
                               lte_session_port_hypotheses[h], soft,
                               trace) != LTE_PBCH_SOFT_BITS)
            continue;
        if (!lte_mib_decode(soft, cell.pci, &mib))
            continue;

        /*
         * The mask is not required to agree with the combining, and requiring
         * it was a mistake that threw away every real message this decoder
         * produced. On the band 20 captures the message comes out under the
         * single-port combining and its mask says two ports -- consistently,
         * in every block, with a frame number advancing at exactly the right
         * rate. What guards against a lucky parity is the repeat below.
         */
        s->mib_parity_passes++;
        event.parity_passed = 1;

        if (lte_mib_same_cell(&s->pending_mib, &mib) &&
            s->pending_mib_hits > 0) {
            s->pending_mib_hits++;
        } else {
            s->pending_mib = mib;
            s->pending_mib_hits = 1;
        }
        if (s->pending_mib_hits < LTE_SESSION_MIB_AGREEMENTS) {
            snprintf(s->status, sizeof(s->status),
                     "A broadcast passed its parity; waiting for a second "
                     "that agrees with it.");
            if (out)
                *out = event;
            return 1;
        }

        s->mib = mib;
        s->mib_valid = 1;
        s->mib_time = now;
        s->mib_ports_used = lte_session_port_hypotheses[h];
        s->mibs_decoded++;
        s->status[0] = '\0';
        event.message_confirmed = 1;
        if (out)
            *out = event;
        return 1;
    }

    if (out)
        *out = event;
    return 1;
}
