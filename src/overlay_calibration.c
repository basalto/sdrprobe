#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "view.h"
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

void open_calibration(struct app *app) {
    app->calibration_open = 1;
    app->calibration_running = 0;
    app->calibration_technology = 0;
    app->calibration_band = 0;
    snprintf(app->calibration_channel, sizeof(app->calibration_channel),
             "113");
    app->calibration_channel_length = 3;
    app->calibration_expected_hz = 0;
    app->calibration_measurements = 0;
    app->calibration_recent_count = 0;
    app->calibration_recent_head = 0;
    app->calibration_recent_center = 0.0;
    app->calibration_recent_spread = 0.0;
    app->calibration_recent_sem = 0.0;
    app->calibration_fcch_locked = 0;
    app->calibration_fcch_confidence = 0.0f;
    app->calibration_source = CALIBRATION_SOURCE_CENTROID;
    app->calibration_fcch_miss = 0;
    app->calibration_fcch_hits = 0;
    app->scan_open = 0;
    app->scan_running = 0;
    app->calibration_stable = 0;
    app->calibration_measured_hz = 0.0;
    app->calibration_offset_hz = 0.0;
    app->calibration_return_frequency = app->applied_frequency;
    app->calibration_suggested_ppm = app->applied_ppm;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "Select GSM 900 ARFCN 1-124, then press Start");
}

static int start_calibration(struct app *app) {
    int arfcn;
    uint32_t expected;
    if (app->calibration_technology != 0 || app->calibration_band != 0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Only 2G GSM 900 is supported in this version");
        return -1;
    }
    if (app->applied_sample_rate < 1000000U) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "GSM calibration requires a sample rate of at least 1 MS/s");
        return -1;
    }
    if (parse_int(app->calibration_channel, &arfcn) < 0 ||
        arfcn < 1 || arfcn > 124 ||
        !gsm_downlink_hz((unsigned int)arfcn, &expected)) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "GSM 900 ARFCN must be between 1 and 124");
        return -1;
    }

    app->calibration_expected_hz = expected;
    app->calibration_tune_hz = expected - 400000U;
    app->calibration_measurements = 0;
    app->calibration_measured_hz = 0.0;
    app->calibration_offset_hz = 0.0;
    app->calibration_recent_count = 0;
    app->calibration_recent_head = 0;
    app->calibration_recent_center = 0.0;
    app->calibration_recent_spread = 0.0;
    app->calibration_recent_sem = 0.0;
    app->calibration_source = CALIBRATION_SOURCE_CENTROID;
    app->calibration_fcch_miss = 0;
    app->calibration_fcch_hits = 0;
    app->calibration_fcch_locked = 0;
    app->calibration_stable = 0;
    if (retune_receiver(app, app->calibration_tune_hz, app->applied_ppm) < 0)
        return -1;
    app->calibration_started_at = GetTime();
    app->calibration_running = 1;
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "Measuring GSM 900 ARFCN %d at %.3f MHz", arfcn,
             expected / 1000000.0);
    return 0;
}

/* Robust center and spread of the recent PPM residuals. The center is the
   median and the spread is a normal-consistent scale estimate (1.4826 x MAD),
   both resistant to a peak that momentarily hops to an adjacent feature. */
static void robust_center_spread(const double *values, int count,
                                 double *center, double *spread) {
    double sorted[CALIBRATION_RECENT];
    double deviations[CALIBRATION_RECENT];
    int i;

    if (count <= 0) {
        *center = 0.0;
        *spread = 0.0;
        return;
    }
    for (i = 0; i < count; i++)
        sorted[i] = values[i];
    qsort(sorted, (size_t)count, sizeof(*sorted), compare_double);
    double median = (count % 2)
                        ? sorted[count / 2]
                        : 0.5 * (sorted[count / 2 - 1] + sorted[count / 2]);
    for (i = 0; i < count; i++)
        deviations[i] = fabs(values[i] - median);
    qsort(deviations, (size_t)count, sizeof(*deviations), compare_double);
    double mad = (count % 2)
                     ? deviations[count / 2]
                     : 0.5 * (deviations[count / 2 - 1] +
                              deviations[count / 2]);
    *center = median;
    *spread = 1.4826 * mad;
}

static void reset_calibration_stats(struct app *app) {
    app->calibration_measurements = 0;
    app->calibration_recent_count = 0;
    app->calibration_recent_head = 0;
    app->calibration_recent_center = 0.0;
    app->calibration_recent_spread = 0.0;
    app->calibration_recent_sem = 0.0;
    app->calibration_stable = 0;
}

static void calibration_set_status(struct app *app) {
    snprintf(app->calibration_status, sizeof(app->calibration_status),
             "%s (%s): %d meas, +/- %.2f PPM (spread %.2f), FCCH hits %d miss %d conf %.2f, suggested %+d PPM",
             app->calibration_stable ? "Stable lock" : "Acquiring",
             app->calibration_source == CALIBRATION_SOURCE_FCCH
                 ? "FCCH tone"
                 : "centroid",
             app->calibration_measurements,
             app->calibration_recent_sem,
             app->calibration_recent_spread,
             app->calibration_fcch_hits,
             app->calibration_fcch_miss,
             app->calibration_fcch_confidence,
             app->calibration_suggested_ppm);
}

void update_calibration_measurement(struct app *app) {
    if (!app->calibration_open || !app->calibration_running ||
        app->scan_open || !app->spectrum_ready)
        return;

    double lower = (double)app->applied_frequency -
                   app->applied_sample_rate / 2.0;
    double upper = (double)app->applied_frequency +
                   app->applied_sample_rate / 2.0;
    double elapsed = GetTime() - app->calibration_started_at;
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
        app->calibration_workspace, &estimate);
    if (have_centroid) {
        app->calibration_peak_hz = estimate.peak_frequency_hz;
        app->calibration_peak_dbfs = estimate.peak_dbfs;
        app->calibration_floor_dbfs = estimate.floor_dbfs;
        app->calibration_prominence_db = estimate.prominence_db;
    }

    /* FCCH and centroid residuals differ by many PPM, so the recent-residual
       buffer must never mix them: switching source resets it, and while locked
       to the tone a burst-free block is skipped rather than recorded. */
    double measured_hz;
    if (have_fcch) {
        if (app->calibration_source != CALIBRATION_SOURCE_FCCH) {
            app->calibration_source = CALIBRATION_SOURCE_FCCH;
            reset_calibration_stats(app);
            app->calibration_fcch_hits = 0;
        }
        app->calibration_fcch_miss = 0;
        app->calibration_fcch_locked = 1;
        app->calibration_fcch_confidence = fcch.confidence;
        app->calibration_fcch_hits++;
        measured_hz = (double)app->applied_frequency +
                      fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
    } else if (app->calibration_source == CALIBRATION_SOURCE_FCCH) {
        app->calibration_fcch_miss++;
        if (app->calibration_fcch_miss < CALIBRATION_FCCH_MISS_LIMIT) {
            calibration_set_status(app); /* hold the tone lock */
            return;
        }
        app->calibration_source = CALIBRATION_SOURCE_CENTROID;
        app->calibration_fcch_locked = 0;
        reset_calibration_stats(app);
        app->calibration_fcch_hits = 0;
        if (!have_centroid) {
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "No isolated GSM carrier at least 8 dB above guard-band floor");
            return;
        }
        measured_hz = estimate.measured_frequency_hz;
    } else {
        app->calibration_fcch_locked = 0;
        if (!have_centroid) {
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "No isolated GSM carrier at least 8 dB above guard-band floor");
            reset_calibration_stats(app);
            return;
        }
        measured_hz = estimate.measured_frequency_hz;
    }

    app->calibration_measured_hz = measured_hz;
    app->calibration_offset_hz = measured_hz - app->calibration_expected_hz;
    double observed_ppm = app->calibration_offset_hz /
                          app->calibration_expected_hz * 1000000.0;
    app->calibration_measurements++;
    app->calibration_recent_ppm[app->calibration_recent_head] = observed_ppm;
    app->calibration_recent_head =
        (app->calibration_recent_head + 1) % CALIBRATION_RECENT;
    if (app->calibration_recent_count < CALIBRATION_RECENT)
        app->calibration_recent_count++;

    /* Individual 65 ms blocks scatter a lot on a modulated GSM channel, but the
       correction we apply is the center of the recent residuals, whose
       uncertainty is the standard error of that center, not the per-block
       spread. Gate on the standard error so the lock reflects how well the
       correction is known. Median/MAD keep a hopping peak from biasing it. */
    robust_center_spread(app->calibration_recent_ppm,
                         app->calibration_recent_count,
                         &app->calibration_recent_center,
                         &app->calibration_recent_spread);
    app->calibration_recent_sem =
        app->calibration_recent_spread /
        sqrt((double)app->calibration_recent_count);

    app->calibration_suggested_ppm = sdr_dsp_corrected_ppm(
        app->applied_ppm, app->calibration_expected_hz *
                              (1.0 + app->calibration_recent_center / 1000000.0),
        app->calibration_expected_hz);
    if (app->calibration_suggested_ppm < -1000)
        app->calibration_suggested_ppm = -1000;
    if (app->calibration_suggested_ppm > 1000)
        app->calibration_suggested_ppm = 1000;

    /* Prominence only gates centroid mode; an FCCH tone is its own quality
       proof and its prominence metric may momentarily dip. */
    int quality_ok = (app->calibration_source == CALIBRATION_SOURCE_FCCH)
                         ? 1
                         : (app->calibration_prominence_db >= 8.0f);
    app->calibration_stable = elapsed >= CALIBRATION_MIN_SECONDS &&
                              app->calibration_measurements >= 32 &&
                              app->calibration_recent_count >= 32 &&
                              app->calibration_recent_sem <=
                                  CALIBRATION_MAX_SEM_PPM &&
                              quality_ok;
    calibration_set_status(app);
}

int scan_strongest_arfcn(const struct app *app) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        if (app->scan_power[arfcn] > best_power) {
            best_power = app->scan_power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

int scan_strongest_bcch(const struct app *app) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        if (app->scan_bcch_conf[arfcn] >= SCAN_BCCH_MIN_CONF &&
            app->scan_power[arfcn] > best_power) {
            best_power = app->scan_power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

int start_scan(struct app *app) {
    if (!app->receiver_mode) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Channel scan requires a live RTL-SDR receiver");
        return -1;
    }
    double accept_half = app->applied_sample_rate / 2.0 - SCAN_EDGE_MARGIN_HZ;
    if (accept_half < 100000.0) {
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Channel scan requires a sample rate of at least 1 MS/s");
        return -1;
    }
    app->scan_accept_half_hz = accept_half;
    app->scan_step_hz = 2.0 * accept_half;
    app->scan_first_center_hz = SCAN_BAND_LOWER_HZ + accept_half;
    int count = 0;
    double center = app->scan_first_center_hz;
    while (center - accept_half < SCAN_BAND_UPPER_HZ) {
        count++;
        center += app->scan_step_hz;
    }
    app->scan_step_count = count;
    for (int arfcn = 0; arfcn < 125; arfcn++) {
        app->scan_power[arfcn] = SCAN_SENTINEL_DBFS;
        app->scan_bcch_conf[arfcn] = 0.0f;
    }
    app->scan_selected_arfcn = 0;
    app->scan_return_frequency = app->applied_frequency;
    app->scan_step = 0;
    if (retune_receiver(app, (uint32_t)llround(app->scan_first_center_hz),
                        app->applied_ppm) < 0)
        return -1;
    app->scan_step_started_at = GetTime();
    app->scan_running = 1;
    app->scan_open = 1;
    return 0;
}

void update_scan(struct app *app) {
    if (!app->scan_running || !app->spectrum_ready)
        return;
    double elapsed = GetTime() - app->scan_step_started_at;
    if (elapsed < SCAN_STEP_SETTLE_SECONDS)
        return;

    double center = (double)app->applied_frequency;
    double lower = center - app->applied_sample_rate / 2.0;
    double upper = center + app->applied_sample_rate / 2.0;
    sdr_dsp_channel_powers(app->spectrum_average, SDR_DSP_FFT_SIZE,
                              lower, upper,
                              center - app->scan_accept_half_hz,
                              center + app->scan_accept_half_hz,
                              GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ,
                              1, 124, app->scan_power);

    /* Flag BCCH channels: probe each channel in this step's window for its
       FCCH tone (carrier + 67.708 kHz). FCCH is intermittent and a strong
       neighbour can lower its coherence, so keep the peak coherence seen at
       the tone offset across the step's blocks (never clearing it) and treat a
       channel as BCCH at a slightly relaxed scan threshold. */
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        double channel = GSM900_BASE_HZ +
                         (double)arfcn * GSM900_ARFCN_SPACING_HZ;
        if (channel < center - app->scan_accept_half_hz ||
            channel > center + app->scan_accept_half_hz)
            continue;
        struct gsm_fcch_result fcch;
        double target = channel - center + GSM_FCCH_TONE_HZ;
        gsm_fcch_detect(app->i_samples, app->q_samples,
                               app->pair_count, app->applied_sample_rate,
                               target, GSM_FCCH_SEARCH_HALF_HZ, &fcch);
        if (fcch.confidence > app->scan_bcch_conf[arfcn])
            app->scan_bcch_conf[arfcn] = fcch.confidence;
    }

    if (elapsed < SCAN_STEP_SETTLE_SECONDS + SCAN_STEP_PROBE_SECONDS)
        return; /* keep probing this step for more FCCH bursts */

    app->scan_step++;
    if (app->scan_step >= app->scan_step_count) {
        app->scan_running = 0;
        int bcch = scan_strongest_bcch(app);
        int chosen = bcch > 0 ? bcch : scan_strongest_arfcn(app);
        if (chosen > 0 && app->gsm_autoselect_pending &&
            app->tab == TAB_DECODE && app->decode == DECODE_GSM &&
            !app->calibration_open) {
            app->gsm_autoselect_pending = 0;
            app->gsm_analysis_mode = 1;     /* Default to Burst mode after scan */
            gsm_tune_selected(app, chosen); /* show the best channel above */
        } else {
            app->scan_selected_arfcn = chosen;
        }
        return;
    }
    double next = app->scan_first_center_hz +
                  (double)app->scan_step * app->scan_step_hz;
    if (retune_receiver(app, (uint32_t)llround(next), app->applied_ppm) < 0) {
        app->scan_running = 0;
        return;
    }
    app->scan_step_started_at = GetTime();
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

    double now = GetTime();

    if (app->drift_phase == DRIFT_IDLE) {
        if (now - app->drift_last_check_at < DRIFT_CHECK_INTERVAL_SECONDS)
            return;
        app->drift_saved_frequency = app->applied_frequency;
        app->drift_health_prev = app->drift_health;
        if (retune_receiver(app, app->gsm_cal_tune_hz, app->gsm_cal_ppm) < 0) {
            app->drift_last_check_at = now; /* retry next interval */
            return;
        }
        app->drift_recent_count = 0;
        app->drift_phase = DRIFT_SETTLE;
        app->drift_phase_started_at = now;
        app->drift_health = CAL_HEALTH_CHECKING;
        return;
    }

    if (app->drift_phase == DRIFT_SETTLE) {
        if (now - app->drift_phase_started_at >= DRIFT_CHECK_SETTLE_SECONDS) {
            app->drift_phase = DRIFT_MEASURE;
            app->drift_phase_started_at = now;
        }
        return;
    }

    /* DRIFT_MEASURE */
    if (have_block && app->spectrum_ready &&
        app->drift_recent_count < DRIFT_RECENT) {
        struct gsm_fcch_result fcch;
        double target = (double)app->gsm_cal_expected_hz -
                        (double)app->applied_frequency + GSM_FCCH_TONE_HZ;
        if (gsm_fcch_detect(app->i_samples, app->q_samples, app->pair_count,
                            app->applied_sample_rate, target,
                            GSM_FCCH_SEARCH_HALF_HZ, &fcch)) {
            double carrier = (double)app->applied_frequency +
                             fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ;
            app->drift_recent_ppm[app->drift_recent_count++] =
                (carrier - (double)app->gsm_cal_expected_hz) /
                (double)app->gsm_cal_expected_hz * 1000000.0;
        }
    }
    if (now - app->drift_phase_started_at < DRIFT_CHECK_MEASURE_SECONDS)
        return;

    retune_receiver(app, app->drift_saved_frequency, app->gsm_cal_ppm);
    app->drift_phase = DRIFT_IDLE;
    app->drift_last_check_at = GetTime();

    if (app->drift_recent_count >= DRIFT_MIN_MEASUREMENTS) {
        double center = 0.0;
        double spread = 0.0;
        robust_center_spread(app->drift_recent_ppm, app->drift_recent_count,
                             &center, &spread);
        app->drift_ppm = center;
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
        app->drift_health = app->drift_health_prev;
    }
}

void close_calibration(struct app *app) {
    if (app->calibration_running) {
        if (retune_receiver(app, app->calibration_return_frequency,
                            app->applied_ppm) < 0)
            return;
    }
    app->calibration_running = 0;
    app->calibration_open = 0;
}

void adjust_waterfall_scale(struct app *app, int zoom_in) {
    app->waterfall_lower_dbfs += zoom_in ? DB_SCALE_STEP : -DB_SCALE_STEP;
    app->waterfall_lower_dbfs = fmaxf(
        SDR_DSP_DBFS_FLOOR,
        fminf(app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS - 20.0f));
    render_waterfall(app);
}

void handle_calibration_input(struct app *app) {
    Rectangle tech_2g = { 24, 72, 74, 34 };
    Rectangle tech_4g = { 106, 72, 74, 34 };
    Rectangle tech_5g = { 188, 72, 74, 34 };
    float right = (float)GetScreenWidth() - 24.0f;
    Rectangle scan = { 470, 72, 90, 34 };
    Rectangle start = { right - 248.0f, 72, 88, 34 };
    Rectangle apply_ppm = { right - 148.0f, 72, 126, 34 };
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };

    int inputs_changed = 0;
    if (!app->calibration_running && clicked(tech_2g)) {
        app->calibration_technology = 0;
        inputs_changed = 1;
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "Select GSM 900 ARFCN 1-124, then press Start");
    }
    if (!app->calibration_running && clicked(tech_4g)) {
        app->calibration_technology = 1;
        inputs_changed = 1;
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "4G channel tables are not implemented yet");
    }
    if (!app->calibration_running && clicked(tech_5g)) {
        app->calibration_technology = 2;
        inputs_changed = 1;
        snprintf(app->calibration_status, sizeof(app->calibration_status),
                 "5G channel tables are not implemented yet");
    }

    int character;
    while (app->calibration_technology == 0 &&
           (character = GetCharPressed()) != 0) {
        if (character >= '0' && character <= '9' &&
            app->calibration_channel_length <
                (int)sizeof(app->calibration_channel) - 1) {
            app->calibration_channel[app->calibration_channel_length++] =
                (char)character;
            app->calibration_channel[app->calibration_channel_length] = '\0';
            inputs_changed = 1;
        }
    }
    if (app->calibration_technology == 0 &&
        IsKeyPressed(KEY_BACKSPACE) &&
        app->calibration_channel_length > 0) {
        app->calibration_channel[--app->calibration_channel_length] = '\0';
        inputs_changed = 1;
    }
    if (inputs_changed) {
        app->calibration_stable = 0;
        app->calibration_measurements = 0;
        app->calibration_recent_count = 0;
        app->calibration_recent_head = 0;
        app->calibration_recent_center = 0.0;
        app->calibration_recent_spread = 0.0;
        app->calibration_recent_sem = 0.0;
        if (app->calibration_running)
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
    if (clicked(apply_ppm) && app->calibration_stable) {
        if (retune_receiver(app, app->calibration_tune_hz,
                            app->calibration_suggested_ppm) == 0) {
            app->options.ppm = app->calibration_suggested_ppm;
            app->calibration_measurements = 0;
            app->calibration_recent_count = 0;
            app->calibration_recent_head = 0;
            app->calibration_stable = 0;
            app->calibration_started_at = GetTime();
            snprintf(app->calibration_status,
                     sizeof(app->calibration_status),
                     "Applied %+d PPM; measuring residual error",
                     app->applied_ppm);
            /* The health indicator turns green only for an FCCH-backed lock;
               record the calibrated channel so drift can be re-checked. */
            if (app->calibration_source == CALIBRATION_SOURCE_FCCH) {
                int arfcn = 0;
                parse_int(app->calibration_channel, &arfcn);
                app->gsm_cal_valid = 1;
                app->gsm_cal_expected_hz = app->calibration_expected_hz;
                app->gsm_cal_tune_hz = app->calibration_tune_hz;
                app->gsm_cal_ppm = app->applied_ppm;
                app->gsm_cal_arfcn = arfcn;
                app->drift_health = CAL_HEALTH_GOOD;
                app->drift_ppm = 0.0;
                app->drift_notice[0] = '\0';
                app->drift_phase = DRIFT_IDLE;
                app->drift_last_check_at = GetTime();
            } else {
                app->gsm_cal_valid = 0;
                app->drift_health = CAL_HEALTH_UNKNOWN;
                app->drift_notice[0] = '\0';
            }
        }
    }
    if (clicked(back) || IsKeyPressed(KEY_ESCAPE))
        close_calibration(app);
}

void draw_calibration(struct app *app) {
    char text[256];
    Rectangle tech_2g = { 24, 72, 74, 34 };
    Rectangle tech_4g = { 106, 72, 74, 34 };
    Rectangle tech_5g = { 188, 72, 74, 34 };
    float right = (float)GetScreenWidth() - 24.0f;
    Rectangle scan = { 470, 72, 90, 34 };
    Rectangle channel = { right - 370.0f, 72, 110, 34 };
    Rectangle start = { right - 248.0f, 72, 88, 34 };
    Rectangle apply_ppm = { right - 148.0f, 72, 126, 34 };
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };

    DrawText("Cellular frequency calibration", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    DrawText("Technology", 24, 50, 16, (Color){ 157, 180, 194, 255 });
    draw_button(tech_2g, "2G", app->calibration_technology == 0);
    draw_button(tech_4g, "4G", app->calibration_technology == 1);
    draw_button(tech_5g, "5G", app->calibration_technology == 2);
    DrawText(app->calibration_technology == 0
                 ? "Band: GSM 900"
                 : "Band: unavailable",
             274, 80, 18,
             (Color){ 209, 221, 228, 255 });
    DrawText("ARFCN", (int)channel.x, 50, 16,
             (Color){ 157, 180, 194, 255 });
    sdrgui_text_field(channel,
                      app->calibration_technology == 0
                          ? app->calibration_channel
                          : "N/A",
                      app->calibration_technology == 0);
    draw_button(start, app->calibration_running ? "Retune" : "Start",
                app->calibration_technology == 0);
    draw_button(apply_ppm, "Apply PPM", app->calibration_stable);
    draw_button(scan, "Scan", app->calibration_technology == 0);
    draw_button(back, "Back", 0);

    snprintf(text, sizeof(text),
             "expected: %.6f MHz   tuned center: %.6f MHz   current correction: %+d PPM",
             app->calibration_expected_hz / 1000000.0,
             app->applied_frequency / 1000000.0, app->applied_ppm);
    DrawText(text, 24, 118, 17, (Color){ 190, 208, 218, 255 });
    if (app->calibration_measurements > 0) {
        snprintf(text, sizeof(text),
                 "measured: %.6f MHz   offset: %+.1f kHz   observed: %+.2f PPM   center: %+.2f +/- %.2f PPM (SEM %.2f)",
                 app->calibration_measured_hz / 1000000.0,
                 app->calibration_offset_hz / 1000.0,
                 app->calibration_offset_hz /
                     app->calibration_expected_hz * 1000000.0,
                 app->calibration_recent_center,
                 app->calibration_recent_spread,
                 app->calibration_recent_sem);
        DrawText(text, 24, 142, 17, (Color){ 255, 205, 91, 255 });
        snprintf(text, sizeof(text),
                 "peak: %.1f dBFS   guard floor: %.1f dBFS   prominence: %.1f dB   suggested correction: %+d PPM",
                 app->calibration_peak_dbfs, app->calibration_floor_dbfs,
                 app->calibration_prominence_db,
                 app->calibration_suggested_ppm);
        DrawText(text, 24, 164, 17,
                 app->calibration_stable ? (Color){ 99, 228, 170, 255 }
                                         : (Color){ 250, 190, 74, 255 });
    }
    DrawText(app->calibration_status, 24, 186, 17,
             (Color){ 158, 204, 230, 255 });

    draw_waterfall(app, 1);
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
        float expected_x = app->plot.x +
                           (float)((app->calibration_expected_hz - lower) /
                                   (upper - lower)) * app->plot.width;
        DrawLine((int)expected_x, (int)app->plot.y, (int)expected_x,
                 (int)(app->plot.y + app->plot.height),
                 (Color){ 87, 229, 173, 230 });
        DrawText("expected", (int)expected_x + 5, (int)app->plot.y + 5, 16,
                 (Color){ 111, 244, 191, 255 });
        if (app->calibration_measurements > 0) {
            float measured_x = app->plot.x +
                               (float)((app->calibration_measured_hz - lower) /
                                       (upper - lower)) * app->plot.width;
            DrawLine((int)measured_x, (int)app->plot.y, (int)measured_x,
                     (int)(app->plot.y + app->plot.height),
                     (Color){ 255, 181, 59, 240 });
            DrawText("measured", (int)measured_x + 5,
                      (int)app->plot.y + 25, 16,
                      (Color){ 255, 202, 105, 255 });
        }
    }
}

static int scan_arfcn_at(const struct app *app, Vector2 point) {
    if (!CheckCollisionPointRec(point, app->plot))
        return 0;
    double fraction = (point.x - app->plot.x) / app->plot.width;
    int arfcn = 1 + (int)(fraction * 124.0);
    if (arfcn < 1)
        arfcn = 1;
    if (arfcn > 124)
        arfcn = 124;
    return arfcn;
}

void draw_scan(struct app *app) {
    char text[160];
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };
    Rectangle rescan = { (float)GetScreenWidth() - 212.0f, 18, 88, 34 };
    int strongest = scan_strongest_arfcn(app);
    int strongest_bcch = scan_strongest_bcch(app);

    DrawText("GSM 900 channel power scan", 24, 18, 26,
             (Color){ 235, 242, 246, 255 });
    draw_button(rescan, "Rescan", !app->scan_running);
    draw_button(back, "Back", 0);

    if (app->scan_running)
        snprintf(text, sizeof(text),
                 "Scanning ARFCN band... step %d / %d",
                 app->scan_step + 1, app->scan_step_count);
    else if (strongest_bcch > 0)
        snprintf(text, sizeof(text),
                 "Strongest BCCH ARFCN %d at %.1f dBFS (conf %.2f)   click a green channel to calibrate it",
                 strongest_bcch, app->scan_power[strongest_bcch],
                 app->scan_bcch_conf[strongest_bcch]);
    else if (strongest > 0)
        snprintf(text, sizeof(text),
                 "No BCCH detected; strongest channel ARFCN %d at %.1f dBFS   click a channel to try it",
                 strongest, app->scan_power[strongest]);
    else
        snprintf(text, sizeof(text),
                 "No channels measured; press Rescan");
    DrawText(text, 24, 54, 18, (Color){ 190, 208, 218, 255 });

    int hover = (!app->scan_running) ? scan_arfcn_at(app, GetMousePosition())
                                     : 0;
    struct sdrgui_scan_chart_params params = {
        app->plot, app->scan_power, app->scan_bcch_conf, 124,
        SCAN_SENTINEL_DBFS, SCAN_BCCH_MIN_CONF, hover,
        GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ, app->scan_selected_arfcn
    };
    sdrgui_scan_chart(&params);
}

void handle_scan_input(struct app *app) {
    Rectangle back = { (float)GetScreenWidth() - 112.0f, 18, 88, 34 };
    Rectangle rescan = { (float)GetScreenWidth() - 212.0f, 18, 88, 34 };

    if (clicked(back) || IsKeyPressed(KEY_ESCAPE)) {
        if (app->receiver_mode)
            retune_receiver(app, app->scan_return_frequency, app->applied_ppm);
        app->scan_running = 0;
        app->scan_open = 0;
        return;
    }
    if (app->scan_running)
        return;
    if (clicked(rescan)) {
        start_scan(app);
        return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int arfcn = scan_arfcn_at(app, GetMousePosition());
        if (arfcn > 0 && app->scan_power[arfcn] > SCAN_SENTINEL_DBFS) {
            snprintf(app->calibration_channel,
                     sizeof(app->calibration_channel), "%d", arfcn);
            app->calibration_channel_length =
                (int)strlen(app->calibration_channel);
            app->scan_open = 0;
            start_calibration(app);
        }
    }
}

void draw_health_indicator(const struct app *app) {
    struct sdrgui_health_params params = {
        app->drift_health, app->gsm_cal_arfcn, app->drift_notice
    };
    sdrgui_health_dot(&params);
}
