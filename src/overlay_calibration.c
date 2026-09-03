#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "view.h"
#include "calibration_gate.h"
#include "lte_dsp.h"
#include "chrome_layout.h"
#include "calibration_layout.h"
#include "calibration_nav.h"
#include "lte_scan.h"

/*
 * Time here is monotonic_seconds(), not raylib's GetTime(). Only differences
 * are ever taken, so the epoch does not matter -- but raylib's clock needs a
 * window, and the calibration has to be runnable without one. A gate this
 * program will not open unless somebody clicks is a decision no check can
 * reach, which is the thing ADR-0012 forbids.
 */
#include "sdrgui.h"

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
    app->calibration_technology = technology;
    if (technology == 1) {
        snprintf(app->cal.channel, sizeof(app->cal.channel), "6200");
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Pick a band and Scan, or type an EARFCN, then press Start");
    } else if (technology == 0) {
        snprintf(app->cal.channel, sizeof(app->cal.channel), "113");
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Select GSM 900 ARFCN 1-124, then press Start");
    } else {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "5G channel tables are not implemented yet");
    }
    app->cal.channel_length = (int)strlen(app->cal.channel);
}

void open_calibration(struct app *app) {
    app->calibration_open = 1;
    app->cal.running = 0;
    app->calibration_technology = 0;
    app->cal.band = 0;
    snprintf(app->cal.channel, sizeof(app->cal.channel),
             "113");
    app->cal.channel_length = 3;
    app->calibration_expected_hz = 0;
    calibration_tracker_init(&app->cal.track);
    app->cal.fcch_confidence = 0.0f;
    app->scan_open = 0;
    app->scan_running = 0;
    app->cal.measured_hz = 0.0;
    app->cal.offset_hz = 0.0;
    app->cal.return_frequency = app->applied_frequency;
    app->cal.suggested_ppm = app->applied_ppm;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "Select GSM 900 ARFCN 1-124, then press Start");
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
static int start_lte_calibration(struct app *app) {
    int earfcn;
    uint32_t carrier;

    if (parse_int(app->cal.channel, &earfcn) < 0 || earfcn <= 0 ||
        !lte_earfcn_downlink_hz((unsigned int)earfcn, &carrier)) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Not an LTE downlink EARFCN this band table knows");
        return -1;
    }
    app->calibration_expected_hz = carrier;
    /* Tuned to the carrier's centre, not beside it as an ARFCN is: LTE never
       transmits on the middle subcarrier, so the receiver's own DC spike
       lands where the standard already leaves a hole. */
    app->cal.tune_hz = carrier;
    app->cal.measured_hz = 0.0;
    app->cal.offset_hz = 0.0;
    calibration_tracker_init(&app->cal.track);
    app->cal.track.source = CALIBRATION_SOURCE_LTE;
    if (!app->cal_return_sample_rate)
        app->cal_return_sample_rate = app->applied_sample_rate;
    if (retune_receiver_at_rate(app, app->cal.tune_hz, LTE_SAMPLE_RATE_HZ,
                                app->applied_ppm) < 0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "The receiver would not take LTE's 1.92 MS/s");
        return -1;
    }
    app->cal.started_at = monotonic_seconds();
    app->cal.running = 1;
    app->lte_cal_earfcn = earfcn;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "Measuring LTE EARFCN %d at %.3f MHz", earfcn,
             carrier / 1000000.0);
    return 0;
}

int start_calibration(struct app *app) {
    int arfcn;
    uint32_t expected;
    if (app->calibration_technology == 1)
        return start_lte_calibration(app);
    if (app->calibration_technology != 0 || app->cal.band != 0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Only 2G and 4G are supported in this version");
        return -1;
    }
    if (app->applied_sample_rate < 1000000U) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "GSM calibration requires a sample rate of at least 1 MS/s");
        return -1;
    }
    if (parse_int(app->cal.channel, &arfcn) < 0 ||
        arfcn < 1 || arfcn > 124 ||
        !gsm_downlink_hz((unsigned int)arfcn, &expected)) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "GSM 900 ARFCN must be between 1 and 124");
        return -1;
    }

    app->calibration_expected_hz = expected;
    app->cal.tune_hz = expected - 400000U;
    app->cal.measured_hz = 0.0;
    app->cal.offset_hz = 0.0;
    calibration_tracker_init(&app->cal.track);
    if (retune_receiver(app, app->cal.tune_hz, app->applied_ppm) < 0)
        return -1;
    app->cal.started_at = monotonic_seconds();
    app->cal.running = 1;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "Measuring GSM 900 ARFCN %d at %.3f MHz", arfcn,
             expected / 1000000.0);
    return 0;
}


static void calibration_set_status(struct app *app) {
    snprintf(app->calibration_status, sizeof(app->calibration_status),
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

    if (app->pair_count < LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE)
        return;
    if (lte_cell_search(app->i_samples, app->q_samples, app->pair_count,
                        (double)app->applied_sample_rate, &cell, NULL) != 1) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "No LTE cell found at EARFCN %d", app->lte_cal_earfcn);
        return;
    }
    /* The buffer must never hold two references at once (ADR-0004), and with
       three sources there are more ways to get that wrong than there were. */
    calibration_tracker_use(&app->cal.track, CALIBRATION_SOURCE_LTE);
    app->cal.fcch_confidence = cell.pss_correlation;
    app->cal.prominence_db = cell.pss_correlation;
    app->cal.measured_hz = (double)app->calibration_expected_hz +
                           cell.frequency_offset_hz;
    app->cal.offset_hz = cell.frequency_offset_hz;
    observed_ppm = app->cal.offset_hz /
                   (double)app->calibration_expected_hz * 1000000.0;
    calibration_tracker_observe(&app->cal.track, observed_ppm);
    app->cal.suggested_ppm = sdr_dsp_corrected_ppm(
        app->applied_ppm, app->cal.measured_hz,
        (double)app->calibration_expected_hz);
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
    snprintf(app->calibration_status, sizeof(app->calibration_status),
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
    if (!app->cal_lte_scanning)
        return;
    update_lte_scan(app, monotonic_seconds(), 1);
    if (lte_scan_running(app)) {
        const struct lte_band *band =
            lte_band_for_number(lte_reachable_band(app->cal_lte_band));
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Scanning band %d: %d of %d channels, %d cells so far",
                 band ? band->band : 0, app->lte.scan.candidate + 1,
                 app->lte.scan.total, app->lte.scan.found_count);
        return;
    }
    app->cal_lte_scanning = 0;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             app->lte.scan.found_count > 0
                 ? "%d cells found -- pick one to calibrate against"
                 : "No cells found in that band",
             app->lte.scan.found_count);
}

void update_calibration_measurement(struct app *app) {
    if (!app->calibration_open || app->scan_open)
        return;
    if (app->cal_lte_scanning) {
        update_lte_calibration_scan(app);
        return;
    }
    if (!app->cal.running)
        return;
    if (app->calibration_technology == 1) {
        double waited = monotonic_seconds() - app->cal.started_at;
        if (waited < CALIBRATION_SETTLE_SECONDS) {
            snprintf(app->calibration_status, sizeof(app->calibration_status),
                     "Settling receiver... %.1f s", waited);
            return;
        }
        update_lte_calibration(app);
        return;
    }
    if (!app->spectrum_ready)
        return;

    double lower = (double)app->applied_frequency -
                   app->applied_sample_rate / 2.0;
    double upper = (double)app->applied_frequency +
                   app->applied_sample_rate / 2.0;
    double elapsed = monotonic_seconds() - app->cal.started_at;
    if (elapsed < CALIBRATION_SETTLE_SECONDS) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Settling receiver... %.1f s", elapsed);
        return;
    }

    /* FCCH detection is independent of the centroid: a dip in centroid
       prominence must not wipe an FCCH accumulation. */
    struct gsm_fcch_result fcch;
    double fcch_target = (double)app->calibration_expected_hz -
                         (double)app->applied_frequency +
                         GSM_FCCH_TONE_HZ;
    int have_fcch = gsm_fcch_detect(app->i_samples, app->q_samples,
                                           app->pair_count,
                                           app->applied_sample_rate,
                                           fcch_target,
                                           GSM_FCCH_SEARCH_HALF_HZ, &fcch);

    /* The centroid supplies the peak/floor/prominence metrics and the
       carrier estimate used in centroid mode. */
    struct sdr_channel_estimate estimate;
    int have_centroid = sdr_dsp_estimate_channel_center(
        app->spectrum_average, SDR_DSP_FFT_SIZE, lower, upper,
        app->calibration_expected_hz, 100000.0, 50000.0,
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
        measured_hz = (double)app->applied_frequency +
                      fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
        break;
    case CALIBRATION_HOLD_TONE:
        calibration_set_status(app); /* hold the tone lock */
        return;
    case CALIBRATION_USE_CENTROID:
        measured_hz = estimate.measured_frequency_hz;
        break;
    default:
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "No isolated GSM carrier at least 8 dB above guard-band "
                 "floor");
        return;
    }

    app->cal.measured_hz = measured_hz;
    app->cal.offset_hz = measured_hz - app->calibration_expected_hz;
    double observed_ppm = app->cal.offset_hz /
                          app->calibration_expected_hz * 1000000.0;
    /* Individual 65 ms blocks scatter a lot on a modulated GSM channel, but the
       correction applied is the centre of the recent residuals, whose
       uncertainty is the standard error of that centre, not the per-block
       spread. The tracker keeps both. */
    calibration_tracker_observe(&app->cal.track, observed_ppm);

    app->cal.suggested_ppm = sdr_dsp_corrected_ppm(
        app->applied_ppm, app->calibration_expected_hz *
                              (1.0 + app->cal.track.recent_center / 1000000.0),
        app->calibration_expected_hz);
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





/* Periodically verify the applied PPM against the calibrated GSM carrier. Each
   check briefly retunes to the calibrated channel, measures the FCCH residual,
   then retunes back to the view frequency, so it only runs when enabled, a
   valid FCCH-backed calibration exists, and no overlay owns the tuning. See
   docs/adr/0006-gsm-drift-indicator.md. */
void update_drift_check(struct app *app, int have_block) {
    if (!app->auto_drift_check || !app->gsm_cal_valid || !app->receiver_mode)
        return;
    if (app->calibration_open || app->scan_open || app->settings_open)
        return;

    double now = monotonic_seconds();

    if (app->drift_phase == DRIFT_IDLE) {
        if (now - app->cal.drift_last_check_at < DRIFT_CHECK_INTERVAL_SECONDS)
            return;
        app->cal.drift_saved_frequency = app->applied_frequency;
        app->cal.drift_health_prev = app->drift_health;
        if (retune_receiver(app, app->cal.gsm_cal_tune_hz, app->gsm_cal_ppm) < 0) {
            app->cal.drift_last_check_at = now; /* retry next interval */
            return;
        }
        app->cal.drift_recent_count = 0;
        app->drift_phase = DRIFT_SETTLE;
        app->cal.drift_phase_started_at = now;
        app->drift_health = CAL_HEALTH_CHECKING;
        return;
    }

    if (app->drift_phase == DRIFT_SETTLE) {
        if (now - app->cal.drift_phase_started_at >= DRIFT_CHECK_SETTLE_SECONDS) {
            app->drift_phase = DRIFT_MEASURE;
            app->cal.drift_phase_started_at = now;
        }
        return;
    }

    /* DRIFT_MEASURE */
    if (have_block && app->spectrum_ready &&
        app->cal.drift_recent_count < DRIFT_RECENT) {
        struct gsm_fcch_result fcch;
        double target = (double)app->cal.gsm_cal_expected_hz -
                        (double)app->applied_frequency + GSM_FCCH_TONE_HZ;
        if (gsm_fcch_detect(app->i_samples, app->q_samples, app->pair_count,
                            app->applied_sample_rate, target,
                            GSM_FCCH_SEARCH_HALF_HZ, &fcch)) {
            double carrier = (double)app->applied_frequency +
                             fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
            app->cal.drift_recent_ppm[app->cal.drift_recent_count++] =
                (carrier - (double)app->cal.gsm_cal_expected_hz) /
                (double)app->cal.gsm_cal_expected_hz * 1000000.0;
        }
    }
    if (now - app->cal.drift_phase_started_at < DRIFT_CHECK_MEASURE_SECONDS)
        return;

    retune_receiver(app, app->cal.drift_saved_frequency, app->gsm_cal_ppm);
    app->drift_phase = DRIFT_IDLE;
    app->cal.drift_last_check_at = monotonic_seconds();

    if (app->cal.drift_recent_count >= DRIFT_MIN_MEASUREMENTS) {
        double center = 0.0;
        double spread = 0.0;
        robust_center_spread(app->cal.drift_recent_ppm, app->cal.drift_recent_count,
                             &center, &spread);
        app->cal.drift_ppm = center;
        if (fabs(center) >= DRIFT_MAX_PPM) {
            app->drift_health = CAL_HEALTH_DRIFT;
            snprintf(app->drift_notice, sizeof(app->drift_notice),
                     "Frequency drift %+.1f PPM on ARFCN %d -- recalibrate",
                     center, app->gsm_cal_arfcn);
            fprintf(stderr, "GSM drift check: %+.2f PPM on ARFCN %d\n",
                    center, app->gsm_cal_arfcn);
        } else {
            app->drift_health = CAL_HEALTH_GOOD;
            app->drift_notice[0] = '\0';
        }
    } else {
        /* Inconclusive (tone not found); keep the prior state, retry later. */
        app->drift_health = app->cal.drift_health_prev;
    }
}

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
    if (app->cal_return_sample_rate &&
        app->cal_return_sample_rate != app->applied_sample_rate) {
        if (retune_receiver_at_rate(app, app->cal.return_frequency,
                                    app->cal_return_sample_rate,
                                    app->applied_ppm) < 0)
            return -1;
        app->cal_return_sample_rate = 0;
    } else if (app->cal.running) {
        if (retune_receiver(app, app->cal.return_frequency,
                            app->applied_ppm) < 0)
            return -1;
    }
    app->cal_return_sample_rate = 0;
    app->cal.running = 0;
    return 0;
}

void close_calibration(struct app *app) {
    if (calibration_stop_measuring(app) < 0)
        return;
    app->calibration_open = 0;
}

/* Whether the 2G scan left results worth returning to. Its powers survive the
   overlay closing -- only a fresh scan clears them -- so reopening the list
   costs nothing and shows what was actually measured. */
static int calibration_scan_has_results(const struct app *app) {
    int arfcn;
    for (arfcn = 1; arfcn <= 124; arfcn++)
        if (app->scan_power[arfcn] > SCAN_SENTINEL_DBFS)
            return 1;
    return 0;
}

void adjust_waterfall_scale(struct app *app, int zoom_in) {
    app->waterfall_lower_dbfs += zoom_in ? DB_SCALE_STEP : -DB_SCALE_STEP;
    app->waterfall_lower_dbfs = fmaxf(
        SDR_DSP_DBFS_FLOOR,
        fminf(app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS - 20.0f));
    render_waterfall(app);
}

void handle_calibration_input(struct app *app) {
    struct calibration_layout cl =
        calibration_layout_now(app->calibration_technology == 1);
    Rectangle tech_2g = cl.tech[0];
    Rectangle tech_4g = cl.tech[1];
    Rectangle tech_5g = cl.tech[2];
    Rectangle scan = cl.scan;
    Rectangle start = cl.start;
    Rectangle apply_ppm = cl.apply_ppm;
    Rectangle back = cl.back;

    int inputs_changed = 0;

    /*
     * The 4G controls: pick a band, scan it, pick a cell. Typing an EARFCN
     * still works and is faster when you know one, but nobody knows one for a
     * band they have not looked at -- which is the whole reason GSM has a scan
     * and this needed one.
     */
    if (app->calibration_technology == 1 && !app->cal.running) {
        int b;
        for (b = 0; b < CALIBRATION_LTE_BANDS; b++)
            if (clicked(cl.lte_band[b])) {
                app->cal_lte_band = b;
                app->lte.scan.found_count = 0;
                inputs_changed = 1;
            }
        if (clicked(cl.lte_scan) && !app->cal_lte_scanning) {
            const struct lte_band *band =
                lte_band_for_number(lte_reachable_band(app->cal_lte_band));
            if (!app->receiver_mode) {
                snprintf(app->calibration_status,
                         sizeof(app->calibration_status),
                         "A band scan needs a live receiver");
            } else {
                /* The scan runs on LTE's own grid, like everything else that
                   looks for a cell (ADR-0014). */
                if (!app->cal_return_sample_rate)
                    app->cal_return_sample_rate = app->applied_sample_rate;
                if (retune_receiver_at_rate(app, app->applied_frequency,
                                            LTE_SAMPLE_RATE_HZ,
                                            app->applied_ppm) == 0 &&
                    band &&
                    lte_scan_begin(app, band->band, monotonic_seconds()) == 0) {
                    app->cal_lte_scanning = 1;
                    snprintf(app->calibration_status,
                             sizeof(app->calibration_status),
                             "Scanning band %d, about %.0f s for the first "
                             "pass", band->band,
                             lte_scan_first_pass_seconds(band));
                } else {
                    snprintf(app->calibration_status,
                             sizeof(app->calibration_status),
                             "Could not start the band scan");
                }
            }
            return;
        }
        if (!app->cal_lte_scanning && app->lte.scan.found_count > 0) {
            int row = calibration_cell_row_at(cl.cell_list,
                                              app->lte.scan.found_count,
                                              GetMousePosition());
            if (row >= 0 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                /* Picking a cell is choosing the EARFCN and starting on it,
                   which is what the operator meant by clicking it. */
                snprintf(app->cal.channel, sizeof(app->cal.channel), "%u",
                         app->lte.scan.found[row].earfcn);
                app->cal.channel_length = (int)strlen(app->cal.channel);
                start_calibration(app);
                return;
            }
        }
    }

    if (!app->cal.running && clicked(tech_2g)) {
        calibration_select_technology(app, 0);
        inputs_changed = 1;
    }
    if (!app->cal.running && clicked(tech_4g)) {
        calibration_select_technology(app, 1);
        inputs_changed = 1;
    }
    if (!app->cal.running && clicked(tech_5g)) {
        calibration_select_technology(app, 2);
        inputs_changed = 1;
    }

    int character;
    while (app->calibration_technology <= 1 &&
           (character = GetCharPressed()) != 0) {
        if (character >= '0' && character <= '9' &&
            app->cal.channel_length <
                (int)sizeof(app->cal.channel) - 1) {
            app->cal.channel[app->cal.channel_length++] =
                (char)character;
            app->cal.channel[app->cal.channel_length] = '\0';
            inputs_changed = 1;
        }
    }
    if (app->calibration_technology <= 1 &&
        IsKeyPressed(KEY_BACKSPACE) &&
        app->cal.channel_length > 0) {
        app->cal.channel[--app->cal.channel_length] = '\0';
        inputs_changed = 1;
    }
    if (inputs_changed) {
        app->cal.track.stable = 0;
        app->cal.track.measurements = 0;
        app->cal.track.recent_count = 0;
        app->cal.track.recent_head = 0;
        app->cal.track.recent_center = 0.0;
        app->cal.track.recent_spread = 0.0;
        app->cal.track.recent_sem = 0.0;
        if (app->cal.running)
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "Editing target ARFCN; press Start to retune");
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
        adjust_waterfall_scale(app, 1);
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
        adjust_waterfall_scale(app, 0);
    if ((clicked(start) || IsKeyPressed(KEY_ENTER)) &&
        app->calibration_technology == 0)
        start_calibration(app);
    if (clicked(scan) && app->calibration_technology == 0)
        start_scan(app);
    if (clicked(apply_ppm) && app->cal.track.stable) {
        if (retune_receiver(app, app->cal.tune_hz,
                            app->cal.suggested_ppm) == 0) {
            app->options.ppm = app->cal.suggested_ppm;
            if (app->calibration_technology == 1) {
                app->lte_cal_valid = 1;
                app->lte_cal_ppm = app->cal.suggested_ppm;
            }
            /* A measured correction belongs to where it was measured. */
            if (app->config.site[0] &&
                config_set_site_ppm(&app->config, app->config.site,
                                    app->cal.suggested_ppm))
                config_save(&app->config);
            app->cal.track.measurements = 0;
            app->cal.track.recent_count = 0;
            app->cal.track.recent_head = 0;
            app->cal.track.stable = 0;
            app->cal.started_at = monotonic_seconds();
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "Applied %+d PPM; measuring residual error",
                     app->applied_ppm);
            /* The health indicator turns green only for an FCCH-backed lock;
               record the calibrated channel so drift can be re-checked. */
            if (app->cal.track.source == CALIBRATION_SOURCE_FCCH) {
                int arfcn = 0;
                parse_int(app->cal.channel, &arfcn);
                app->gsm_cal_valid = 1;
                app->cal.gsm_cal_expected_hz = app->calibration_expected_hz;
                app->cal.gsm_cal_tune_hz = app->cal.tune_hz;
                app->gsm_cal_ppm = app->applied_ppm;
                app->gsm_cal_arfcn = arfcn;
                app->drift_health = CAL_HEALTH_GOOD;
                app->cal.drift_ppm = 0.0;
                app->drift_notice[0] = '\0';
                app->drift_phase = DRIFT_IDLE;
                app->cal.drift_last_check_at = monotonic_seconds();
            } else {
                app->gsm_cal_valid = 0;
                app->drift_health = CAL_HEALTH_UNKNOWN;
                app->drift_notice[0] = '\0';
            }
        }
    }
    /*
     * Back is one step up, Exit leaves. Escape follows Back while there is a
     * step to take and Exit once there is not, so it never traps and never
     * skips the list on the way out.
     */
    {
        enum calibration_back target =
            calibration_back_target(app->calibration_technology,
                                    app->cal.running,
                                    calibration_scan_has_results(app));
        int escape = IsKeyPressed(KEY_ESCAPE);

        if ((clicked(back) || escape) && target != CALIBRATION_BACK_NONE) {
            if (calibration_stop_measuring(app) == 0 &&
                target == CALIBRATION_BACK_SCAN)
                app->scan_open = 1;
            return;
        }
        if (clicked(cl.exit) || escape) {
            close_calibration(app);
            /* Out of calibration is out to the survey: it is where the
               program opens and the only view that says what is on air
               rather than what one tuning looks like. */
            if (!app->calibration_open)
                app->view = VIEW_SURVEY;
        }
    }
}

void draw_calibration(struct app *app) {
    char text[256];
    struct calibration_layout cl =
        calibration_layout_now(app->calibration_technology == 1);
    Rectangle tech_2g = cl.tech[0];
    Rectangle tech_4g = cl.tech[1];
    Rectangle tech_5g = cl.tech[2];
    Rectangle scan = cl.scan;
    Rectangle channel = cl.channel;
    Rectangle start = cl.start;
    Rectangle apply_ppm = cl.apply_ppm;
    Rectangle back = cl.back;

    DrawText("Cellular frequency calibration", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    DrawText("Technology", 24, 50, 16, (Color){ 157, 180, 194, 255 });
    draw_button(tech_2g, "2G", app->calibration_technology == 0);
    draw_button(tech_4g, "4G", app->calibration_technology == 1);
    draw_button(tech_5g, "5G", app->calibration_technology == 2);
    sdrgui_text_fit(app->calibration_technology == 0
                        ? "Band: GSM 900"
                        : app->calibration_technology == 1
                              ? "Band: LTE, 1.92 MS/s while measuring"
                              : "Band: unavailable",
                    (int)cl.band_label.x, (int)cl.band_label.y, 17,
                    cl.band_label.width, (Color){ 209, 221, 228, 255 });
    DrawText(app->calibration_technology == 1 ? "EARFCN" : "ARFCN",
             (int)channel.x, 50, 16, (Color){ 157, 180, 194, 255 });
    /*
     * The picker takes the chart's rectangle, so exactly one of them is drawn.
     * Both were, and the waterfall went over the list -- an overlap no
     * geometry check can see, because the two agree about where they are.
     */
    if (app->calibration_technology == 1 && !app->cal.running) {
        char row[128];
        int b, i, rows;

        for (b = 0; b < CALIBRATION_LTE_BANDS; b++) {
            snprintf(row, sizeof(row), "Band %d", lte_reachable_band(b));
            draw_button(cl.lte_band[b], row, b == app->cal_lte_band);
        }
        draw_button(cl.lte_scan,
                    app->cal_lte_scanning ? "Scanning" : "Scan band",
                    app->cal_lte_scanning);

        DrawRectangleRec(cl.cell_list, (Color){ 6, 10, 17, 255 });
        DrawRectangleLinesEx(cl.cell_list, 1.0f, (Color){ 82, 109, 126, 255 });
        snprintf(row, sizeof(row), "Cells found (%d)",
                 app->lte.scan.found_count);
        DrawText(row, (int)cl.cell_list.x + 10, (int)cl.cell_list.y + 8, 16,
                 (Color){ 151, 174, 188, 255 });
        rows = app->lte.scan.found_count;
        if (rows > calibration_cell_rows(cl.cell_list))
            rows = calibration_cell_rows(cl.cell_list);
        if (rows == 0) {
            DrawText(app->cal_lte_scanning ? "scanning..."
                                           : "pick a band and press Scan",
                     (int)cl.cell_list.x + 10, (int)cl.cell_list.y + 34, 16,
                     (Color){ 126, 151, 166, 255 });
        }
        for (i = 0; i < rows; i++) {
            const struct lte_found_cell *found = &app->lte.scan.found[i];
            float y = cl.cell_list.y + 30.0f +
                      CALIBRATION_CELL_ROW_H * (float)i;
            int hovered = calibration_cell_row_at(cl.cell_list, rows,
                                                  GetMousePosition()) == i;
            if (hovered)
                DrawRectangle((int)cl.cell_list.x + 1, (int)y,
                              (int)cl.cell_list.width - 2,
                              (int)CALIBRATION_CELL_ROW_H,
                              (Color){ 255, 174, 62, 40 });
            /* The correlation is on the row because it is what decides
               whether the offset this cell yields is worth believing. */
            snprintf(row, sizeof(row),
                     "EARFCN %-6u %9.3f MHz   cell %-4d PSS %.2f",
                     found->earfcn, found->frequency_hz / 1e6, found->pci,
                     (double)found->pss);
            DrawText(row, (int)cl.cell_list.x + 10, (int)y + 5, 16,
                     found->pss >= CALIBRATION_MIN_PSS
                         ? (Color){ 213, 226, 234, 255 }
                         : (Color){ 150, 140, 120, 255 });
        }
    }
    /* 4G takes a channel too now. The field said N/A because it was written
       when only 2G did, and nothing made it say otherwise when 4G started
       working -- so the overlay offered an EARFCN caption over a field that
       refused to show one. */
    sdrgui_text_field(channel,
                      app->calibration_technology <= 1
                          ? app->cal.channel
                          : "N/A",
                      app->calibration_technology <= 1);
    draw_button(start, app->cal.running ? "Retune" : "Start",
                app->calibration_technology == 0);
    draw_button(apply_ppm, "Apply PPM", app->cal.track.stable);
    draw_button(scan, "Scan", app->calibration_technology == 0);
    draw_button_enabled(back, "Back",
                        calibration_back_target(app->calibration_technology,
                                                app->cal.running,
                                                calibration_scan_has_results(app))
                            != CALIBRATION_BACK_NONE);
    draw_button(cl.exit, "Exit", 0);

    snprintf(text, sizeof(text),
             "expected: %.6f MHz   tuned center: %.6f MHz   current correction: %+d PPM",
             app->calibration_expected_hz / 1000000.0,
             app->applied_frequency / 1000000.0, app->applied_ppm);
    sdrgui_text_fit(text, (int)cl.status[0].x, (int)cl.status[0].y, 17,
                    cl.status[0].width, (Color){ 190, 208, 218, 255 });
    if (app->cal.track.measurements > 0) {
        snprintf(text, sizeof(text),
                 "measured: %.6f MHz   offset: %+.1f kHz   observed: %+.2f PPM   center: %+.2f +/- %.2f PPM (SEM %.2f)",
                 app->cal.measured_hz / 1000000.0,
                 app->cal.offset_hz / 1000.0,
                 app->cal.offset_hz /
                     app->calibration_expected_hz * 1000000.0,
                 app->cal.track.recent_center,
                 app->cal.track.recent_spread,
                 app->cal.track.recent_sem);
        sdrgui_text_fit(text, (int)cl.status[1].x, (int)cl.status[1].y, 17,
                        cl.status[1].width, (Color){ 255, 205, 91, 255 });
        snprintf(text, sizeof(text),
                 "peak: %.1f dBFS   guard floor: %.1f dBFS   prominence: %.1f dB   suggested correction: %+d PPM",
                 app->cal.peak_dbfs, app->cal.floor_dbfs,
                 app->cal.prominence_db,
                 app->cal.suggested_ppm);
        sdrgui_text_fit(text, (int)cl.status[2].x, (int)cl.status[2].y, 17,
                        cl.status[2].width,
                 app->cal.track.stable ? (Color){ 99, 228, 170, 255 }
                                         : (Color){ 250, 190, 74, 255 });
    }
    sdrgui_text_fit(app->calibration_status, (int)cl.status[3].x,
                    (int)cl.status[3].y, 17, cl.status[3].width,
             (Color){ 158, 204, 230, 255 });

    /* One or the other, never both. Before a cell is chosen the chart has
       nothing to say, and the picker is what the operator needs to see. */
    if (app->calibration_technology == 1 && !app->cal.running)
        return;

    /*
     * Into the layout's chart, not app->plot.
     *
     * draw_waterfall draws into the Scope's plot rectangle, which begins
     * higher up the window than this overlay's chart does -- so the waterfall
     * came out over the status line, while the expected and measured markers
     * below were placed against cl.chart and therefore against a different
     * chart from the one they marked. check-layout could not see either: it
     * compares the rectangles the layout returns, and app->plot is not one of
     * them.
     */
    draw_waterfall_rect(app, 1, cl.chart,
                        (double)app->calibration_expected_hz);
    if (app->calibration_expected_hz > 0) {
        double full_lower = (double)app->applied_frequency -
                            app->applied_sample_rate / 2.0;
        double full_upper = (double)app->applied_frequency +
                            app->applied_sample_rate / 2.0;
        double lower = (double)app->calibration_expected_hz -
                       CALIBRATION_VIEW_HALF_WIDTH_HZ;
        double upper = (double)app->calibration_expected_hz +
                       CALIBRATION_VIEW_HALF_WIDTH_HZ;
        if (lower < full_lower)
            lower = full_lower;
        if (upper > full_upper)
            upper = full_upper;
        if (upper - lower <= 1.0) {
            lower = full_lower;
            upper = full_upper;
        }
        float expected_x = cl.chart.x +
                           (float)((app->calibration_expected_hz - lower) /
                                   (upper - lower)) * cl.chart.width;
        DrawLine((int)expected_x, (int)cl.chart.y, (int)expected_x,
                 (int)(cl.chart.y + cl.chart.height),
                 (Color){ 87, 229, 173, 230 });
        DrawText("expected", (int)expected_x + 5, (int)cl.chart.y + 5, 16,
                 (Color){ 111, 244, 191, 255 });
        if (app->cal.track.measurements > 0) {
            float measured_x = cl.chart.x +
                               (float)((app->cal.measured_hz - lower) /
                                       (upper - lower)) * cl.chart.width;
            DrawLine((int)measured_x, (int)cl.chart.y, (int)measured_x,
                     (int)(cl.chart.y + cl.chart.height),
                     (Color){ 255, 181, 59, 240 });
            DrawText("measured", (int)measured_x + 5,
                      (int)cl.chart.y + 25, 16,
                      (Color){ 255, 202, 105, 255 });
        }
    }
}




/*
 * One circle per reference. Two, because two independent measurements of one
 * crystal are worth far more than either alone -- and the moment they stop
 * agreeing is the moment to distrust the correction, which a single dot could
 * never show.
 */
void draw_health_indicator(const struct app *app) {
    struct chrome_layout chrome = chrome_layout_now();
    struct sdrgui_health_params gsm;
    struct sdrgui_health_params lte;

    memset(&gsm, 0, sizeof(gsm));
    gsm.centre = chrome.gsm_dot;
    gsm.state = app->drift_health;
    gsm.label = "GSM cal";
    gsm.channel_name = "ARFCN";
    gsm.channel = app->gsm_cal_arfcn;
    gsm.notice = app->drift_notice;
    sdrgui_health_dot(&gsm);

    memset(&lte, 0, sizeof(lte));
    lte.centre = chrome.lte_dot;
    /*
     * No drift monitor behind this one yet, so it says only what it knows:
     * measuring now, measured and applied, or nothing yet. Claiming a health
     * it has not checked would be worse than an honest grey.
     */
    if (app->calibration_open && app->cal.running &&
        app->calibration_technology == 1)
        lte.state = SDRGUI_HEALTH_CHECKING;
    else if (app->lte_cal_valid)
        lte.state = SDRGUI_HEALTH_GOOD;
    else
        lte.state = SDRGUI_HEALTH_UNKNOWN;
    lte.label = "LTE cal";
    lte.channel_name = "EARFCN";
    lte.channel = app->lte_cal_earfcn;
    lte.notice = NULL;
    sdrgui_health_dot(&lte);
}
