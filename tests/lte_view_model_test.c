#include "check.h"

#include "model/lte_view_model.h"
#include "runtime/app.h"

#include <string.h>

/*
 * What the LTE screen says, decided without a screen.
 *
 * The three things worth a check are the three the screen decides rather
 * than reads: the crystal error, which is a property of the *receiver* and
 * not of this carrier; the statistics table, whose reset on a change of
 * identity is load-bearing because a carrier here alternates between two
 * cells block to block; and the findings, which are prose one decider
 * chose.
 */

static struct lte_view lte;
static struct lte_view_context ctx;

static struct lte_view_model build(uint32_t hz) {
    struct lte_view_model out;

    ctx.centre_hz = hz;
    lte_view_model_build(&lte, &ctx, &out);
    return out;
}

static void blank(void) {
    memset(&lte, 0, sizeof(lte));
    memset(&ctx, 0, sizeof(ctx));
    ctx.on_grid = 1;
    ctx.receiver_mode = 1;
}

/*
 * The identity, and nothing under it unless there is one.
 *
 * A cell's fields left standing after the search stopped finding it would
 * put last block's PCI under this block's heading -- wrong in the one way
 * a reader would never question.
 */
static void test_no_cell_carries_no_cell(void) {
    struct lte_view_model m;

    blank();
    lte.session.cell.pci = 28;
    lte.session.cell.n_id_1 = 9;
    lte.session.cell.n_id_2 = 1;

    m = build(796000000u);
    check_int("no cell this block", m.cell_valid, 0);
    check_int("so no identity under it", m.pci, 0);
    check_int("nor its group", m.n_id_1, 0);

    lte.session.cell_valid = 1;
    m = build(796000000u);
    check_int("found", m.cell_valid, 1);
    check_int("PCI", m.pci, 28);
    check_int("N_ID_1", m.n_id_1, 9);
    check_int("N_ID_2", m.n_id_2, 1);
}

/*
 * The crystal error in parts per million, which is the figure that
 * transfers: it is a property of the receiver's crystal rather than of this
 * carrier, so it compares with what a GSM calibration measured on a
 * different band. The hertz stay in the statistics table.
 */
static void test_the_crystal_error_is_the_receivers(void) {
    struct lte_view_model m;

    blank();
    lte.session.cell_valid = 1;
    lte.session.cell.frequency_offset_hz = -28100.0;

    m = build(796000000u);
    check_close("-28.1 kHz at 796 MHz is -35.3 ppm", m.crystal_ppm,
                -28100.0 * 1e6 / 796000000.0, 1e-6);

    /* The same *offset* at a different tuning is a different ppm -- which
       is the whole reason the ppm is the figure carried up with the facts
       and the hertz are left down in the readings. */
    m = build(1800000000u);
    check_true("and the same offset higher up is a smaller error",
               m.crystal_ppm > -35.3 + 1e-9);

    /* A tuning of zero is a refusal, not a division. */
    m = build(0u);
    check_close("no tuning, no ppm", m.crystal_ppm, 0.0, 1e-12);
}

/*
 * The statistics are a table of what each measurement *did*, and they are
 * carried only while they belong to somebody. `valid` says whether the
 * `pci` beside them means anything yet.
 */
static void test_statistics_travel_only_when_they_belong_to_a_cell(void) {
    struct lte_view_model m;

    blank();
    lte.session.stats.pci = 28;
    lte.session.stats.rsrp_dbfs.min = -40.6f;
    lte.session.stats.rsrp_dbfs.max = -39.3f;
    lte.session.stats.rsrp_dbfs.sum = -80.0;
    lte.session.stats.rsrp_dbfs.count = 2;

    m = build(796000000u);
    check_int("nobody owns them yet", m.stats_valid, 0);
    check_int("so they are not carried", (long)m.stats.rsrp_dbfs.count, 0);

    lte.session.stats.valid = 1;
    m = build(796000000u);
    check_int("now they belong to a cell", m.stats_valid, 1);
    check_int("whose", m.stats.pci, 28);
    check_close("smallest", m.stats.rsrp_dbfs.min, -40.6, 1e-5);
    check_close("largest", m.stats.rsrp_dbfs.max, -39.3, 1e-5);
    check_close("and the mean is the sum over the count",
                lte_stat_mean(&m.stats.rsrp_dbfs), -40.0, 1e-9);
}

/*
 * The broadcast, and its bandwidth in megahertz -- which is what is
 * *transmitted* rather than what is allocated: a 50-block cell occupies
 * 9 MHz of a 10 MHz channel.
 */
static void test_the_broadcast_reports_what_is_transmitted(void) {
    struct lte_view_model m;

    blank();
    lte.session.mib.bandwidth_prb = 50;
    lte.session.mib.phich_resource_sixths = 1;
    lte.session.mib.antenna_ports = 2;
    lte.session.mib.system_frame_number = 618;
    lte.session.mib.quarter = 2;

    m = build(796000000u);
    check_int("no broadcast read yet", m.mib_valid, 0);
    check_int("so no bandwidth", m.bandwidth_rb, 0);

    lte.session.mib_valid = 1;
    m = build(796000000u);
    check_int("read", m.mib_valid, 1);
    check_int("fifty resource blocks", m.bandwidth_rb, 50);
    check_str("and the PHICH is one sentence, worded once", m.phich,
              "normal, 1/6");
    check_close("which is 9 MHz transmitted, not the 10 allocated",
                m.bandwidth_mhz, 9.0, 1e-9);
    check_int("two antenna ports", m.antenna_ports, 2);
    check_int("frame number", m.frame_number, 618);
    check_int("and which quarter of the 40 ms period", m.quarter, 2);
}

/* The findings are chosen by one decider, so the window and a browser
   cannot word the same numbers differently. With no statistics there is
   nothing to say, and saying nothing is the honest answer. */
static void test_findings_come_already_worded(void) {
    struct lte_view_model m;

    blank();
    m = build(796000000u);
    check_int("nothing measured, nothing claimed", m.findings.count, 0);

    lte.session.stats.valid = 1;
    lte.session.stats.pci = 28;
    /* The primary sequence's correlation is what says anything was heard at
       all, and `lte_findings_from()` refuses without it. */
    lte.session.stats.pss.sum = 0.82;
    lte.session.stats.pss.count = 1;
    lte.session.stats.sinr_db.min = 25.0f;
    lte.session.stats.sinr_db.max = 25.0f;
    lte.session.stats.sinr_db.sum = 25.0;
    lte.session.stats.sinr_db.count = 1;
    lte.session.stats.rsrq_db.sum = -14.0;
    lte.session.stats.rsrq_db.count = 1;
    m = build(796000000u);
    check_true("with measurements there is something to say",
               m.findings.count > 0);
    check_true("and it is a sentence, not a code",
               strlen(m.findings.line[0]) > 8);
}

/* The band scan's rows travel, clamped -- a scan needs a live receiver, and
   a live receiver is exactly who watches from elsewhere. */
static void test_the_scan_rows_travel_clamped(void) {
    struct lte_view_model m;

    blank();
    lte.scan.found[0].pci = 28;
    lte.scan.found[0].earfcn = 6200;
    lte.scan.found_count = 1;
    m = build(796000000u);
    check_int("one row", m.found_count, 1);
    check_int("its identity", m.found[0].pci, 28);
    check_int("and its channel", (long)m.found[0].earfcn, 6200);

    check_str("with rows there is no note to make", m.scan_note, "");

    lte.scan.found_count = LTE_SCAN_MAX_FOUND * 2;
    m = build(796000000u);
    check_int("an impossible count is clamped", m.found_count,
              LTE_VIEW_MODEL_FOUND);
    lte.scan.found_count = -3;
    m = build(796000000u);
    check_int("and a negative one carries nothing", m.found_count, 0);
}

/*
 * Four things produce an empty scan table and they are not the same answer.
 * A reader seeing no rows and no sentence cannot tell "nobody pressed Scan"
 * from "everything found was withdrawn on its second look", and it is the
 * second that is a finding about the band.
 */
static void test_an_empty_table_says_why(void) {
    struct lte_view_model m;

    blank();
    m = build(796000000u);
    check_str("idle, with a receiver", m.scan_note, "Press Scan band.");

    ctx.receiver_mode = 0;
    m = build(796000000u);
    check_str("a capture holds one tuning", m.scan_note,
              "A scan needs a live receiver; a capture holds one tuning.");

    ctx.receiver_mode = 1;
    lte.scan.confirm_dropped = 3;
    m = build(796000000u);
    check_str("looked at, and everything withdrawn", m.scan_note,
              "Nothing held up: every candidate failed its second look.");

    lte.scan.running = 1;
    m = build(796000000u);
    check_str("still going", m.scan_note, "Looking...");
    check_true("and the progress line is empty without a band",
               m.scan_progress[0] == '\0');

    /* The confirmation pass counts its own entries, because the sweep's line
       would sit at 100% and read as a scan that had hung. */
    lte.scan.confirming = 1;
    lte.scan.confirm_index = 2;
    lte.scan.confirm_total = 9;
    m = build(796000000u);
    check_str("the pass says where it is", m.scan_progress,
              "confirming 6 of 9   3 dropped");
}

int main(void) {
    test_no_cell_carries_no_cell();
    test_the_crystal_error_is_the_receivers();
    test_statistics_travel_only_when_they_belong_to_a_cell();
    test_the_broadcast_reports_what_is_transmitted();
    test_findings_come_already_worded();
    test_the_scan_rows_travel_clamped();
    test_an_empty_table_says_why();
    return check_report("what the LTE screen says, decided without a screen");
}
