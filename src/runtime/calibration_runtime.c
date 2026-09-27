/*
 * Calibration's per-block work: filling the residual buffer that the lock
 * gate reads, and the drift re-check that watches the crystal afterwards.
 *
 * Out of `overlay_calibration.c` by `.scratch/layer-boundaries/issues/02-*`.
 * `frame_advance()` calls both on every path, including `--calibrate`, which
 * has no window at all -- ADR-0012 is the reason that path exists. The
 * overlay keeps its charts, its buttons and its status line.
 *
 * ADR-0004 is the rule the gate enforces and it is untouched here: the
 * residual buffer stays source-homogeneous, because mixing centroid and FCCH
 * residuals is the bug it exists to prevent.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "runtime/app.h"
#include "runtime/debug_log.h"
#include "runtime/runtime.h"

/*
 * Opening the calibration overlay: the staged reference, and the receiver
 * handed back by whatever had it.
 *
 * In `runtime/` rather than beside the drawing because a Viewer's
 * `view calibration` opens it and the server links no window (ADR-0028,
 * `web-visualization/17`). It decides nothing about pixels.
 */
int cal_band_count(const struct app *app) {
    int bands[LTE_BANDS_MAX];
    return view_lte_bands(app, bands);
}


/*
 * GSM 900 channel calibration, the band scan that feeds it, and the periodic
 * drift re-check -- one overlay, drawn over whichever tab is active.
 *
 * The stability gate in update_calibration_measurement is the subtle part and
 * is documented in docs/adr/0004-calibration-stability-gate.md: the residual
 * buffer must stay source-homogeneous, because mixing centroid and FCCH
 * residuals is exactly the mistake the gate exists to catch.
 */

/* The scan picks a channel and calibration measures it, so selecting one fills
   in calibration's channel field. A function rather than a reach into that
   buffer: the format calibration parses is its own business. */
void calibration_select_channel(struct app *app, int arfcn) {
    snprintf(app->cal.channel, sizeof(app->cal.channel), "%d", arfcn);
    app->cal.channel_length = (int)strlen(app->cal.channel);
}

/*
 * Choose a technology, with everything that entails: the channel it defaults
 * to and the instruction that names it.
 *
 * One path, because there are two callers -- the button and opening the
 * overlay already on 4G -- and when the button's side effects lived inline the
 * second one showed the LTE arrangement under "Select GSM 900 ARFCN 1-124".
 */
void calibration_select_technology(struct app *app, int technology) {
    app->cal.technology = technology;
    if (technology == 1) {
        snprintf(app->cal.channel, sizeof(app->cal.channel), "6200");
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Pick a band and Scan, or type an EARFCN, then press Start");
    } else if (technology == 0) {
        snprintf(app->cal.channel, sizeof(app->cal.channel), "113");
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Select GSM 900 ARFCN 1-124, then press Start");
    } else {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "5G channel tables are not implemented yet");
    }
    app->cal.channel_length = (int)strlen(app->cal.channel);
}

/*
 * Calibrating against an LTE cell.
 *
 * The cell search measures the receiver's frequency error twice over -- a
 * phase from the primary sequence, which only sees it modulo one subcarrier,
 * and then the whole subcarriers by search. That second half is why this is
 * worth having: an uncalibrated dongle is two subcarriers out at 800 MHz, and
 * a reference that could not see them would report the error as the remainder
 * and be confidently wrong by 30 kHz.
 *
 * It runs on LTE's own 1.92 MS/s grid and refuses anything else (ADR-0014),
 * so the calibration borrows the rate and gives it back on the way out.
 */
/*
 * Calibration takes the receiver once, whichever phase takes it first: a band
 * scan looking for something worth measuring against, or the measurement
 * itself. `acquired` says whether this call was the one that took it, so a
 * refused retune cancels only a claim it created.
 */
int calibration_borrow(struct app *app, int *acquired) {
    *acquired = 0;
    if (receiver_lease_token_active(&app->cal.lease_token))
        return 0;
    if (receiver_borrow(app, &app->cal.lease_token) < 0)
        return -1;
    *acquired = 1;
    return 0;
}



static int start_lte_calibration(struct app *app) {
    int earfcn;
    uint32_t carrier;

    if (parse_int(app->cal.channel, &earfcn) < 0 || earfcn <= 0 ||
        !lte_earfcn_downlink_hz((unsigned int)earfcn, &carrier)) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Not an LTE downlink EARFCN this band table knows");
        return -1;
    }
    app->cal.expected_hz = carrier;
    chart_window_sync(&app->cal.window, app->applied.frequency_hz,
                      app->applied.sample_rate_hz, CHART_MIN_SPAN_HZ);
    chart_window_centre_on(&app->cal.window, (double)carrier,
                           CALIBRATION_VIEW_HALF_WIDTH_HZ,
                           CHART_MIN_SPAN_HZ);
    /* Tuned to the carrier's centre, not beside it as an ARFCN is: LTE never
       transmits on the middle subcarrier, so the receiver's own DC spike
       lands where the standard already leaves a hole. */
    app->cal.tune_hz = carrier;
    app->cal.measured_hz = 0.0;
    app->cal.offset_hz = 0.0;
    calibration_tracker_init(&app->cal.track);
    app->cal.track.source = CALIBRATION_SOURCE_LTE;
    int acquired;
    if (calibration_borrow(app, &acquired) < 0) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Could not take the receiver: %.100s", app->receiver_error);
        return -1;
    }
    if (retune_receiver_at_rate(app, app->cal.tune_hz, LTE_SAMPLE_RATE_HZ,
                                app->applied.ppm) < 0) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "The receiver would not take LTE's 1.92 MS/s: %.100s",
                 app->receiver_error);
        if (acquired)
            receiver_lease_cancel(&app->lease, &app->cal.lease_token);
        return -1;
    }
    app->cal.started_at = monotonic_seconds();
    app->cal.running = 1;
    app->cal.lte_earfcn = earfcn;
    debug_log_write("cal", "begin lte earfcn %d expected_hz %u applied_ppm %d",
                    earfcn, app->cal.expected_hz, app->applied.ppm);
    snprintf(app->cal.status, sizeof(app->cal.status),
             "Measuring LTE EARFCN %d at %.3f MHz", earfcn,
             carrier / 1000000.0);
    return 0;
}

int start_calibration(struct app *app) {
    int arfcn;
    uint32_t expected;
    if (app->cal.technology == 1)
        return start_lte_calibration(app);
    if (app->cal.technology != 0 || app->cal.band != 0) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Only 2G and 4G are supported in this version");
        return -1;
    }
    if (app->applied.sample_rate_hz < 1000000U) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "GSM calibration requires a sample rate of at least 1 MS/s");
        return -1;
    }
    if (parse_int(app->cal.channel, &arfcn) < 0 ||
        arfcn < 1 || arfcn > 124 ||
        !gsm_downlink_hz((unsigned int)arfcn, &expected)) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "GSM 900 ARFCN must be between 1 and 124");
        return -1;
    }

    app->cal.expected_hz = expected;
    chart_window_sync(&app->cal.window, app->applied.frequency_hz,
                      app->applied.sample_rate_hz,
                      chart_min_span(GSM900_ARFCN_SPACING_HZ));
    chart_window_centre_on(&app->cal.window, (double)expected,
                           CALIBRATION_VIEW_HALF_WIDTH_HZ,
                           chart_min_span(GSM900_ARFCN_SPACING_HZ));
    app->cal.tune_hz = expected - 400000U;
    app->cal.measured_hz = 0.0;
    app->cal.offset_hz = 0.0;
    calibration_tracker_init(&app->cal.track);
    int acquired;
    if (calibration_borrow(app, &acquired) < 0) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Could not take the receiver: %.100s", app->receiver_error);
        return -1;
    }
    if (retune_receiver(app, app->cal.tune_hz, app->applied.ppm) < 0) {
        /*
         * Quoted rather than assumed. Both of these used to return with no
         * status at all and let the headless report print whatever
         * retune_receiver() had left in this same buffer -- which worked by
         * accident and said "Calibration requires a live RTL-SDR receiver"
         * for any receiver fault at all. The reason has its own name now
         * (`app->receiver_error`), so asking for it is explicit.
         */
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Could not tune to %.3f MHz: %.100s",
                 app->cal.tune_hz / 1e6, app->receiver_error);
        if (acquired)
            receiver_lease_cancel(&app->lease, &app->cal.lease_token);
        return -1;
    }
    app->cal.started_at = monotonic_seconds();
    app->cal.running = 1;
    debug_log_write("cal", "begin gsm arfcn %d expected_hz %u applied_ppm %d",
                    arfcn, app->cal.expected_hz, app->applied.ppm);
    snprintf(app->cal.status, sizeof(app->cal.status),
             "Measuring GSM 900 ARFCN %d at %.3f MHz", arfcn,
             expected / 1000000.0);
    return 0;
}










/* Periodically verify the applied PPM against the calibrated GSM carrier. Each
   check briefly retunes to the calibrated channel, measures the FCCH residual,
   then retunes back to the view frequency, so it only runs when enabled, a
   valid FCCH-backed calibration exists, and no overlay owns the tuning. See
   docs/adr/0006-gsm-drift-indicator.md. */

/*
 * Stop measuring and give the receiver back, without leaving calibration.
 *
 * An LTE calibration borrowed the sample rate as well as the tuning
 * (ADR-0014), so both go back. Leaving the receiver on 1.92 MS/s would strand
 * every Scope view at a rate they do not expect, which is the same trap the
 * LTE decode view has to avoid on the way out.
 *
 * This is the whole of what closing used to do apart from the last line, and
 * it is separate now because Back stops a measurement while staying on the
 * screen -- which is the step that did not exist before.
 */
int calibration_stop_measuring(struct app *app) {
    /*
     * One claim, so one return, and the snapshot carries the rate as well as
     * the frequency -- which is what retired the separate
     * cal_return_sample_rate this used to have to reason about. A PPM applied
     * while the receiver was borrowed survives, because the restore uses the
     * current correction and the snapshot has no field to undo it with.
     */
    if (receiver_return(app, &app->cal.lease_token) < 0)
        return -1;
    app->cal.running = 0;
    return 0;
}

void close_calibration(struct app *app) {
    if (calibration_stop_measuring(app) < 0)
        return;
    app->cal.open = 0;
}

void open_calibration(struct app *app) {
    app->cal.open = 1;
    app->cal.running = 0;
    app->cal.technology = 0;
    app->cal.band = 0;
    snprintf(app->cal.channel, sizeof(app->cal.channel),
             "113");
    app->cal.channel_length = 3;
    app->cal.expected_hz = 0;
    calibration_tracker_init(&app->cal.track);
    app->cal.fcch_confidence = 0.0f;
    app->bandscan.open = 0;
    app->bandscan.running = 0;
    scan_release_receiver(app);
    app->cal.measured_hz = 0.0;
    app->cal.offset_hz = 0.0;
    app->cal.suggested_ppm = app->applied.ppm;
    snprintf(app->cal.status, sizeof(app->cal.status),
             "Select GSM 900 ARFCN 1-124, then press Start");
}

/*
 * One line per residual, in the field names `--calibrate` already prints.
 *
 * The same names deliberately: `cal-measure` on stdout and `cal measure` in
 * the log describe one measurement, and two vocabularies for it would mean a
 * grep stops working at the seam between the window and the command line.
 *
 * Every residual rather than a summary, for the reason the headless report
 * gives: the verdict is one bit, and the sequence is what shows whether the
 * scatter is the estimator or the crystal. It is called once per
 * calibration_tracker_observe(), so a block that records nothing logs
 * nothing, and the whole thing costs nothing with the log closed.
 */
static void cal_log_measure(const struct app *app) {
    if (!debug_log_active())
        return;
    debug_log_write("cal",
                    "measure %d observed_ppm %.2f centre_ppm %.2f "
                    "sem_ppm %.2f spread_ppm %.2f source %s quality %.2f",
                    app->cal.track.measurements,
                    app->cal.expected_hz
                        ? app->cal.offset_hz / (double)app->cal.expected_hz *
                              1e6
                        : 0.0,
                    app->cal.track.recent_center, app->cal.track.recent_sem,
                    app->cal.track.recent_spread,
                    app->cal.track.source == CALIBRATION_SOURCE_FCCH
                        ? "fcch"
                        : app->cal.track.source == CALIBRATION_SOURCE_LTE
                              ? "lte" : "centroid",
                    (double)app->cal.fcch_confidence);
}

const struct lte_band *cal_selected_band(const struct app *app) {
    int bands[LTE_BANDS_MAX];
    int count = view_lte_bands(app, bands);
    int index = app->cal.lte_band;

    if (count <= 0)
        return NULL;
    if (index < 0 || index >= count)
        index = 0;
    return lte_band_for_number(bands[index]);
}

static void calibration_set_status(struct app *app) {
    snprintf(app->cal.status, sizeof(app->cal.status),
             "%s (%s): %d meas, +/- %.2f PPM (spread %.2f), FCCH hits %d miss %d conf %.2f, suggested %+d PPM",
             app->cal.track.stable ? "Stable lock" : "Acquiring",
             app->cal.track.source == CALIBRATION_SOURCE_FCCH
                 ? "FCCH tone"
                 : "centroid",
             app->cal.track.measurements,
             app->cal.track.recent_sem,
             app->cal.track.recent_spread,
             app->cal.track.fcch_hits,
             app->cal.track.fcch_miss,
             app->cal.fcch_confidence,
             app->cal.suggested_ppm);
}

/*
 * One block of an LTE calibration: find the cell, take its frequency error.
 *
 * Nothing here estimates a frequency. The cell search already did, better than
 * anything this overlay could: coarsely from the primary sequence, then the
 * whole subcarriers by search, then refined from the reference signals. The
 * calibration's job is only to decide whether the number has settled, which is
 * what the gate is for.
 */
static void update_lte_calibration(struct app *app) {
    struct lte_cell cell;
    double observed_ppm;

    if (app->frame.pair_count < LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE)
        return;
    if (lte_cell_search(app->frame.i_samples, app->frame.q_samples, app->frame.pair_count,
                        (double)app->applied.sample_rate_hz, &cell, NULL) != 1) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "No LTE cell found at EARFCN %d", app->cal.lte_earfcn);
        return;
    }
    /* The buffer must never hold two references at once (ADR-0004), and with
       three sources there are more ways to get that wrong than there were. */
    calibration_tracker_use(&app->cal.track, CALIBRATION_SOURCE_LTE);
    app->cal.fcch_confidence = cell.pss_correlation;
    app->cal.prominence_db = cell.pss_correlation;
    app->cal.measured_hz = (double)app->cal.expected_hz +
                           cell.frequency_offset_hz;
    app->cal.offset_hz = cell.frequency_offset_hz;
    observed_ppm = app->cal.offset_hz /
                   (double)app->cal.expected_hz * 1000000.0;
    calibration_tracker_observe(&app->cal.track, observed_ppm);
    cal_log_measure(app);
    app->cal.suggested_ppm = sdr_dsp_corrected_ppm(
        app->applied.ppm, app->cal.measured_hz,
        (double)app->cal.expected_hz);
    /*
     * And the gate. Easy to leave out, and invisible when you do: the numbers
     * on screen all look right and the lock simply never comes. Headlessly it
     * was obvious at once -- 2217 measurements with a standard error of
     * 0.01 PPM and `locked 0`.
     */
    app->cal.track.stable = calibration_is_stable(
        monotonic_seconds() - app->cal.started_at,
        app->cal.track.measurements, app->cal.track.recent_count,
        app->cal.track.recent_sem, app->cal.track.source,
        cell.pss_correlation);
    snprintf(app->cal.status, sizeof(app->cal.status),
             "%s (LTE cell %d): %d meas, +/- %.2f PPM (spread %.2f), "
             "offset %+.1f kHz, PSS %.2f, suggested %+d PPM",
             app->cal.track.stable ? "Stable lock" : "Acquiring", cell.pci,
             app->cal.track.measurements, app->cal.track.recent_sem,
             app->cal.track.recent_spread, app->cal.offset_hz / 1e3,
             (double)cell.pss_correlation, app->cal.suggested_ppm);
}

/*
 * The 4G scan: the same walk the LTE decode view does, driven from here.
 *
 * Reusing it rather than writing a second one is the point -- the coarse-to-
 * fine order, the repeated looks, the confirmation pass and the ghost
 * suppression were all earned against real signals, and a calibration scan
 * that quietly did something simpler would find different cells than the
 * decode view does on the same band.
 */
static void update_lte_calibration_scan(struct app *app) {
    if (!app->cal.lte_scanning)
        return;
    update_lte_scan(app, monotonic_seconds(), 1);
    if (lte_scan_running(app)) {
        const struct lte_band *band =
            cal_selected_band(app);
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Scanning band %d: %d of %d channels, %d cells so far",
                 band ? band->band : 0, app->lte.scan.candidate + 1,
                 app->lte.scan.total, app->lte.scan.found_count);
        return;
    }
    app->cal.lte_scanning = 0;
    snprintf(app->cal.status, sizeof(app->cal.status),
             app->lte.scan.found_count > 0
                 ? "%d cells found -- pick one to calibrate against"
                 : "No cells found in that band",
             app->lte.scan.found_count);
}

void update_calibration_measurement(struct app *app) {
    if (!app->cal.open || app->bandscan.open)
        return;
    if (app->cal.lte_scanning) {
        update_lte_calibration_scan(app);
        return;
    }
    if (!app->cal.running)
        return;
    if (app->cal.technology == 1) {
        double waited = monotonic_seconds() - app->cal.started_at;
        if (waited < CALIBRATION_SETTLE_SECONDS) {
            snprintf(app->cal.status, sizeof(app->cal.status),
                     "Settling receiver... %.1f s", waited);
            return;
        }
        update_lte_calibration(app);
        return;
    }
    if (!app->frame.spectrum_ready)
        return;

    double lower = (double)app->applied.frequency_hz -
                   app->applied.sample_rate_hz / 2.0;
    double upper = (double)app->applied.frequency_hz +
                   app->applied.sample_rate_hz / 2.0;
    double elapsed = monotonic_seconds() - app->cal.started_at;
    if (elapsed < CALIBRATION_SETTLE_SECONDS) {
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "Settling receiver... %.1f s", elapsed);
        return;
    }

    /* FCCH detection is independent of the centroid: a dip in centroid
       prominence must not wipe an FCCH accumulation. */
    struct gsm_fcch_result fcch;
    double fcch_target = (double)app->cal.expected_hz -
                         (double)app->applied.frequency_hz +
                         GSM_FCCH_TONE_HZ;
    int have_fcch = gsm_fcch_detect(app->frame.i_samples, app->frame.q_samples,
                                           app->frame.pair_count,
                                           app->applied.sample_rate_hz,
                                           fcch_target,
                                           GSM_FCCH_SEARCH_HALF_HZ, &fcch);

    /* The centroid supplies the peak/floor/prominence metrics and the
       carrier estimate used in centroid mode. */
    struct sdr_channel_estimate estimate;
    int have_centroid = sdr_dsp_estimate_channel_center(
        app->frame.spectrum_average, SDR_DSP_FFT_SIZE, lower, upper,
        app->cal.expected_hz, 100000.0, 50000.0,
        app->cal.workspace, &estimate);
    if (have_centroid) {
        app->cal.peak_hz = estimate.peak_frequency_hz;
        app->cal.peak_dbfs = estimate.peak_dbfs;
        app->cal.floor_dbfs = estimate.floor_dbfs;
        app->cal.prominence_db = estimate.prominence_db;
    }

    /* Which source this block may contribute to, and the buffer resets that
       keep the two from mixing, are in calibration_gate.h with the gate they
       feed -- the rule is ADR-0004's and it is checked there. */
    double measured_hz;
    switch (calibration_track(&app->cal.track, have_fcch, have_centroid)) {
    case CALIBRATION_USE_FCCH:
        app->cal.fcch_confidence = fcch.confidence;
        measured_hz = (double)app->applied.frequency_hz +
                      fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
        break;
    case CALIBRATION_HOLD_TONE:
        calibration_set_status(app); /* hold the tone lock */
        return;
    case CALIBRATION_USE_CENTROID:
        measured_hz = estimate.measured_frequency_hz;
        break;
    default:
        snprintf(app->cal.status, sizeof(app->cal.status),
                 "No isolated GSM carrier at least 8 dB above guard-band "
                 "floor");
        return;
    }

    app->cal.measured_hz = measured_hz;
    app->cal.offset_hz = measured_hz - app->cal.expected_hz;
    double observed_ppm = app->cal.offset_hz /
                          app->cal.expected_hz * 1000000.0;
    /* Individual 65 ms blocks scatter a lot on a modulated GSM channel, but the
       correction applied is the centre of the recent residuals, whose
       uncertainty is the standard error of that centre, not the per-block
       spread. The tracker keeps both. */
    calibration_tracker_observe(&app->cal.track, observed_ppm);
    cal_log_measure(app);

    app->cal.suggested_ppm = sdr_dsp_corrected_ppm(
        app->applied.ppm, app->cal.expected_hz *
                              (1.0 + app->cal.track.recent_center / 1000000.0),
        app->cal.expected_hz);
    if (app->cal.suggested_ppm < -1000)
        app->cal.suggested_ppm = -1000;
    if (app->cal.suggested_ppm > 1000)
        app->cal.suggested_ppm = 1000;

    app->cal.track.stable = calibration_is_stable(elapsed, app->cal.track.measurements,
                                            app->cal.track.recent_count,
                                            app->cal.track.recent_sem,
                                            app->cal.track.source,
                                            app->cal.prominence_db);
    calibration_set_status(app);
}

void update_drift_check(struct app *app, int have_block) {
    if (!app->cal.auto_drift || !app->cal.gsm_valid || !app->receiver_mode)
        return;
    if (app->cal.open || app->bandscan.open || app->set.open)
        return;

    double now = monotonic_seconds();

    if (app->cal.drift_phase == DRIFT_IDLE) {
        if (now - app->cal.drift_last_check_at < DRIFT_CHECK_INTERVAL_SECONDS)
            return;
        app->cal.drift_health_prev = app->cal.drift_health;
        /* Borrowed from whichever decode view is on screen, and given back
           before that view can leave. */
        if (receiver_borrow(app, &app->cal.drift_token) < 0) {
            app->cal.drift_last_check_at = now;
            return;
        }
        if (retune_receiver(app, app->cal.gsm_tune_hz, app->cal.gsm_ppm) < 0) {
            receiver_lease_cancel(&app->lease, &app->cal.drift_token);
            app->cal.drift_last_check_at = now; /* retry next interval */
            return;
        }
        app->cal.drift_recent_count = 0;
        app->cal.drift_phase = DRIFT_SETTLE;
        app->cal.drift_phase_started_at = now;
        app->cal.drift_health = CAL_HEALTH_CHECKING;
        return;
    }

    if (app->cal.drift_phase == DRIFT_SETTLE) {
        if (now - app->cal.drift_phase_started_at >= DRIFT_CHECK_SETTLE_SECONDS) {
            app->cal.drift_phase = DRIFT_MEASURE;
            app->cal.drift_phase_started_at = now;
        }
        return;
    }

    /* DRIFT_MEASURE */
    if (have_block && app->frame.spectrum_ready &&
        app->cal.drift_recent_count < DRIFT_RECENT) {
        struct gsm_fcch_result fcch;
        double target = (double)app->cal.gsm_expected_hz -
                        (double)app->applied.frequency_hz + GSM_FCCH_TONE_HZ;
        if (gsm_fcch_detect(app->frame.i_samples, app->frame.q_samples, app->frame.pair_count,
                            app->applied.sample_rate_hz, target,
                            GSM_FCCH_SEARCH_HALF_HZ, &fcch)) {
            double carrier = (double)app->applied.frequency_hz +
                             fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
            app->cal.drift_recent_ppm[app->cal.drift_recent_count++] =
                (carrier - (double)app->cal.gsm_expected_hz) /
                (double)app->cal.gsm_expected_hz * 1000000.0;
        }
    }
    if (now - app->cal.drift_phase_started_at < DRIFT_CHECK_MEASURE_SECONDS)
        return;

    receiver_return(app, &app->cal.drift_token);
    app->cal.drift_phase = DRIFT_IDLE;
    app->cal.drift_last_check_at = monotonic_seconds();

    if (app->cal.drift_recent_count >= DRIFT_MIN_MEASUREMENTS) {
        double center = 0.0;
        double spread = 0.0;
        robust_center_spread(app->cal.drift_recent_ppm, app->cal.drift_recent_count,
                             &center, &spread);
        app->cal.drift_ppm = center;
        if (fabs(center) >= DRIFT_MAX_PPM) {
            app->cal.drift_health = CAL_HEALTH_DRIFT;
            snprintf(app->cal.drift_notice, sizeof(app->cal.drift_notice),
                     "Frequency drift %+.1f PPM on ARFCN %d -- recalibrate",
                     center, app->cal.gsm_arfcn);
            fprintf(stderr, "GSM drift check: %+.2f PPM on ARFCN %d\n",
                    center, app->cal.gsm_arfcn);
        } else {
            app->cal.drift_health = CAL_HEALTH_GOOD;
            app->cal.drift_notice[0] = '\0';
        }
    } else {
        /* Inconclusive (tone not found); keep the prior state, retry later. */
        app->cal.drift_health = app->cal.drift_health_prev;
    }
    /*
     * The verdict, every time, including the inconclusive one.
     *
     * This check runs unattended every five minutes and interrupts whichever
     * screen is up, and until now it reported only by changing the colour of
     * a dot -- so a session that drifted at three in the morning left nothing
     * saying when, by how much, or how many times the tone was simply not
     * found. `measurements` below is what tells a real "no drift" from a
     * check that never measured anything.
     */
    debug_log_write("cal",
                    "drift %+.2f ppm on arfcn %d, measurements %d, health %s",
                    app->cal.drift_ppm, app->cal.gsm_arfcn,
                    app->cal.drift_recent_count,
                    app->cal.drift_health == CAL_HEALTH_DRIFT ? "drift"
                        : app->cal.drift_health == CAL_HEALTH_GOOD ? "good"
                        : "unknown");
}
