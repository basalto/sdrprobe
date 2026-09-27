#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "model/gsm_view_model.h"
#include "runtime/app.h"
#include "runtime/scan_plan.h"

/*
 * The GSM screen's sentences and numbers, decided once.
 *
 * In `runtime/` and not beside the model it fills, because a builder reads
 * the application's state and a contract may not (ADR-0028). The same split
 * the other four view models have.
 */

/* The mirrored bound `gsm_view_model.h` explains: a band that grew would
   stop the build here rather than truncate a chart somewhere. */
typedef char gsm_view_model_channels_match
    [(SCAN_ARFCN_LAST == GSM_VIEW_MODEL_LAST_ARFCN) ? 1 : -1];

/*
 * Which of four things the SCH line says.
 *
 * The order is the window's own `if` chain and the precedence matters: a
 * recording in progress replaces the readout entirely, which is why it sits
 * above "searching" rather than beside it. Nothing is said at all until a
 * channel has been chosen and any scan has finished -- the chart underneath
 * is the scan's while it runs.
 */
static enum gsm_sch_reading sch_reading(const struct gsm_view *gsm,
                                        const struct band_scan *scan,
                                        int recording, int receiver_mode) {
    if (gsm->selected_arfcn <= 0 || scan->running)
        return GSM_SCH_IDLE;
    if (gsm->session.sch_valid)
        return GSM_SCH_DECODED;
    if (recording)
        return GSM_SCH_RECORDING;
    if (receiver_mode)
        return GSM_SCH_SEARCHING;
    return GSM_SCH_IDLE;
}

/*
 * And which of three the BCCH line says.
 *
 * `MISSED` is the interesting one and it is not "no message": the broadcast
 * channel occupies frames 2 to 5 of the 51-multiframe, so only the SCH at
 * frame 1 is followed by one. Four SCH decodes in five have nothing due and
 * nothing wrong; the fifth is a block that should have survived and did not.
 * A reader shown "waiting" for both would have no way to tell a weak signal
 * from an ordinary multiframe position.
 */
static enum gsm_bcch_reading bcch_reading(const struct gsm_view *gsm) {
    if (!gsm->session.sch_valid)
        return GSM_BCCH_NONE;
    if (gsm->session.cell.blocks > 0)
        return GSM_BCCH_READ;
    if (gsm->session.sch.frame_number % 51 == 1)
        return GSM_BCCH_MISSED;
    return GSM_BCCH_WAITING;
}

void gsm_view_model_build(const struct gsm_view *gsm,
                          const struct band_scan *scan,
                          const struct sdr_signal_stats *stats,
                          int stats_ready, int recording, int receiver_mode,
                          struct gsm_view_model *out) {
    int arfcn;

    memset(out, 0, sizeof(*out));

    out->selected_arfcn = gsm->selected_arfcn;
    out->selected_hz = gsm->selected_hz;
    out->sch = sch_reading(gsm, scan, recording, receiver_mode);
    out->bcch = bcch_reading(gsm);

    /*
     * The fields under each line are filled only when that line is the one
     * carrying them, and zeroed otherwise -- not left at whatever the last
     * decode held. A reader that showed the previous minute's cell under this
     * minute's heading would be wrong in the one way nobody would question.
     */
    if (out->sch == GSM_SCH_DECODED) {
        const struct gsm_sch_result *sch = &gsm->session.sch;

        out->bsic = sch->bsic;
        out->ncc = sch->ncc;
        out->bcc = sch->bcc;
        out->frame_number = sch->frame_number;
        out->t1 = sch->t1;
        out->t2 = sch->t2;
        out->t3 = sch->t3;
        out->confidence = sch->confidence;
        out->implausible = gsm->session.continuity.implausible;
    }
    if (out->bcch == GSM_BCCH_READ) {
        const struct gsm_cell *cell = &gsm->session.cell;

        out->blocks = cell->blocks;
        out->have_lai = cell->have_lai;
        out->mcc = cell->mcc;
        out->mnc = cell->mnc;
        out->mnc_digits = cell->mnc_digits;
        out->lac = cell->lac;
        out->have_cell_id = cell->have_cell_id;
        out->cell_id = cell->cell_id;
        out->neighbour_count = cell->neighbour_count;
        memcpy(out->neighbours, cell->neighbours, sizeof(out->neighbours));
    }

    out->signal_stats_ready = stats_ready;
    if (stats_ready)
        out->signal_stats = *stats;

    out->scanning = scan->running;
    out->step = scan->step;
    out->step_count = scan->plan.step_count;
    for (arfcn = 0; arfcn < GSM_VIEW_MODEL_CHANNELS; arfcn++) {
        out->power[arfcn] = scan->power[arfcn];
        out->bcch_confidence[arfcn] = scan->bcch_conf[arfcn];
        if (scan->power[arfcn] > SCAN_SENTINEL_DBFS)
            out->have_scan = 1;
    }
}
