/*
 * LTE's runtime: entering and leaving the view at 1.92 MS/s, the band
 * scan and its confirmation pass, and one block of cell search and broadcast
 * decode.
 *
 * Out of `view_lte.c` by `.scratch/layer-boundaries/issues/02-*`. The
 * drawing and the input stayed behind. No raylib here.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "debug_log.h"
#include "runtime.h"

int lte_on_grid(const struct app *app) {
    return app->applied.sample_rate_hz == (uint32_t)LTE_SAMPLE_RATE_HZ;
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
const struct lte_band *selected_band(const struct app *app) {
    int bands[LTE_BANDS_MAX];
    int count = view_lte_bands(app, bands);
    int index = app->lte.scan.band;

    /* A receiver that reaches no band -- a capture -- selects none, and the
       caller gets NULL rather than whatever the first row used to be. */
    if (count <= 0)
        return NULL;
    if (index < 0 || index >= count)
        index = 0;
    return lte_band_for_number(bands[index]);
}

void park_in_band(struct app *app) {
    const struct lte_band *band = selected_band(app);
    uint32_t low = 0, high = 0, first = 0;

    if (!app->receiver_mode || !band)
        return;
    if (!lte_earfcn_downlink_hz(band->earfcn_low, &low) ||
        !lte_earfcn_downlink_hz(band->earfcn_high, &high))
        return;
    if (app->applied.frequency_hz >= low && app->applied.frequency_hz <= high)
        return;
    if (lte_earfcn_downlink_hz(lte_scan_candidate(band, 0), &first))
        retune_receiver(app, first, app->applied.ppm);
}

void enter_lte(struct app *app) {
    if (!app->receiver_mode)
        return;
    if (receiver_borrow(app, &app->lte.lease_token) < 0)
        return;
    /*
     * The cell search refuses any rate but 1.92 MS/s (ADR-0014), so this view
     * borrows the rate as well as the tuning. A receiver that will not move
     * cancels the claim -- retune_receiver_at_rate() has already put both
     * halves back, so there is nothing left to return.
     */
    if (!lte_on_grid(app) &&
        retune_receiver_at_rate(app, app->applied.frequency_hz,
                                (uint32_t)LTE_SAMPLE_RATE_HZ,
                                app->applied.ppm) < 0) {
        snprintf(app->lte.session.status, sizeof(app->lte.session.status),
                 "The receiver would not move to 1.92 MS/s: %.100s",
                 app->receiver_error);
        receiver_lease_cancel(&app->lease, &app->lte.lease_token);
        return;
    }
    park_in_band(app);
}

void leave_lte(struct app *app) {
    app->lte.scan.running = 0;
    /* Both halves of the snapshot: the frequency the operator was on and the
       rate they were sampling at, neither of which this view chose. */
    receiver_return(app, &app->lte.lease_token);
}

/* Tune to one channel of the scan and start its clock. */
static int scan_tune(struct app *app, unsigned int earfcn, double now) {
    uint32_t hz = 0;
    if (!lte_earfcn_downlink_hz(earfcn, &hz))
        return -1;
    if (retune_receiver(app, hz, app->applied.ppm) < 0)
        return -1;
    app->lte.scan.step_started = now;
    app->lte.scan.settled = 0;
    app->lte.scan.looks = 0;
    app->lte.scan.pending_pci = -1;
    app->lte.scan.pending_hits = 0;
    return 0;
}

void scan_stop(struct app *app) {
    app->lte.scan.running = 0;
}

int scan_start(struct app *app, double now) {
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
    app->lte.session.cell_valid = 0;
    app->lte.session.mib_valid = 0;
    if (scan_tune(app, lte_scan_candidate(band, 0), now) < 0) {
        scan->running = 0;
        return -1;
    }
    return 0;
}

int lte_scan_begin(struct app *app, int band_number, double now) {
    int i;
    {
    int bands[LTE_BANDS_MAX];
    int count = view_lte_bands(app, bands);
    for (i = 0; i < count; i++)
        if (bands[i] == band_number) {
            const struct lte_band *band = lte_band_for_number(band_number);
            app->lte.scan.band = i;
            debug_log_write("lte-scan", "begin band %d, %d channels, ~%.0f s",
                            band_number, lte_scan_count(band),
                            lte_scan_seconds(band));
            return scan_start(app, now);
        }
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

void scan_select(struct app *app, int row) {
    struct lte_band_scan *scan = &app->lte.scan;
    if (row < 0 || row >= scan->found_count || !app->receiver_mode)
        return;
    scan->selected = row;
    app->lte.session.cell_valid = 0;
    app->lte.session.mib_valid = 0;
    app->lte.announced_pci = -1;
    retune_receiver(app, scan->found[row].frequency_hz, app->applied.ppm);
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
        app->frame.pair_count >= LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE) {
        scan->looks++;
        if (lte_cell_search(app->frame.i_samples, app->frame.q_samples, app->frame.pair_count,
                            (double)app->applied.sample_rate_hz, &cell,
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
        /* `dropped` is the part worth logging: the sweep's gate is
           deliberately loose, so what the confirmation pass took away is the
           measure of how much of the list was never real. */
        debug_log_write("lte-scan", "done, %d cells, %d dropped",
                        scan->found_count, scan->confirm_dropped);
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
        app->frame.pair_count >= LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE) {
        scan->looks++;
        if (lte_cell_search(app->frame.i_samples, app->frame.q_samples, app->frame.pair_count,
                            (double)app->applied.sample_rate_hz, &cell,
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
        debug_log_write("lte-scan", "done, no cells");
        scan->running = 0;
        return;
    }
    if (scan_tune(app, lte_scan_candidate(band, scan->candidate), now) < 0)
        scan->running = 0;
}

/*
 * Feed the latest block to the decode.
 *
 * Everything the decode does is `lte_session.h`'s -- the cell search, the
 * reference power, the port coherence, the channel shape, the statistics and
 * the rule that a broadcast counts only when a second one agrees. This is the
 * adapter, and `print_lte()` in sdrprobe.c is the other.
 */
void update_lte(struct app *app, double now) {
    struct lte_session_event event;
    /* Collecting the trace costs a second pass over the correlation and a
       copy of the candidate scores, so it is only done when something is
       drawing them. */
    struct lte_trace *trace = app->lte.analysis_mode ? &app->lte.trace : NULL;

    app->lte.earfcn = lte_earfcn_for_hz((double)app->applied.frequency_hz);
    lte_session_feed(&app->lte.session, app->frame.i_samples, app->frame.q_samples,
                     app->frame.pair_count, (double)app->applied.sample_rate_hz,
                     app->device.full_scale, now, trace, &event);

    if (event.cell_found) {
        if (app->lte.session.mib_valid) {
            debug_log_write("lte", "pci %d sfn %d ports %d offset %+.1f kHz",
                            app->lte.session.cell.pci,
                            app->lte.session.mib.system_frame_number,
                            app->lte.session.mib.antenna_ports,
                            app->lte.session.cell.frequency_offset_hz / 1e3);
        } else {
            debug_log_write("lte", "pci %d offset %+.1f kHz",
                            app->lte.session.cell.pci,
                            app->lte.session.cell.frequency_offset_hz / 1e3);
        }
    }
}
