#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "view.h"
#include "lte_layout.h"
#include "sdrgui.h"

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
static const int port_hypotheses[3] = { 1, 2, 4 };

/* The bands a dongle can actually reach, in the order they are offered. The
   table in lte_dsp.c also holds 1, 3 and 7, which sit above an R820T's
   tuning range; offering them would be offering a button that cannot work. */
static const int selectable_bands[LTE_LAYOUT_BANDS] = { 28, 20, 8 };

static struct lte_layout lte_layout_now(void) {
    return lte_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

int lte_on_grid(const struct app *app) {
    return app->applied_sample_rate == (uint32_t)LTE_SAMPLE_RATE_HZ;
}

static const struct lte_band *band_numbered(int number) {
    int i;
    for (i = 0; i < lte_band_count(); i++)
        if (lte_band_at(i)->band == number)
            return lte_band_at(i);
    return NULL;
}

static const struct lte_band *selected_band(const struct app *app) {
    int index = app->lte.scan.band;
    if (index < 0 || index >= LTE_LAYOUT_BANDS)
        index = 1;
    return band_numbered(selectable_bands[index]);
}

void view_lte_defaults(struct app *app) {
    memset(&app->lte, 0, sizeof(app->lte));
    /* Band 20 is the widest coverage layer here and usually the strongest
       indoors, so it is the one a reader most likely wants first. */
    app->lte.scan.band = 1;
    app->lte.scan.selected = -1;
    /* Cell 0 is a real identity, so "nothing announced yet" cannot be zero. */
    app->lte.announced_pci = -1;
}


/* ------------------------------------------------------------------ */
/* Borrowing the receiver, and giving it back.                         */
/* ------------------------------------------------------------------ */

void enter_lte(struct app *app) {
    if (!app->receiver_mode)
        return;
    if (!app->lte.return_valid) {
        app->lte.return_frequency = app->applied_frequency;
        app->lte.return_sample_rate = app->applied_sample_rate;
        app->lte.return_valid = 1;
    }
    if (lte_on_grid(app))
        return;
    if (retune_receiver_at_rate(app, app->applied_frequency,
                                (uint32_t)LTE_SAMPLE_RATE_HZ,
                                app->applied_ppm) < 0)
        snprintf(app->lte.status, sizeof(app->lte.status),
                 "The receiver would not move to 1.92 MS/s: %.100s",
                 app->calibration_status);
}

void leave_lte(struct app *app) {
    app->lte.scan.running = 0;
    if (!app->receiver_mode || !app->lte.return_valid) {
        app->lte.return_valid = 0;
        return;
    }
    retune_receiver_at_rate(app, app->lte.return_frequency,
                            app->lte.return_sample_rate, app->applied_ppm);
    app->lte.return_valid = 0;
}


/* ------------------------------------------------------------------ */
/* The band scan.                                                      */
/* ------------------------------------------------------------------ */

/* Tune to one channel of the scan and start its clock. */
static int scan_tune(struct app *app, unsigned int earfcn, double now) {
    uint32_t hz = 0;
    if (!lte_earfcn_downlink_hz(earfcn, &hz))
        return -1;
    if (retune_receiver(app, hz, app->applied_ppm) < 0)
        return -1;
    app->lte.scan.step_started = now;
    app->lte.scan.settled = 0;
    app->lte.scan.looks = 0;
    app->lte.scan.pending_pci = -1;
    app->lte.scan.pending_hits = 0;
    return 0;
}

static void scan_stop(struct app *app) {
    app->lte.scan.running = 0;
}

static int scan_start(struct app *app, double now) {
    struct lte_band_scan *scan = &app->lte.scan;
    const struct lte_band *band = selected_band(app);

    if (!app->receiver_mode || !band)
        return -1;
    scan->total = lte_scan_count(band);
    scan->candidate = 0;
    scan->found_count = 0;
    scan->selected = -1;
    scan->running = 1;
    app->lte.cell_valid = 0;
    app->lte.mib_valid = 0;
    if (scan_tune(app, lte_scan_candidate(band, 0), now) < 0) {
        scan->running = 0;
        return -1;
    }
    return 0;
}

int lte_scan_begin(struct app *app, int band_number, double now) {
    int i;
    for (i = 0; i < LTE_LAYOUT_BANDS; i++)
        if (selectable_bands[i] == band_number) {
            app->lte.scan.band = i;
            return scan_start(app, now);
        }
    return -1;
}

int lte_scan_running(const struct app *app) {
    return app->lte.scan.running;
}

/*
 * A cell the scan has not seen before goes in the list.
 *
 * Keyed on the identity rather than the channel, because a strong cell is
 * found from more than one tuning: the correlation survives a kilohertz or
 * two of error, and neighbouring raster points are 100 kHz apart, so the
 * shoulders of a strong carrier answer as well as its centre. The first
 * sighting is the one nearest the true centre, since the scan walks the
 * channels in order within each pass.
 */
static void scan_record(struct app *app, unsigned int earfcn,
                        const struct lte_cell *cell) {
    struct lte_band_scan *scan = &app->lte.scan;
    uint32_t hz = 0;
    int i;

    for (i = 0; i < scan->found_count; i++)
        if (scan->found[i].pci == cell->pci)
            return;
    if (scan->found_count >= LTE_SCAN_MAX_FOUND)
        return;
    lte_earfcn_downlink_hz(earfcn, &hz);
    scan->found[scan->found_count].earfcn = earfcn;
    scan->found[scan->found_count].frequency_hz = hz;
    scan->found[scan->found_count].pci = cell->pci;
    scan->found[scan->found_count].pss = cell->pss_correlation;
    scan->found[scan->found_count].sss_margin =
        cell->sss_correlation - cell->sss_runner_up;
    scan->found_count++;

    /*
     * Strongest first. The list is read top down and the top of it is where a
     * reader will click, so it should be the cell most likely to reward that
     * -- and the weak end of the list is where a confirmed identity is still
     * worth doubting, which an ordering by confidence says without a caption.
     */
    for (i = scan->found_count - 1; i > 0; i--) {
        struct lte_found_cell swap;
        if (scan->found[i - 1].pss >= scan->found[i].pss)
            break;
        swap = scan->found[i - 1];
        scan->found[i - 1] = scan->found[i];
        scan->found[i] = swap;
    }
}

/* Park on a cell: tune to it and let the ordinary per-block decode take over
   from there. */
static void scan_select(struct app *app, int row) {
    struct lte_band_scan *scan = &app->lte.scan;
    if (row < 0 || row >= scan->found_count || !app->receiver_mode)
        return;
    scan->selected = row;
    app->lte.cell_valid = 0;
    app->lte.mib_valid = 0;
    app->lte.announced_pci = -1;
    retune_receiver(app, scan->found[row].frequency_hz, app->applied_ppm);
}

void update_lte_scan(struct app *app, double now, int have_block) {
    struct lte_band_scan *scan = &app->lte.scan;
    const struct lte_band *band = selected_band(app);
    struct lte_cell cell;
    unsigned int earfcn;

    if (!scan->running || !band)
        return;

    if (!scan->settled) {
        /* Samples arriving now were taken while the tuner was still moving,
           or before it moved at all: the pipeline holds the previous
           channel's. */
        if (now - scan->step_started >= LTE_SCAN_SETTLE_SECONDS) {
            scan->settled = 1;
            scan->step_started = now;
        }
        return;
    }

    earfcn = lte_scan_candidate(band, scan->candidate);
    if (have_block &&
        app->pair_count >= LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE) {
        scan->looks++;
        if (lte_cell_search(app->i_samples, app->q_samples, app->pair_count,
                            (double)app->applied_sample_rate, &cell,
                            NULL) == 1) {
            if (cell.pci == scan->pending_pci) {
                scan->pending_hits++;
            } else {
                scan->pending_pci = cell.pci;
                scan->pending_hits = 1;
            }
            scan->pending_cell = cell;
        }
        if (scan->pending_hits >= LTE_SCAN_CONFIRMATIONS) {
            scan_record(app, earfcn, &scan->pending_cell);
        } else if (scan->looks < LTE_SCAN_MIN_LOOKS ||
                   (scan->pending_hits > 0 &&
                    scan->looks < LTE_SCAN_MAX_LOOKS)) {
            /* Either this channel has not had its minimum look yet, or it has
               said something once and is worth asking again. */
            return;
        }
    } else if (now - scan->step_started <
               LTE_SCAN_MAX_LOOKS * LTE_SCAN_PROBE_SECONDS + 1.0) {
        return;   /* still waiting for a block worth looking at */
    }

    scan->candidate++;
    if (scan->candidate >= scan->total) {
        scan->running = 0;
        /* Park on the first cell found, so a finished scan leaves the view
           looking at something rather than at wherever it stopped. */
        if (scan->found_count > 0)
            scan_select(app, 0);
        return;
    }
    if (scan_tune(app, lte_scan_candidate(band, scan->candidate), now) < 0)
        scan->running = 0;
}


/* ------------------------------------------------------------------ */
/* What one sample block yields.                                       */
/* ------------------------------------------------------------------ */

void update_lte(struct app *app, double now) {
    struct lte_cell cell;
    double rate = (double)app->applied_sample_rate;
    /* Collecting the trace costs a second pass over the correlation and a
       copy of the candidate scores, so it is only done when something is
       drawing them. */
    struct lte_trace *trace = app->lte.analysis_mode ? &app->lte.trace : NULL;
    int h;

    app->lte.blocks_seen++;
    app->lte.earfcn = lte_earfcn_for_hz((double)app->applied_frequency);

    if (!lte_on_grid(app)) {
        snprintf(app->lte.status, sizeof(app->lte.status),
                 "Receiver is at %.3f MS/s; LTE's grid is 1.920.",
                 app->applied_sample_rate / 1e6);
        return;
    }
    /* The search needs a whole half-frame plus a symbol to be sure of holding
       one synchronisation signal, and a whole subframe after it. */
    if (app->pair_count < LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE) {
        snprintf(app->lte.status, sizeof(app->lte.status),
                 "Block holds %zu samples; a cell search needs %d.",
                 app->pair_count, LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE);
        return;
    }

    if (lte_cell_search(app->i_samples, app->q_samples, app->pair_count, rate,
                        &cell, trace) != 1) {
        snprintf(app->lte.status, sizeof(app->lte.status),
                 "No synchronisation signal here. Scan the band to find one.");
        return;
    }

    app->lte.cell = cell;
    app->lte.cell_valid = 1;
    app->lte.cell_time = now;
    app->lte.cells_found++;
    snprintf(app->lte.status, sizeof(app->lte.status),
             "Cell %d found; its broadcast has not decoded yet.", cell.pci);

    for (h = 0; h < 3; h++) {
        float soft[LTE_PBCH_SOFT_BITS];
        struct lte_mib mib;

        if (lte_pbch_soft_bits(app->i_samples, app->q_samples, app->pair_count,
                               rate, &cell, cell.subframe0_start,
                               port_hypotheses[h], soft,
                               trace) != LTE_PBCH_SOFT_BITS)
            continue;
        if (!lte_mib_decode(soft, cell.pci, &mib))
            continue;
        /*
         * The parity mask names the antenna-port count, and these soft bits
         * were combined assuming one. If the two disagree the parity passed by
         * chance rather than because the block decoded, so it is thrown away.
         * Sixteen bits of parity make that rare; taking the message anyway
         * would make it invisible.
         */
        if (mib.antenna_ports != port_hypotheses[h])
            continue;

        app->lte.mib = mib;
        app->lte.mib_valid = 1;
        app->lte.mib_time = now;
        app->lte.mib_ports_used = port_hypotheses[h];
        app->lte.mibs_decoded++;
        app->lte.status[0] = '\0';
        return;
    }
}


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

/* A panel and its caption; returns the y the first row goes on. */
static int draw_panel(Rectangle rect, const char *caption) {
    DrawRectangleRec(rect, (Color){ 17, 26, 37, 255 });
    DrawRectangleLinesEx(rect, 1.0f, panel_edge);
    DrawText(caption, (int)rect.x + 12, (int)rect.y + 10, 16, panel_caption);
    return (int)rect.y + 36;
}

static void draw_row(Rectangle rect, int y, const char *label,
                     const char *value, Color colour) {
    int label_x = (int)rect.x + 12;
    int value_x = label_x + 170;
    float room = rect.x + rect.width - 12.0f - (float)value_x;
    DrawText(label, label_x, y, 15, row_label);
    sdrgui_text_fit(value, value_x, y, 15, room, colour);
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

static void draw_found_panel(const struct app *app, Rectangle rect) {
    const struct lte_band_scan *scan = &app->lte.scan;
    const struct lte_band *band = selected_band(app);
    int y = draw_panel(rect, "Scan -- MHz, cell, PSS / SSS margin");
    int hovered = found_row_at(rect, scan->found_count, GetMousePosition());
    char text[160];
    int i;

    if (scan->running && band) {
        double done = scan->total > 0
                          ? 100.0 * scan->candidate / scan->total : 0.0;
        uint32_t hz = 0;
        lte_earfcn_downlink_hz(lte_scan_candidate(band, scan->candidate), &hz);
        snprintf(text, sizeof(text), "%.1f MHz   %d of %d   %.0f%%",
                 hz / 1e6, scan->candidate + 1, scan->total, done);
        sdrgui_text_fit(text, (int)rect.x + 12,
                        (int)(rect.y + rect.height) - 22, 15,
                        rect.width - 24.0f, warning);
    }

    if (scan->found_count == 0) {
        const char *note;
        if (scan->running)
            note = "Looking...";
        else if (!app->receiver_mode)
            note = "A scan needs a live receiver; a capture holds one tuning.";
        else
            note = "Press Scan band. The first pass tries every whole "
                   "megahertz, which is where carriers usually sit.";
        sdrgui_text_fit(note, (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        row_muted);
        return;
    }

    for (i = 0; i < scan->found_count; i++) {
        const struct lte_found_cell *found = &scan->found[i];
        int row_y = y + i * LTE_FOUND_ROW_HEIGHT;
        Color colour = row_value;
        if ((float)(row_y + LTE_FOUND_ROW_HEIGHT) > rect.y + rect.height - 26.0f)
            break;
        if (i == scan->selected) {
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

static void draw_cell_panel(const struct app *app, Rectangle rect,
                            double now) {
    const struct lte_cell *cell = &app->lte.cell;
    char text[160];
    int y = draw_panel(rect, "Cell search -- what PSS and SSS found");

    if (!app->lte.cell_valid) {
        sdrgui_text_fit(app->lte.status[0] ? app->lte.status
                                           : "Waiting for samples...",
                        (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        lte_on_grid(app) ? row_muted : warning);
        return;
    }

    snprintf(text, sizeof(text), "%d", cell->pci);
    draw_row(rect, y, "Physical cell identity", text, row_value);
    y += 21;
    snprintf(text, sizeof(text), "%d and %d", cell->n_id_1, cell->n_id_2);
    draw_row(rect, y, "N_ID_1 / N_ID_2", text, row_value);
    y += 21;
    draw_row(rect, y, "Cyclic prefix",
             cell->extended_cp ? "extended" : "normal", row_value);
    y += 21;
    snprintf(text, sizeof(text), "sample %zu, %s half-frame",
             cell->subframe0_start, cell->half_frame ? "second" : "first");
    draw_row(rect, y, "Subframe 0 begins", text, row_value);
    y += 21;
    /* The offset in parts per million is the figure that transfers: it is a
       property of the receiver's crystal rather than of this carrier, so it
       can be compared with what the GSM calibration measured. */
    snprintf(text, sizeof(text), "%+.0f Hz  (%+.2f ppm)",
             cell->frequency_offset_hz,
             app->applied_frequency > 0
                 ? cell->frequency_offset_hz * 1e6 /
                       (double)app->applied_frequency
                 : 0.0);
    draw_row(rect, y, "Frequency offset", text, row_value);
    y += 21;
    snprintf(text, sizeof(text), "%.2f  (others %.2f)",
             (double)cell->pss_correlation, (double)cell->pss_runner_up);
    draw_row(rect, y, "PSS correlation", text, row_value);
    y += 21;
    snprintf(text, sizeof(text), "%.2f  (runner-up %.2f)",
             (double)cell->sss_correlation, (double)cell->sss_runner_up);
    draw_row(rect, y, "SSS correlation", text, row_value);
    y += 25;
    snprintf(text, sizeof(text), "last seen %.1f s ago",
             now - app->lte.cell_time);
    sdrgui_text_fit(text, (int)rect.x + 12, y, 14, rect.width - 24.0f,
                    row_muted);
}

static void draw_mib_panel(const struct app *app, Rectangle rect, double now) {
    const struct lte_mib *mib = &app->lte.mib;
    char text[200];
    int y = draw_panel(rect, "Broadcast -- what the cell says about itself");

    if (!app->lte.mib_valid) {
        const char *note =
            app->lte.cell_valid
                ? "A cell is there; its broadcast has not survived its "
                  "parity yet."
                : "Nothing to read until a cell is found.";
        sdrgui_text_fit(note, (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        row_muted);
        y += 28;
    } else {
        snprintf(text, sizeof(text), "%d blocks, %.2f MHz",
                 mib->bandwidth_prb,
                 lte_mib_occupied_hz(mib->bandwidth_prb) / 1e6);
        draw_row(rect, y, "Downlink bandwidth", text, row_value);
        y += 21;
        snprintf(text, sizeof(text), "%s, %s",
                 mib->phich_extended ? "extended" : "normal",
                 lte_phich_resource_name(mib->phich_resource_sixths));
        draw_row(rect, y, "PHICH", text, row_value);
        y += 21;
        snprintf(text, sizeof(text), "%d  (quarter %d)",
                 mib->system_frame_number, mib->quarter);
        draw_row(rect, y, "System frame number", text, row_value);
        y += 21;
        snprintf(text, sizeof(text), "%d", mib->antenna_ports);
        draw_row(rect, y, "Antenna ports", text, row_value);
        y += 25;
        snprintf(text, sizeof(text), "last read %.1f s ago",
                 now - app->lte.mib_time);
        sdrgui_text_fit(text, (int)rect.x + 12, y, 14, rect.width - 24.0f,
                        row_muted);
        y += 24;
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
    struct lte_layout l = lte_layout_now();
    const struct lte_band *band = selected_band(app);
    double now = GetTime();
    uint64_t record_bytes = 0;
    char record_path[ACQUISITION_PATH_MAX];
    int recording = acquisition_recording_status(&app->acq, &record_bytes,
                                                 record_path,
                                                 sizeof(record_path));
    char text[400];
    int header_x = (int)l.header_left;
    int i;

    draw_button(l.record_button, recording ? "Recording..." : "Record 2s",
                recording);
    draw_button(l.view_toggle,
                app->lte.analysis_mode ? "View: Signal" : "View: Charts", 0);

    for (i = 0; i < LTE_LAYOUT_BANDS; i++) {
        char label[24];
        snprintf(label, sizeof(label), "Band %d", selectable_bands[i]);
        draw_button(l.band_button[i], label, i == app->lte.scan.band);
    }
    draw_button(l.scan_button, app->lte.scan.running ? "Stop" : "Scan band",
                app->lte.scan.running);

    if (app->lte.earfcn)
        snprintf(text, sizeof(text),
                 "LTE downlink   EARFCN %d   %.3f MHz   band %d (%s)",
                 app->lte.earfcn, app->applied_frequency / 1e6,
                 band ? band->band : 0, band ? band->name : "unknown");
    else
        snprintf(text, sizeof(text),
                 "LTE downlink   %.3f MHz   not on the 100 kHz raster",
                 app->applied_frequency / 1e6);
    sdrgui_text_fit(text, header_x, 88, 17, l.header_right - l.header_left,
                    panel_caption);

    /* The funnel. Two empty panels look the same whether nothing is
       transmitting or every message is failing, and this is the difference. */
    snprintf(text, sizeof(text),
             "funnel   blocks %llu -> cells %llu -> messages %llu%s",
             (unsigned long long)app->lte.blocks_seen,
             (unsigned long long)app->lte.cells_found,
             (unsigned long long)app->lte.mibs_decoded,
             lte_on_grid(app) ? "" : "   [wrong sample rate]");
    sdrgui_text_fit(text, header_x, 110, 16, l.header_right - l.header_left,
                    (app->lte.cells_found > 0 && app->lte.mibs_decoded == 0)
                        ? warning
                        : (Color){ 151, 174, 188, 255 });

    /* Beside the scan button, what pressing it costs. Three hundred tunings
       is not a thing to start without being told. */
    if (band && !app->lte.scan.running) {
        float from = l.scan_button.x + l.scan_button.width + 14.0f;
        snprintf(text, sizeof(text),
                 "%d channels; about %.0f s for the first pass, %.0f s for "
                 "all of them",
                 lte_scan_count(band), lte_scan_first_pass_seconds(band),
                 lte_scan_seconds(band));
        sdrgui_text_fit(text, (int)from, (int)l.scan_button.y + 6, 15,
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
        draw_waterfall_rect(app, 0, l.waterfall, 0.0);
        draw_cell_panel(app, l.cell_panel, now);
        draw_mib_panel(app, l.mib_panel, now);
    }
    draw_found_panel(app, found_rect(app, &l));
}

void handle_lte_input(struct app *app) {
    struct lte_layout l = lte_layout_now();
    int i;

    if (IsKeyPressed(KEY_ESCAPE)) {
        set_tab(app, TAB_SCOPE);
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
    for (i = 0; i < LTE_LAYOUT_BANDS; i++) {
        if (!clicked(l.band_button[i]))
            continue;
        /* Choosing a band abandons what the last one found: the list is this
           band's, and leaving a neighbour's cells in it would invite tuning
           to a frequency this scan never checked. */
        scan_stop(app);
        app->lte.scan.band = i;
        app->lte.scan.found_count = 0;
        app->lte.scan.selected = -1;
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
