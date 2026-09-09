/*
 * check-lte-session -- an LTE decode replayed block by block, with no window,
 * no receiver and no process launch.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`, third and largest. The
 * thing most worth having reachable here is the **rule that decides when a
 * broadcast message is believed**: thirty-six parity attempts a block makes a
 * lucky pass expected rather than rare, so a message counts only when a second
 * one agrees about what a cell does not change between frames.
 *
 * `lte_b20_pci28.bin` is at **1.92 MS/s**, not the house rate, and must keep
 * reading cell 28 under the normal cyclic prefix in every block -- that
 * identity is the check a conjugated primary sequence cannot pass.
 */

#include "check.h"

#include "lte_session.h"
#include "sdr_dsp.h"

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
static struct lte_session session;

struct replay {
    int blocks_fed;
    int cells_found;
    int parity_passes;
    int messages;
    int off_grid;
    int pci_changes;
    int last_pci;
};

static int replay(const char *path, double rate, struct replay *out) {
    struct device_profile profile =
        device_profile_capture(path, SAMPLE_FORMAT_U8, FULL_SCALE, 0.0,
                               1920000);
    FILE *f = fopen(path, "rb");

    memset(out, 0, sizeof(*out));
    out->last_pci = -1;
    lte_session_reset(&session);
    if (!f)
        return 0;
    for (;;) {
        struct lte_session_event event;
        size_t got = fread(raw, 1, sizeof(raw), f);
        size_t pairs;

        if (got < sizeof(raw))
            break;  /* whole blocks only, so a run is repeatable */
        pairs = sdr_dsp_convert_iq(&profile, raw, got, I, Q, M, BLOCK_PAIRS);
        out->blocks_fed++;
        lte_session_feed(&session, I, Q, pairs, rate, FULL_SCALE,
                         (double)out->blocks_fed, NULL, &event);
        if (event.off_grid)
            out->off_grid++;
        if (event.cell_found) {
            out->cells_found++;
            if (session.cell.pci != out->last_pci) {
                out->pci_changes++;
                out->last_pci = session.cell.pci;
            }
        }
        if (event.parity_passed)
            out->parity_passes++;
        if (event.message_confirmed)
            out->messages++;
    }
    fclose(f);
    return 1;
}

/* The identity, which is what a conjugated primary sequence cannot produce. */
static void test_cell_28(void) {
    struct replay r;

    if (!replay("testfiles/lte_b20_pci28.bin", RATE, &r)) {
        check_true("testfiles/lte_b20_pci28.bin opens", 0);
        return;
    }
    check_int("thirty whole blocks", r.blocks_fed, 30);
    /*
     * Twenty-nine of them find the cell, not thirty, and CLAUDE.md's "in
     * every block" overstates it. **Block 25 reads a primary sequence at 0.80
     * and no secondary one at all** -- SSS 0.000, which is the "PSS without
     * SSS" case the LTE notes name as its own diagnosis. It is pre-existing:
     * calling `lte_cell_search()` directly on that block does the same, with
     * no session involved.
     *
     * Pinned exactly rather than loosened to "most blocks", because a change
     * either way is worth noticing -- 30 would mean something improved and 28
     * that something regressed.
     */
    check_int("twenty-nine of them find the cell", r.cells_found, 29);
    check_int("cell 28", session.cell.pci, 28);
    check_int("N_ID_1 9", session.cell.n_id_1, 9);
    check_int("N_ID_2 1", session.cell.n_id_2, 1);
    check_int("normal cyclic prefix", session.cell.extended_cp, 0);
    check_int("and it never changes identity", r.pci_changes, 1);

    check_true("its broadcast decoded", session.mib_valid != 0);
    check_int("two antenna ports", session.mib.antenna_ports, 2);
    check_int("50 resource blocks", session.mib.bandwidth_prb, 50);

    /* The port coherence is a second, independent count of the antennas: it
       reads the reference phases and shares no code with the parity mask. */
    check_true("the ports were measured", session.port_coherence_valid != 0);
}

/*
 * The rule this module exists for. Every message that is believed had a
 * parity pass; not every parity pass becomes a message.
 */
static void test_a_message_needs_a_second_that_agrees(void) {
    struct replay r;

    if (!replay("testfiles/lte_b20_pci28.bin", RATE, &r))
        return;
    check_true("parity passed", r.parity_passes > 0);
    check_true("and messages were believed", r.messages > 0);
    check_msg(r.messages < r.parity_passes,
              "%d messages from %d parity passes: the repeat must cost "
              "something\n", r.messages, r.parity_passes);
    check_int("the first pass is never a message on its own",
              r.messages, r.parity_passes - 1);
    check_int("and the counters agree with the events",
              (int)session.mibs_decoded, r.messages);
    check_int("as do the parity ones",
              (int)session.mib_parity_passes, r.parity_passes);
    check_int("two agreements make a message", LTE_SESSION_MIB_AGREEMENTS, 2);
}

/*
 * ADR-0014: the arithmetic is the 1.92 MS/s grid and nothing else will do. A
 * run at the house 2 MS/s must say so rather than quietly finding nothing.
 */
static void test_off_the_grid_says_so(void) {
    struct replay r;

    if (!replay("testfiles/lte_b20_pci28.bin", 2000000.0, &r))
        return;
    check_int("every block is off grid", r.off_grid, r.blocks_fed);
    check_int("and no cell is claimed", r.cells_found, 0);
    check_true("the reason names the grid",
               strstr(session.status, "1.920") != NULL);
    check_int("and nothing was latched", session.cell_valid, 0);
}

/* A block too short for a half-frame is its own answer. */
static void test_a_short_block(void) {
    struct lte_session_event event;
    static float zero[LTE_HALF_FRAME_SAMPLES];

    lte_session_reset(&session);
    check_int("too few samples",
              lte_session_feed(&session, zero, zero, 1000, RATE, FULL_SCALE,
                               0.0, NULL, &event),
              0);
    check_int("and it says which", event.block_too_short, 1);
    check_int("not off grid", event.off_grid, 0);
    check_true("the reason names the shortfall",
               strstr(session.status, "cell search needs") != NULL);

    check_int("a null session",
              lte_session_feed(NULL, zero, zero, 1000, RATE, FULL_SCALE, 0.0,
                               NULL, &event),
              0);
    check_int("null buffers",
              lte_session_feed(&session, NULL, NULL, 1000, RATE, FULL_SCALE,
                               0.0, NULL, &event),
              0);
}

/*
 * The statistics reset when the identity changes, which is load-bearing: a
 * carrier here alternates between two cells block to block, and an average
 * across both would sit under a heading naming one of them.
 */
static void test_the_statistics_follow_the_identity(void) {
    struct replay r;

    if (!replay("testfiles/lte_b20_pci28.bin", RATE, &r))
        return;
    check_int("the statistics belong to cell 28", session.stats.pci, 28);
    check_true("and counted every block", session.stats.pss.count > 0);
    check_msg((int)session.stats.pss.count == r.cells_found,
              "pss counted %d of the %d blocks that found a cell\n",
              (int)session.stats.pss.count, r.cells_found);

    /* Handing the statistics a different identity clears them. */
    lte_stats_for_cell(&session.stats, 99);
    check_int("a new identity resets them", (int)session.stats.pss.count, 0);
    check_int("and takes the new one", session.stats.pci, 99);
}

/* Reset forgets the cell and everything measured about it. */
static void test_reset(void) {
    struct replay r;

    if (!replay("testfiles/lte_b20_pci28.bin", RATE, &r))
        return;
    check_true("a cell is known", session.cell_valid != 0);
    lte_session_reset(&session);
    check_int("and then it is not", session.cell_valid, 0);
    check_int("nor its broadcast", session.mib_valid, 0);
    check_int("nor the pending message", session.pending_mib_hits, 0);
    check_msg(session.blocks_seen == 0, "the funnel is cleared too\n");
}

int main(void) {
    test_cell_28();
    test_a_message_needs_a_second_that_agrees();
    test_off_the_grid_says_so();
    test_a_short_block();
    test_the_statistics_follow_the_identity();
    test_reset();
    return check_report("an LTE decode, block by block");
}
