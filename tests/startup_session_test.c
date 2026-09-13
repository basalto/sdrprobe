/*
 * The startup sequence, driven with no window, no receiver and no capture.
 *
 * `.scratch/startup-installation/issues/01-startup-session-machine.md`. What
 * this has to reach is every transition: a GSM band with a broadcast carrier,
 * a GSM band with power and no broadcast carrier falling through to LTE, an
 * LTE scan that finds nothing, each of the gate's three refusals, and Skip
 * from every running phase.
 *
 * The one that matters most is the settle, because no capture retunes and so
 * nothing else in this repository can exercise it: a step's clock must start
 * when the tuner moved, not when the tuning was asked for. That is the fault
 * the survey extraction shipped with fifty-five suites green.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "sdr_dsp.h"
#include "startup_session.h"

#define PAIRS 65536
#define RATE 2000000.0

static float g_i[PAIRS];
static float g_q[PAIRS];
static float g_spectrum[SDR_DSP_FFT_SIZE];

/* A block that carries nothing: the spectrum flat at the floor and the
   samples silent. Every scan step over it finds no channel and no tone. */
static void quiet_block(struct startup_block *block, double centre_hz) {
    size_t i;

    for (i = 0; i < PAIRS; i++) {
        g_i[i] = 0.0f;
        g_q[i] = 0.0f;
    }
    for (i = 0; i < SDR_DSP_FFT_SIZE; i++)
        g_spectrum[i] = -120.0f;
    memset(block, 0, sizeof(*block));
    block->i_samples = g_i;
    block->q_samples = g_q;
    block->pair_count = PAIRS;
    block->spectrum = g_spectrum;
    block->centre_hz = centre_hz;
    block->sample_rate = RATE;
}

/*
 * A block carrying the FCCH tone of one ARFCN: an unmodulated carrier at
 * 67.708 kHz above the channel, which is what the detector is looking for and
 * what a real all-zeros burst is.
 */
static void fcch_block(struct startup_block *block, double centre_hz,
                       int arfcn, double ppm_error) {
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;
    double tone = channel * (1.0 + ppm_error / 1e6) - centre_hz +
                  GSM_FCCH_TONE_HZ;
    size_t i;

    quiet_block(block, centre_hz);
    for (i = 0; i < PAIRS; i++) {
        double phase = 2.0 * M_PI * tone * (double)i / RATE;
        g_i[i] = (float)(40.0 * cos(phase));
        g_q[i] = (float)(40.0 * sin(phase));
    }
    /* And a bin of power where that channel sits, so the scan's channel-power
       reducer has something to read. The tone alone would leave the spectrum
       at the floor, since this fixture does not transform anything. */
    for (i = 0; i < SDR_DSP_FFT_SIZE; i++) {
        double bin_hz = centre_hz - RATE / 2.0 +
                        RATE * (double)i / (double)SDR_DSP_FFT_SIZE;
        if (fabs(bin_hz - channel) < GSM900_ARFCN_SPACING_HZ / 2.0)
            g_spectrum[i] = -20.0f;
    }
}

/* Power on one channel and no tone anywhere, which is the fall-through
   condition and the case scan_choose() would get wrong. */
static void power_only_block(struct startup_block *block, double centre_hz,
                             int arfcn) {
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;
    size_t i;

    quiet_block(block, centre_hz);
    for (i = 0; i < SDR_DSP_FFT_SIZE; i++) {
        double bin_hz = centre_hz - RATE / 2.0 +
                        RATE * (double)i / (double)SDR_DSP_FFT_SIZE;
        if (fabs(bin_hz - channel) < GSM900_ARFCN_SPACING_HZ / 2.0)
            g_spectrum[i] = -10.0f;
    }
}

/*
 * Drive the machine for `seconds`, delivering a block every 65 ms and obeying
 * every retune it asks for -- which is what an adapter does.
 *
 * `retune_lag` is how long the tuner takes to report back. It is the whole
 * subject of test_the_settle_is_timed_from_the_tuning below.
 */
struct fake_receiver {
    double now;
    double centre_hz;
    double retune_lag;
    double retune_at;       /* when the pending retune reports, or 0 */
    uint32_t pending_hz;
    int retunes;
    int refuse;             /* refuse every tuning */
    void (*fill)(struct startup_block *, double, int);
    int fill_arfcn;
};

static void pump(struct startup_session *s, struct fake_receiver *rx,
                 double seconds) {
    struct startup_session_event ev;
    double until = rx->now + seconds;

    while (rx->now < until && startup_session_running(s)) {
        struct startup_block block;
        int have_block;

        /* A pending retune reports back once the tuner has caught up. */
        if (rx->retune_at > 0.0 && rx->now >= rx->retune_at) {
            rx->centre_hz = (double)rx->pending_hz;
            rx->retune_at = 0.0;
            startup_session_retuned(s, rx->now);
        }

        have_block = 1;
        if (rx->fill)
            rx->fill(&block, rx->centre_hz, rx->fill_arfcn);
        else
            quiet_block(&block, rx->centre_hz);

        startup_session_tick(s, &block, have_block, rx->now, &ev);
        if (ev.retune_hz) {
            rx->retunes++;
            if (rx->refuse) {
                startup_session_retune_failed(s, ev.retune_hz, "refused", &ev);
            } else {
                rx->pending_hz = ev.retune_hz;
                rx->retune_at = rx->now + rx->retune_lag;
            }
        }
        rx->now += 0.065;
    }
}

static void fill_quiet(struct startup_block *b, double centre, int arfcn) {
    (void)arfcn;
    quiet_block(b, centre);
}

static void fill_fcch(struct startup_block *b, double centre, int arfcn) {
    fcch_block(b, centre, arfcn, 0.0);
}

static void fill_power_only(struct startup_block *b, double centre,
                            int arfcn) {
    power_only_block(b, centre, arfcn);
}

/* ------------------------------------------------------------------ */

static void test_a_fresh_session_is_idle_and_committable(void) {
    struct startup_session s;

    startup_session_reset(&s);
    check_int("a fresh session is idle", s.phase, STARTUP_IDLE);
    check_int("and not running", startup_session_running(&s), 0);
    /*
     * Committable, and that is deliberate rather than incidental: a form with
     * no calibration behind it must not be held shut waiting for one. Every
     * skip condition in ticket 04 leaves the machine exactly here.
     */
    check_int("and may be committed", startup_session_may_commit(&s), 1);
}

static void test_it_begins_by_scanning_gsm(void) {
    struct startup_session s;
    struct startup_session_event ev;
    const struct lte_band *band = lte_band_for_number(20);

    check_int("begins", startup_session_begin(&s, RATE, 0, 1, band, 0.0, &ev),
              0);
    check_int("scanning GSM first", s.phase, STARTUP_SCAN_GSM);
    check_true("and asks for a tuning", ev.retune_hz > 0);
    check_true("inside GSM 900", (double)ev.retune_hz > SCAN_BAND_LOWER_HZ &&
                                 (double)ev.retune_hz < SCAN_BAND_UPPER_HZ);
    check_int("at whatever rate it is already using", (int)ev.retune_rate_hz,
              0);
    check_int("running", startup_session_running(&s), 1);
    check_int("and not committable while it runs",
              startup_session_may_commit(&s), 0);
}

static void test_an_unreachable_gsm_band_goes_straight_to_lte(void) {
    struct startup_session s;
    struct startup_session_event ev;
    const struct lte_band *band = lte_band_for_number(20);

    /* A tuner with a reach hole over GSM 900 gets the fall-back directly,
       rather than a scan that would tune where it cannot hear and report an
       absence it never tested. */
    check_int("begins", startup_session_begin(&s, RATE, 0, 0, band, 0.0, &ev),
              0);
    check_int("scanning LTE", s.phase, STARTUP_SCAN_LTE);
    check_int("at LTE's own rate", (int)ev.retune_rate_hz,
              (int)LTE_SAMPLE_RATE_HZ);
}

static void test_a_rate_too_narrow_for_a_step_goes_to_lte(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct scan_plan plan;

    /* Below this the accept window would be under 100 kHz, which is less than
       a channel, and scan_plan_make() refuses. */
    check_int("the plan refuses it",
              scan_plan_make(400000.0, &plan), SCAN_PLAN_RATE_TOO_LOW);
    check_int("begins", startup_session_begin(&s, 400000.0, 0, 1,
                                              lte_band_for_number(20), 0.0,
                                              &ev), 0);
    check_int("so it scans LTE instead", s.phase, STARTUP_SCAN_LTE);
}

static void test_nothing_to_measure_at_all_is_a_refusal(void) {
    struct startup_session s;
    struct startup_session_event ev;

    /* No GSM within reach and no LTE band either -- a capture, and a tuner
       too narrow for any band's whole downlink. */
    check_int("refuses", startup_session_begin(&s, RATE, 0, 0, NULL, 0.0, &ev),
              -1);
    check_int("failed", s.phase, STARTUP_FAILED);
    check_int("with nothing to calibrate against", s.reason,
              STARTUP_REASON_NO_CELL);
    check_int("and the form is released", startup_session_may_commit(&s), 1);
    check_true("saying so", s.status[0] != '\0');
}

/*
 * A tone is found, and then refused -- which is the on-air fault in miniature.
 *
 * This test used to assert that a channel carrying an FCCH-like tone was
 * chosen and measured. It was, and that was the bug: `GSM_FCCH_SEARCH_HALF_HZ`
 * is 50 kHz, so the detector reports any coherent line within that of where a
 * broadcast carrier's tone would be, and nothing about a high coherence says
 * the line is an FCCH.
 *
 * This fixture is the synthetic half of that, and it is the half that makes
 * the gate worth having: a bare tone, which no base station is, must be
 * refused. On air the gate has so far confirmed every channel it was given,
 * so a check that never showed it firing would leave a negative from it
 * worthless.
 *
 * The fixture here is that carrier exactly: a pure tone at the right offset
 * and nothing else. It must be **found by the scan and then turned down**,
 * because it cannot produce a parity-valid synchronisation burst.
 */
static void test_a_tone_is_not_a_broadcast_carrier(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct fake_receiver rx;
    const int arfcn = 40;

    memset(&rx, 0, sizeof(rx));
    rx.retune_lag = 0.1;
    rx.fill = fill_fcch;
    rx.fill_arfcn = arfcn;
    startup_session_begin(&s, RATE, 0, 1, lte_band_for_number(20), 0.0, &ev);
    rx.pending_hz = ev.retune_hz;
    rx.retune_at = rx.retune_lag;

    pump(&s, &rx, 40.0);

    check_int("the tone was seen and turned down", s.rejected[arfcn], 1);
    check_int("so nothing was measured against it", s.track.measurements, 0);
    check_true("and it did not end up locked on it", s.phase != STARTUP_LOCKED);
    check_true("having moved on rather than stopped at GSM",
               s.phase != STARTUP_MEASURE_GSM);
}

static void test_the_measured_correction_is_the_error_that_was_there(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct fake_receiver rx;
    struct startup_block block;
    const int arfcn = 40;
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;
    int i;

    /* Skip the scan: measure a tone that is 20 ppm high and check the
       suggestion comes back as the correction that removes it. */
    startup_session_reset(&s);
    memset(&rx, 0, sizeof(rx));
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = arfcn;
    s.expected_hz = (uint32_t)channel;
    s.applied_ppm = 0;
    s.tuned = 1;
    s.measure_budget = STARTUP_MEASURE_SECONDS;
    calibration_tracker_init(&s.track);

    for (i = 0; i < 40; i++) {
        fcch_block(&block, channel - 400000.0, arfcn, 20.0);
        startup_session_tick(&s, &block, 1, 0.5 * (double)i, &ev);
    }
    check_close("the residual is the error that was there",
                s.track.recent_center, 20.0, 1.0);
    /*
     * And the correction is its **negation**, which is the whole of
     * sdr_dsp_corrected_ppm(): `current - residual`. Written as +20 first, and
     * that was a wrong claim beside right arithmetic -- the same shape of
     * fault CLAUDE.md records for the reading-origin sign. A tone reading
     * 20 ppm high needs -20 applied to bring it back, and applying the
     * residual itself would drive the error to twice what it was.
     */
    check_int("and the suggestion is its negation", s.suggested_ppm, -20);
}

static void test_power_without_a_tone_falls_through_to_lte(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct fake_receiver rx;

    /*
     * The trap this whole fall-through exists for. scan_choose() would hand
     * back the loudest channel, which has no FCCH to calibrate against, and
     * the measurement would spend its entire budget failing to lock on it. A
     * detector cannot tell a tone from a broadcast carrier on its own.
     */
    memset(&rx, 0, sizeof(rx));
    rx.retune_lag = 0.1;
    rx.fill = fill_power_only;
    rx.fill_arfcn = 40;
    startup_session_begin(&s, RATE, 0, 1, lte_band_for_number(20), 0.0, &ev);
    rx.pending_hz = ev.retune_hz;
    rx.retune_at = rx.retune_lag;

    pump(&s, &rx, 25.0);

    check_int("no broadcast carrier was chosen", s.arfcn, 0);
    check_true("and it did not stop at GSM",
               s.phase != STARTUP_SCAN_GSM && s.phase != STARTUP_MEASURE_GSM);
}

static void test_an_empty_band_finds_nothing_and_says_so(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct fake_receiver rx;
    /* Band 1 is 599 channels; walking all of them takes far longer than this
       test runs, so use the narrowest band available to reach the end. */
    const struct lte_band *band = lte_band_for_number(20);

    memset(&rx, 0, sizeof(rx));
    rx.retune_lag = 0.05;
    rx.fill = fill_quiet;
    startup_session_begin(&s, RATE, 0, 0, band, 0.0, &ev);
    check_int("it is scanning LTE", s.phase, STARTUP_SCAN_LTE);
    rx.pending_hz = ev.retune_hz;
    rx.retune_at = rx.retune_lag;

    /* Not to the end of the band -- that is minutes -- but far enough to see
       it walking rather than stalling. */
    pump(&s, &rx, 20.0);
    check_true("it kept walking the band", rx.retunes > 5);
    check_true("finding nothing", s.earfcn == 0 || s.pci == 0);
    check_true("and it is still looking rather than claiming a cell",
               s.phase == STARTUP_SCAN_LTE || s.phase == STARTUP_FAILED);
}

static void test_the_settle_is_timed_from_the_tuning(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct startup_block block;
    double first_centre;

    /*
     * The fault the survey extraction shipped, and the one no capture can
     * reach: a retune flushes the pipeline and costs about a tenth of a
     * second, so a step timed from the *request* believes the blocks that
     * were already in flight -- the previous step's samples, folded into this
     * step's answer.
     *
     * Here the tuner reports back a full second late. If the step's clock ran
     * from the request, the settle and the probe would both have expired by
     * then and the machine would have advanced to the next step without ever
     * measuring this one.
     */
    startup_session_begin(&s, RATE, 0, 1, lte_band_for_number(20), 0.0, &ev);
    first_centre = (double)ev.retune_hz;
    check_true("it asked for a tuning", ev.retune_hz > 0);

    /* A whole second of blocks arrive before the tuner reports. Every one of
       them holds the previous tuning's samples. */
    quiet_block(&block, first_centre);
    startup_session_tick(&s, &block, 1, 0.5, &ev);
    check_int("no step was taken while the tuner was still moving", s.step, 0);
    startup_session_tick(&s, &block, 1, 1.0, &ev);
    check_int("still none", s.step, 0);
    check_int("and no tuning was asked for", (int)ev.retune_hz, 0);

    /* Now the tuner reports, at t = 1.0. */
    startup_session_retuned(&s, 1.0);

    /* A step is SETTLE + PROBE long. Just before that, from the *tuning*, it
       must still be on the same step. */
    startup_session_tick(&s, &block, 1,
                         1.0 + SCAN_STEP_SETTLE_SECONDS +
                             SCAN_STEP_PROBE_SECONDS - 0.05, &ev);
    check_int("the step is still the first one", s.step, 0);

    /* And just after, it moves on. */
    startup_session_tick(&s, &block, 1,
                         1.0 + SCAN_STEP_SETTLE_SECONDS +
                             SCAN_STEP_PROBE_SECONDS + 0.05, &ev);
    check_int("now it has stepped", s.step, 1);
    check_true("asking for the next tuning", ev.retune_hz > 0);
    check_true("which is a different one", (double)ev.retune_hz != first_centre);
}

static void test_a_measurement_does_not_believe_blocks_before_the_retune(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct startup_block block;
    const int arfcn = 40;
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;

    /*
     * The same rule on the other phase, and it matters more here: the survey
     * found that one stale block blurs into a spectrum average invisibly but
     * ruins a carrier measurement outright -- a 75 MHz clock harmonic read
     * 19 dB where the settled measurement reads 40.7.
     */
    startup_session_reset(&s);
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = arfcn;
    s.expected_hz = (uint32_t)channel;
    s.tuned = 0;
    s.measure_budget = STARTUP_MEASURE_SECONDS;
    calibration_tracker_init(&s.track);

    fcch_block(&block, channel - 400000.0, arfcn, 0.0);
    startup_session_tick(&s, &block, 1, 0.1, &ev);
    check_int("nothing was recorded before the tuner reported",
              s.track.measurements, 0);

    startup_session_retuned(&s, 0.2);
    startup_session_tick(&s, &block, 1, 0.3, &ev);
    check_int("and it records once it has", s.track.measurements, 1);
    check_int("saying so", ev.measured, 1);
}

static void test_the_budget_names_the_clause_that_was_not_met(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct startup_block block;
    const int arfcn = 40;
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;

    /* Too few measurements: a tone-free channel records nothing at all, so
       the budget expires with an empty buffer. */
    startup_session_reset(&s);
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = arfcn;
    s.expected_hz = (uint32_t)channel;
    s.tuned = 1;
    s.measure_started_at = 0.0;
    s.measure_budget = 5.0;
    calibration_tracker_init(&s.track);

    quiet_block(&block, channel - 400000.0);
    startup_session_tick(&s, &block, 1, 6.0, &ev);
    check_int("it gave up", s.phase, STARTUP_FAILED);
    check_int("naming the clause", s.reason, STARTUP_REASON_TOO_FEW);
    check_str("in the words --calibrate already prints",
              startup_reason_name(s.reason), "too-few-measurements");
    check_int("and the form is released", ev.finished, 1);
    check_int("and the receiver given back", ev.release_receiver, 1);
}

static void test_a_scattered_measurement_is_refused_as_too_wide(void) {
    struct startup_session s;
    struct startup_session_event ev;
    int i;

    /* Enough measurements, scattered too far for the standard error to pass.
       The residuals go in directly: what is under test is which clause the
       machine names, not the FCCH detector. */
    startup_session_reset(&s);
    s.phase = STARTUP_MEASURE_GSM;
    s.expected_hz = 943000000U;
    s.tuned = 1;
    s.measure_budget = 5.0;
    calibration_tracker_init(&s.track);
    s.track.source = CALIBRATION_SOURCE_FCCH;
    for (i = 0; i < CALIBRATION_MIN_MEASUREMENTS + 4; i++)
        calibration_tracker_observe(&s.track, (i % 2) ? -40.0 : 40.0);
    check_true("the scatter is wide", s.track.recent_sem >
                                          CALIBRATION_MAX_SEM_PPM);

    startup_session_tick(&s, NULL, 0, 6.0, &ev);
    check_int("it gave up", s.phase, STARTUP_FAILED);
    check_int("naming the scatter", s.reason, STARTUP_REASON_SEM_WIDE);
    check_str("in the same words", startup_reason_name(s.reason),
              "sem-too-wide");
}

static void test_skip_from_each_running_phase(void) {
    struct startup_session s;
    struct startup_session_event ev;
    enum startup_phase phases[4] = { STARTUP_SCAN_GSM, STARTUP_MEASURE_GSM,
                                     STARTUP_SCAN_LTE, STARTUP_MEASURE_LTE };
    int i;

    for (i = 0; i < 4; i++) {
        startup_session_reset(&s);
        s.phase = phases[i];
        check_int("it is running", startup_session_running(&s), 1);
        startup_session_skip(&s, &ev);
        check_int("skipping stops it", s.phase, STARTUP_SKIPPED);
        check_int("saying why", s.reason, STARTUP_REASON_SKIPPED);
        check_int("releasing the form", startup_session_may_commit(&s), 1);
        check_int("and the receiver", ev.release_receiver, 1);
    }

    /* And a session that has already finished is not un-finished by a skip
       arriving late -- a click that lands on the frame the gate opened must
       not throw the lock away. */
    startup_session_reset(&s);
    s.phase = STARTUP_LOCKED;
    s.reason = STARTUP_REASON_LOCKED;
    startup_session_skip(&s, &ev);
    check_int("a late skip leaves a lock alone", s.phase, STARTUP_LOCKED);
    check_int("and its reason", s.reason, STARTUP_REASON_LOCKED);
}

static void test_a_refused_tuning_moves_a_scan_on_but_ends_a_measurement(void) {
    struct startup_session s;
    struct startup_session_event ev;

    /* A search can go on -- the next step is a different frequency and the
       tuner may well take it. */
    startup_session_begin(&s, RATE, 0, 1, lte_band_for_number(20), 0.0, &ev);
    check_int("scanning", s.phase, STARTUP_SCAN_GSM);
    startup_session_retune_failed(&s, ev.retune_hz, "refused", &ev);
    check_int("the scan moved on", s.step, 1);
    check_int("still scanning", s.phase, STARTUP_SCAN_GSM);
    check_true("and asked for the next step", ev.retune_hz > 0);

    /* A measurement cannot: there is exactly one carrier it is measuring
       against. */
    startup_session_reset(&s);
    s.phase = STARTUP_MEASURE_GSM;
    s.expected_hz = 943000000U;
    startup_session_retune_failed(&s, 942600000U, "device busy", &ev);
    check_int("the measurement ended", s.phase, STARTUP_FAILED);
    check_int("because it could not tune", s.reason, STARTUP_REASON_NO_TUNE);
    check_true("quoting the reason it was given",
               strstr(s.status, "device busy") != NULL);
}

static void test_every_phase_and_reason_has_a_name(void) {
    /* The log and the report both print these, and a name that came back
       empty would turn an unanswered question into a wrong answer. */
    check_str("idle", startup_phase_name(STARTUP_IDLE), "idle");
    check_str("scan-gsm", startup_phase_name(STARTUP_SCAN_GSM), "scan-gsm");
    check_str("measure-gsm", startup_phase_name(STARTUP_MEASURE_GSM),
              "measure-gsm");
    check_str("confirm-tone", startup_phase_name(STARTUP_CONFIRM_TONE),
              "confirm-tone");
    check_str("scan-lte", startup_phase_name(STARTUP_SCAN_LTE), "scan-lte");
    check_str("measure-lte", startup_phase_name(STARTUP_MEASURE_LTE),
              "measure-lte");
    check_str("locked", startup_phase_name(STARTUP_LOCKED), "locked");
    check_str("failed", startup_phase_name(STARTUP_FAILED), "failed");
    check_str("skipped", startup_phase_name(STARTUP_SKIPPED), "skipped");

    check_str("none", startup_reason_name(STARTUP_REASON_NONE), "none");
    check_str("locked", startup_reason_name(STARTUP_REASON_LOCKED), "locked");
    check_str("no-cell", startup_reason_name(STARTUP_REASON_NO_CELL),
              "no-cell");
    check_str("timeout", startup_reason_name(STARTUP_REASON_TIMEOUT),
              "timeout");
    check_str("no-tune", startup_reason_name(STARTUP_REASON_NO_TUNE),
              "no-tune");
    check_str("skipped", startup_reason_name(STARTUP_REASON_SKIPPED),
              "skipped");
    check_str("references-disagree",
              startup_reason_name(STARTUP_REASON_DISAGREE),
              "references-disagree");
}

/*
 * A block is measured once.
 *
 * This is the fault the fold in ticket 06 uncovered in the *old* headless
 * `--calibrate`, which had shipped: it called
 * `update_calibration_measurement()` on every loop iteration rather than once
 * per block, so each block's residual was recorded five to twenty times over.
 *
 * That is not a cosmetic inflation of a counter. The residual ring holds
 * CALIBRATION_RECENT = 64 values and the gate reads a **median and a MAD**
 * over it, which exist precisely to resist a peak that hops to an adjacent
 * feature for a few blocks -- and duplicates defeat exactly that: sixty-four
 * slots filled by six distinct blocks make the median the median of six. On
 * air it locked on a run of outliers in two of three runs and suggested a
 * correction 45 ppm wrong, with `locked` beside it.
 *
 * Here the machine is ticked repeatedly with `have_block` false, which is what
 * a loop spinning between blocks does.
 */
static void test_a_block_is_measured_once(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct startup_block block;
    const int arfcn = 40;
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;
    int i;

    startup_session_reset(&s);
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = arfcn;
    s.expected_hz = (uint32_t)channel;
    s.tuned = 1;
    s.measure_budget = STARTUP_MEASURE_SECONDS;
    calibration_tracker_init(&s.track);

    fcch_block(&block, channel - 400000.0, arfcn, 0.0);
    startup_session_tick(&s, &block, 1, 0.1, &ev);
    check_int("the block was measured", s.track.measurements, 1);

    /* Twenty ticks with nothing new, as a 5 ms poll loop produces between two
       65 ms blocks. */
    for (i = 0; i < 20; i++)
        startup_session_tick(&s, &block, 0, 0.1 + 0.005 * (double)i, &ev);
    check_int("and not again while nothing new arrived", s.track.measurements,
              1);

    /* The next block counts once more, and once only. */
    startup_session_tick(&s, &block, 1, 0.2, &ev);
    check_int("the next block counts once", s.track.measurements, 2);
}

/*
 * Whether a tone-free block may fall back to a centroid is the caller's, and
 * it is a real difference rather than a convenience (ADR-0004): the startup
 * form files a correction unattended and refuses, while `--calibrate` serves
 * an operator reading every residual and allows it.
 */
static void test_the_centroid_is_the_callers_choice(void) {
    struct startup_session s;
    struct startup_session_event ev;

    check_int("the form's search refuses it",
              (startup_session_begin(&s, RATE, 0, 1, lte_band_for_number(20),
                                     0.0, &ev),
               s.allow_centroid), 0);

    check_int("a named GSM channel takes what it is told",
              (startup_session_measure_gsm(&s, 40, 0, 1, 0.0, &ev),
               s.allow_centroid), 1);
    check_int("and refuses when told to",
              (startup_session_measure_gsm(&s, 40, 0, 0, 0.0, &ev),
               s.allow_centroid), 0);
    check_int("arming a named channel measures it", s.phase,
              STARTUP_MEASURE_GSM);
    check_int("against its own carrier", (int)s.expected_hz,
              (int)(GSM900_BASE_HZ + 40.0 * GSM900_ARFCN_SPACING_HZ));

    /*
     * LTE has no second estimator to fall back to -- the offset comes from
     * lte_cell_search, which either found a cell or did not -- so the flag
     * would name a choice that does not exist.
     */
    check_int("LTE never allows one",
              (startup_session_measure_lte(&s, 6200, 0, 0.0, &ev),
               s.allow_centroid), 0);
    check_int("and measures the cell it was given", s.phase,
              STARTUP_MEASURE_LTE);

    check_int("a channel that is not one is refused",
              startup_session_measure_gsm(&s, 0, 0, 1, 0.0, &ev), -1);
    check_int("and says so", s.phase, STARTUP_FAILED);
    check_int("a budget may be set", (startup_session_set_budget(&s, 12.0),
                                      (int)s.measure_budget), 12);
    startup_session_set_budget(&s, -1.0);
    check_int("and a nonsense one is ignored", (int)s.measure_budget, 12);
}

/*
 * The verification pass tries the next-best candidate rather than giving up.
 *
 * The loudest channel carrying a tone need not be the one that can be
 * calibrated against, so rejecting one candidate must put the next in its
 * place. Giving up on the band at the first refusal would send every site
 * with one such channel down the slow LTE path.
 */
static void test_a_refused_candidate_lets_the_next_one_try(void) {
    struct startup_session s;
    struct startup_session_event ev;

    startup_session_reset(&s);
    s.phase = STARTUP_SCAN_GSM;
    /* Two channels carrying tones, one louder. */
    s.power[40] = -10.0f;
    s.bcch_conf[40] = 0.99f;
    s.power[63] = -20.0f;
    s.bcch_conf[63] = 0.95f;
    s.plan.step_count = 1;

    startup_session_retune_failed(&s, 0, "end of band", &ev);
    check_int("the loudest is tried first", s.arfcn, 40);
    check_int("and it is being verified, not measured", s.phase,
              STARTUP_VERIFY_GSM);
    check_int("nothing is measured yet", s.track.measurements, 0);

    /* Eight blocks with no synchronisation burst in them. */
    {
        struct startup_block block;
        int i;

        startup_session_retuned(&s, 1.0);
        quiet_block(&block, (double)s.expected_hz - 400000.0);
        for (i = 0; i < STARTUP_VERIFY_BLOCKS + 1; i++)
            startup_session_tick(&s, &block, 1, 1.0 + 0.065 * (double)i, &ev);
    }
    check_int("the loudest was turned down", s.rejected[40], 1);
    /* And why, captured before the search moved on -- reading `status` after
       a rejection gives the *next* candidate's line, which is what the first
       trace of this printed. */
    check_int("naming which", s.rejected_arfcn, 40);
    check_true("with its own reason, not the next candidate's",
               strstr(s.rejected_why, "40") != NULL &&
               strstr(s.rejected_why, "63") == NULL);
    check_int("and the next best is being asked", s.arfcn, 63);
    check_int("still verifying", s.phase, STARTUP_VERIFY_GSM);

    /* And when that one fails too, the band is out of candidates and the
       machine falls through rather than stalling. */
    {
        struct startup_block block;
        int i;

        startup_session_retuned(&s, 2.0);
        quiet_block(&block, (double)s.expected_hz - 400000.0);
        for (i = 0; i < STARTUP_VERIFY_BLOCKS + 1; i++)
            startup_session_tick(&s, &block, 1, 2.0 + 0.065 * (double)i, &ev);
    }
    check_int("both were turned down", s.rejected[63], 1);
    check_true("and GSM is finished with",
               s.phase != STARTUP_VERIFY_GSM &&
               s.phase != STARTUP_MEASURE_GSM);
}

/*
 * Two references, and what happens when they disagree.
 *
 * `.scratch/startup-installation/issues/08-*`: two parity-verified GSM cells
 * at one site measured this crystal 15 ppm apart, and nothing about either
 * measurement on its own said which to believe -- the spreads overlap, the
 * tone coherence is flat across a channel, and the wrong one decodes 284
 * synchronisation bursts in 25 s. So the search does not lock on one.
 *
 * The residuals go in directly here: what is under test is the comparison and
 * what it does about each outcome, not the FCCH detector.
 */
static void settle_reference(struct startup_session *s, double ppm,
                             double now, struct startup_session_event *out) {
    int i;

    s->track.source = CALIBRATION_SOURCE_FCCH;
    calibration_tracker_reset(&s->track);
    for (i = 0; i < CALIBRATION_MIN_MEASUREMENTS + 2; i++)
        calibration_tracker_observe(&s->track, 0.0);
    s->suggested_ppm = (int)ppm;
    s->track.stable = 1;
    startup_session_tick(s, NULL, 0, now, out);
}

static void test_two_references_must_agree(void) {
    struct startup_session s;
    struct startup_session_event ev;

    /* Two candidates in the band, so a second opinion is available. */
    startup_session_reset(&s);
    s.cross_check = 1;
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = 40;
    s.expected_hz = 943000000U;
    s.tuned = 1;
    s.measure_budget = STARTUP_MEASURE_SECONDS;
    s.power[40] = -10.0f; s.bcch_conf[40] = 0.99f;
    s.power[63] = -20.0f; s.bcch_conf[63] = 0.95f;

    settle_reference(&s, 35.0, 1.0, &ev);
    check_int("the first reference does not lock on its own", s.phase,
              STARTUP_VERIFY_GSM);
    check_int("it is remembered", s.first_ppm, 35);
    check_int("and the next candidate is being checked", s.arfcn, 63);
    check_int("nothing finished", ev.finished, 0);

    /* The second agrees. */
    s.phase = STARTUP_MEASURE_GSM;
    s.tuned = 1;
    settle_reference(&s, 34.0, 2.0, &ev);
    check_int("agreement locks it", s.phase, STARTUP_LOCKED);
    check_int("on the first one's answer", s.suggested_ppm, 35);
    check_int("saying so", ev.finished, 1);
    check_true("and naming both", strstr(s.status, "agree") != NULL);
}

static void test_references_that_disagree_apply_nothing(void) {
    struct startup_session s;
    struct startup_session_event ev;

    startup_session_reset(&s);
    s.cross_check = 1;
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = 40;
    s.expected_hz = 943000000U;
    s.tuned = 1;
    s.measure_budget = STARTUP_MEASURE_SECONDS;
    s.power[40] = -10.0f; s.bcch_conf[40] = 0.99f;
    s.power[63] = -20.0f; s.bcch_conf[63] = 0.95f;

    settle_reference(&s, 50.0, 1.0, &ev);
    s.phase = STARTUP_MEASURE_GSM;
    s.tuned = 1;
    /* The 15 ppm the two real cells differ by. */
    settle_reference(&s, 35.0, 2.0, &ev);

    check_int("it refuses", s.phase, STARTUP_FAILED);
    check_int("naming the disagreement", s.reason, STARTUP_REASON_DISAGREE);
    check_str("in a word both adapters print",
              startup_reason_name(s.reason), "references-disagree");
    check_true("and reports both numbers", strstr(s.status, "+50") != NULL &&
                                           strstr(s.status, "+35") != NULL);
    /* A refusal, so nothing may be filed: the caller reads the phase, and
       LOCKED is the only one that applies anything (ADR-0004). */
    check_true("which is not a lock", s.phase != STARTUP_LOCKED);
}

/*
 * A second candidate that exists but fails verification must still leave the
 * first reference standing.
 *
 * This was a fall-through and it threw away a correct answer on the first
 * live run of the cross-check: ARFCN 113 measured +34 ppm, the only other
 * candidate failed its checks, and the machine went on to the LTE fall-back
 * and reported `no-cell` with 123 measurements behind it. The band running
 * out of *second opinions* is not the same as having nothing to calibrate
 * against.
 */
static void test_a_failed_second_candidate_leaves_the_first_standing(void) {
    struct startup_session s;
    struct startup_session_event ev;
    struct startup_block block;
    int i;

    startup_session_reset(&s);
    s.cross_check = 1;
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = 40;
    s.expected_hz = 943000000U;
    s.tuned = 1;
    s.measure_budget = STARTUP_MEASURE_SECONDS;
    s.power[40] = -10.0f; s.bcch_conf[40] = 0.99f;
    s.power[63] = -20.0f; s.bcch_conf[63] = 0.95f;
    /* No LTE band either, so a fall-through would end in `no-cell` -- which
       is exactly what it did. */
    s.band = NULL;

    settle_reference(&s, 34.0, 1.0, &ev);
    check_int("the second candidate is being verified", s.arfcn, 63);

    /* It never produces a synchronisation burst. */
    startup_session_retuned(&s, 2.0);
    quiet_block(&block, (double)s.expected_hz - 400000.0);
    for (i = 0; i < STARTUP_VERIFY_BLOCKS + 1; i++)
        startup_session_tick(&s, &block, 1, 2.0 + 0.065 * (double)i, &ev);

    check_int("the first reference still stands", s.phase, STARTUP_LOCKED);
    check_int("with its answer", s.suggested_ppm, 34);
    check_int("and its channel", s.arfcn, 40);
    check_true("saying nothing checked it",
               strstr(s.status, "nothing checked it") != NULL);
}

static void test_one_carrier_locks_and_says_it_was_alone(void) {
    struct startup_session s;
    struct startup_session_event ev;

    /* The only broadcast carrier within reach. The correction stands -- it is
       the best this site has -- but an operator deciding whether to keep it
       should know nothing checked it. */
    startup_session_reset(&s);
    s.cross_check = 1;
    s.phase = STARTUP_MEASURE_GSM;
    s.arfcn = 40;
    s.expected_hz = 943000000U;
    s.tuned = 1;
    s.measure_budget = STARTUP_MEASURE_SECONDS;
    s.power[40] = -10.0f; s.bcch_conf[40] = 0.99f;

    settle_reference(&s, 35.0, 1.0, &ev);
    check_int("it locks", s.phase, STARTUP_LOCKED);
    check_int("on the one answer it has", s.suggested_ppm, 35);
    check_true("and says nothing checked it",
               strstr(s.status, "nothing checked it") != NULL);
}

/*
 * A caller that names the channel gets that channel measured, and no search
 * for a second opinion -- `--calibrate gsm --arfcn N` is an instruction, and
 * going off to find another carrier would answer a question nobody asked.
 */
static void test_a_named_channel_is_not_second_guessed(void) {
    struct startup_session s;
    struct startup_session_event ev;

    startup_session_measure_gsm(&s, 40, 0, 1, 0.0, &ev);
    check_int("it is not cross-checking", s.cross_check, 0);
    s.tuned = 1;
    s.power[63] = -20.0f;
    s.bcch_conf[63] = 0.95f;   /* a second candidate is available */
    settle_reference(&s, 35.0, 1.0, &ev);
    check_int("and it locks on the channel it was given", s.phase,
              STARTUP_LOCKED);
    check_int("with that answer", s.suggested_ppm, 35);
    check_int("having measured one reference", s.references, 0);
}

/*
 * A tone pinned to a **baseband** offset rather than to a frequency on the
 * air: whatever the receiver is tuned to, it appears at the same place in the
 * block. That is what a detector artefact looks like -- the strongest thing
 * inside a search window, where the window moves with the receiver.
 */
static void baseband_tone_block(struct startup_block *block, double centre_hz,
                                int arfcn, double offset_from_nominal) {
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;
    /*
     * Placed relative to where the detector will *look* -- the nominal tone
     * offset for this tuning -- rather than to a frequency on the air. So it
     * follows the search window, which is what the artefact does, and the
     * carrier it implies is displaced by `offset_from_nominal`.
     *
     * The displacement has to stay inside GSM_FCCH_SEARCH_HALF_HZ or there is
     * nothing to find and the case being modelled never arises. A first
     * version moved it by the whole 200 kHz retune, which put it outside the
     * window: the machine correctly reported no tone rather than a wrong one,
     * and the test was measuring the wrong failure.
     */
    double tone = channel - centre_hz + GSM_FCCH_TONE_HZ + offset_from_nominal;
    size_t i;

    quiet_block(block, centre_hz);
    for (i = 0; i < PAIRS; i++) {
        double phase = 2.0 * M_PI * tone * (double)i / RATE;
        g_i[i] = (float)(40.0 * cos(phase));
        g_q[i] = (float)(40.0 * sin(phase));
    }
}

/*
 * A tone that holds still passes, and one that follows the receiver does not.
 *
 * `.scratch/startup-installation/issues/09-*`: the SCH gate proves a base
 * station is on the channel, not that the line the tone detector locked onto
 * is its FCCH -- the search is +/-50 kHz and returns the strongest thing in
 * it. On air ARFCN 63 repeated to 71 Hz and ARFCN 113 to 272 over six
 * recordings, while ARFCN 17's "tone" moved 3277 Hz and was not an FCCH.
 */
static void run_tone_check(struct startup_session *s, int arfcn, int moving) {
    struct startup_session_event ev;
    struct startup_block block;
    double channel = GSM900_BASE_HZ + (double)arfcn * GSM900_ARFCN_SPACING_HZ;
    double centre = channel - 400000.0;
    int i;

    s->phase = STARTUP_CONFIRM_TONE;
    s->arfcn = arfcn;
    s->expected_hz = (uint32_t)channel;
    s->tuned = 1;
    s->tone_first_hz = 0.0;
    s->tone_second_hz = 0.0;
    s->tone_blocks = 0;
    s->tone_second_look = 0;

    /* The first look, where the verification left the receiver. */
    for (i = 0; i < STARTUP_TONE_BLOCKS; i++) {
        fcch_block(&block, centre, arfcn, 0.0);
        startup_session_tick(s, &block, 1, 0.1 * (double)i, &ev);
    }
    check_int("it asks for a second look elsewhere",
              (int)(ev.retune_hz > 0), 1);
    check_int("shifted by the amount it says", (int)ev.retune_hz,
              (int)(centre + STARTUP_TONE_SHIFT_HZ));

    /* The receiver moves, and the second look happens there. */
    centre += STARTUP_TONE_SHIFT_HZ;
    startup_session_retuned(s, 1.0);
    for (i = 0; i < STARTUP_TONE_BLOCKS; i++) {
        if (moving)
            /* The 3277 Hz ARFCN 17 moved by, on air. */
            baseband_tone_block(&block, centre, arfcn, 3277.0);
        else
            fcch_block(&block, centre, arfcn, 0.0);
        startup_session_tick(s, &block, 1, 1.1 + 0.1 * (double)i, &ev);
    }
}

static void test_a_tone_that_holds_still_is_measured(void) {
    struct startup_session s;

    startup_session_reset(&s);
    run_tone_check(&s, 40, 0);
    check_int("a line on the air passes", s.phase, STARTUP_MEASURE_GSM);
    check_true("having barely moved", fabs(s.tone_moved_hz) < 200.0);
    check_int("and nothing was rejected", s.rejected[40], 0);
}

static void test_a_tone_that_follows_the_receiver_is_refused(void) {
    struct startup_session s;

    startup_session_reset(&s);
    /* A second candidate, so a refusal has somewhere to go. */
    s.power[63] = -20.0f;
    s.bcch_conf[63] = 0.95f;
    run_tone_check(&s, 40, 1);

    check_int("it is turned down", s.rejected[40], 1);
    check_true("having moved with the receiver",
               fabs(s.tone_moved_hz) > STARTUP_TONE_REPEAT_PPM *
                                           943.0e6 / 1e6);
    check_true("nothing was measured against it", s.track.measurements == 0);
    check_int("and the next candidate is asked", s.arfcn, 63);
}

int main(void) {
    /* No sdr_dsp state is needed here: the scan reads a spectrum it is handed
       and the two detectors take raw I/Q, so nothing in this file transforms
       anything. */
    test_a_fresh_session_is_idle_and_committable();
    test_it_begins_by_scanning_gsm();
    test_an_unreachable_gsm_band_goes_straight_to_lte();
    test_a_rate_too_narrow_for_a_step_goes_to_lte();
    test_nothing_to_measure_at_all_is_a_refusal();
    test_a_tone_is_not_a_broadcast_carrier();
    test_a_refused_candidate_lets_the_next_one_try();
    test_the_measured_correction_is_the_error_that_was_there();
    test_power_without_a_tone_falls_through_to_lte();
    test_an_empty_band_finds_nothing_and_says_so();
    test_the_settle_is_timed_from_the_tuning();
    test_a_measurement_does_not_believe_blocks_before_the_retune();
    test_the_budget_names_the_clause_that_was_not_met();
    test_a_scattered_measurement_is_refused_as_too_wide();
    test_skip_from_each_running_phase();
    test_a_refused_tuning_moves_a_scan_on_but_ends_a_measurement();
    test_a_block_is_measured_once();
    test_the_centroid_is_the_callers_choice();
    test_a_tone_that_holds_still_is_measured();
    test_a_tone_that_follows_the_receiver_is_refused();
    test_two_references_must_agree();
    test_references_that_disagree_apply_nothing();
    test_one_carrier_locks_and_says_it_was_alone();
    test_a_failed_second_candidate_leaves_the_first_standing();
    test_a_named_channel_is_not_second_guessed();
    test_every_phase_and_reason_has_a_name();
    return check_report("the startup sequence: scan, measure, gate, give up");
}
