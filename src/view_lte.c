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
/* The reachable list lives with the band table now, so the calibration
   picker and this view cannot offer different bands. */

static struct lte_layout lte_layout_now(void) {
    return lte_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

int lte_on_grid(const struct app *app) {
    return app->applied_sample_rate == (uint32_t)LTE_SAMPLE_RATE_HZ;
}

/* lte_band_for_number() lives with the table now, so the calibration picker
   and this view cannot disagree about what a band number means. */

static const struct lte_band *selected_band(const struct app *app) {
    int index = app->lte.scan.band;
    if (index < 0 || index >= LTE_LAYOUT_BANDS)
        index = 1;
    return lte_band_for_number(lte_reachable_band(index));
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

/*
 * Put the receiver inside the band the buttons say.
 *
 * A tuning already in the band is left alone -- someone who arrived with
 * --earfcn meant that carrier, and moving off it would be rude. Otherwise the
 * band's first channel, which is where a scan starts too, so pressing Scan
 * costs one retune fewer.
 *
 * Without this the view opens wherever the program happened to be -- 1090 MHz
 * on a default start -- with band 20 highlighted, a header reading "not on the
 * 100 kHz raster", and a cell search grinding away against the ADS-B band. The
 * GSM view has never had that problem because entering it tunes to a channel
 * or starts a scan; this is the same idea with the scan left to the operator,
 * since an LTE scan is a hundred and thirty seconds rather than ten.
 */
static void park_in_band(struct app *app) {
    const struct lte_band *band = selected_band(app);
    uint32_t low = 0, high = 0, first = 0;

    if (!app->receiver_mode || !band)
        return;
    if (!lte_earfcn_downlink_hz(band->earfcn_low, &low) ||
        !lte_earfcn_downlink_hz(band->earfcn_high, &high))
        return;
    if (app->applied_frequency >= low && app->applied_frequency <= high)
        return;
    if (lte_earfcn_downlink_hz(lte_scan_candidate(band, 0), &first))
        retune_receiver(app, first, app->applied_ppm);
}

void enter_lte(struct app *app) {
    if (!app->receiver_mode)
        return;
    if (!app->lte.return_valid) {
        app->lte.return_frequency = app->applied_frequency;
        app->lte.return_sample_rate = app->applied_sample_rate;
        app->lte.return_valid = 1;
    }
    if (!lte_on_grid(app) &&
        retune_receiver_at_rate(app, app->applied_frequency,
                                (uint32_t)LTE_SAMPLE_RATE_HZ,
                                app->applied_ppm) < 0) {
        snprintf(app->lte.status, sizeof(app->lte.status),
                 "The receiver would not move to 1.92 MS/s: %.100s",
                 app->calibration_status);
        return;
    }
    park_in_band(app);
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
    scan->confirming = 0;
    scan->confirm_index = 0;
    scan->confirm_total = 0;
    scan->confirm_dropped = 0;
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
        if (lte_reachable_band(i) == band_number) {
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
    if (!lte_earfcn_downlink_hz(earfcn, &hz))
        return;

    /*
     * And one already found too close to this to be a different carrier.
     * Two real ones are never nearer than the narrowest bandwidth the
     * standard allows; anything closer is this carrier seen from beside
     * itself, under an identity the wrong subcarriers invented. Keep
     * whichever correlated better -- that is the one on the centre.
     */
    for (i = 0; i < scan->found_count; i++) {
        if (!lte_scan_same_carrier((double)hz,
                                   (double)scan->found[i].frequency_hz))
            continue;
        if (scan->found[i].pss >= cell->pss_correlation)
            return;
        scan->found_count = lte_scan_remove(scan->found, scan->found_count, i);
        if (scan->selected >= scan->found_count)
            scan->selected = -1;
        break;
    }
    if (scan->found_count >= LTE_SCAN_MAX_FOUND)
        return;
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

/*
 * The confirmation pass: revisit one listed cell and ask it again.
 *
 * The sweep has to be generous, because it gets one chance at each of three
 * hundred channels and an entry it never lists can never be recovered. That
 * generosity is what lets a repeatable artefact onto the list: clearing the
 * gate twice with the same identity is exactly what a weak cell does, and
 * repeatability is the one property an artefact shares with it.
 *
 * What separates them is how they behave when asked properly. By the end of
 * the sweep there are a handful of entries rather than three hundred
 * channels, so each can afford five more looks -- and an entry that cannot
 * produce the identity it was listed under twice in five loses its place.
 */
static void scan_confirm_step(struct app *app, double now, int have_block) {
    struct lte_band_scan *scan = &app->lte.scan;
    struct lte_cell cell;

    if (!scan->settled) {
        if (now - scan->step_started >= LTE_SCAN_SETTLE_SECONDS) {
            scan->settled = 1;
            scan->step_started = now;
        }
        return;
    }

    if (have_block &&
        app->pair_count >= LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE) {
        scan->looks++;
        if (lte_cell_search(app->i_samples, app->q_samples, app->pair_count,
                            (double)app->applied_sample_rate, &cell,
                            NULL) == 1 &&
            cell.pci == scan->found[scan->confirm_index].pci)
            scan->pending_hits++;
        /* Stop early once it has answered: the remaining looks would be spent
           on a question already settled, and this pass is worth having
           because it is cheap. */
        if (!lte_scan_confirmed(scan->pending_hits) &&
            scan->looks < LTE_SCAN_CONFIRM_LOOKS)
            return;
    } else if (now - scan->step_started <
               LTE_SCAN_CONFIRM_LOOKS * LTE_SCAN_PROBE_SECONDS + 1.0) {
        return;   /* still waiting for a block worth looking at */
    }

    if (lte_scan_confirmed(scan->pending_hits)) {
        scan->confirm_index++;
    } else {
        scan->found_count = lte_scan_remove(scan->found, scan->found_count,
                                            scan->confirm_index);
        scan->confirm_dropped++;
        if (scan->selected == scan->confirm_index)
            scan->selected = -1;
        else if (scan->selected > scan->confirm_index)
            scan->selected--;
        /* Whatever followed has moved into this slot, so the index stays
           where it is. */
    }

    if (scan->confirm_index >= scan->found_count) {
        scan->confirming = 0;
        scan->running = 0;
        if (scan->found_count > 0)
            scan_select(app, 0);
        return;
    }
    if (scan_tune(app, scan->found[scan->confirm_index].earfcn, now) < 0) {
        scan->confirming = 0;
        scan->running = 0;
    }
}

void update_lte_scan(struct app *app, double now, int have_block) {
    struct lte_band_scan *scan = &app->lte.scan;
    const struct lte_band *band = selected_band(app);
    struct lte_cell cell;
    unsigned int earfcn;

    if (!scan->running || !band)
        return;

    if (scan->confirming) {
        scan_confirm_step(app, now, have_block);
        return;
    }

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
        /* The band is walked. Now ask the few it listed to say it again --
           four seconds against the two minutes already spent, and the only
           part of the scan that can take an entry away. */
        if (scan->found_count > 0) {
            scan->confirming = 1;
            scan->confirm_index = 0;
            scan->confirm_total = scan->found_count;
            scan->confirm_dropped = 0;
            if (scan_tune(app, scan->found[0].earfcn, now) < 0) {
                scan->confirming = 0;
                scan->running = 0;
            }
            return;
        }
        scan->running = 0;
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
        /* The search fills in what it measured even when it refuses, and the
           two cases are worth telling apart: an empty channel, or a carrier
           whose primary sequence locked and whose secondary one did not. The
           second is where this decoder currently stands on live air. */
        if (cell.pss_correlation > 0.5f)
            snprintf(app->lte.status, sizeof(app->lte.status),
                     "A primary sequence at %.2f, but the secondary one only "
                     "reached %.2f against %.2f -- no identity.",
                     (double)cell.pss_correlation,
                     (double)cell.sss_correlation,
                     (double)cell.sss_runner_up);
        else
            snprintf(app->lte.status, sizeof(app->lte.status),
                     "No synchronisation signal here. Scan the band to find "
                     "one.");
        return;
    }

    if (app->lte.cell_valid && cell.pci != app->lte.cell.pci)
        app->lte.pending_mib_hits = 0;
    app->lte.cell = cell;
    app->lte.cell_valid = 1;
    app->lte.cell_time = now;
    app->lte.cells_found++;
    /* Measured from the same block the cell was found in, so the level on
       screen always belongs to the identity beside it. */
    app->lte.power_valid = lte_reference_power(app->i_samples, app->q_samples,
                                               app->pair_count, rate, &cell,
                                               &app->lte.power);
    app->lte.port_coherence_valid =
        lte_port_coherence(app->i_samples, app->q_samples, app->pair_count,
                           rate, &cell, app->lte.port_coherence);
    app->lte.shape_valid = lte_channel_shape(app->i_samples, app->q_samples,
                                             app->pair_count, rate, &cell,
                                             &app->lte.shape);
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
         * The mask is not required to agree with the combining, and requiring
         * it was a mistake that threw away every real message this decoder
         * produced. The combining is only a way of getting soft bits good
         * enough to decode; which one manages that is a property of the
         * signal, not of the cell. On the band 20 captures the message comes
         * out under the single-port combining and its mask says two ports --
         * consistently, in every block, with a frame number advancing at
         * exactly the right rate. What guards against a lucky parity is the
         * repeat below, which is a far better test than agreement with a
         * hypothesis the receiver chose itself.
         */
        app->lte.mib_parity_passes++;

        /*
         * And now the part the parity cannot do on its own. Thirty-six
         * attempts a block means a pass by chance is not rare but expected,
         * so a message counts only when a second one agrees about the things
         * a cell does not change: its bandwidth, its acknowledgement channel
         * and its antenna count. The frame number is left out of that
         * comparison because it advances, which is what it is for.
         */
        if (lte_mib_same_cell(&app->lte.pending_mib, &mib) &&
            app->lte.pending_mib_hits > 0) {
            app->lte.pending_mib_hits++;
        } else {
            app->lte.pending_mib = mib;
            app->lte.pending_mib_hits = 1;
        }
        if (app->lte.pending_mib_hits < 2) {
            snprintf(app->lte.status, sizeof(app->lte.status),
                     "A broadcast passed its parity; waiting for a second "
                     "that agrees with it.");
            return;
        }

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

    if (index < 0 || index >= rows->capacity)
        return;
    y = (int)(rows->first_y + (float)index * rows->step);
    sdrgui_text_fit(label, (int)rows->label_x, y, LTE_PANEL_ROW_FONT,
                    rows->label_width, row_label);
    sdrgui_text_fit(value, (int)rows->value_x, y, LTE_PANEL_ROW_FONT,
                    rows->value_width, colour);
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
    int y = draw_panel(rect, "Scan -- MHz, cell, PSS/margin");
    int hovered = found_row_at(rect, scan->found_count, GetMousePosition());
    char text[160];
    int i;

    if (scan->running && scan->confirming) {
        /* The sweep's own progress line would sit at 100% and stop moving,
           which reads as a scan that has hung rather than one spending its
           most useful four seconds. */
        snprintf(text, sizeof(text), "confirming %d of %d   %d dropped",
                 scan->confirm_index + scan->confirm_dropped + 1,
                 scan->confirm_total, scan->confirm_dropped);
        sdrgui_text_fit(text, (int)rect.x + 12,
                        (int)(rect.y + rect.height) - 22, 15,
                        rect.width - 24.0f, warning);
    } else if (scan->running && band) {
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
        else if (scan->confirm_dropped > 0)
            /* Otherwise this is indistinguishable from a band nobody has
               looked at, when in fact it was looked at and everything it
               offered was withdrawn. */
            note = "Nothing held up: every candidate failed its second look.";
        else if (!app->receiver_mode)
            note = "A scan needs a live receiver; a capture holds one tuning.";
        else
            note = "Press Scan band.";
        sdrgui_text_fit(note, (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        row_muted);
        if (!scan->running && app->receiver_mode) {
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
    const struct lte_reference_power *power = &app->lte.power;
    const struct lte_channel_shape *shape = &app->lte.shape;
    struct lte_panel_rows rows = lte_panel_rows_for(rect);
    char text[160];
    int r = 0;

    draw_panel(rect, "Cell search -- what PSS and SSS found");

    if (!app->lte.cell_valid) {
        sdrgui_text_fit(app->lte.status[0] ? app->lte.status
                                           : "Waiting for samples...",
                        (int)rows.label_x, (int)rows.first_y,
                        LTE_PANEL_ROW_FONT, rect.width - 24.0f,
                        lte_on_grid(app) ? row_muted : warning);
        return;
    }

    /*
     * Ordered so that what a short window keeps is what identifies the cell.
     * The rows below the identity are measurements of how well it was read,
     * and a panel with room for six should spend them on which cell this is
     * rather than on how clean its channel was.
     */
    snprintf(text, sizeof(text), "%d", cell->pci);
    draw_row_at(&rows, r++, "Cell identity", text, row_value);
    snprintf(text, sizeof(text), "%d and %d", cell->n_id_1, cell->n_id_2);
    draw_row_at(&rows, r++, "N_ID_1 / N_ID_2", text, row_value);
    draw_row_at(&rows, r++, "Cyclic prefix",
                cell->extended_cp ? "extended" : "normal", row_value);
    snprintf(text, sizeof(text), "sample %zu, %s half",
             cell->subframe0_start, cell->half_frame ? "second" : "first");
    draw_row_at(&rows, r++, "Subframe 0 at", text, row_value);
    /* The offset in parts per million is the figure that transfers: it is a
       property of the receiver's crystal rather than of this carrier, so it
       can be compared with what the GSM calibration measured. */
    /* Hertz and parts per million; the whole-subcarrier part is in the
       headless report, and putting three numbers here truncated all of them.
       The ppm is the figure that transfers -- it is a property of the
       receiver's crystal rather than of this carrier, so it compares with
       what the GSM calibration measured. */
    snprintf(text, sizeof(text), "%+.1f kHz  %+.1f ppm",
             cell->frequency_offset_hz / 1e3,
             app->applied_frequency > 0
                 ? cell->frequency_offset_hz * 1e6 /
                       (double)app->applied_frequency
                 : 0.0);
    draw_row_at(&rows, r++, "Freq offset", text, row_value);
    snprintf(text, sizeof(text), "%.2f  (%.2f)",
             (double)cell->pss_correlation, (double)cell->pss_runner_up);
    draw_row_at(&rows, r++, "PSS correlation", text, row_value);
    snprintf(text, sizeof(text), "%.2f  (%.2f)",
             (double)cell->sss_correlation, (double)cell->sss_runner_up);
    draw_row_at(&rows, r++, "SSS correlation", text, row_value);

    /* dBFS and not dBm: nothing here knows the antenna's gain. RSRQ and
       RS-SINR are ratios through the same chain and carry no calibration,
       which is why they are worth reading beside a level that does. */
    if (app->lte.power_valid)
        snprintf(text, sizeof(text), "%.1f dBFS", (double)power->rsrp_dbfs);
    else
        snprintf(text, sizeof(text), "not measured");
    draw_row_at(&rows, r++, "RSRP", text,
                app->lte.power_valid ? row_value : row_muted);
    if (app->lte.power_valid)
        snprintf(text, sizeof(text), "%.1f dB", (double)power->rsrq_db);
    else
        snprintf(text, sizeof(text), "not measured");
    draw_row_at(&rows, r++, "RSRQ", text,
                app->lte.power_valid ? row_value : row_muted);
    if (app->lte.power_valid)
        snprintf(text, sizeof(text), "%.1f dB", (double)power->sinr_db);
    else
        snprintf(text, sizeof(text), "not measured");
    draw_row_at(&rows, r++, "RS-SINR", text,
                app->lte.power_valid ? row_value : row_muted);

    /* Delay and its spread on one row: they are the same measurement read two
       ways, and the units are shared. */
    if (app->lte.shape_valid)
        snprintf(text, sizeof(text), "%+.0f / %.0f ns", (double)shape->delay_ns,
                 (double)shape->delay_spread_ns);
    else
        snprintf(text, sizeof(text), "not measured");
    draw_row_at(&rows, r++, "Delay / spread", text,
                app->lte.shape_valid ? row_value : row_muted);
    /*
     * What is left after the search's own frequency correction, which on a
     * static receiver checks that correction rather than measuring motion --
     * a Doppler and a residual tuning error are the same phase.
     */
    if (app->lte.shape_valid)
        snprintf(text, sizeof(text), "%+.0f Hz", (double)shape->drift_hz);
    else
        snprintf(text, sizeof(text), "not measured");
    draw_row_at(&rows, r++, "Residual drift", text,
                app->lte.shape_valid ? row_value : row_muted);
    /*
     * How many antennas are transmitting, read from the reference phases
     * alone. It agrees with the antenna-port count in the broadcast panel
     * without sharing any code with it, so the two disagreeing is worth
     * noticing -- and this one is available before any message decodes.
     */
    if (app->lte.port_coherence_valid) {
        int ports = 0, p;
        for (p = 0; p < LTE_PORT_COUNT; p++)
            if (app->lte.port_coherence[p] >= LTE_PORT_COHERENCE_PRESENT)
                ports++;
        snprintf(text, sizeof(text), "%d transmitting", ports);
    } else {
        snprintf(text, sizeof(text), "not measured");
    }
    draw_row_at(&rows, r++, "Antenna ports", text,
                app->lte.port_coherence_valid ? row_value : row_muted);

    snprintf(text, sizeof(text), "last seen %.1f s ago",
             now - app->lte.cell_time);
    sdrgui_text_fit(text, (int)rows.label_x,
                    (int)lte_panel_footer_after(&rows, r), 14,
                    rect.width - 24.0f, row_muted);
}

static void draw_mib_panel(const struct app *app, Rectangle rect, double now) {
    const struct lte_mib *mib = &app->lte.mib;
    struct lte_panel_rows rows = lte_panel_rows_for(rect);
    char text[200];
    int y = draw_panel(rect, "Broadcast -- what the cell says about itself");

    if (!app->lte.mib_valid) {
        const char *note =
            app->lte.cell_valid
                ? "A cell is there; its broadcast has not survived its "
                  "parity yet."
                : "Nothing to read until a cell is found.";
        sdrgui_text_fit(note, (int)rows.label_x, (int)rows.first_y, 15,
                        rect.width - 24.0f, row_muted);
        y = (int)rows.first_y + 28;
    } else {
        int r = 0;
        snprintf(text, sizeof(text), "%d blocks, %.2f MHz",
                 mib->bandwidth_prb,
                 lte_mib_occupied_hz(mib->bandwidth_prb) / 1e6);
        draw_row_at(&rows, r++, "Bandwidth", text, row_value);
        snprintf(text, sizeof(text), "%s, %s",
                 mib->phich_extended ? "extended" : "normal",
                 lte_phich_resource_name(mib->phich_resource_sixths));
        draw_row_at(&rows, r++, "PHICH", text, row_value);
        snprintf(text, sizeof(text), "%d  (quarter %d)",
                 mib->system_frame_number, mib->quarter);
        draw_row_at(&rows, r++, "Frame number", text, row_value);
        snprintf(text, sizeof(text), "%d", mib->antenna_ports);
        draw_row_at(&rows, r++, "Antenna ports", text, row_value);
        snprintf(text, sizeof(text), "last read %.1f s ago",
                 now - app->lte.mib_time);
        sdrgui_text_fit(text, (int)rows.label_x,
                        (int)lte_panel_footer_after(&rows, r), 14,
                        rect.width - 24.0f, row_muted);
        y = (int)lte_panel_footer_after(&rows, r) + 24;
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
            app->lte.port_coherence_valid ? app->lte.port_coherence : NULL,
            app->lte.port_coherence_valid ? LTE_PORT_COUNT : 0,
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
        snprintf(label, sizeof(label), "Band %d", lte_reachable_band(i));
        draw_button(l.band_button[i], label, i == app->lte.scan.band);
    }
    draw_button(l.scan_button, app->lte.scan.running ? "Stop" : "Scan band",
                app->lte.scan.running);

    if (app->lte.earfcn) {
        /*
         * The band an EARFCN is in, not the one the picker is showing. These
         * are different facts and the header was printing the second under a
         * caption promising the first: `--earfcn 3475` tunes 927.5 MHz in
         * band 8 and the header read "band 20 (800 MHz)", because
         * selected_band() reports which button is lit and the buttons default
         * to band 20. Nothing on screen contradicted it -- the frequency
         * beside it was right.
         */
        const struct lte_band *tuned = lte_band_for_earfcn(app->lte.earfcn);
        snprintf(text, sizeof(text),
                 "LTE downlink   EARFCN %d   %.3f MHz   band %d (%s)",
                 app->lte.earfcn, app->applied_frequency / 1e6,
                 tuned ? tuned->band : 0, tuned ? tuned->name : "unknown");
    }
    else
        snprintf(text, sizeof(text),
                 "LTE downlink   %.3f MHz   outside band %d -- pick a band, "
                 "or scan one", app->applied_frequency / 1e6,
                 band ? band->band : 0);
    sdrgui_text_fit(text, header_x, 88, 17, l.header_right - l.header_left,
                    panel_caption);

    /* The funnel. Two empty panels look the same whether nothing is
       transmitting or every message is failing, and this is the difference. */
    snprintf(text, sizeof(text),
             "funnel   blocks %llu -> cells %llu -> parity %llu -> "
             "messages %llu%s",
             (unsigned long long)app->lte.blocks_seen,
             (unsigned long long)app->lte.cells_found,
             (unsigned long long)app->lte.mib_parity_passes,
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
        draw_waterfall_rect(app, 0, l.waterfall, &app->lte.window);
        draw_cell_panel(app, l.cell_panel, now);
        draw_mib_panel(app, l.mib_panel, now);
    }
    draw_found_panel(app, found_rect(app, &l));
}

Rectangle lte_waterfall_rect(void) {
    return lte_layout_now().waterfall;
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
        app->lte.cell_valid = 0;
        app->lte.mib_valid = 0;
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
