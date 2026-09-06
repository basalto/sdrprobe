/*
 * Which cell identities on a carrier are believed, and why a count of blocks
 * is not the answer.
 */

#include "check.h"
#include "lte_confirm.h"

static void test_a_message_confirms(void) {
    struct lte_cell_tally t;
    memset(&t, 0, sizeof(t));

    /* Two blocks, both read. */
    lte_confirm_saw(&t, 190, 1);
    lte_confirm_saw(&t, 190, 1);
    check_int("one identity is tallied", t.count, 1);
    check_int("both looks are counted", t.cell[0].looks, 2);
    check_int("both messages are counted", t.cell[0].messages, 2);
    check_str("two messages confirm it",
              lte_cell_verdict_name(lte_cell_verdict_for(&t.cell[0])),
              "confirmed");
}

static void test_one_message_is_not_enough(void) {
    struct lte_cell_sighting seen = { 190, 40, 1 };

    /*
     * Thirty-six chances a block -- four scrambling offsets against three
     * masks for each of three port hypotheses -- so a long run sees one
     * parity pass by luck. Forty looks and one message is that run.
     */
    check_str("a single message does not confirm",
              lte_cell_verdict_name(lte_cell_verdict_for(&seen)), "unread");
}

static void test_repetition_does_not_confirm(void) {
    /*
     * The measurement this file exists for. On EARFCN 3625, PCI 410 was
     * reported in twenty-nine blocks of 352 and never decoded, while PCI 402
     * decoded routinely at the same reference power. A search that mistakes a
     * sidelobe for a cell makes the same mistake every block, so the false
     * identity repeats as faithfully as the true one and **any threshold on
     * hits would have confirmed it**.
     */
    struct lte_cell_sighting repeated = { 410, 29, 0 };
    struct lte_cell_sighting read_twice = { 402, 3, 2 };

    check_str("twenty-nine sightings and no message is not a cell",
              lte_cell_verdict_name(lte_cell_verdict_for(&repeated)),
              "unread");
    check_str("three sightings and two messages is",
              lte_cell_verdict_name(lte_cell_verdict_for(&read_twice)),
              "confirmed");
    check_true("the identity seen ten times as often is the unconfirmed one",
               repeated.looks > read_twice.looks * 5);
}

static void test_one_look_is_an_opinion(void) {
    struct lte_cell_sighting once = { 163, 1, 0 };
    struct lte_cell_sighting none = { 0, 0, 0 };

    check_str("a single sighting is spurious",
              lte_cell_verdict_name(lte_cell_verdict_for(&once)), "spurious");
    check_str("nothing seen is pending",
              lte_cell_verdict_name(lte_cell_verdict_for(&none)), "pending");
    check_str("a null sighting is pending too",
              lte_cell_verdict_name(lte_cell_verdict_for(NULL)), "pending");
}

static void test_the_table_does_not_churn(void) {
    struct lte_cell_tally t;
    int i;
    memset(&t, 0, sizeof(t));

    for (i = 0; i < LTE_CONFIRM_MAX_CELLS + 4; i++)
        lte_confirm_saw(&t, 100 + i, 1);
    check_int("the table fills and stops", t.count, LTE_CONFIRM_MAX_CELLS);
    /* What was there first stays there. A table that evicts reports whatever
       arrived last, which on a noisy carrier is the newest artefact. */
    check_int("the first identity is still the first", t.cell[0].pci, 100);

    /* And an identity already in the table is still counted once it is full. */
    lte_confirm_saw(&t, 100, 1);
    check_int("a known identity still accumulates", t.cell[0].looks, 2);
}

static void test_blocks_are_counted_separately(void) {
    struct lte_cell_tally t;
    memset(&t, 0, sizeof(t));

    lte_confirm_block(&t);
    lte_confirm_block(&t);
    lte_confirm_saw(&t, 190, 0);
    check_int("blocks looked at", t.blocks, 2);
    check_int("and one of them held an identity", t.cell[0].looks, 1);
    /* A carrier where nothing was found is not the same as one never looked
       at, and only the block count can tell them apart. */
}

int main(void) {
    test_a_message_confirms();
    test_one_message_is_not_enough();
    test_repetition_does_not_confirm();
    test_one_look_is_an_opinion();
    test_the_table_does_not_churn();
    test_blocks_are_counted_separately();
    return check_report("which cell identities are believed");
}
