#include "lte_chain_analysis.h"

#include <string.h>

/*
 * The order here is the order both adapters used, and it is kept rather than
 * tidied: the search, then the primary choice, then the neighbours, then the
 * primary's own measurements, then its broadcast channel. Nothing below
 * changes an arithmetic or a threshold -- this is one implementation of a walk
 * that had two.
 */

void lte_chain_run_reset(struct lte_chain_run *run) {
    if (!run)
        return;
    memset(run, 0, sizeof(*run));
    lte_stats_clear(&run->stats);
    lte_mib_repeat_reset(&run->repeat);
}

/*
 * The strongest *correlation*, not the strongest level.
 *
 * `lte_cell_search_all` ranks by reference power, which is the right order for
 * presenting neighbours and the wrong way to pick the cell being walked: an
 * identity the search invented has its power measured at reference positions
 * belonging to no transmitter, and that reads high often enough to take first
 * place. The primary then flips block to block, which is invisible in a
 * per-block report and obvious the moment anything accumulates -- it reset a
 * run's statistics to three samples out of a hundred and forty-six. The
 * correlation is what the detector locked onto, and it is stable.
 */
static int strongest_correlation(const struct lte_cell *cells, int count) {
    int best = 0, i;

    for (i = 1; i < count; i++)
        if (cells[i].pss_correlation > cells[best].pss_correlation)
            best = i;
    return best;
}

/*
 * Whether this identity's own broadcast channel decodes, under each combining
 * hypothesis in turn. It is what settles a neighbour: repetition cannot
 * manufacture a Master Information Block, because the message is scrambled
 * with the identity and checked by a CRC, so it cannot fit unless the
 * identity is right.
 *
 * `mib` and `ports` may be NULL for a caller that only wants the yes or no.
 */
static int broadcast_decodes(const struct lte_chain_block *block,
                             const struct lte_cell *cell, struct lte_mib *mib,
                             int *ports) {
    struct lte_mib scratch;
    int h;

    if (!mib)
        mib = &scratch;
    for (h = 0; h < LTE_SESSION_PORT_HYPOTHESES; h++) {
        float soft[LTE_PBCH_SOFT_BITS];
        int hypothesis = lte_session_port_hypotheses[h];

        if (lte_pbch_soft_bits(block->i_samples, block->q_samples,
                               block->pair_count, block->sample_rate_hz, cell,
                               cell->subframe0_start, hypothesis, soft,
                               NULL) != LTE_PBCH_SOFT_BITS)
            continue;
        if (!lte_mib_decode(soft, cell->pci, mib))
            continue;
        if (ports)
            *ports = hypothesis;
        return 1;
    }
    return 0;
}

int lte_chain_analyse(struct lte_chain_run *run,
                      const struct lte_chain_block *block,
                      struct lte_chain_result *out) {
    struct lte_cell fallback;
    int found, c;

    if (!run || !block || !out || !block->i_samples || !block->q_samples ||
        !(block->sample_rate_hz > 0.0))
        return -1;
    memset(out, 0, sizeof(*out));
    if (block->pair_count < LTE_CHAIN_MIN_PAIRS)
        return -1;

    run->blocks++;

    /*
     * One search for the whole carrier. This used to run twice -- once for the
     * cell being walked and once again for its neighbours -- and the counter
     * is here so a check can say so rather than infer it from equal answers.
     */
    run->searches++;
    found = lte_cell_search_all(block->i_samples, block->q_samples,
                                block->pair_count, block->sample_rate_hz,
                                block->full_scale, out->on_carrier,
                                LTE_MAX_CELLS_PER_CARRIER, NULL);
    if (found > 0) {
        out->cell_count = found;
        out->primary = out->on_carrier[strongest_correlation(out->on_carrier,
                                                             found)];
        out->primary_from_all = 1;
        out->have_cell = 1;
    } else {
        /*
         * The single-cell search is called only when nothing survived, and it
         * fills in what it measured even when it refuses -- which is what the
         * no-cell line reports, and a block with no cell in it has the time to
         * spare.
         */
        memset(&fallback, 0, sizeof(fallback));
        if (lte_cell_search(block->i_samples, block->q_samples,
                            block->pair_count, block->sample_rate_hz,
                            &fallback, NULL) != 1) {
            out->primary = fallback;
            out->refused = 1;
            return 0;
        }
        out->primary = fallback;
        out->have_cell = 1;
    }
    run->cells++;

    /*
     * Anyone else on the carrier. A block holding two cells reported one and
     * looked, across blocks, like a single cell changing its mind -- which is
     * how EARFCN 3625's pair were found.
     */
    for (c = 0; c < out->cell_count; c++) {
        struct lte_chain_neighbour *n;

        if (out->on_carrier[c].pci == out->primary.pci)
            continue;
        n = &out->neighbour[out->neighbour_count++];
        n->cell = out->on_carrier[c];
        n->timing_from_primary = (long)n->cell.subframe0_start -
                                 (long)out->primary.subframe0_start;
        n->mib_decoded = broadcast_decodes(block, &n->cell, NULL, NULL);
        n->have_power = lte_reference_power(block->i_samples, block->q_samples,
                                            block->pair_count,
                                            block->sample_rate_hz,
                                            block->full_scale, &n->cell,
                                            &n->power);
        lte_confirm_saw(&run->tally, n->cell.pci, n->mib_decoded);
    }

    /* The primary, into the run's statistics as well as onto the line:
       everything here moves, and a summary of what it did beats three hundred
       lines of what it was. */
    lte_stats_for_cell(&run->stats, out->primary.pci);
    lte_stat_add(&run->stats.frequency_khz,
                 (float)(out->primary.frequency_offset_hz / 1e3));
    lte_stat_add(&run->stats.pss, out->primary.pss_correlation);
    lte_stat_add(&run->stats.sss, out->primary.sss_correlation);

    out->have_shape = lte_channel_shape(block->i_samples, block->q_samples,
                                        block->pair_count,
                                        block->sample_rate_hz,
                                        block->full_scale, &out->primary,
                                        &out->shape);
    if (out->have_shape) {
        lte_stat_add(&run->stats.delay_ns, out->shape.delay_ns);
        lte_stat_add(&run->stats.spread_ns, out->shape.delay_spread_ns);
        lte_stat_add(&run->stats.drift_hz, out->shape.drift_hz);
    }

    out->have_ports = lte_port_coherence(block->i_samples, block->q_samples,
                                         block->pair_count,
                                         block->sample_rate_hz, &out->primary,
                                         out->coherence);
    if (out->have_ports) {
        int p;
        for (p = 0; p < LTE_PORT_COUNT; p++)
            if (out->coherence[p] >= LTE_PORT_COHERENCE_PRESENT)
                out->port_count++;
        lte_stat_add(&run->stats.ports, (float)out->port_count);
    }

    /* dBFS and not dBm: there is no calibrated gain in front of this
       receiver. RSRQ is the comparable one. */
    out->have_power = lte_reference_power(block->i_samples, block->q_samples,
                                          block->pair_count,
                                          block->sample_rate_hz,
                                          block->full_scale, &out->primary,
                                          &out->power);
    if (out->have_power) {
        lte_stat_add(&run->stats.rsrp_dbfs, out->power.rsrp_dbfs);
        lte_stat_add(&run->stats.rsrq_db, out->power.rsrq_db);
        lte_stat_add(&run->stats.sinr_db, out->power.sinr_db);
    }

    out->have_mib = broadcast_decodes(block, &out->primary, &out->mib,
                                      &out->mib_ports_combined);
    if (out->have_mib) {
        run->decoded++;
        /* A parity that passes is not yet a message. What separates them is a
           repeat that agrees, and this is the one implementation of it. */
        out->mib_agreed = lte_mib_repeat_observe(&run->repeat, &out->mib);
        if (out->mib_agreed)
            run->agreed++;
    }

    /* The cell the block was walked for goes in the tally beside the
       neighbours, so the verdicts cover the whole carrier. */
    lte_confirm_saw(&run->tally, out->primary.pci, out->have_mib);
    return 1;
}
