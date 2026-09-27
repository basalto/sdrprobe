#ifndef LTE_VIEW_MODEL_H
#define LTE_VIEW_MODEL_H

#include <stdint.h>

#include "tech/lte_findings.h"
#include "tech/lte_mib.h"
#include "tech/lte_scan.h"
#include "tech/lte_stats.h"

/*
 * What the LTE screen says, as plain data -- no raylib type anywhere
 * (`web-visualization/07`). The last and the largest of the decode views.
 *
 * Three things on that screen are decisions rather than readings, and all
 * three travel as values:
 *
 *  - **The statistics are a table of what each measurement *did***, not of
 *    what it says this block: smallest, mean and largest since the identity
 *    last changed. One reading cannot tell a marginal cell from a steady
 *    one, and the reset on a change of identity is load-bearing -- a
 *    carrier here alternates between two cells block to block, and an
 *    average across both would sit under a heading naming one of them.
 *  - **`lte_findings` is prose with its numbers attached**, already chosen
 *    by `lte_findings_from()`. One of its lines is a *refusal* and that is
 *    the point: a Doppler and a residual tuning error are one phase, so the
 *    drift measures the crystal with any motion buried inside it.
 *  - **`status` is the session's own sentence** for why a block produced
 *    nothing -- the common answer, that the receiver is not on LTE's
 *    1.92 MS/s grid (ADR-0014), is one a reader acts on.
 *
 * Takes the structs it reads rather than a `const struct app *`, so
 * `check-lte-view-model` links `-lm` alone.
 */

/* The band scan's rows travel too, as GSM's channel scan does: a scan needs
   a live receiver, and a live receiver is exactly who watches from
   elsewhere. 24 rows of five numbers is nothing. */
#define LTE_VIEW_MODEL_FOUND LTE_SCAN_MAX_FOUND

/*
 * What the view cannot know about itself, gathered by the caller.
 *
 * `band_number` is here rather than read from `scan.band`, which is an
 * *index* into a table this module deliberately does not link, and
 * `scan_candidate_hz` for the same reason: turning an EARFCN into a
 * frequency lives in `lte_dsp.c`, and a view model that linked it would
 * bring the whole cell search with it.
 */
struct lte_view_context {
    uint32_t centre_hz;
    int band_number;            /* the picker's, 0 when none */
    uint32_t scan_candidate_hz; /* where the sweep is now, 0 when idle */
    int on_grid;                /* the receiver is at LTE's 1.92 MS/s */
    int receiver_mode;          /* a live receiver, not a capture */
    double now;                 /* the session clock, for the two ages */
};

struct lte_view_model {
    /* Where the receiver is, in the terms this screen uses. `earfcn` is 0
       when the tuning is not on the raster at all. */
    int earfcn;
    double centre_hz;
    /* The band the *picker* has selected, which is what the window's header
       names -- not the band the tuning falls in. 0 when none is selected,
       which is the ordinary case on a capture. */
    int band;
    /* Whether the receiver is on LTE's own 1.92 MS/s grid (ADR-0014). Off
       it, nothing could have decoded, and `status` says so -- which is a
       different answer from "nothing is transmitting". */
    int on_grid;

    /* The funnel: blocks -> cells -> decoded -> confirmed. `decoded` is a
       broadcast whose parity passed; `confirmed` is one that also agreed
       with the pass before it, which is what repetition cannot fake. */
    unsigned long long blocks_seen, cells_found;
    unsigned long long mibs_decoded, mibs_confirmed;
    /* Why the last block produced nothing, when it produced nothing. */
    char status[160];

    /* The cell, from the primary and secondary sequences. */
    int cell_valid;
    int pci, n_id_1, n_id_2;
    int extended_cp;
    int subframe_sample;
    int second_half;
    double crystal_ppm;
    int crystal_subcarriers;
    double cell_age_seconds;

    /* What each measurement has done since the identity last changed. */
    int stats_valid;
    struct lte_cell_stats stats;

    /* The broadcast: what the cell says about itself. */
    int mib_valid;
    int bandwidth_rb;
    double bandwidth_mhz;
    char phich[48];             /* "normal, one sixth" -- one wording */
    int frame_number;
    int quarter;
    int antenna_ports;
    double mib_age_seconds;

    /* And what all of it adds up to, already worded. */
    struct lte_findings findings;

    /* The band scan. `running` with `total` is its progress; the rows are
       what it has found so far. */
    int scanning;
    int confirming;
    int found_count;
    struct lte_found_cell found[LTE_VIEW_MODEL_FOUND];
    /* The two sentences the scan panel shows, chosen here: how far along the
       pass is, and -- when nothing has been found -- which of four reasons
       that is. "Nothing held up: every candidate failed its second look" and
       "A scan needs a live receiver" are different answers, and a reader who
       sees an empty table without them cannot tell them apart. */
    char scan_progress[96];
    char scan_note[128];
};

struct lte_view;

/* Fills `out`. Reads plain fields only -- no I/O, no raylib call. */
void lte_view_model_build(const struct lte_view *lte,
                          const struct lte_view_context *ctx,
                          struct lte_view_model *out);

#endif
