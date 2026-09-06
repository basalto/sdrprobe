/*
 * Smallest, mean and largest of the measurements that move.
 */

#include "check.h"
#include "lte_stats.h"

static void test_the_first_sample_sets_both_ends(void) {
    struct lte_stat s;
    lte_stat_reset(&s);

    check_int("nothing added yet", (long)s.count, 0);
    check_close("and no mean", lte_stat_mean(&s), 0.0, 0.0001);

    /*
     * The case this is written for: a reference power is dBFS and therefore
     * always negative. Starting the ends at zero and comparing would leave
     * the maximum at zero for ever, which is not a level this receiver can
     * measure and would sit above every real reading.
     */
    lte_stat_add(&s, -35.8f);
    check_close("one sample is its own smallest", s.min, -35.8, 0.001);
    check_close("and its own largest", s.max, -35.8, 0.001);
    check_close("and its own mean", lte_stat_mean(&s), -35.8, 0.001);

    lte_stat_add(&s, -40.2f);
    lte_stat_add(&s, -31.0f);
    check_close("smallest of three", s.min, -40.2, 0.001);
    check_close("largest of three", s.max, -31.0, 0.001);
    check_close("mean of three", lte_stat_mean(&s),
                (-35.8 - 40.2 - 31.0) / 3.0, 0.001);
    check_int("counted", (long)s.count, 3);
}

static void test_a_long_run_keeps_its_mean(void) {
    struct lte_stat s;
    int n;
    lte_stat_reset(&s);

    /* Ten thousand blocks is under twelve minutes at this block rate, and a
       float accumulator loses the tail of a sum that long -- hence the
       double. */
    for (n = 0; n < 10000; n++)
        lte_stat_add(&s, 0.1f);
    check_close("the mean does not drift", lte_stat_mean(&s), 0.1, 0.0005);
    check_int("all of them counted", (long)s.count, 10000);
}

static void test_a_new_cell_starts_again(void) {
    struct lte_cell_stats st;
    memset(&st, 0, sizeof(st));

    check_int("the first cell clears the accumulator",
              lte_stats_for_cell(&st, 402), 1);
    lte_stat_add(&st.rsrp_dbfs, -32.8f);
    lte_stat_add(&st.rsrp_dbfs, -33.2f);

    check_int("the same cell does not", lte_stats_for_cell(&st, 402), 0);
    check_int("so its samples survive", (long)st.rsrp_dbfs.count, 2);

    /*
     * The reason this exists. EARFCN 3625 alternates between PCI 402 and
     * PCI 190 block to block; an average across both describes neither, and
     * would sit under a heading naming one of them.
     */
    check_int("a different cell clears it", lte_stats_for_cell(&st, 190), 1);
    check_int("and its samples are gone", (long)st.rsrp_dbfs.count, 0);
    check_int("the accumulator now belongs to the new cell", st.pci, 190);
    check_close("with no mean to report", lte_stat_mean(&st.rsrp_dbfs), 0.0,
                0.0001);
}

static void test_clearing_leaves_nothing_behind(void) {
    struct lte_cell_stats st;
    memset(&st, 0, sizeof(st));

    lte_stats_for_cell(&st, 28);
    lte_stat_add(&st.pss, 0.9f);
    lte_stat_add(&st.sinr_db, 24.0f);
    lte_stat_add(&st.ports, 2.0f);
    lte_stats_clear(&st);
    check_int("every field is cleared, not just the one read",
              (long)(st.pss.count + st.sinr_db.count + st.ports.count), 0);
    check_int("and the identity with them", st.valid, 0);
}

static void test_nulls_are_ignored(void) {
    /* The view calls these from a draw path; a null must not be a crash. */
    lte_stat_reset(NULL);
    lte_stat_add(NULL, 1.0f);
    lte_stats_clear(NULL);
    check_close("a null has no mean", lte_stat_mean(NULL), 0.0, 0.0001);
    check_int("and a null cell is never cleared",
              lte_stats_for_cell(NULL, 1), 0);
}

int main(void) {
    test_the_first_sample_sets_both_ends();
    test_a_long_run_keeps_its_mean();
    test_a_new_cell_starts_again();
    test_clearing_leaves_nothing_behind();
    test_nulls_are_ignored();
    return check_report("statistics over the measurements that move");
}
