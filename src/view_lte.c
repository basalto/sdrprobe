#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "view.h"
#include "lte_layout.h"
#include "sdrgui.h"

/*
 * The Decode tab's LTE screen: which cell is on this carrier, and what it
 * broadcasts about itself.
 *
 * Two panels, and both are needed to read either. The left one is what the
 * synchronisation signals found -- an identity, a frame boundary, a frequency
 * error -- and it fills as soon as a cell is there at all. The right one is
 * what the broadcast channel said, and it stays empty until a message has
 * survived its parity. A cell identity with nothing beside it is the honest
 * picture of a carrier that is present but too weak to read, which one panel
 * alone could not tell from a carrier that is not there.
 *
 * The screen only works on LTE's own sample grid. It says so rather than
 * showing nothing (ADR-0014).
 */

/* The three antenna-port arrangements a cell can use. Nothing before the
   message's own parity says which, so all three are tried. */
static const int port_hypotheses[3] = { 1, 2, 4 };

static struct lte_layout lte_layout_now(void) {
    return lte_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

int lte_on_grid(const struct app *app) {
    return app->applied_sample_rate == (uint32_t)LTE_SAMPLE_RATE_HZ;
}


/* ------------------------------------------------------------------ */
/* What one sample block yields.                                       */
/* ------------------------------------------------------------------ */

void update_lte(struct app *app, double now) {
    struct lte_cell cell;
    double rate = (double)app->applied_sample_rate;
    int h;

    app->lte.blocks_seen++;
    app->lte.earfcn = lte_earfcn_for_hz((double)app->applied_frequency);

    if (!lte_on_grid(app)) {
        snprintf(app->lte.status, sizeof(app->lte.status),
                 "Receiver is at %.3f MS/s; LTE's grid is 1.920. "
                 "Restart with --earfcn, or --sample-rate 1.92M.",
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
                        &cell) != 1) {
        snprintf(app->lte.status, sizeof(app->lte.status),
                 "No synchronisation signal in this block.");
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
                               port_hypotheses[h],
                               soft) != LTE_PBCH_SOFT_BITS)
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

/* A panel and its caption; returns the y the first row goes on. */
static int draw_panel(Rectangle rect, const char *caption) {
    DrawRectangleRec(rect, (Color){ 17, 26, 37, 255 });
    DrawRectangleLinesEx(rect, 1.0f, panel_edge);
    DrawText(caption, (int)rect.x + 12, (int)rect.y + 10, 17, panel_caption);
    return (int)rect.y + 38;
}

static void draw_row(Rectangle rect, int y, const char *label,
                     const char *value, Color colour) {
    int label_x = (int)rect.x + 12;
    int value_x = label_x + 186;
    float room = rect.x + rect.width - 12.0f - (float)value_x;
    DrawText(label, label_x, y, 16, row_label);
    sdrgui_text_fit(value, value_x, y, 16, room, colour);
}

static void draw_cell_panel(const struct app *app, Rectangle rect,
                            double now) {
    const struct lte_cell *cell = &app->lte.cell;
    char text[160];
    int y = draw_panel(rect, "Cell search -- what PSS and SSS found");

    if (!app->lte.cell_valid) {
        sdrgui_text_fit(app->lte.status[0] ? app->lte.status
                                           : "Waiting for samples...",
                        (int)rect.x + 12, y, 16, rect.width - 24.0f,
                        lte_on_grid(app) ? row_muted : warning);
        return;
    }

    snprintf(text, sizeof(text), "%d", cell->pci);
    draw_row(rect, y, "Physical cell identity", text, row_value);
    y += 22;
    snprintf(text, sizeof(text), "%d and %d", cell->n_id_1, cell->n_id_2);
    draw_row(rect, y, "N_ID_1 / N_ID_2", text, row_value);
    y += 22;
    draw_row(rect, y, "Cyclic prefix",
             cell->extended_cp ? "extended" : "normal", row_value);
    y += 22;
    snprintf(text, sizeof(text), "sample %zu, from the %s half-frame",
             cell->subframe0_start, cell->half_frame ? "second" : "first");
    draw_row(rect, y, "Subframe 0 begins", text, row_value);
    y += 22;
    /* The offset in parts per million is the figure that transfers: it is a
       property of the receiver's crystal rather than of this carrier, so it
       can be compared with what the GSM calibration measured. */
    snprintf(text, sizeof(text), "%+.0f Hz  (%+.2f ppm at %.1f MHz)",
             cell->frequency_offset_hz,
             app->applied_frequency > 0
                 ? cell->frequency_offset_hz * 1e6 /
                       (double)app->applied_frequency
                 : 0.0,
             app->applied_frequency / 1e6);
    draw_row(rect, y, "Frequency offset", text, row_value);
    y += 22;
    snprintf(text, sizeof(text), "%.2f  (other roots reached %.2f)",
             (double)cell->pss_correlation, (double)cell->pss_runner_up);
    draw_row(rect, y, "PSS correlation", text, row_value);
    y += 22;
    snprintf(text, sizeof(text), "%.2f  (runner-up %.2f)",
             (double)cell->sss_correlation, (double)cell->sss_runner_up);
    draw_row(rect, y, "SSS correlation", text, row_value);
    y += 26;
    snprintf(text, sizeof(text), "last seen %.1f s ago",
             now - app->lte.cell_time);
    sdrgui_text_fit(text, (int)rect.x + 12, y, 15, rect.width - 24.0f,
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
        sdrgui_text_fit(note, (int)rect.x + 12, y, 16, rect.width - 24.0f,
                        row_muted);
        y += 30;
    } else {
        snprintf(text, sizeof(text), "%d blocks, %.2f MHz occupied",
                 mib->bandwidth_prb,
                 lte_mib_occupied_hz(mib->bandwidth_prb) / 1e6);
        draw_row(rect, y, "Downlink bandwidth", text, row_value);
        y += 22;
        snprintf(text, sizeof(text), "%s duration, %s",
                 mib->phich_extended ? "extended" : "normal",
                 lte_phich_resource_name(mib->phich_resource_sixths));
        draw_row(rect, y, "PHICH", text, row_value);
        y += 22;
        snprintf(text, sizeof(text), "%d  (quarter %d of the 40 ms period)",
                 mib->system_frame_number, mib->quarter);
        draw_row(rect, y, "System frame number", text, row_value);
        y += 22;
        snprintf(text, sizeof(text), "%d", mib->antenna_ports);
        draw_row(rect, y, "Antenna ports", text, row_value);
        y += 26;
        snprintf(text, sizeof(text), "last read %.1f s ago",
                 now - app->lte.mib_time);
        sdrgui_text_fit(text, (int)rect.x + 12, y, 15, rect.width - 24.0f,
                        row_muted);
        y += 26;
    }

    /*
     * Where this stops, and why. Everything above the Master Information
     * Block is scheduled across the cell's whole bandwidth; a receiver
     * sampling 1.08 MHz of a 9 MHz carrier is not missing a feature, it is
     * missing the samples.
     */
    sdrgui_text_fit("SIB1 and above ride the full bandwidth. At 1.92 MS/s "
                    "only the central 1.08 MHz is sampled --",
                    (int)rect.x + 12, y, 15, rect.width - 24.0f, row_muted);
    sdrgui_text_fit("which is where the standard puts everything a handset "
                    "needs before it knows the bandwidth.",
                    (int)rect.x + 12, y + 18, 15, rect.width - 24.0f,
                    row_muted);
}

void draw_lte(struct app *app) {
    struct lte_layout l = lte_layout_now();
    double now = GetTime();
    uint64_t record_bytes = 0;
    char record_path[ACQUISITION_PATH_MAX];
    int recording = acquisition_recording_status(&app->acq, &record_bytes,
                                                 record_path,
                                                 sizeof(record_path));
    char text[400];
    int header_x = (int)l.header_left;

    draw_button(l.record_button, recording ? "Recording..." : "Record 2s",
                recording);

    if (app->lte.earfcn) {
        const struct lte_band *band =
            lte_band_for_earfcn((unsigned int)app->lte.earfcn);
        snprintf(text, sizeof(text),
                 "LTE downlink   EARFCN %d   %.3f MHz   band %d (%s)",
                 app->lte.earfcn, app->applied_frequency / 1e6,
                 band ? band->band : 0, band ? band->name : "unknown");
    } else {
        snprintf(text, sizeof(text),
                 "LTE downlink   %.3f MHz   not on the 100 kHz raster",
                 app->applied_frequency / 1e6);
    }
    sdrgui_text_fit(text, header_x, 88, 17, l.header_right - l.header_left,
                    panel_caption);

    /* The funnel. An empty pair of panels looks the same whether nothing is
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

    if (recording) {
        snprintf(text, sizeof(text), "Recording raw I/Q to %s  (%.1f MB)",
                 record_path, record_bytes / 1e6);
        sdrgui_text_fit(text, header_x, (int)l.waterfall.y - 18, 15,
                        l.header_right - l.header_left, warning);
    }

    draw_waterfall_rect(app, 0, l.waterfall, 0.0);
    draw_cell_panel(app, l.cell_panel, now);
    draw_mib_panel(app, l.mib_panel, now);
}

void handle_lte_input(struct app *app) {
    struct lte_layout l = lte_layout_now();

    if (IsKeyPressed(KEY_ESCAPE)) {
        set_tab(app, TAB_SCOPE);
        return;
    }
    if (clicked(l.record_button)) {
        /* No channel offset: LTE's synchronisation signals sit on the carrier
           centre, so the recorded centre is the carrier. */
        start_capture_record(app, "lte", "lte", 0, 0.0,
                             ACQUISITION_RECORD_BUTTON_SECONDS);
        return;
    }
}
