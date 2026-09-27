#include "check.h"

#include "runtime/app.h"
#include "runtime/frame_advance.h"

#include <math.h>
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
    int process_block_fft_size;
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

int process_block(struct app *app, double now, int fft_size) {
    (void)app;
    fake.process_block_calls++;
    fake.process_block_now = now;
    fake.process_block_fft_size = fft_size;
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

void update_fm_audio(struct app *app) {
    (void)app;
    fake.update_fm_audio_calls++;
}

/*
 * The three `fm_runtime.c` reaches for. They are `app_runtime.c`'s, and that
 * file pulls in acquisition, the backends, the config and the installation --
 * the whole program, to check a dispatcher. `update_fm()` itself touches none
 * of them (only `fm_tune()` and the scan's steps do), so a fake that records
 * nothing is honest here: if the FM path ever starts retuning per block, this
 * suite stops linking rather than quietly growing a receiver.
 */
int receiver_borrow_at(struct app *app, struct receiver_lease_token *token,
                       uint32_t frequency_hz) {
    (void)app; (void)token; (void)frequency_hz;
    return 0;
}

int receiver_return(struct app *app, struct receiver_lease_token *token) {
    (void)app; (void)token;
    return 0;
}

int retune_receiver(struct app *app, uint32_t frequency, int ppm) {
    (void)app; (void)frequency; (void)ppm;
    return 0;
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

/*
 * A carrier with a 19 kHz pilot on it, straight into the frame's I/Q.
 *
 * `process_block()` is a fake here, so nothing fills these arrays for us --
 * which is what makes this a check of the *dispatch composed with a real
 * callee* rather than of the DSP: the samples are handed in, and the only
 * question is whether `frame_advance()` gets them to `update_fm()`.
 *
 * FM is phase modulation, so a tone at 19 kHz is
 * `phase(t) = beta * sin(2*pi*19000*t)`; the discriminator differentiates it
 * back into that tone. `beta` is the deviation over the modulating frequency,
 * and a real pilot runs at about 9% of 75 kHz deviation -- 6.75 kHz, so 0.355
 * at 19 kHz. Being roughly right matters: far too small and the pilot is
 * under the quantisation, far too large and the discriminator wraps.
 */
#define PILOT_TEST_RATE 2048000u

/*
 * `pilot_t` is the sample clock and it has to be one: the pilot is a loop
 * tracking a continuous tone, so each block must carry on where the last
 * left off. The first version of this took its phase from
 * `session.blocks_seen`, which does not advance until the pilot has locked
 * -- so every block regenerated the same samples from t = 0, the phase reset
 * twelve times, and the loop never converged. Measured directly against
 * `fm_rds_front_feed()`: fed continuously this locks on **block 8** of
 * 65536 samples and emits 113 baseband samples, at 19000.05 Hz.
 */
static long pilot_t;

static void fill_pilot_carrier(struct app *app, size_t pairs) {
    const double rate = (double)PILOT_TEST_RATE;
    const double beta = 6750.0 / 19000.0;
    size_t i;

    if (pairs > SAMPLE_BLOCK_PAIRS)
        pairs = SAMPLE_BLOCK_PAIRS;
    for (i = 0; i < pairs; i++, pilot_t++) {
        double phase = beta * sin(2.0 * M_PI * 19000.0 * (double)pilot_t / rate);

        app->frame.i_samples[i] = (float)cos(phase);
        app->frame.q_samples[i] = (float)sin(phase);
    }
    app->frame.pair_count = pairs;
    app->frame.have_samples = 1;
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

    spectrum_updated = frame_advance(&app, &snapshot, 12.5, 2048);

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

    frame_advance(&app, &snapshot, 1.0, 2048);

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

    spectrum_updated = frame_advance(&app, &snapshot, 2.0, 2048);

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
        frame_advance(&app, &snapshot, 3.0, 2048);

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
        frame_advance(&app, &snapshot, 3.0, 2048);

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
    frame_advance(&app, &snapshot, 4.0, 2048);
    check_int("LTE: scan step runs while the scan is running",
              fake.update_lte_scan_calls, 1);
    check_int("LTE: cell search does not run while the scan is running",
              fake.update_lte_calls, 0);

    fake_reset();
    app.lte.scan.running = 0;
    frame_advance(&app, &snapshot, 4.0, 2048);
    check_int("LTE: cell search runs once the scan is not",
              fake.update_lte_calls, 1);

    fake_reset();
    app.cal.open = 1;
    frame_advance(&app, &snapshot, 4.0, 2048);
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
    pilot_t = 0;
    app.tab = TAB_DECODE;
    app.decode = DECODE_FM;
    app.applied.sample_rate_hz = PILOT_TEST_RATE;
    app.cal.open = 1;
    fill_pilot_carrier(&app, 65536);
    frame_advance(&app, &snapshot, 5.0, 2048);

    /*
     * The real `update_fm_scan()` and `update_fm()` run here, not fakes --
     * so what this asserts is what the step *did*, which is the whole
     * difference between checking a dispatch table and checking that the
     * dispatch reached something that works
     * (`.scratch/layer-boundaries/issues/04-*`, item 2).
     *
     * The multiplex spectrum is the cheapest thing to observe that only a
     * real run can produce: `update_fm_flush()` fills it from the
     * discriminator's output, so a dispatcher that called `update_fm()`
     * with a frame it had not filled, or with the wrong `pair_count`, would
     * leave it at zero while a call counter read 1.
     *
     * Not `blocks_seen`, which is **not** a count of blocks fed --
     * `fm_session_feed()` increments it only once baseband comes out, which
     * needs the pilot locked. That is what the next test is for, and
     * learning it here cost a wrong claim.
     */
    check_true("FM: the block reached the real decode with calibration open",
               app.fm.spectrum_bins > 0);
    /*
     * And the sound is *not* pumped from here at all any more. It feeds a
     * raylib `AudioStream`, only a window has one, and this step is what
     * `headless` and `server` drive -- so it moved into `run_gui()`'s own
     * frame loop (layer-boundaries ticket 02, item 1). The stub stays so
     * this asserts the absence rather than merely not mentioning it.
     */
    check_int("FM: the sound card is not pumped by the shared step",
              fake.update_fm_audio_calls, 0);
}

/*
 * The pilot locks, over as many blocks as it takes -- and that is the
 * assertion a fake could never make.
 *
 * `fm_rds_front_feed()` emits **nothing** before the pilot locks: without it
 * there is no carrier to mix by and no clock to emit on, so `bb_count > 0` is
 * the lock, reached through the whole path `frame_advance()` is responsible
 * for. `fm_pilot_locked()` says the same thing directly and both are checked,
 * because they could disagree only if the front end started inventing
 * baseband, which is exactly the fault worth catching.
 *
 * And the gate that stops it: while the band scan is running it owns the
 * receiver and feeds the chain itself, so `frame_advance()` must *not* also
 * feed it. A call count showed that as a zero; this shows it as a pilot that
 * never locks on a signal that plainly carries one.
 */
static void test_fm_pilot_locks_through_the_dispatch(void) {
    static struct app app;
    struct slot_snapshot snapshot;
    int block;

    zero_app(&app);
    fake_reset();
    pilot_t = 0;
    app.tab = TAB_DECODE;
    app.decode = DECODE_FM;
    app.applied.sample_rate_hz = PILOT_TEST_RATE;

    for (block = 0; block < 16; block++) {
        fill_pilot_carrier(&app, 65536);
        frame_advance(&app, &snapshot, 1.0 + block, 2048);
    }

    check_true("FM: the pilot locked",
               fm_pilot_locked(&app.fm.session.front.pilot));
    check_true("FM: and baseband came out of it, which needs the lock",
               app.fm.session.bb_count > 0);
    check_true("FM: the loop settled on 19 kHz, not on something nearby",
               fabs(fm_pilot_hz(&app.fm.session.front.pilot) - 19000.0) < 5.0);

    /*
     * And with the band scan running, `frame_advance()` does not call
     * `update_fm()` at all -- the scan owns the receiver and feeds the chain
     * itself, on its own schedule, which is why the pilot still locks here.
     *
     * That was the opposite of the first claim written for this, which
     * asserted nothing reached the chain. `frame_advance()`'s own comment
     * says otherwise in as many words, and the check caught the reader
     * rather than the code. It is kept because it is a real guard: a scan
     * that stopped feeding would leave this unlocked, and a
     * `frame_advance()` that fed it *as well* would be a double feed nobody
     * else is looking for.
     */
    zero_app(&app);
    fake_reset();
    pilot_t = 0;
    app.tab = TAB_DECODE;
    app.decode = DECODE_FM;
    app.applied.sample_rate_hz = PILOT_TEST_RATE;
    app.fm.scan.running = 1;

    for (block = 0; block < 16; block++) {
        fill_pilot_carrier(&app, 65536);
        frame_advance(&app, &snapshot, 1.0 + block, 2048);
    }

    check_true("FM: a running scan feeds the chain itself",
               fm_pilot_locked(&app.fm.session.front.pilot));
}

/* The survey only ticks on its own tab, and not while calibration or the
   startup form is on top of it. */
static void test_survey_dispatch(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_SURVEY;
    frame_advance(&app, &snapshot, 6.0, 2048);
    check_int("survey: ticks on its own tab", fake.update_survey_calls, 1);

    fake_reset();
    app.cal.open = 1;
    frame_advance(&app, &snapshot, 6.0, 2048);
    check_int("survey: not while calibration is open",
              fake.update_survey_calls, 0);

    fake_reset();
    app.cal.open = 0;
    app.startup.open = 1;
    frame_advance(&app, &snapshot, 6.0, 2048);
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
    frame_advance(&app, &snapshot, 7.0, 2048);
    check_int("scatter: inserts on its own view", fake.advance_scatter_insert, 1);

    fake_reset();
    app.view = VIEW_SPECTRUM;
    frame_advance(&app, &snapshot, 7.0, 2048);
    check_int("scatter: does not insert off its own view",
              fake.advance_scatter_insert, 0);

    fake_reset();
    app.view = VIEW_SCATTER;
    app.cal.open = 1;
    frame_advance(&app, &snapshot, 7.0, 2048);
    check_int("scatter: does not insert while calibration is open",
              fake.advance_scatter_insert, 0);
}

/* `now` and the transform size both reach process_block unmodified: the
   caller's clock and the caller's choice, neither read from inside.

   The size matters as much as the clock and for the same reason. It used to
   be looked up inside `process_block()` by asking what was on screen, on
   `headless` and `server` runs too, where there is no screen to ask
   (layer-boundaries ticket 02, item 2). A check that only pinned `now`
   would not notice it being looked up again. */
static void test_now_and_size_pass_through(void) {
    static struct app app;
    zero_app(&app);
    struct slot_snapshot snapshot;

    fake_reset();
    app.tab = TAB_SCOPE;
    /* A size deliberately unlike any default, and unlike `app.sv.fft_size`,
       which is zero here -- so this cannot pass by the value being looked up
       from the app after all. */
    frame_advance(&app, &snapshot, 123.5, 4096);
    check_close("now reaches process_block unmodified", fake.process_block_now,
                123.5, 1e-9);
    check_int("and so does the transform size the caller chose",
              fake.process_block_fft_size, 4096);
    check_int("which is not what app.sv.fft_size holds", app.sv.fft_size, 0);
}

int main(void) {
    test_no_new_block();
    test_spectrum_updated_gates_three_calls();
    test_have_new_without_spectrum_updated();
    test_decode_dispatch_by_tab_and_kind();
    test_lte_dispatch();
    test_fm_dispatch_ignores_calibration();
    test_fm_pilot_locks_through_the_dispatch();
    test_survey_dispatch();
    test_scatter_insert_condition();
    test_now_and_size_pass_through();
    return check_report("the per-block dispatch, and FM through its real runtime");
}
