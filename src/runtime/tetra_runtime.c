/*
 * TETRA's runtime: one block of a 25 kHz carrier down to a network
 * identity, and the remembering of what it read.
 *
 * Out of `view_tetra.c` by `.scratch/layer-boundaries/issues/02-*`. The
 * drawing and the input stayed behind. No raylib here.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "runtime/app.h"
#include "runtime/debug_log.h"
#include "runtime/runtime.h"

static void remember(struct app *app, double now, int mcc, int mnc, int colour,
                     int la) {
    struct tetra_view *t = &app->tetra;
    int i;

    /* One entry per identity, not one per burst: the identity repeats seventy
       times a second and is the same every time. What is worth a row is a
       change -- a different cell, or the first sight of one. */
    if (t->log_count > 0 && t->log[0].colour == colour && t->log[0].la == la) {
        t->log[0].bursts = t->session.bursts;
        t->log[0].blocks = t->session.blocks;
        t->log[0].broadcast = t->session.broadcast;
        return;
    }
    for (i = TETRA_LOG_CAPACITY - 1; i > 0; i--)
        t->log[i] = t->log[i - 1];
    t->log[0].at = now;
    t->log[0].mcc = mcc;
    t->log[0].mnc = mnc;
    t->log[0].colour = colour;
    t->log[0].la = la;
    t->log[0].bursts = t->session.bursts;
    t->log[0].blocks = t->session.blocks;
    t->log[0].broadcast = t->session.broadcast;
    if (t->log_count < TETRA_LOG_CAPACITY)
        t->log_count++;
    debug_log_write("tetra", "mcc %d mnc %d colour %d la %d", mcc, mnc, colour, la);
}

/*
 * Feed the latest block to the decode, then take what the charts draw out of
 * it.
 *
 * Everything past `tetra_session_feed()` is drawing: the constellation is the
 * phase *step*, which is what carries the dibit -- four points at odd multiples
 * of pi/4, not the eight the raw symbols make. Drawing the raw symbols would
 * show a ring and say nothing.
 */
void update_tetra(struct app *app, double now) {
    struct tetra_view *t = &app->tetra;
    struct tetra_session_event event;
    int k;

    tetra_session_feed(&t->session, app->frame.i_samples, app->frame.q_samples,
                       app->frame.pair_count, (double)app->applied.sample_rate_hz,
                       &event);

    t->point_count = 0;
    t->profile_valid = 0;
    if (!t->session.symbols_valid)
        return;

    t->point_count = t->session.symbols.count < TETRA_MAX_SYMBOLS
                         ? t->session.symbols.count
                         : TETRA_MAX_SYMBOLS;
    for (k = 0; k < t->point_count; k++) {
        t->point_x[k] = (float)cos((double)t->session.symbols.step[k]);
        t->point_y[k] = (float)sin((double)t->session.symbols.step[k]);
        t->point_bit[k] = t->session.symbols.dibit[k];
    }
    if (t->session.sync_valid) {
        memcpy(t->profile, t->session.sync.profile, sizeof(t->profile));
        t->profile_fixed = t->session.sync.fixed;
        t->profile_valid = 1;
    }
    if (t->session.blocks > 0)
        remember(app, now, t->session.mcc, t->session.mnc, t->session.colour,
                 t->session.la);
}
