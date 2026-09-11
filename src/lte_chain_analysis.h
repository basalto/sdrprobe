#ifndef LTE_CHAIN_ANALYSIS_H
#define LTE_CHAIN_ANALYSIS_H

#include <stddef.h>

#include "lte_confirm.h"
#include "lte_dsp.h"
#include "lte_mib.h"
#include "lte_session.h"
#include "lte_stats.h"

/*
 * The public LTE chain walk, once, for a live receiver and for a capture.
 *
 * `.scratch/deepening/issues/14-*`. Two diagnostics answered overlapping
 * questions through separate implementations -- `run_headless()`'s
 * `--lte-chain` branch and `scripts/lte_chain_probe.c` -- and the rules had
 * already drifted twice and been repaired one at a time. The repeated-message
 * rule was written out in both until it was proved equivalent and moved to
 * `lte_mib_repeat_observe()`. The multi-cell search reached a committed
 * capture only after the live-only path had become able to return **fewer**
 * cells than the single-cell search it generalises, with nothing to notice.
 * Both are the same fault: the public chain had no owner.
 *
 * This is that owner. It knows nothing about `struct app`, acquisition,
 * files, stdout or any private `lte_dsp.c` symbol; it takes centred I/Q and
 * returns measured facts, and each adapter formats them. It composes the
 * existing public modules and changes none of their arithmetic:
 * `lte_cell_search_all`, `lte_cell_search`, `lte_reference_power`,
 * `lte_channel_shape`, `lte_port_coherence`, `lte_pbch_soft_bits`,
 * `lte_mib_decode`, `lte_mib_repeat_observe`, `lte_confirm` and `lte_stats`.
 *
 * **It is not a chain interface for GSM, TETRA, FM or ADS-B.** ADR-0023 asks
 * technology modules to share dependency and testability constraints, not one
 * function shape. This is one LTE module with two real adapters.
 *
 * What stays outside, deliberately: `probe-lte-chain`'s white-box
 * diagnostics -- per-root correlation landscapes, cyclic-prefix competitors,
 * timing sweeps, parity-bit distance, reference-sequence coherence,
 * broadcast repetition controls. Those compile `lte_dsp.c` directly to reach
 * its statics, and moving them here would widen this interface until it was a
 * result type exposing every internal measurement, which is the opposite of
 * the depth this exists to create.
 */

/*
 * **The block size is the caller's and is not assumed**, and that is not
 * politeness. The two adapters disagree about it by a factor of two:
 * `lte_chain_probe.c` reads 262144 pairs and the program's
 * `SAMPLE_BLOCK_PAIRS` is 131072, which is dump1090's byte count wearing the
 * pairs name (`.scratch/device-model/issues/09-*` fixed that for the program
 * and did not reach the probe).
 *
 * It matters because block size changes the answer here: halving it took LTE
 * from 28 Master Information Blocks to 55 over one capture, since an attempt
 * needs enough samples to reach across a 40 ms period. So this module is
 * agnostic, and **no gate may compare a live figure with a capture figure** --
 * each adapter is compared against its own baseline.
 */
struct lte_chain_block {
    const float *i_samples;
    const float *q_samples;
    size_t pair_count;
    double sample_rate_hz;
    float full_scale;
};

/* Another identity on the same carrier, and whether its own broadcast channel
   decoded -- which is what settles it, since a search repeats its mistakes and
   a sidelobe reported every block looks exactly like a neighbour. */
struct lte_chain_neighbour {
    struct lte_cell cell;
    /* How far its frame boundary sits from the primary's. An identity
       invented out of a strong cell's sidelobe lands at that cell's timing;
       a genuine neighbour has no reason to. */
    long timing_from_primary;
    int have_power;
    struct lte_reference_power power;
    int mib_decoded;
};

/* One block's worth of measured fact. No formatted text, and no pointer into
   anything the caller owns. */
struct lte_chain_result {
    /*
     * Three outcomes and not two. `have_cell` 0 with `refused` 1 is the
     * "no cell" line -- the multi-cell search came back empty *and* the
     * single-cell search declined -- and it still carries what the refusal
     * measured, which is what that line reports.
     */
    int have_cell;
    int refused;

    struct lte_cell primary;
    /* Where the primary came from: 1 when the multi-cell search supplied it,
       0 when it fell back to the single-cell search. */
    int primary_from_all;

    struct lte_cell on_carrier[LTE_MAX_CELLS_PER_CARRIER];
    int cell_count;
    struct lte_chain_neighbour neighbour[LTE_MAX_CELLS_PER_CARRIER];
    int neighbour_count;

    int have_shape;
    struct lte_channel_shape shape;
    int have_power;
    struct lte_reference_power power;
    int have_ports;
    int port_count;
    float coherence[LTE_PORT_COUNT];

    int have_mib;
    /* Which combining hypothesis read it: one of `lte_session_port_hypotheses`
       as a port count, not an index. */
    int mib_ports_combined;
    struct lte_mib mib;
    /* Whether the adjacent message agreed. A parity that passes is not yet a
       message -- sixteen bits accept one block in 65536 and a block tries
       thirty-six -- so this is a separate fact from `have_mib` and the two
       must never be summed into one number. */
    int mib_agreed;
};

/* What accumulates across blocks. */
struct lte_chain_run {
    unsigned long blocks;
    unsigned long cells;
    /*
     * `decoded` counts **blocks whose primary broadcast decoded**, and the
     * per-identity figures in `tally` count decodes per identity. A block
     * decoding for two identities is one here and two there, so the two sums
     * differ legitimately -- on EARFCN 3625 they read 713 and 722 -- and a
     * refactor that makes them agree has broken one of them.
     */
    unsigned long decoded;
    unsigned long agreed;

    struct lte_cell_tally tally;
    struct lte_cell_stats stats;
    struct lte_mib_repeat repeat;

    /*
     * How many multi-cell searches have been run, which exists so a check can
     * prove **one per block** rather than infer it from equal answers after
     * duplicated work. The live path searched twice once -- eleven
     * milliseconds of a sixty-eight millisecond block spent finding the same
     * peaks again -- and no output showed it.
     */
    unsigned long searches;
};

void lte_chain_run_reset(struct lte_chain_run *run);

/*
 * Analyse one block, accumulating into `run`.
 *
 * Returns 1 when a cell was analysed, 0 when the block held none (the result's
 * `refused` says so and carries what the refusal measured), and -1 when the
 * block cannot be analysed at all -- too few pairs, or an argument missing.
 * **A -1 counts nothing**: `run->blocks` advances only for a block that was
 * actually looked at, which is what makes a rate over blocks mean anything.
 */
int lte_chain_analyse(struct lte_chain_run *run,
                      const struct lte_chain_block *block,
                      struct lte_chain_result *out);

/* The fewest pairs a block must hold to be worth looking at: a half frame plus
   one transform, which is what the primary sequence search needs. */
#define LTE_CHAIN_MIN_PAIRS (LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE)

#endif /* LTE_CHAIN_ANALYSIS_H */
