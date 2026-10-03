#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "model/lte_view_model.h"
#include "runtime/app.h"
#include "runtime/runtime.h"

/*
 * The LTE screen's identity, statistics, broadcast and findings, gathered
 * once.
 *
 * In `runtime/` and not beside the model it fills: a builder reads the
 * application's state and a contract may not (ADR-0028).
 */

void lte_view_model_build(const struct lte_view *lte,
                          const struct lte_view_context *ctx,
                          struct lte_view_model *out) {
    const struct lte_session *s = &lte->session;
    const struct lte_band_scan *scan = &lte->scan;
    uint32_t centre_hz = ctx->centre_hz;
    int i;

    memset(out, 0, sizeof(*out));

    out->earfcn = lte->earfcn;
    out->centre_hz = (double)centre_hz;
    out->band = ctx->band_number;
    out->tuned_band = ctx->tuned_band;
    snprintf(out->tuned_band_name, sizeof(out->tuned_band_name), "%s",
             ctx->tuned_band_name ? ctx->tuned_band_name : "unknown");
    out->on_grid = ctx->on_grid;

    out->blocks_seen = s->blocks_seen;
    out->cells_found = s->cells_found;
    out->mibs_decoded = s->mibs_decoded;
    out->mibs_confirmed = s->mibs_confirmed;
    snprintf(out->status, sizeof(out->status), "%s", s->status);
    /*
     * A cell is being found and none of its broadcasts confirmed. It reads
     * `cells_found` rather than `blocks_seen` because a block is only an
     * opportunity: the fault worth colouring is a cell that is there and
     * will not give up a message, not a channel nobody has heard anything
     * on.
     */
    out->funnel_warn = s->cells_found > 0 && s->mibs_confirmed == 0;

    /*
     * The identity, and nothing under it unless there is one. A cell's
     * fields left standing after the search stopped finding it would put
     * last block's PCI under this block's heading.
     */
    out->cell_valid = s->cell_valid;
    if (s->cell_valid) {
        const struct lte_cell *c = &s->cell;

        out->pci = c->pci;
        out->n_id_1 = c->n_id_1;
        out->n_id_2 = c->n_id_2;
        out->extended_cp = c->extended_cp;
        out->subframe_sample = (int)c->subframe0_start;
        out->second_half = c->half_frame;
        /*
         * Parts per million, which is the figure that transfers: it is a
         * property of the receiver's *crystal* rather than of this carrier,
         * so it compares with what a GSM calibration measured on another
         * band. The hertz are in the statistics table.
         */
        out->crystal_ppm = centre_hz > 0
            ? c->frequency_offset_hz * 1e6 / (double)centre_hz : 0.0;
        out->crystal_subcarriers = c->integer_offset;
        out->cell_age_seconds = ctx->now - s->cell_time;
        snprintf(out->marker_label, sizeof(out->marker_label), "PCI %d",
                 c->pci);
        /* Where the mark sits: the tuning plus the cell's own offset, the
           same frequency `view_lte.c` drew it at before it read this. */
        out->marker_hz = (double)centre_hz + c->frequency_offset_hz;
    }

    out->stats_valid = s->stats.valid;
    if (s->stats.valid)
        out->stats = s->stats;

    out->mib_valid = s->mib_valid;
    if (s->mib_valid) {
        const struct lte_mib *m = &s->mib;

        out->bandwidth_rb = m->bandwidth_prb;
        out->bandwidth_mhz = lte_mib_occupied_hz(m->bandwidth_prb)
                             / 1e6;
        /* One wording, chosen here: the window spells the duration and the
           resource allocation as one sentence and a browser that assembled
           its own would be a second decider about the same two fields. */
        {
            /* Guarded: `lte_phich_resource_name()` returns NULL outside the
               four the two-bit field can encode, and `%s` on a NULL is
               undefined however obligingly this libc prints "(null)". A MIB
               whose parity passed cannot be outside them; a builder handed
               anything at all can. */
            const char *res = lte_phich_resource_name(m->phich_resource_sixths);

            snprintf(out->phich, sizeof(out->phich), "%s, %s",
                     m->phich_extended ? "extended" : "normal",
                     res ? res : "?");
        }
        out->frame_number = m->system_frame_number;
        out->quarter = m->quarter;
        out->antenna_ports = m->antenna_ports;
        out->mib_age_seconds = ctx->now - s->mib_time;
    }

    /*
     * The sentences, already chosen. `lte_findings_from()` is the one
     * decider, so the window and a browser cannot word the same numbers
     * differently -- and one of its lines is a refusal, which is exactly
     * the kind of thing a second implementation would quietly drop.
     */
    lte_findings_from(&s->stats, (double)centre_hz, &out->findings);

    out->scanning = scan->running;
    out->confirming = scan->confirming;
    out->receiver_scan_possible = ctx->receiver_mode;
    out->found_count = scan->found_count;
    if (out->found_count > LTE_VIEW_MODEL_FOUND)
        out->found_count = LTE_VIEW_MODEL_FOUND;
    if (out->found_count < 0)
        out->found_count = 0;
    for (i = 0; i < out->found_count; i++)
        out->found[i] = scan->found[i];
    out->scan_selected = (scan->selected >= 0 && scan->selected < out->found_count)
                             ? scan->selected : -1;

    /*
     * How far along the pass is. The confirmation pass gets its own line
     * because the sweep's would sit at 100% and stop moving, which reads as
     * a scan that has hung rather than one spending its most useful four
     * seconds.
     */
    if (scan->running && scan->confirming)
        snprintf(out->scan_progress, sizeof(out->scan_progress),
                 "confirming %d of %d   %d dropped",
                 scan->confirm_index + scan->confirm_dropped + 1,
                 scan->confirm_total, scan->confirm_dropped);
    else if (scan->running && ctx->band_number)
        snprintf(out->scan_progress, sizeof(out->scan_progress),
                 "%.1f MHz   %d of %d   %.0f%%",
                 ctx->scan_candidate_hz / 1e6, scan->candidate + 1,
                 scan->total,
                 scan->total > 0 ? 100.0 * scan->candidate / scan->total
                                 : 0.0);

    /* What a scan would cost, while one could be started. Silent during a
       scan, because the progress line is saying where it is. */
    if (ctx->band_number && !scan->running && ctx->scan_channels > 0)
        snprintf(out->scan_cost, sizeof(out->scan_cost),
                 "%d channels; about %.0f s for the first pass, %.0f s for "
                 "all of them", ctx->scan_channels,
                 ctx->scan_first_pass_seconds, ctx->scan_all_seconds);

    /* And why the table is empty, when it is. Four reasons, and they are not
       the same answer. */
    if (scan->found_count == 0) {
        const char *note;

        if (scan->running)
            note = "Looking...";
        else if (scan->confirm_dropped > 0)
            note = "Nothing held up: every candidate failed its second look.";
        else if (!ctx->receiver_mode)
            note = "A scan needs a live receiver; a capture holds one tuning.";
        else
            note = "Press Scan band.";
        snprintf(out->scan_note, sizeof(out->scan_note), "%s", note);
    }

    /*
     * The analysis charts: the cell-search trace and the port coherence,
     * copied out for the browser. The counts say how much of each fixed array
     * is meaningful; the arrays themselves are small enough to carry whole.
     */
    out->trace_valid = lte->trace.valid;
    if (lte->trace.valid) {
        const struct lte_trace *t = &lte->trace;

        out->profile_count = t->profile_count;
        memcpy(out->profile, t->profile, sizeof(out->profile));
        out->candidate_count = t->candidate_count;
        out->candidate_best = t->candidate_best;
        memcpy(out->candidate, t->candidate, sizeof(out->candidate));
        out->channel_count = t->channel_count;
        memcpy(out->channel_db, t->channel_db, sizeof(out->channel_db));
        out->element_count = t->element_count;
        memcpy(out->element_i, t->element_i, sizeof(out->element_i));
        memcpy(out->element_q, t->element_q, sizeof(out->element_q));
    }
    out->port_coherence_valid = lte->session.port_coherence_valid;
    if (lte->session.port_coherence_valid) {
        out->port_count = LTE_PORT_COUNT;
        memcpy(out->port_coherence, lte->session.port_coherence,
               sizeof(out->port_coherence));
    }
}
