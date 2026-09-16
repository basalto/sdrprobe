#include "check.h"

#include "app.h"
#include "frame_advance.h"

#include <string.h>

/*
 * frame_advance() is a dispatcher, not a computation: it decides which of
 * about twenty functions runs this block, under what combination of tab,
 * decode kind, whether calibration is open and whether a block arrived. That
 * dispatch is the "decision" ADR-0012 asks for a name and a check, and this
 * is it. Each callee below is a fake that only records that it ran and with
 * what arguments -- their own DSP is proven by their own checks (check-lte-dsp,
 * check-adsb-dsp, and so on), and re-proving it here would test nothing this
 * file owns.
 *
 * Compiled against `-lm` alone: no fake calls a raylib function, so this
 * check pins the property the ticket asked for directly, by construction --
 * if `frame_advance.c` itself called one, the link (not just this file)
 * would fail to resolve it.
 */

static struct {
    int process_block_calls;
    double process_block_now;
    int process_block_returns;

    int consume_latest_calls;
    int consume_latest_returns;

    int decay_calls;
    int advance_waterfall_calls;
    int update_scan_calls;
    int update_calibration_calls;

    int update_startup_calls;
    int update_startup_have_block;

    int update_survey_calls;
    int update_adsb_calls;
    int update_gsm_sch_calls;
    int update_tetra_calls;
    int update_srd_calls;

    int update_lte_scan_calls;
    int update_lte_scan_have_block;
    int update_lte_calls;

    int update_fm_scan_calls;
    int update_fm_calls;
    int update_fm_audio_calls;

    int update_drift_calls;
    int update_drift_have_block;

    int advance_scatter_calls;
    int advance_scatter_insert;
} fake;

static void fake_reset(void) {
    memset(&fake, 0, sizeof(fake));
    fake.consume_latest_returns = 1;
    fake.process_block_returns = 1;
}

int consume_latest(struct acquisition *acq, struct slot_snapshot *snapshot) {
    (void)acq;
    memset(snapshot, 0, sizeof(*snapshot));
    fake.consume_latest_calls++;
    return fake.consume_latest_returns;
}

void decay_spectrum_peak(struct app *app, double now) {
    (void)app;
    (void)now;
    fake.decay_calls++;
}

int process_block(struct app *app, double now) {
    (void)app;
    fake.process_block_calls++;
    fake.process_block_now = now;
    return fake.process_block_returns;
}

void advance_waterfall_row(struct app *app) {
    (void)app;
    fake.advance_waterfall_calls++;
}

void update_scan(struct app *app) {
    (void)app;
    fake.update_scan_calls++;
}

void update_calibration_measurement(struct app *app) {
    (void)app;
    fake.update_calibration_calls++;
}

void update_startup(struct app *app, int have_block) {
    (void)app;
    fake.update_startup_calls++;
    fake.update_startup_have_block = have_block;
}

void update_survey(struct app *app, double now, int spectrum_updated) {
    (void)app;
    (void)now;
    (void)spectrum_updated;
    fake.update_survey_calls++;
}

void update_adsb(struct app *app, double now) {
    (void)app;
    (void)now;
    fake.update_adsb_calls++;
}

void update_gsm_sch(struct app *app, double now) {
    (void)app;
    (void)now;
    fake.update_gsm_sch_calls++;
}

void update_tetra(struct app *app, double now) {
    (void)app;
    (void)now;
    fake.update_tetra_calls++;
}

void update_srd(struct app *app, double now) {
    (void)app;
    (void)now;
    fake.update_srd_calls++;
}

void update_lte_scan(struct app *app, double now, int have_block) {
    (void)app;
    (void)now;
    fake.update_lte_scan_calls++;
    fake.update_lte_scan_have_block = have_block;
}

void update_lte(struct app *app, double now) {
    (void)app;
    (void)now;
    fake.update_lte_calls++;
}

void update_fm_scan(struct app *app, double now, int have_block) {
    (void)app;
    (void)now;
    (void)have_block;
    fake.update_fm_scan_calls++;
}

void update_fm(struct app *app, double now) {
    (void)app;
    (void)now;
    fake.update_fm_calls++;
}

void update_fm_audio(struct app *app) {
    (void)app;
    fake.update_fm_audio_calls++;
}

void update_drift_check(struct app *app, int have_block) {
    (void)app;
    fake.update_drift_calls++;
    fake.update_drift_have_block = have_block;
}

void advance_scatter_history(struct app *app, double now, int insert) {
    (void)app;
    (void)now;
    fake.advance_scatter_calls++;
    fake.advance_scatter_insert = insert;
}

/*
 * `struct app` is nearly 9 MB (the signal frame's several 131072-float
 * arrays), so it is never a stack local or a by-value return here -- each
 * test keeps its own `static struct app` and zeroes it explicitly.
 */
static void zero_app(struct app *app) {
    memset(app, 0, sizeof(*app));
}

/* A block with no new sample data: process_block never runs, nothing gated
   on `have_new` runs, and the always-on calls still happen every frame. */
static void test_no_new_block(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;
    int spectrum_updated;

    fake_reset();
    fake.consume_latest_returns = 0;
    app.tab = TAB_SCOPE;

    spectrum_updated = frame_advance(&app, &snapshot, 12.5);

    check_int("no block: consume_latest still asked", fake.consume_latest_calls, 1);
    check_int("no block: process_block skipped", fake.process_block_calls, 0);
    check_int("no block: spectrum_updated is false", spectrum_updated, 0);
    check_int("no block: waterfall row skipped", fake.advance_waterfall_calls, 0);
    check_int("no block: scan skipped", fake.update_scan_calls, 0);
    check_int("no block: calibration measurement skipped",
              fake.update_calibration_calls, 0);
    check_int("no block: startup still ticks, told nothing arrived",
              fake.update_startup_have_block, 0);
    check_int("no block: drift check still ticks, told nothing arrived",
              fake.update_drift_have_block, 0);
    check_int("no block: scatter history still ages",
              fake.advance_scatter_calls, 1);
    check_int("no block: scatter insert is false",
              fake.advance_scatter_insert, 0);
}

/* A block arrives and produces a spectrum: the waterfall row, the scan and
   the calibration measurement all run, gated together on spectrum_updated. */
static void test_spectrum_updated_gates_three_calls(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_SCOPE;

    frame_advance(&app, &snapshot, 1.0);

    check_int("spectrum updated: waterfall row advances",
              fake.advance_waterfall_calls, 1);
    check_int("spectrum updated: scan runs", fake.update_scan_calls, 1);
    check_int("spectrum updated: calibration measurement runs",
              fake.update_calibration_calls, 1);
}

/* A block arrives but yields no spectrum (process_block's own refusal, e.g.
   the block ran past the end of what it needed): the three geometry-gated
   calls are skipped, but a decode gated only on `have_new` still runs. */
static void test_have_new_without_spectrum_updated(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;
    int spectrum_updated;

    fake_reset();
    fake.process_block_returns = 0;
    app.tab = TAB_DECODE;
    app.decode = DECODE_ADSB;

    spectrum_updated = frame_advance(&app, &snapshot, 2.0);

    check_int("no spectrum: still counted as a block", fake.process_block_calls, 1);
    check_int("no spectrum: spectrum_updated is false", spectrum_updated, 0);
    check_int("no spectrum: waterfall row skipped", fake.advance_waterfall_calls, 0);
    check_int("no spectrum: scan skipped", fake.update_scan_calls, 0);
    check_int("no spectrum: calibration measurement skipped",
              fake.update_calibration_calls, 0);
    check_int("no spectrum: ADS-B still decodes -- it only needs have_new",
              fake.update_adsb_calls, 1);
}

/* Each decode technology fires only on its own tab/decode pair, and only
   while calibration is not open -- except FM, which runs regardless. That
   asymmetry is in the code this ticket copied verbatim and is pinned here
   rather than silently changed by the refactor. */
static void test_decode_dispatch_by_tab_and_kind(void) {
    static const struct {
        enum decode_kind decode;
        const char *name;
    } cases[] = {
        { DECODE_ADSB, "ADS-B" },
        { DECODE_GSM, "GSM" },
        { DECODE_TETRA, "TETRA" },
        { DECODE_SRD, "SRD" },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        static struct app app;
        struct slot_snapshot snapshot;
        int calls;

        zero_app(&app);
        fake_reset();
        app.tab = TAB_DECODE;
        app.decode = cases[i].decode;
        frame_advance(&app, &snapshot, 3.0);

        calls = fake.update_adsb_calls + fake.update_gsm_sch_calls +
                fake.update_tetra_calls + fake.update_srd_calls;
        check_int(cases[i].name, calls, 1);
    }

    /* Calibration open suppresses every one of those four -- and LTE, which
       is gated the same way -- but not FM. */
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        static struct app app;
        struct slot_snapshot snapshot;
        int calls;

        zero_app(&app);
        fake_reset();
        app.tab = TAB_DECODE;
        app.decode = cases[i].decode;
        app.cal.open = 1;
        frame_advance(&app, &snapshot, 3.0);

        calls = fake.update_adsb_calls + fake.update_gsm_sch_calls +
                fake.update_tetra_calls + fake.update_srd_calls;
        check_int(cases[i].name, calls, 0);
    }
}

/* LTE dispatches its scan every frame it is the active decode (the scan
   drives the tuning and spends most of its time settling) and only calls
   the cell search itself once a block has arrived and no scan is running. */
static void test_lte_dispatch(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_DECODE;
    app.decode = DECODE_LTE;
    app.lte.scan.running = 1;
    frame_advance(&app, &snapshot, 4.0);
    check_int("LTE: scan step runs while the scan is running",
              fake.update_lte_scan_calls, 1);
    check_int("LTE: cell search does not run while the scan is running",
              fake.update_lte_calls, 0);

    fake_reset();
    app.lte.scan.running = 0;
    frame_advance(&app, &snapshot, 4.0);
    check_int("LTE: cell search runs once the scan is not",
              fake.update_lte_calls, 1);

    fake_reset();
    app.cal.open = 1;
    frame_advance(&app, &snapshot, 4.0);
    check_int("LTE: neither runs while calibration is open",
              fake.update_lte_scan_calls + fake.update_lte_calls, 0);
}

/* FM is the one technology that keeps running while calibration is open --
   the calibration overlay borrows the receiver but does not stop the FM
   chain the way it stops every other decode. Preserved exactly. */
static void test_fm_dispatch_ignores_calibration(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_DECODE;
    app.decode = DECODE_FM;
    app.cal.open = 1;
    frame_advance(&app, &snapshot, 5.0);

    check_int("FM: scan still runs with calibration open",
              fake.update_fm_scan_calls, 1);
    check_int("FM: cell decode still runs with calibration open",
              fake.update_fm_calls, 1);
    check_int("FM: audio still runs with calibration open",
              fake.update_fm_audio_calls, 1);
}

/* The survey only ticks on its own tab, and not while calibration or the
   startup form is on top of it. */
static void test_survey_dispatch(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_SURVEY;
    frame_advance(&app, &snapshot, 6.0);
    check_int("survey: ticks on its own tab", fake.update_survey_calls, 1);

    fake_reset();
    app.cal.open = 1;
    frame_advance(&app, &snapshot, 6.0);
    check_int("survey: not while calibration is open",
              fake.update_survey_calls, 0);

    fake_reset();
    app.cal.open = 0;
    app.startup.open = 1;
    frame_advance(&app, &snapshot, 6.0);
    check_int("survey: not while the startup form is open",
              fake.update_survey_calls, 0);
}

/* The scatter history only takes a new point on the Scope tab, in the
   scatter view, with calibration closed and a block having arrived. */
static void test_scatter_insert_condition(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_SCOPE;
    app.view = VIEW_SCATTER;
    frame_advance(&app, &snapshot, 7.0);
    check_int("scatter: inserts on its own view", fake.advance_scatter_insert, 1);

    fake_reset();
    app.view = VIEW_SPECTRUM;
    frame_advance(&app, &snapshot, 7.0);
    check_int("scatter: does not insert off its own view",
              fake.advance_scatter_insert, 0);

    fake_reset();
    app.view = VIEW_SCATTER;
    app.cal.open = 1;
    frame_advance(&app, &snapshot, 7.0);
    check_int("scatter: does not insert while calibration is open",
              fake.advance_scatter_insert, 0);
}

/* `now` reaches process_block unmodified: the caller's clock, never read
   from inside. */
static void test_now_passes_through(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_SCOPE;
    frame_advance(&app, &snapshot, 123.5);
    check_close("now reaches process_block unmodified", fake.process_block_now,
                123.5, 1e-9);
}

int main(void) {
    test_no_new_block();
    test_spectrum_updated_gates_three_calls();
    test_have_new_without_spectrum_updated();
    test_decode_dispatch_by_tab_and_kind();
    test_lte_dispatch();
    test_fm_dispatch_ignores_calibration();
    test_survey_dispatch();
    test_scatter_insert_condition();
    test_now_passes_through();
    return check_report("the per-block dispatch, with every callee faked");
}
