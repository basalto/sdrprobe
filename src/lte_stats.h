#ifndef LTE_STATS_H
#define LTE_STATS_H

/*
 * What a measurement did over a run, rather than what it says this block.
 *
 * Every figure the cell panel carries is one block's answer, and a single
 * block is a poor witness for anything that moves: a correlation drops when
 * somebody walks past the antenna, a reference power follows the fading, and
 * the frequency offset drifts with the crystal's temperature. A reader
 * watching the numbers flicker cannot tell a cell that is marginal from one
 * that is steady, and that is the difference worth seeing.
 *
 * Smallest, mean and largest, over the blocks since the cell last changed.
 *
 * **The reset is not a detail.** Statistics taken across two cells describe
 * neither, and a carrier here holds more than one -- EARFCN 3625 alternates
 * between PCI 402 and PCI 190 block to block. `lte_stats_for_cell()` clears
 * everything when the identity changes, so what is on screen always belongs
 * to the cell named above it.
 *
 * Pure arithmetic -- no window, no receiver, no samples (ADR-0012).
 */

struct lte_stat {
    float min;
    float max;
    double sum;      /* double, because a long run of floats loses the tail */
    unsigned long count;
};

static inline void lte_stat_reset(struct lte_stat *s) {
    if (!s)
        return;
    s->min = 0.0f;
    s->max = 0.0f;
    s->sum = 0.0;
    s->count = 0;
}

static inline void lte_stat_add(struct lte_stat *s, float value) {
    if (!s)
        return;
    /* The first sample sets both ends. Starting them at zero and comparing
       would make every all-negative measurement report a maximum of zero,
       which is what a reference power in dBFS is. */
    if (!s->count || value < s->min)
        s->min = value;
    if (!s->count || value > s->max)
        s->max = value;
    s->sum += (double)value;
    s->count++;
}

/* Zero when nothing has been added: a caller with no samples has nothing to
   draw, and `count` is what says so. */
static inline float lte_stat_mean(const struct lte_stat *s) {
    if (!s || !s->count)
        return 0.0f;
    return (float)(s->sum / (double)s->count);
}

/*
 * The measurements that move. Everything else the panel shows -- the
 * identity, the cyclic prefix, which half-frame -- is a fact about the cell
 * rather than a reading, and a mean of it would be nonsense.
 */
struct lte_cell_stats {
    struct lte_stat frequency_khz;
    struct lte_stat pss;
    struct lte_stat sss;
    struct lte_stat rsrp_dbfs;
    struct lte_stat rsrq_db;
    struct lte_stat sinr_db;
    struct lte_stat delay_ns;
    struct lte_stat spread_ns;
    struct lte_stat drift_hz;
    struct lte_stat ports;
    int pci;             /* whose these are */
    int valid;           /* whether pci means anything yet */
};

static inline void lte_stats_clear(struct lte_cell_stats *st) {
    if (!st)
        return;
    lte_stat_reset(&st->frequency_khz);
    lte_stat_reset(&st->pss);
    lte_stat_reset(&st->sss);
    lte_stat_reset(&st->rsrp_dbfs);
    lte_stat_reset(&st->rsrq_db);
    lte_stat_reset(&st->sinr_db);
    lte_stat_reset(&st->delay_ns);
    lte_stat_reset(&st->spread_ns);
    lte_stat_reset(&st->drift_hz);
    lte_stat_reset(&st->ports);
    st->pci = 0;
    st->valid = 0;
}

/*
 * Point the accumulator at a cell, clearing it if that is a different cell
 * from the one it was following. Call before adding a block's readings.
 * Returns 1 when it cleared, which is worth knowing on screen: a run of one
 * block is not a statistic.
 */
static inline int lte_stats_for_cell(struct lte_cell_stats *st, int pci) {
    if (!st)
        return 0;
    if (st->valid && st->pci == pci)
        return 0;
    lte_stats_clear(st);
    st->pci = pci;
    st->valid = 1;
    return 1;
}

#endif
