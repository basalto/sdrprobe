#ifndef LTE_SCAN_H
#define LTE_SCAN_H

#include "lte_dsp.h"

/*
 * The LTE band scan's arithmetic: which channels to try, and in what order.
 *
 * Plain integers, no receiver and no window (ADR-0012), checked by
 * tests/lte_scan_test.c.
 *
 * The order is the whole content of this file, and it exists because of a
 * constraint the GSM scan does not have. That scan measures ten channels from
 * one tuning, because its channels are 200 kHz apart and a 2 MHz window covers
 * ten of them at once. LTE cannot: the primary sequence is found by a
 * correlation in the time domain, which a frequency error smears -- past about
 * five kilohertz the correlation is gone -- so the receiver has to be tuned to
 * within a few kHz of the carrier's own centre. The raster is 100 kHz, so
 * covering a band means tuning to every one of its channels, three hundred of
 * them for band 20, at about a fifth of a second each.
 *
 * So the order is chosen to put the likely answers first. Operators are
 * allocated blocks from a band edge and centre their carriers in them, which
 * in practice lands them on whole megahertz: the three live band 20 carriers
 * here sit at 796, 806 and 816 MHz. A scan that tries every whole megahertz
 * first is done in thirty tunings rather than three hundred, and a reader
 * watching the list fill can stop it there. Left alone it still walks the
 * whole raster, so a carrier on an odd centre is found, just later.
 *
 * Every channel appears exactly once. That is the property worth checking:
 * an order that skipped one would leave a cell permanently invisible, and
 * nothing about the scan would say so.
 */

/* How long a tuning is given before its samples are believed. The receiver
   needs a moment to settle and the pipeline holds samples from the previous
   channel. */
#define LTE_SCAN_SETTLE_SECONDS 0.20
/* And how long one look takes: a sample block at 1.92 MS/s is 68 ms and holds
   seven frames, which is more than a cell search needs. */
#define LTE_SCAN_PROBE_SECONDS 0.12

/*
 * How many looks a channel gets, and how many must agree.
 *
 * A scan is three hundred independent chances to be wrong, and a gate set for
 * "is there a cell in front of me" is far too loose for that: the first live
 * band 20 scan reported five cells, and three of them were on frequencies no
 * carrier is centred on, with margins just over the threshold. Meanwhile a
 * genuine carrier at 806 MHz was missed, because it only yields a cell in
 * about half its blocks and the scan happened to look at the other half.
 *
 * Both are fixed by looking more than once and believing an identity only
 * when it repeats. Noise clearing the gate twice is not the problem -- noise
 * clearing it twice *with the same identity out of five hundred and four* is,
 * and that does not happen. A weak cell, on the other hand, says the same
 * thing every time it says anything.
 *
 * A channel with nothing on it costs the minimum; only a channel that has
 * already shown something is looked at again, so the extra looks are spent
 * where they decide something.
 */
#define LTE_SCAN_MIN_LOOKS 2
#define LTE_SCAN_MAX_LOOKS 5
#define LTE_SCAN_CONFIRMATIONS 2

/* The passes, in channels: a whole megahertz, then every half, then the rest
   of the 100 kHz raster. */
#define LTE_SCAN_COARSE_STEP 10
#define LTE_SCAN_MEDIUM_STEP 5

/* How many cells a scan will remember. More than any band holds in practice;
   the list is what the reader picks from, not a database. */
#define LTE_SCAN_MAX_FOUND 24

/* How many channels a band holds. */
static inline int lte_scan_count(const struct lte_band *band) {
    if (!band)
        return 0;
    return (int)(band->earfcn_high - band->earfcn_low) + 1;
}

/*
 * The `index`-th channel a scan of this band should try, as an EARFCN, or 0
 * when `index` is past the end.
 *
 * Pass 1 is every tenth channel from the band's first, pass 2 every tenth
 * offset by five, and pass 3 everything neither reached, in order. Between
 * them they name each channel once.
 */
static inline unsigned int lte_scan_candidate(const struct lte_band *band,
                                              int index) {
    int count, coarse, medium, offset;
    if (!band || index < 0)
        return 0;
    count = lte_scan_count(band);
    if (index >= count)
        return 0;

    /* Pass 1: 0, 10, 20, ... */
    coarse = (count + LTE_SCAN_COARSE_STEP - 1) / LTE_SCAN_COARSE_STEP;
    if (index < coarse)
        return band->earfcn_low +
               (unsigned int)(index * LTE_SCAN_COARSE_STEP);

    /* Pass 2: 5, 15, 25, ... */
    index -= coarse;
    medium = (count - LTE_SCAN_MEDIUM_STEP + LTE_SCAN_COARSE_STEP - 1) /
             LTE_SCAN_COARSE_STEP;
    if (medium < 0)
        medium = 0;
    if (index < medium)
        return band->earfcn_low +
               (unsigned int)(LTE_SCAN_MEDIUM_STEP +
                              index * LTE_SCAN_COARSE_STEP);

    /* Pass 3: everything whose offset from the band's first channel is not a
       multiple of five, in order. */
    index -= medium;
    for (offset = 0; offset < count; offset++) {
        if (offset % LTE_SCAN_MEDIUM_STEP == 0)
            continue;
        if (index == 0)
            return band->earfcn_low + (unsigned int)offset;
        index--;
    }
    return 0;
}

/* Roughly how long a whole scan of this band takes, in seconds, so the view
   can say so before anyone starts one. */
static inline double lte_scan_seconds(const struct lte_band *band) {
    return (double)lte_scan_count(band) *
           (LTE_SCAN_SETTLE_SECONDS +
            LTE_SCAN_MIN_LOOKS * LTE_SCAN_PROBE_SECONDS);
}

/* And how long the first pass takes, which is the number that matters: it is
   when the list stops filling for most bands. */
static inline double lte_scan_first_pass_seconds(const struct lte_band *band) {
    int coarse = (lte_scan_count(band) + LTE_SCAN_COARSE_STEP - 1) /
                 LTE_SCAN_COARSE_STEP;
    return (double)coarse * (LTE_SCAN_SETTLE_SECONDS +
                            LTE_SCAN_MIN_LOOKS * LTE_SCAN_PROBE_SECONDS);
}

#endif
