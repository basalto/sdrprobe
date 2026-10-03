#include "check.h"

#include "model/gsm_view_model.h"
#include "runtime/app.h"
#include "runtime/scan_plan.h"

#include <string.h>

/*
 * What the GSM screen says, decided without a screen.
 *
 * The two readouts it is about were `DrawText` calls in `view_gsm.c`: four
 * possible SCH sentences and three possible BCCH ones, each with a different
 * set of fields attached, chosen inside the drawing where nothing could
 * reach them. That is the decision ADR-0012 asks for a name and a check, and
 * this is it.
 *
 * Two plain structs in, one out -- no `struct app`, so this links `-lm`
 * alone (`.scratch/layer-boundaries/issues/03-*`).
 */

static struct gsm_view gsm;
static struct band_scan scan;
static struct sdr_signal_stats stats;

static struct gsm_view_model build(int stats_ready, int recording,
                                   int receiver_mode) {
    struct gsm_view_model out;

    gsm_view_model_build(&gsm, &scan, &stats, stats_ready, recording,
                         receiver_mode, &out);
    return out;
}

static void blank(void) {
    memset(&gsm, 0, sizeof(gsm));
    memset(&scan, 0, sizeof(scan));
    memset(&stats, 0, sizeof(stats));
}

/*
 * The SCH line's four sentences, and the order they are decided in.
 *
 * The precedence is the window's own and it is not arbitrary: a recording in
 * progress *replaces* the readout, so it outranks "searching"; and while a
 * scan is walking the band the chart underneath belongs to the scan, so
 * nothing is said at all even though a channel is selected.
 */
static void test_the_sch_line_says_one_of_four(void) {
    struct gsm_view_model m;

    blank();
    m = build(0, 0, 1);
    check_str("no channel chosen: idle", gsm_sch_reading_name(m.sch), "idle");

    gsm.selected_arfcn = 69;
    m = build(0, 0, 1);
    check_str("a channel and a receiver: searching",
              gsm_sch_reading_name(m.sch), "searching");

    m = build(0, 0, 0);
    check_str("a capture that has decoded nothing says nothing",
              gsm_sch_reading_name(m.sch), "idle");

    m = build(0, 1, 1);
    check_str("a recording replaces the readout",
              gsm_sch_reading_name(m.sch), "recording");

    gsm.session.sch_valid = 1;
    m = build(0, 1, 1);
    check_str("and a decode outranks even the recording",
              gsm_sch_reading_name(m.sch), "decoded");

    scan.running = 1;
    m = build(0, 0, 1);
    check_str("while a scan walks the band, the screen is the scan's",
              gsm_sch_reading_name(m.sch), "idle");
}

/*
 * The BCCH line, and the distinction that is the reason it has three
 * sentences rather than two.
 *
 * The broadcast channel occupies frames 2 to 5 of the 51-multiframe, so only
 * the SCH at frame 1 is followed by one. Four decodes in five have nothing
 * due and nothing wrong. Collapsing "missed" into "waiting" would leave a
 * reader unable to tell a weak signal from an ordinary position in the
 * multiframe -- which is the whole question that line answers.
 */
static void test_waiting_and_missed_are_different_facts(void) {
    struct gsm_view_model m;

    blank();
    m = build(0, 0, 1);
    check_str("no SCH, so the question does not arise",
              gsm_bcch_reading_name(m.bcch), "none");

    gsm.selected_arfcn = 69;
    gsm.session.sch_valid = 1;
    /* 1053203 is not one a block follows; 1053202 -- the frame the committed
       capture decodes, and the one on the GSM screenshot -- is. That is not a
       coincidence to be smoothed over: the screenshot shows a BCCH line at
       exactly that frame, which is what makes these two the right numbers to
       assert on. The first draft of this had them the wrong way round and
       said "1053202 % 51 == 10"; it is 1. */
    gsm.session.sch.frame_number = 1053203;
    m = build(0, 0, 1);
    check_int("this frame is not one a block follows",
              gsm.session.sch.frame_number % 51 == 1, 0);
    check_str("so nothing is wrong: waiting",
              gsm_bcch_reading_name(m.bcch), "waiting");

    gsm.session.sch.frame_number = 1053202;
    m = build(0, 0, 1);
    check_int("and this one is", gsm.session.sch.frame_number % 51 == 1, 1);
    check_str("a frame a block was due on, with none read: missed",
              gsm_bcch_reading_name(m.bcch), "missed");

    gsm.session.cell.blocks = 1;
    m = build(0, 0, 1);
    check_str("and once one is read: read",
              gsm_bcch_reading_name(m.bcch), "read");
}

/*
 * Fields belong to the sentence carrying them, and are zero otherwise.
 *
 * This is the fault a reader would never question: last minute's cell shown
 * under this minute's heading. The window gets it right by only running the
 * `snprintf` inside the branch; a model has to zero them deliberately.
 */
static void test_stale_fields_do_not_survive(void) {
    struct gsm_view_model m;

    blank();
    gsm.selected_arfcn = 69;
    gsm.session.sch_valid = 1;
    gsm.session.sch.bsic = 59;
    gsm.session.sch.ncc = 7;
    gsm.session.sch.bcc = 3;
    gsm.session.sch.confidence = 1.0f;
    gsm.session.cell.blocks = 2;
    gsm.session.cell.have_lai = 1;
    gsm.session.cell.mcc = 268;
    gsm.session.cell.mnc = 3;
    gsm.session.cell.mnc_digits = 2;
    gsm.session.cell.lac = 4010;
    gsm.session.cell.have_cell_id = 1;
    gsm.session.cell.cell_id = 5131;
    gsm.session.cell.neighbour_count = 2;
    gsm.session.cell.neighbours[0] = 50;
    gsm.session.cell.neighbours[1] = 51;

    m = build(0, 0, 1);
    check_int("BSIC passes through", m.bsic, 59);
    check_int("MCC passes through", m.mcc, 268);
    check_int("the cell identity passes through", m.cell_id, 5131);
    check_int("the first neighbour passes through", m.neighbours[0], 50);
    check_int("and the count", m.neighbour_count, 2);

    /* Now the decode goes away -- the cell's own fields are still sitting in
       the session, exactly as they would be on air. */
    gsm.session.sch_valid = 0;
    m = build(0, 0, 1);
    check_str("the SCH line is searching again",
              gsm_sch_reading_name(m.sch), "searching");
    check_int("and the BSIC is not last minute's", m.bsic, 0);
    check_int("nor the MCC", m.mcc, 0);
    check_int("nor the cell identity", m.cell_id, 0);
    check_int("nor a neighbour", m.neighbours[0], 0);
}

/* `[T1 JUMPED]` in the window: T1 advances once per 1326 frames, so a jump
   is a decode that cannot be right. It travels as the fact, not the text. */
static void test_an_implausible_frame_number_travels(void) {
    struct gsm_view_model m;

    blank();
    gsm.selected_arfcn = 69;
    gsm.session.sch_valid = 1;
    m = build(0, 0, 1);
    check_int("an ordinary decode is plausible", m.implausible, 0);

    gsm.session.continuity.implausible = 1;
    m = build(0, 0, 1);
    check_int("and a jumped T1 says so", m.implausible, 1);
}

/*
 * The Channel Power Scan, and `have_scan` -- which is what tells "the band is
 * quiet" from "nobody has looked". `SCAN_SENTINEL_DBFS` is the unvisited
 * marker, and a chart drawn without that distinction shows a flat floor
 * either way.
 */
static void test_an_unvisited_band_is_not_a_quiet_one(void) {
    struct gsm_view_model m;
    int i;

    blank();
    for (i = 0; i < GSM_VIEW_MODEL_CHANNELS; i++)
        scan.power[i] = SCAN_SENTINEL_DBFS;
    m = build(0, 0, 1);
    check_int("nothing visited yet", m.have_scan, 0);

    scan.power[69] = -42.0f;
    scan.bcch_conf[69] = 0.9f;
    m = build(0, 0, 1);
    check_int("one channel measured is a scan", m.have_scan, 1);
    check_close("its power passes through", m.power[69], -42.0, 1e-6);
    check_close("and its BCCH confidence", m.bcch_confidence[69], 0.9, 1e-6);
    check_close("an unvisited neighbour keeps the sentinel", m.power[70],
                SCAN_SENTINEL_DBFS, 1e-6);
}

/* The header's statistics, and the flag that says whether to believe them. */
static void test_the_signal_statistics(void) {
    struct gsm_view_model m;

    blank();
    stats.snr_db = 7.5f;
    m = build(0, 0, 1);
    check_int("not ready, so not carried", m.signal_stats_ready, 0);
    check_close("and zeroed rather than stale", m.signal_stats.snr_db, 0.0,
                1e-6);

    m = build(1, 0, 1);
    check_int("ready", m.signal_stats_ready, 1);
    check_close("and the SNR passes through", m.signal_stats.snr_db, 7.5,
                1e-6);
}

/* Every name the wire can carry, walked rather than sampled: a value added
   to either enum without a name here falls through to the first, and this is
   what says so. Names, never ordinals (`web-visualization/15`). */
static void test_every_reading_has_a_name(void) {
    check_str("idle", gsm_sch_reading_name(GSM_SCH_IDLE), "idle");
    check_str("searching", gsm_sch_reading_name(GSM_SCH_SEARCHING),
              "searching");
    check_str("recording", gsm_sch_reading_name(GSM_SCH_RECORDING),
              "recording");
    check_str("decoded", gsm_sch_reading_name(GSM_SCH_DECODED), "decoded");

    check_str("none", gsm_bcch_reading_name(GSM_BCCH_NONE), "none");
    check_str("waiting", gsm_bcch_reading_name(GSM_BCCH_WAITING), "waiting");
    check_str("missed", gsm_bcch_reading_name(GSM_BCCH_MISSED), "missed");
    check_str("read", gsm_bcch_reading_name(GSM_BCCH_READ), "read");
}

/* The analysis charts behind "View: Burst": the SCH burst's correlation,
   soft magnitudes and phase, plus the constellation projected onto the unit
   circle (diff_im as x, -diff_re as y). All from session.sch_symbols. */
static void test_the_analysis_charts_are_carried(void) {
    struct gsm_view_model m;
    int i;

    blank();
    gsm.session.sch_valid = 1;
    gsm.session.sch_symbols.count = 60;
    for (i = 0; i < 60; i++) {
        gsm.session.sch_symbols.corr[i] = (float)i;
        gsm.session.sch_symbols.soft_mag[i] = 2.0f;
        gsm.session.sch_symbols.phase[i] = (float)i * 0.1f;
        gsm.session.sch_symbols.diff_im[i] = 3.0f;   /* x */
        gsm.session.sch_symbols.diff_re[i] = 0.0f;   /* -> y 0 */
    }

    m = build(0, 0, 1);

    check_int("the SCH charts are carried", m.sch_valid, 1);
    check_int("the count is carried", m.sch_count, 60);
    check_close("the correlation is carried", m.corr[3], 3.0, 1e-6);
    check_close("the soft magnitude is carried", m.soft_mag[0], 2.0, 1e-6);
    check_close("the phase is carried", m.phase[10], 1.0, 1e-5);
    check_int("the constellation has a point per symbol", m.scatter_count, 60);
    check_close("projected onto the unit circle: x is +1",
                m.scatter_x[0], 1.0, 1e-6);
    check_close("and y is 0", m.scatter_y[0], 0.0, 1e-6);
}

int main(void) {
    test_the_sch_line_says_one_of_four();
    test_the_analysis_charts_are_carried();
    test_waiting_and_missed_are_different_facts();
    test_stale_fields_do_not_survive();
    test_an_implausible_frame_number_travels();
    test_an_unvisited_band_is_not_a_quiet_one();
    test_the_signal_statistics();
    test_every_reading_has_a_name();
    return check_report("what the GSM screen says, decided without a screen");
}
