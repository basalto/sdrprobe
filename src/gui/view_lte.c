#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gui/view.h"
#include "model/lte_view_model.h"
#include "gui/lte_layout.h"
#include "tech/lte_findings.h"
#include "gui/sdrgui.h"
#include "runtime/debug_log.h"

/*
 * The Decode tab's LTE screen: which cell is on this carrier, what it
 * broadcasts about itself, and a scan for finding one in the first place.
 *
 * The scan is the part the other two decode views do not need. A GSM channel
 * is 200 kHz and a 2 MHz window holds ten of them, so that view measures a
 * tenth of the band per tuning. LTE cannot be swept that way: the primary
 * sequence is found by a correlation in the time domain, which a frequency
 * error smears, so the receiver has to sit within a few kilohertz of a
 * carrier's own centre. Every channel is its own tuning, and a band is three
 * hundred of them -- lte_scan.h holds the order that makes that bearable.
 *
 * The view also takes the receiver to 1.92 MS/s while it is open, and gives
 * the rate and the tuning back on the way out. It is the only view that
 * changes the sample rate, and it has no choice: LTE's arithmetic is that
 * grid (ADR-0014).
 */

/* The three antenna-port arrangements a cell can use. Nothing before the
   message's own parity says which, so all three are tried. */

/* The bands a dongle can actually reach, in the order they are offered. The
   table in lte_dsp.c also holds 1, 3 and 7, which sit above an R820T's
   tuning range; offering them would be offering a button that cannot work. */
/* The reachable list lives with the band table now, so the calibration
   picker and this view cannot offer different bands. */

static struct lte_layout lte_layout_now(const struct app *app) {
    int bands[LTE_BANDS_MAX];

    return lte_layout_for((float)GetScreenWidth(), (float)GetScreenHeight(),
                          view_lte_bands(app, bands));
}


/* lte_band_for_number() lives with the table now, so the calibration picker
   and this view cannot disagree about what a band number means. */




/* ------------------------------------------------------------------ */
/* Borrowing the receiver, and giving it back.                         */
/* ------------------------------------------------------------------ */





/* ------------------------------------------------------------------ */
/* The band scan.                                                      */
/* ------------------------------------------------------------------ */







/* Park on a cell: tune to it and let the ordinary per-block decode take over
   from there. */




/* ------------------------------------------------------------------ */
/* What one sample block yields.                                       */
/* ------------------------------------------------------------------ */


/* ------------------------------------------------------------------ */
/* Drawing.                                                            */
/* ------------------------------------------------------------------ */

static const Color panel_edge = { 44, 62, 80, 255 };
static const Color panel_caption = { 187, 205, 216, 255 };
static const Color row_label = { 132, 156, 172, 255 };
static const Color row_value = { 226, 236, 243, 255 };
static const Color row_muted = { 120, 140, 155, 255 };
static const Color warning = { 250, 190, 74, 255 };
static const Color row_pick = { 120, 230, 255, 255 };

/*
 * A panel and its caption; returns the y the first row goes on.
 *
 * The caption is clipped to the panel rather than drawn straight, because the
 * scan column is narrow and a caption that overruns does not stop at the edge
 * -- it lands on top of the panel beside it, which is what it did.
 */
static int draw_panel(Rectangle rect, const char *caption) {
    DrawRectangleRec(rect, (Color){ 17, 26, 37, 255 });
    DrawRectangleLinesEx(rect, 1.0f, panel_edge);
    sdrgui_text_fit(caption, (int)rect.x + 12, (int)rect.y + 10, 16,
                    rect.width - 24.0f, panel_caption);
    return (int)rect.y + 36;
}

/*
 * A row carrying what a measurement did rather than what it says now:
 * smallest, mean and largest since the cell last changed.
 *
 * A count of zero draws the label and nothing else. That is the honest state
 * for a measurement whose block failed -- an empty column says "not read",
 * where a zero would say "read, and it was zero", and for a reference power
 * in dBFS zero is a level this receiver cannot reach.
 */
static void draw_stat_row(const struct lte_panel_rows *rows, int index,
                          const char *label, const struct lte_stat *stat,
                          const char *format) {
    char text[32];
    float values[3];
    int y, c;

    if (index < 0 || index >= rows->row.capacity)
        return;
    y = (int)(rows->row.first_y + (float)index * rows->row.step);
    sdrgui_text_fit(label, (int)rows->row.label_x, y, LTE_PANEL_ROW_FONT,
                    rows->row.label_width, row_label);
    if (!stat->count) {
        sdrgui_text_fit("--", (int)rows->stat_x[1], y, LTE_PANEL_ROW_FONT,
                        rows->stat_width, row_muted);
        return;
    }
    values[0] = stat->min;
    values[1] = lte_stat_mean(stat);
    values[2] = stat->max;
    for (c = 0; c < 3; c++) {
        snprintf(text, sizeof(text), format, (double)values[c]);
        sdrgui_text_fit(text, (int)rows->stat_x[c], y, LTE_PANEL_ROW_FONT,
                        rows->stat_width, c == 1 ? row_value : row_muted);
    }
}

/*
 * One row of a panel, positioned by lte_layout.h rather than by counting
 * pixels between draw calls. `index` is which row, and a row past the panel's
 * capacity is not drawn at all -- silently off the bottom edge is worse than
 * absent, and the caller orders its rows so the ones that fit are the ones
 * that matter.
 *
 * The label truncates like the value. It used to be drawn without a width, so
 * a long label on a narrow panel ran underneath the number beside it.
 */
static void draw_row_at(const struct lte_panel_rows *rows, int index,
                        const char *label, const char *value, Color colour) {
    int y;

    if (index < 0 || index >= rows->row.capacity)
        return;
    y = (int)(rows->row.first_y + (float)index * rows->row.step);
    sdrgui_text_fit(label, (int)rows->row.label_x, y, LTE_PANEL_ROW_FONT,
                    rows->row.label_width, row_label);
    sdrgui_text_fit(value, (int)rows->row.value_x, y, LTE_PANEL_ROW_FONT,
                    rows->row.value_width, colour);
}

/* Which row of the scan list the pointer is over, or -1. Shared by the click
   handler and the highlight, so the two cannot disagree about what is under
   the pointer. */
#define LTE_FOUND_ROW_HEIGHT 20

static int found_row_at(Rectangle rect, int count, Vector2 point) {
    int first_y = (int)rect.y + 36;
    int row;
    if (!CheckCollisionPointRec(point, rect))
        return -1;
    row = ((int)point.y - first_y) / LTE_FOUND_ROW_HEIGHT;
    if (row < 0 || row >= count)
        return -1;
    return row;
}

/* In analysis mode the list moves under the charts, beside the
   constellation. One function so the drawing and the clicking agree. */
static Rectangle found_rect(const struct app *app,
                            const struct lte_layout *l) {
    if (!app->lte.analysis_mode)
        return l->found_panel;
    return (Rectangle){ l->found_panel.x, l->constellation.y,
                        l->constellation.x - 24.0f - l->found_panel.x,
                        l->constellation.height };
}

static void draw_found_panel(const struct lte_view_model *m, int selected,
                             Rectangle rect) {
    int y = draw_panel(rect, "Scan -- MHz, cell, PSS/margin");
    int hovered = found_row_at(rect, m->found_count, GetMousePosition());
    char text[160];
    int i;

    /* Both sentences are the model's: which of four reasons the table is
       empty, and how far along a pass is -- the confirmation pass writes
       its own because the sweep's would sit at 100% and read as a hang. */
    if (m->scan_progress[0])
        sdrgui_text_fit(m->scan_progress, (int)rect.x + 12,
                        (int)(rect.y + rect.height) - 22, 15,
                        rect.width - 24.0f, warning);

    if (m->found_count == 0) {
        sdrgui_text_fit(m->scan_note, (int)rect.x + 12, y, 15,
                        rect.width - 24.0f, row_muted);
        if (!m->scanning && m->receiver_scan_possible) {
            /* Two short lines rather than one long one: the column is narrow
               and sdrgui_text_fit truncates rather than wrapping. */
            sdrgui_text_fit("The first pass tries", (int)rect.x + 12, y + 20,
                            15, rect.width - 24.0f, row_muted);
            sdrgui_text_fit("every whole megahertz,", (int)rect.x + 12, y + 38,
                            15, rect.width - 24.0f, row_muted);
            sdrgui_text_fit("where carriers sit.", (int)rect.x + 12, y + 56,
                            15, rect.width - 24.0f, row_muted);
        }
        return;
    }

    for (i = 0; i < m->found_count; i++) {
        const struct lte_found_cell *found = &m->found[i];
        int row_y = y + i * LTE_FOUND_ROW_HEIGHT;
        Color colour = row_value;
        if ((float)(row_y + LTE_FOUND_ROW_HEIGHT) > rect.y + rect.height - 26.0f)
            break;
        if (i == selected) {
            DrawRectangle((int)rect.x + 4, row_y - 3, (int)rect.width - 8,
                          LTE_FOUND_ROW_HEIGHT, (Color){ 30, 48, 66, 255 });
            colour = row_pick;
        } else if (i == hovered) {
            DrawRectangle((int)rect.x + 4, row_y - 3, (int)rect.width - 8,
                          LTE_FOUND_ROW_HEIGHT, (Color){ 24, 36, 50, 255 });
        }
        snprintf(text, sizeof(text), "%.1f  cell %-3d  %.2f / %.2f",
                 found->frequency_hz / 1e6, found->pci, (double)found->pss,
                 (double)found->sss_margin);
        sdrgui_text_fit(text, (int)rect.x + 12, row_y, 15, rect.width - 24.0f,
                        colour);
    }
}

/*
 * The screen's data, gathered once wherever it is needed.
 *
 * Everything this view *decides* -- the crystal error in ppm, the PHICH's
 * wording, which of four notes an empty scan table gets, what a scan would
 * cost, which band the *tuning* is in, whether the funnel is a fault, what
 * the findings say -- is `lte_view_model_build()`'s, so this drawing and the
 * browser's cannot come to different answers (`web-visualization/16`).
 */
static void lte_model_of(const struct app *app, double now,
                         struct lte_view_model *m) {
    const struct lte_band *picked = selected_band(app);
    const struct lte_band *tuned = lte_band_for_earfcn(app->lte.earfcn);
    struct lte_view_context ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.centre_hz = app->applied.frequency_hz;
    ctx.band_number = picked ? picked->band : 0;
    ctx.tuned_band = tuned ? tuned->band : 0;
    ctx.tuned_band_name = tuned ? tuned->name : NULL;
    if (picked) {
        ctx.scan_channels = lte_scan_count(picked);
        ctx.scan_first_pass_seconds = lte_scan_first_pass_seconds(picked);
        ctx.scan_all_seconds = lte_scan_seconds(picked);
        if (app->lte.scan.running)
            lte_earfcn_downlink_hz(lte_scan_candidate(picked,
                                                      app->lte.scan.candidate),
                                   &ctx.scan_candidate_hz);
    }
    ctx.on_grid = lte_on_grid(app);
    ctx.receiver_mode = app->receiver_mode;
    ctx.now = now;
    lte_view_model_build(&app->lte, &ctx, m);
}

static void draw_cell_panel(const struct lte_view_model *m, Rectangle rect) {
    const struct lte_cell_stats *st = &m->stats;
    struct lte_panel_rows rows = lte_panel_rows_for(rect);
    char text[160];
    int r = 0;

    draw_panel(rect, "Cell search -- what PSS and SSS found");

    if (!m->cell_valid) {
        sdrgui_text_fit(m->status[0] ? m->status : "Waiting for samples...",
                        (int)rows.row.label_x, (int)rows.row.first_y,
                        LTE_PANEL_ROW_FONT, rect.width - 24.0f,
                        m->on_grid ? row_muted : warning);
        return;
    }

    /*
     * Ordered so that what a short window keeps is what identifies the cell.
     * The rows below the identity are measurements of how well it was read,
     * and a panel with room for six should spend them on which cell this is
     * rather than on how clean its channel was.
     */
    snprintf(text, sizeof(text), "%d", m->pci);
    draw_row_at(&rows, r++, "Cell identity", text, row_value);
    snprintf(text, sizeof(text), "%d and %d", m->n_id_1, m->n_id_2);
    draw_row_at(&rows, r++, "N_ID_1 / N_ID_2", text, row_value);
    draw_row_at(&rows, r++, "Cyclic prefix",
                m->extended_cp ? "extended" : "normal", row_value);
    snprintf(text, sizeof(text), "sample %d, %s half",
             m->subframe_sample, m->second_half ? "second" : "first");
    draw_row_at(&rows, r++, "Subframe 0 at", text, row_value);
    /* The offset in parts per million is the figure that transfers: it is a
       property of the receiver's crystal rather than of this carrier, so it
       can be compared with what the GSM calibration measured. */
    /*
     * Parts per million only: the hertz are in the table below, and printing
     * both put the same measurement on screen twice.
     *
     * The ppm is the one that belongs up here with the facts rather than down
     * there with the readings -- it is a property of the receiver's crystal
     * and not of this carrier, so it compares with what the GSM calibration
     * measured on a different band.
     */
    snprintf(text, sizeof(text), "%+.1f ppm  (%+d sc)", m->crystal_ppm,
             m->crystal_subcarriers);
    draw_row_at(&rows, r++, "Crystal error", text, row_value);
    /*
     * From here down the panel is a table: smallest, mean and largest since
     * this cell was found, because every one of these moves. A single block's
     * correlation drops when somebody walks past the antenna and its
     * reference power follows the fading, so one number cannot tell a
     * marginal cell from a steady one -- which is the distinction a reader
     * actually wants.
     */
    if (r < rows.row.capacity) {
        /* Each heading over the column it names. Written as one string in
           the value column first, which lined up only by luck and only at
           one window width. */
        static const char *heading[3] = { "min", "mean", "max" };
        int hy = (int)(rows.row.first_y + (float)r * rows.row.step), c;
        for (c = 0; c < 3; c++)
            sdrgui_text_fit(heading[c], (int)rows.stat_x[c], hy,
                            LTE_PANEL_ROW_FONT, rows.stat_width, row_label);
    }
    r++;
    draw_stat_row(&rows, r++, "Freq offset kHz", &st->frequency_khz, "%+.1f");
    draw_stat_row(&rows, r++, "PSS correlation", &st->pss, "%.2f");
    draw_stat_row(&rows, r++, "SSS correlation", &st->sss, "%.2f");
    draw_stat_row(&rows, r++, "RSRP dBFS", &st->rsrp_dbfs, "%.1f");
    draw_stat_row(&rows, r++, "RSRQ dB", &st->rsrq_db, "%.1f");
    draw_stat_row(&rows, r++, "RS-SINR dB", &st->sinr_db, "%.1f");
    draw_stat_row(&rows, r++, "Delay ns", &st->delay_ns, "%+.0f");
    draw_stat_row(&rows, r++, "Spread ns", &st->spread_ns, "%.0f");
    draw_stat_row(&rows, r++, "Drift Hz", &st->drift_hz, "%+.0f");
    draw_stat_row(&rows, r++, "Antenna ports", &st->ports, "%.0f");

    snprintf(text, sizeof(text), "%lu blocks, last seen %.1f s ago",
             st->rsrp_dbfs.count, m->cell_age_seconds);
    sdrgui_text_fit(text, (int)rows.row.label_x,
                    (int)panel_footer_after(&rows.row, r), 14,
                    rect.width - 24.0f, row_muted);

}

static void draw_mib_panel(const struct lte_view_model *m, Rectangle rect) {
    struct lte_panel_rows rows = lte_panel_rows_for(rect);
    char text[200];
    int y = draw_panel(rect, "Broadcast -- what the cell says about itself");

    if (!m->mib_valid) {
        const char *note =
            m->cell_valid
                ? "A cell is there; its broadcast has not survived its "
                  "parity yet."
                : "Nothing to read until a cell is found.";
        sdrgui_text_fit(note, (int)rows.row.label_x, (int)rows.row.first_y, 15,
                        rect.width - 24.0f, row_muted);
        y = (int)rows.row.first_y + 28;
    } else {
        int r = 0;
        snprintf(text, sizeof(text), "%d blocks, %.2f MHz", m->bandwidth_rb,
                 m->bandwidth_mhz);
        draw_row_at(&rows, r++, "Bandwidth", text, row_value);
        /* One wording, the model's -- and guarded there against the NULL
           `lte_phich_resource_name()` returns outside the four values its
           two-bit field can encode, which this passed straight to `%s`. */
        draw_row_at(&rows, r++, "PHICH", m->phich, row_value);
        snprintf(text, sizeof(text), "%d  (quarter %d)", m->frame_number,
                 m->quarter);
        draw_row_at(&rows, r++, "Frame number", text, row_value);
        snprintf(text, sizeof(text), "%d", m->antenna_ports);
        draw_row_at(&rows, r++, "Antenna ports", text, row_value);
        snprintf(text, sizeof(text), "last read %.1f s ago",
                 m->mib_age_seconds);
        sdrgui_text_fit(text, (int)rows.row.label_x,
                        (int)panel_footer_after(&rows.row, r), 14,
                        rect.width - 24.0f, row_muted);
        y = (int)panel_footer_after(&rows.row, r) + 24;
    }

    /*
     * Where this stops, and why. Everything above the Master Information
     * Block is scheduled across the cell's whole bandwidth; a receiver
     * sampling 1.08 MHz of a 9 MHz carrier is not missing a feature, it is
     * missing the samples.
     */
    sdrgui_text_fit("SIB1 and above ride the full bandwidth. At 1.92 MS/s "
                    "only the central 1.08 MHz is sampled --",
                    (int)rect.x + 12, y, 14, rect.width - 24.0f, row_muted);
    sdrgui_text_fit("which is where the standard puts everything a handset "
                    "needs before it knows the bandwidth.",
                    (int)rect.x + 12, y + 17, 14, rect.width - 24.0f,
                    row_muted);

    /*
     * And what the measurements next door amount to.
     *
     * Here rather than under the numbers they summarise, because the cell
     * panel is thirteen rows and a footer and had room for two of these --
     * and the two it dropped were the refusals, which are the half a reader
     * most needs. This is the prose column and it has the space.
     *
     * Measured before drawing: sdrgui_text_block does not clip, so a finding
     * that would not fit whole is not started. A conclusion cut off
     * mid-sentence is worse than one absent, because a reader cannot tell
     * which half is missing.
     */
    {
        float top = y + 44.0f;
        float bottom = rect.y + rect.height - 8.0f;
        int i;

        /* The sentences are the model's, chosen once by
           `lte_findings_from()` -- this used to call it again with its own
           idea of the carrier frequency, which is two callers of one
           decision and one of them reading `struct app`. */
        if (!m->findings.count)
            return;
        sdrgui_text_fit("What that adds up to", (int)rect.x + 12, (int)top, 15,
                        rect.width - 24.0f, panel_caption);
        top += 22.0f;
        for (i = 0; i < m->findings.count; i++) {
            Rectangle box = { rect.x + 12.0f, top, rect.width - 24.0f,
                              bottom - top };
            float used = sdrgui_text_block(box, m->findings.line[i], 14, 2,
                                           row_muted, 0);
            if (top + used > bottom)
                break;
            sdrgui_text_block(box, m->findings.line[i], 14, 2, row_muted, 1);
            top += used + 8.0f;
        }
    }
}

/*
 * Analysis mode: the three measurements the numbers are a summary of.
 *
 * Each answers a question the panels cannot. The correlation profile says
 * whether the primary sequence was a sharp lock or a broad hump -- a broad one
 * is a reflection, or nothing at all. The candidate scores say by how much the
 * winning identity beat the other hundred and sixty-seven, which is the gate
 * the whole cell search turns on and the only number that separates a cell
 * from noise. And the channel says why the broadcast did not decode: a notch
 * across the middle subcarriers is a reason, and it is invisible everywhere
 * else on the screen.
 */
static void draw_charts(const struct app *app, const struct lte_layout *l) {
    const struct lte_trace *trace = &app->lte.trace;
    char candidates[160];

    {
        struct sdrgui_burst_chart_params params = {
            l->chart[0], trace->profile, trace->profile_count,
            SDRGUI_BURST_LINE, 0.0f, 1.0f,
            "PSS correlation, 96 samples either side of the peak",
            "no cell found in this block"
        };
        sdrgui_burst_chart(&params);
    }
    if (trace->candidate_count)
        snprintf(candidates, sizeof(candidates),
                 "SSS candidates: N_ID_1 %d of 168 won",
                 trace->candidate_best);
    else
        snprintf(candidates, sizeof(candidates), "SSS candidates");
    {
        struct sdrgui_burst_chart_params params = {
            l->chart[1], trace->candidate, trace->candidate_count,
            SDRGUI_BURST_LINE, 0.0f, 1.0f, candidates,
            "no cell found in this block"
        };
        sdrgui_burst_chart(&params);
    }
    {
        struct sdrgui_burst_chart_params params = {
            l->chart[2], trace->channel_db, trace->channel_count,
            SDRGUI_BURST_LINE, -25.0f, 15.0f,
            "Channel across the broadcast's 72 subcarriers, dB",
            "no reference signals read"
        };
        sdrgui_burst_chart(&params);
    }
    {
        /*
         * The fourth chart, and the one the other three cannot replace: the
         * channel chart beside it is a magnitude, and a magnitude cannot see
         * this. Reference symbols have unit magnitude, so a port that is
         * silent and a port that is transmitting look identical in level and
         * differ only in phase.
         *
         * Four bars against a marked chance line therefore say how many
         * antennas the cell has, before any message decodes -- which is the
         * measurement that identified the band 8 cell as four-port after
         * every other explanation had been eliminated. It also corroborates
         * the antenna-port count the broadcast's parity mask gives, sharing
         * no code with it.
         */
        struct sdrgui_burst_chart_params params = {
            l->chart[3],
            app->lte.session.port_coherence_valid ? app->lte.session.port_coherence : NULL,
            app->lte.session.port_coherence_valid ? LTE_PORT_COUNT : 0,
            /*
             * The axis floor is the chance level rather than zero, so the
             * scale itself is the reference: eleven phase differences per
             * port put an untransmitted port at about 0.30, and starting
             * there means a silent port draws no bar at all while a
             * transmitting one draws a tall one. A caption cannot promise a
             * line this component has no way to draw, and a bar chart from
             * zero with nothing marked on it leaves the reader unable to say
             * whether 0.45 is a port or noise.
             */
            SDRGUI_BURST_BAR, LTE_PORT_COHERENCE_CHANCE, 1.0f,
            "Antenna ports: coherence above chance (0.30)",
            "No cell yet"
        };
        sdrgui_burst_chart(&params);
    }

    {
        struct sdrgui_constellation_params params = {
            l->constellation, trace->element_i, trace->element_q,
            trace->element_bit, trace->element_count,
            "Broadcast elements",
            "no broadcast channel read"
        };
        sdrgui_constellation(&params);
    }
}

void draw_lte(struct app *app) {
    struct lte_layout l = lte_layout_now(app);
    double now = GetTime();
    struct lte_view_model m;
    uint64_t record_bytes = 0;
    char record_path[ACQUISITION_PATH_MAX];
    int recording = acquisition_recording_status(&app->acq, &record_bytes,
                                                 record_path,
                                                 sizeof(record_path));
    char text[400];
    int header_x = (int)l.header_left;
    int i;

    lte_model_of(app, now, &m);

    draw_button(l.record_button, recording ? "Recording..." : "Record 2s",
                recording);
    draw_button(l.view_toggle,
                app->lte.analysis_mode ? "Show signal" : "Show charts", 0);

    {
        int bands[LTE_BANDS_MAX];
        int count = view_lte_bands(app, bands);

        for (i = 0; i < count; i++) {
            char label[24];
            snprintf(label, sizeof(label), "Band %d", bands[i]);
            draw_button(l.band_button[i], label, i == app->lte.scan.band);
        }
    }
    draw_button(l.scan_button, app->lte.scan.running ? "Stop" : "Scan band",
                app->lte.scan.running);

    if (m.earfcn) {
        /*
         * The band the EARFCN is in, not the one the picker is showing.
         * These are different facts and the header printed the second under
         * a caption promising the first: `--earfcn 3475` tunes 927.5 MHz in
         * band 8 and the header read "band 20 (800 MHz)", because
         * `selected_band()` reports which button is lit and the buttons
         * default to band 20. Nothing on screen contradicted it -- the
         * frequency beside it was right. Both facts are the model's now, so
         * the browser cannot repeat the mistake.
         */
        snprintf(text, sizeof(text),
                 "LTE downlink   EARFCN %d   %.3f MHz   band %d (%s)",
                 m.earfcn, m.centre_hz / 1e6, m.tuned_band,
                 m.tuned_band_name);
    }
    else
        snprintf(text, sizeof(text),
                 "LTE downlink   %.3f MHz   outside band %d -- pick a band, "
                 "or scan one", m.centre_hz / 1e6, m.band);
    sdrgui_text_fit(text, header_x, 88, 17, l.header_right - l.header_left,
                    panel_caption);

    /* The funnel. Two empty panels look the same whether nothing is
       transmitting or every message is failing, and this is the difference. */
    snprintf(text, sizeof(text),
             "funnel   blocks %llu -> cells %llu -> decoded %llu -> "
             "confirmed %llu%s",
             m.blocks_seen, m.cells_found, m.mibs_decoded, m.mibs_confirmed,
             m.on_grid ? "" : "   [wrong sample rate]");
    /* Amber when a cell is being found and none of its broadcasts is
       confirmed, which is the model's `funnel_warn`. */
    sdrgui_text_fit(text, header_x, 110, 16, l.header_right - l.header_left,
                    m.funnel_warn ? warning
                                  : (Color){ 151, 174, 188, 255 });

    /* Beside the scan button, what pressing it costs. Three hundred tunings
       is not a thing to start without being told. */
    if (m.scan_cost[0]) {
        float from = l.scan_button.x + l.scan_button.width + 14.0f;

        sdrgui_text_fit(m.scan_cost, (int)from, (int)l.scan_button.y + 6, 15,
                        (float)GetScreenWidth() - from - 22.0f, row_muted);
    }

    if (recording) {
        snprintf(text, sizeof(text), "Recording raw I/Q to %s  (%.1f MB)",
                 record_path, record_bytes / 1e6);
        sdrgui_text_fit(text, header_x, (int)l.waterfall.y - 18, 15,
                        l.header_right - l.header_left, warning);
    }

    if (app->lte.analysis_mode) {
        draw_charts(app, &l);
    } else {
        struct sdrgui_waterfall_marker lte_marker;
        int m_cnt = 0;
        /* A marker is a claim that something is there, so there is one
           exactly when the model has a label for it. */
        if (m.marker_label[0]) {
            lte_marker.frequency_hz = m.marker_hz;
            lte_marker.bandwidth_hz = 1400000.0; /* 6 PRB minimum */
            lte_marker.age_seconds = m.cell_age_seconds;
            lte_marker.duration_seconds = 0.010; /* 10 ms frame */
            lte_marker.id = 0;
            lte_marker.highlighted = 1;
            lte_marker.color = (Color){ 80, 220, 240, 220 };
            lte_marker.label = m.marker_label;
            m_cnt = 1;
        }
        draw_waterfall_rect_with_markers(app, 0, l.waterfall, &app->lte.window,
                                         m_cnt ? &lte_marker : NULL, m_cnt, NULL);
        draw_cell_panel(&m, l.cell_panel);
        draw_mib_panel(&m, l.mib_panel);
    }
    draw_found_panel(&m, m.scan_selected, found_rect(app, &l));
}

Rectangle lte_waterfall_rect(const struct app *app) {
    return lte_layout_now(app).waterfall;
}

void handle_lte_input(struct app *app) {
    struct lte_layout l = lte_layout_now(app);
    int i;

    if (IsKeyPressed(KEY_ESCAPE)) {
        set_tab(app, TAB_SCOPE, GetTime());
        return;
    }
    if (clicked(l.view_toggle)) {
        app->lte.analysis_mode = !app->lte.analysis_mode;
        return;
    }
    if (clicked(l.record_button)) {
        /* No channel offset: LTE's synchronisation signals sit on the carrier
           centre, so the recorded centre is the carrier. */
        start_capture_record(app, "lte", "lte", 0, 0.0,
                             ACQUISITION_RECORD_BUTTON_SECONDS);
        return;
    }
    for (i = 0; i < l.band_count; i++) {
        if (!clicked(l.band_button[i]))
            continue;
        /* Choosing a band abandons what the last one found: the list is this
           band's, and leaving a neighbour's cells in it would invite tuning
           to a frequency this scan never checked. */
        scan_stop(app);
        app->lte.scan.band = i;
        app->lte.scan.found_count = 0;
        app->lte.scan.selected = -1;
        app->lte.session.cell_valid = 0;
        app->lte.session.mib_valid = 0;
        park_in_band(app);
        return;
    }
    if (clicked(l.scan_button)) {
        if (app->lte.scan.running)
            scan_stop(app);
        else
            (void)scan_start(app, GetTime());
        return;
    }
    if (!app->lte.scan.running && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        int row = found_row_at(found_rect(app, &l), app->lte.scan.found_count,
                               GetMousePosition());
        if (row >= 0)
            scan_select(app, row);
    }
}
