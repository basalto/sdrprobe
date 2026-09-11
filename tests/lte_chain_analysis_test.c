/*
 * check-lte-chain-analysis -- the public LTE chain walk, over both committed
 * captures, with no window, no receiver and no process launch.
 *
 * `.scratch/deepening/issues/14-*`. The module exists because two diagnostics
 * implemented the same walk and drifted apart twice; this suite is the thing
 * neither of them had. Its job is to pin the facts an adapter may format and
 * the invariants a reorganisation must not quietly move, and to say which is
 * which -- because most of the numbers below are allowed to change with the
 * air and a few are not allowed to change at all.
 *
 * **Both captures, deliberately.** One real cell identity establishes nothing
 * about a walk that is supposed to work on any carrier: `lte_b20_pci28.bin`
 * is a two-port cell under the normal cyclic prefix and
 * `lte_b8_pci330_4port.bin` is a four-port one, which is the same argument
 * that gives the GSM set three captures for three BCCs.
 *
 * **The block size is this suite's own** -- 131072 pairs, the program's
 * `SAMPLE_BLOCK_PAIRS` -- and the figures here are per block of that size.
 * `probe-lte-chain` reads 262144 and its numbers are its own; nothing here
 * may be compared with a live run or with the probe.
 */

#include "check.h"

#include "lte_chain_analysis.h"
#include "sdr_dsp.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 131072 pairs at 1.92 MS/s is 68.3 ms, not the house 65.5. */
#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
#define RATE 1920000.0
#define FULL_SCALE 127.5f

static uint8_t raw[BLOCK_BYTES];
static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];

struct walk {
    struct lte_chain_run run;
    int blocks_read;            /* whole blocks pulled from the file */
    int analysed;               /* lte_chain_analyse returned 1 */
    int refused;                /* returned 0 */
    int primary_pci;            /* the last primary seen */
    int primary_changes;
    int normal_cp_blocks;
    int max_ports;
    int neighbours_total;
    int most_at_once;
    /* The invariant: whenever the single-cell search finds a cell, the
       multi-cell list this module used must contain it. */
    int single_found;
    int single_missing_from_all;
    /* Whether the primary was ever not the strongest correlation on the
       carrier, which would mean the choice had changed. */
    int primary_not_strongest;
    /* The port count the broadcast message itself carries, which is the
       CRC-masked truth, as against the coherence estimate above. */
    int mib_ports;
    int mib_prb;
    int combining_used;
};

static int walk_capture(const char *path, struct walk *out) {
    struct device_profile profile =
        device_profile_capture(path, SAMPLE_FORMAT_U8, FULL_SCALE, 0.0,
                               1920000);
    FILE *f = fopen(path, "rb");

    memset(out, 0, sizeof(*out));
    lte_chain_run_reset(&out->run);
    out->primary_pci = -1;
    if (!f)
        return 0;
    for (;;) {
        struct lte_chain_result r;
        struct lte_chain_block block;
        struct lte_cell single;
        size_t got = fread(raw, 1, sizeof(raw), f);
        int verdict;

        if (got < sizeof(raw))
            break;  /* whole blocks only, so a run is repeatable */
        block.i_samples = I;
        block.q_samples = Q;
        block.pair_count = sdr_dsp_convert_iq(&profile, raw, got, I, Q, M,
                                              BLOCK_PAIRS);
        block.sample_rate_hz = RATE;
        block.full_scale = FULL_SCALE;
        out->blocks_read++;

        verdict = lte_chain_analyse(&out->run, &block, &r);
        if (verdict < 0)
            continue;
        if (verdict == 0) {
            out->refused++;
            continue;
        }
        out->analysed++;

        if (r.primary.pci != out->primary_pci) {
            out->primary_changes++;
            out->primary_pci = r.primary.pci;
        }
        if (!r.primary.extended_cp)
            out->normal_cp_blocks++;
        if (r.have_ports && r.port_count > out->max_ports)
            out->max_ports = r.port_count;
        if (r.have_mib) {
            out->mib_ports = r.mib.antenna_ports;
            out->mib_prb = r.mib.bandwidth_prb;
            out->combining_used = r.mib_ports_combined;
        }
        out->neighbours_total += r.neighbour_count;
        if (r.cell_count > out->most_at_once)
            out->most_at_once = r.cell_count;

        /* The primary must be the strongest correlation among the cells the
           search returned -- not the strongest power, which is the ordering
           lte_cell_search_all uses and the wrong one to walk. */
        if (r.primary_from_all) {
            int c;
            for (c = 0; c < r.cell_count; c++)
                if (r.on_carrier[c].pss_correlation >
                    r.primary.pss_correlation + 1e-6f)
                    out->primary_not_strongest++;
        }

        /* And the invariant this ticket's own history turns on. */
        memset(&single, 0, sizeof(single));
        if (lte_cell_search(I, Q, block.pair_count, RATE, &single, NULL) == 1) {
            int c, present = 0;
            out->single_found++;
            for (c = 0; c < r.cell_count; c++)
                if (r.on_carrier[c].pci == single.pci)
                    present = 1;
            if (r.cell_count == 0 && r.primary.pci == single.pci)
                present = 1;   /* the fallback supplied it */
            if (!present)
                out->single_missing_from_all++;
        }
    }
    fclose(f);
    return 1;
}

/*
 * Band 20, physical cell identity 28, two ports, normal cyclic prefix.
 *
 * The identity is the check a conjugated primary sequence cannot pass, and it
 * is asserted here at this interface as well as in `check-lte-session`,
 * because the question is different: there it is "the session latches the
 * right cell", here it is "the walk chooses the right primary out of
 * everything on the carrier".
 */
static void test_band_20_reads_cell_28(void) {
    struct walk w;

    check_int("the band 20 capture opens",
              walk_capture("testfiles/lte_b20_pci28.bin", &w), 1);
    check_int("thirty whole blocks", w.blocks_read, 30);
    /*
     * **29 of 30**, and the missing one is not a defect here.
     * `CLAUDE.md` records it: one block reads a primary sequence at 0.80 and
     * no secondary one at all -- the "PSS without SSS" case named as its own
     * diagnosis -- and `check-lte-session` pins the same 29 from a different
     * interface. Two suites reaching the same number through different code is
     * corroboration; pinning it exactly is what makes 30 mean "something
     * improved" and 28 "something regressed".
     */
    check_int("twenty-nine of them hold a cell", w.analysed, 29);
    check_int("and exactly one does not", w.refused, 1);
    check_int("the primary is 28", w.primary_pci, 28);
    check_int("and never changed", w.primary_changes, 1);
    check_int("under the normal cyclic prefix throughout",
              w.normal_cp_blocks, w.analysed);

    /*
     * The port count the **message** carries, which is the CRC-masked answer
     * and the one to assert. `lte_port_coherence()` corroborates it from the
     * reference phases and shares no code with it, but it is a per-block
     * estimate and wobbles -- it reads 3 in at least one block here where the
     * message says 2 throughout. Asserting the estimate would be pinning
     * noise; asserting the message is pinning the decode.
     */
    check_int("the broadcast says two antenna ports", w.mib_ports, 2);
    check_int("over fifty resource blocks", w.mib_prb, 50);
    check_int("read under one-port combining", w.combining_used, 1);
    check_true("and the coherence estimate is in the same neighbourhood",
               w.max_ports >= 2 && w.max_ports <= 4);

    /*
     * Decoded and agreed are two facts. Agreement is adjacent-message
     * equality, so the first decode has nothing to agree with and the
     * relation is exact when every analysed block decodes: 29 and 28.
     * Collapsing them into one number is what this ticket's history did once
     * already, with "messages" meaning both.
     */
    check_int("every block with a cell decoded", (int)w.run.decoded, 29);
    check_int("and all but the first agreed", (int)w.run.agreed, 28);
    check_true("agreement can never exceed decodes",
               w.run.agreed <= w.run.decoded);
}

/*
 * Band 8, physical cell identity 330, four ports. The second capture is the
 * point: a walk fitted to one identity or one port count passes the test
 * above and fails here.
 */
static void test_band_8_reads_cell_330_on_four_ports(void) {
    struct walk w;

    check_int("the band 8 capture opens",
              walk_capture("testfiles/lte_b8_pci330_4port.bin", &w), 1);
    check_int("twelve whole blocks", w.blocks_read, 12);
    check_int("every one holds a cell", w.analysed, 12);
    check_int("none refused", w.refused, 0);
    check_int("the primary is 330", w.primary_pci, 330);
    check_int("and never changed", w.primary_changes, 1);

    /*
     * Four ports and twenty-five resource blocks, read under **four-port**
     * combining where band 20 needed one -- so the walk tries all three
     * hypotheses in order and takes the one that fits, rather than having a
     * favourite. A module fitted to the first capture reads nothing here.
     */
    check_int("the broadcast says four antenna ports", w.mib_ports, 4);
    check_int("over twenty-five resource blocks", w.mib_prb, 25);
    check_int("read under four-port combining", w.combining_used, 4);
    check_int("and the coherence estimate agrees for once", w.max_ports, 4);

    check_int("every block decoded", (int)w.run.decoded, 12);
    check_int("and all but the first agreed", (int)w.run.agreed, 11);
}

/*
 * **Whatever the single-cell search finds, the multi-cell walk finds too.**
 *
 * This is not a tidy property, it is the fault that shipped. The
 * antenna-port coherence gate belongs on the *neighbours* and was applied to
 * the cell the carrier is about as well, so `lte_cell_search_all` came back
 * empty on a block where `lte_cell_search` returns cell 330 -- two co-channel
 * cells depress each other's reference coherence under the threshold and both
 * were dropped. The multi-cell search must never return less than the path it
 * generalises.
 */
static void test_the_multi_cell_walk_never_loses_a_cell(void) {
    struct walk b20, b8;

    walk_capture("testfiles/lte_b20_pci28.bin", &b20);
    walk_capture("testfiles/lte_b8_pci330_4port.bin", &b8);

    check_true("the single-cell search finds something on band 20",
               b20.single_found > 0);
    check_int("and the walk never lost it", b20.single_missing_from_all, 0);
    check_true("the single-cell search finds something on band 8",
               b8.single_found > 0);
    check_int("and the walk never lost it", b8.single_missing_from_all, 0);
}

/* The primary is chosen by correlation, never by reference power. A run's
   statistics reset when the identity changes, so a primary that flips block to
   block empties them -- 146 blocks reset to 3, measured. */
static void test_the_primary_is_the_strongest_correlation(void) {
    struct walk w;

    walk_capture("testfiles/lte_b20_pci28.bin", &w);
    check_int("no block chose a weaker correlation", w.primary_not_strongest,
              0);
    check_int("so the identity never flipped", w.primary_changes, 1);
    check_true("and the statistics kept every sample",
               w.run.stats.pss.count == (unsigned)w.analysed);
    check_int("under one identity", w.run.stats.pci, 28);
}

/* Looks, decodes and verdicts are three facts and not one. An identity seen
   as often as a real cell and never decoding is `unread`, which is the case a
   sightings threshold gets wrong. */
static void test_the_verdicts_stay_distinct(void) {
    struct walk w;
    int i, confirmed = 0, unread = 0, spurious = 0, primary_confirmed = 0;

    walk_capture("testfiles/lte_b20_pci28.bin", &w);
    for (i = 0; i < w.run.tally.count; i++) {
        const struct lte_cell_sighting *s = &w.run.tally.cell[i];
        enum lte_cell_verdict v = lte_cell_verdict_for(s);

        if (v == LTE_CELL_CONFIRMED) {
            confirmed++;
            if (s->pci == 28)
                primary_confirmed = 1;
        } else if (v == LTE_CELL_UNREAD) {
            unread++;
        } else if (v == LTE_CELL_SPURIOUS) {
            spurious++;
        }
        check_true("no identity decoded more often than it was looked at",
                   s->decodes <= s->looks);
    }
    check_int("cell 28 is confirmed", primary_confirmed, 1);
    check_int("and every identity has a verdict",
              confirmed + unread + spurious, w.run.tally.count);
    check_int("the tally covers the whole carrier, not just the primary",
              w.run.tally.count, 2);
    check_true("so a neighbour was seen and judged", confirmed + unread +
                                                         spurious >
                                                     1);
}

/*
 * One multi-cell search per block.
 *
 * The counter is inside the module because equal answers prove nothing about
 * duplicated work: the live path searched twice for a while -- eleven
 * milliseconds of a sixty-eight millisecond block spent finding the same peaks
 * again -- and every printed figure was identical throughout. This says the
 * module does not search twice; it says nothing about what `lte_dsp.c` does
 * inside one call, which is that module's own business.
 */
static void test_one_search_per_block(void) {
    struct walk w;

    walk_capture("testfiles/lte_b20_pci28.bin", &w);
    check_true("blocks were analysed", w.run.blocks > 0);
    check_true("one search each", w.run.searches == w.run.blocks);
    check_true("and a block that was refused still counts as one",
               w.run.blocks == (unsigned long)(w.analysed + w.refused));
}

/*
 * The control-flow cases a real capture cannot force, and only those: noise
 * holds no cell, and a short block cannot be looked at.
 *
 * Noise is used here to exercise a *refusal path*, never to establish a radio
 * convention -- a synthetic signal agrees with whatever assumption built it,
 * and every identity above comes from a real capture.
 */
static unsigned rng_state = 12345u;

static float rng_noise(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (float)((rng_state >> 16) & 0xffff) / 32768.0f - 1.0f;
}

static void test_the_refusals(void) {
    static float ni[BLOCK_PAIRS], nq[BLOCK_PAIRS];
    struct lte_chain_run run;
    struct lte_chain_result r;
    struct lte_chain_block block;
    size_t k;

    for (k = 0; k < BLOCK_PAIRS; k++) {
        ni[k] = rng_noise() * 20.0f;
        nq[k] = rng_noise() * 20.0f;
    }
    lte_chain_run_reset(&run);
    block.i_samples = ni;
    block.q_samples = nq;
    block.pair_count = BLOCK_PAIRS;
    block.sample_rate_hz = RATE;
    block.full_scale = FULL_SCALE;

    check_int("noise holds no cell", lte_chain_analyse(&run, &block, &r), 0);
    check_int("which is a refusal, not an absence of measurement", r.refused,
              1);
    check_int("and no cell to report", r.have_cell, 0);
    check_int("the block still counted", (int)run.blocks, 1);
    check_int("and was still searched once", (int)run.searches, 1);
    check_int("but no cell was tallied", (int)run.cells, 0);

    /* A block too short to look at counts as nothing at all, which is what
       makes a rate over blocks mean anything. */
    block.pair_count = LTE_CHAIN_MIN_PAIRS - 1;
    check_int("a short block cannot be analysed",
              lte_chain_analyse(&run, &block, &r), -1);
    check_int("and does not count", (int)run.blocks, 1);

    block.pair_count = BLOCK_PAIRS;
    block.i_samples = NULL;
    check_int("nor can one with no samples",
              lte_chain_analyse(&run, &block, &r), -1);
    check_int("still one block", (int)run.blocks, 1);
    check_int("a null run is refused", lte_chain_analyse(NULL, &block, &r),
              -1);
}

int main(void) {
    test_band_20_reads_cell_28();
    test_band_8_reads_cell_330_on_four_ports();
    test_the_multi_cell_walk_never_loses_a_cell();
    test_the_primary_is_the_strongest_correlation();
    test_the_verdicts_stay_distinct();
    test_one_search_per_block();
    test_the_refusals();
    return check_report("one LTE chain walk, live or captured");
}
