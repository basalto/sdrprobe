#ifndef TETRA_VIEW_MODEL_H
#define TETRA_VIEW_MODEL_H

#include "tech/tetra_session.h"

/*
 * What the TETRA screen says, as plain data -- no raylib type anywhere
 * (`web-visualization/07`).
 *
 * The screen is an identity, a funnel and a log of the identities seen, and
 * the funnel's middle term is the one that earns it: `blocks_failed` is a
 * burst whose synchronisation word matched and whose **parity did not**, so
 * a carrier that is TETRA but too weak reads differently from one that is
 * not TETRA at all. Both look like an empty table.
 *
 * Takes the structs it reads rather than a `const struct app *`, so
 * `check-tetra-view-model` links `-lm` alone.
 */

/*
 * One identity, as the window's table draws a row of it.
 *
 * It was in `runtime/app.h` beside the view that keeps them. It is a
 * contract -- the browser renders these rows -- and a contract may not live
 * above the layer that reads it (ADR-0028), which is the same move
 * `site_seen.h` and `survey_tuning.h` already made.
 */
#define TETRA_LOG_CAPACITY 64

struct tetra_log_entry {
    double at;                  /* seconds since the run started */
    int mcc, mnc, colour, la;
    int bursts, blocks, broadcast;
};

/* The whole log travels. 64 entries of seven numbers is about 5 KB, which
   is nothing beside ADS-B's 48 message rows -- so there is no newest-N
   question here, and a reader gets the same scrollback the window has. */
#define TETRA_VIEW_MODEL_LOG TETRA_LOG_CAPACITY

struct tetra_view_model {
    /*
     * Whether this rate can decode TETRA at all.
     *
     * The channel filter needs a sample rate that is a whole multiple of
     * `TETRA_WORK_RATE_HZ`, and a run at the wrong one decodes nothing for
     * a reason that has nothing to do with what is on air. The window says
     * so rather than drawing an empty chart; this carries the fact so a
     * browser does not have to re-derive the arithmetic.
     */
    int rate_supported;

    /* How much of the carrier is TETRA-shaped: 0.80 on a real one here
       against under 0.035 for empty spectrum *and* for an FM station. */
    float lock;
    double offset_hz;

    /* The network, once a parity check has established it. Zeroed when it
       has not -- a reader must not be shown last minute's colour code under
       this minute's heading. */
    int have_identity;
    int mcc, mnc, colour, la;
    /*
     * And whether the location area has actually been read.
     *
     * It comes from the **broadcast** block, which is scrambled with the
     * network's own colour code and so cannot be read until the
     * synchronization block has given that up -- so there is a window in
     * which the colour code is known and the location area is not, and
     * `la` is 0 rather than unknown. The window asked
     * `broadcast_total > 0` in two places and worded the answer two
     * different ways ("LA un4375" in the header, "location area unread" in
     * the panel); the wire did not carry it at all.
     */
    int la_read;

    /* The funnel: this block, and the run. `blocks_failed` is the middle
       term -- synchronisation matched, parity did not. */
    int bursts, blocks, broadcast;
    unsigned long long bursts_total, blocks_total, broadcast_total;
    unsigned long long blocks_failed;

    /*
     * What the waterfall marker over this carrier says, and whether there is
     * one at all. Empty when nothing has decoded: a marker is a claim that
     * something is there, and `lock` alone is not one.
     */
    char marker_label[24];

    /* The identities seen, newest first. */
    int log_count;
    struct tetra_log_entry log[TETRA_VIEW_MODEL_LOG];
};

struct tetra_view;

/* Fills `out`. Reads plain fields only -- no I/O, no raylib call. */
void tetra_view_model_build(const struct tetra_view *tetra,
                            struct tetra_view_model *out);

#endif
