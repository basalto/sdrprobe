#ifndef APP_H
#define APP_H

#include <pthread.h>
#include <raylib.h>
#include <signal.h>
#include <rtl-sdr.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "acquisition.h"
#include "adsb_dsp.h"
#include "gsm_dsp.h"
#include "options.h"
#include "sdr_dsp.h"


/*
 * The application's shared state.
 *
 * Acquisition now owns its own (struct acquisition, in acquisition.h). What
 * is left here is still one record every view reads, so the view files are an
 * organisation of that coupling rather than modules in their own right.
 */
#define GSM900_BASE_HZ 935000000.0
#define GSM900_ARFCN_SPACING_HZ 200000.0

/*
 * The calibration overlay's own state: the GSM 900 channel calibration, the
 * band scan that feeds it, and the periodic drift re-check. One struct because
 * they are one screen -- the scan picks a channel, calibration measures it,
 * and the drift check re-measures it later.
 *
 * The calibration_ prefix is dropped inside here; it was only ever there to
 * separate these from everything else in struct app.
 */
struct calibration {
    float workspace[SDR_DSP_FFT_SIZE];
    int running;
    int band;
    char channel[16];
    int channel_length;
    uint32_t tune_hz;
    double measured_hz;
    double offset_hz;
    int measurements;
    float peak_dbfs;
    float floor_dbfs;
    float prominence_db;
    double peak_hz;
    double started_at;
    double recent_ppm[CALIBRATION_RECENT];
    int recent_count;
    int recent_head;
    double recent_center;
    double recent_spread;
    double recent_sem;
    int fcch_locked;
    float fcch_confidence;
    int source;
    int fcch_miss;
    int fcch_hits;
    int stable;
    uint32_t return_frequency;
    int suggested_ppm;
    double scan_step_started_at;
    double scan_first_center_hz;
    double scan_step_hz;
    double scan_accept_half_hz;
    uint32_t scan_return_frequency;
    uint32_t gsm_cal_expected_hz;  /* calibrated carrier */
    uint32_t gsm_cal_tune_hz;      /* receiver center used for the re-check */
    int drift_health_prev;         /* restored if a re-check is inconclusive */
    double drift_ppm;              /* last measured residual drift */
    double drift_last_check_at;
    double drift_phase_started_at;
    uint32_t drift_saved_frequency; /* view frequency to return to */
    double drift_recent_ppm[DRIFT_RECENT];
    int drift_recent_count;
};

/*
 * What the Settings panel is holding while it is open: the text being
 * typed and which control has focus. Applying it writes through to the
 * applied_* fields; until then this is the panel's own draft.
 */
struct settings_panel {
    char frequency[32];
    int frequency_length;
    char ppm[16];
    int ppm_length;
    int focus;
    int gain_choice;
    int remove_dc;
    int auto_drift;       /* Settings-panel working copy */
};

/*
 * The ADS-B screen's own state: the decoder's position-pairing cache and
 * the newest-first log of messages it has recovered.
 */
struct adsb_view {
    struct adsb_decoder decoder;
    struct adsb_message scratch[64];
    struct adsb_log_entry log[ADSB_LOG_CAPACITY]; /* newest first */
    int log_count;
    uint64_t frames_total;
    uint64_t positions_total;
};

struct app {
    struct adsb_view adsb;
    struct settings_panel set;
    struct calibration cal;
    struct acquisition acq;
    struct options options;
    struct sdr_dsp dsp;
    rtlsdr_dev_t *dev;
    FILE *capture;
    int record_mutex_ready;
    int window_ready;
    int scatter_ready;
    int waterfall_ready;
    int signals_ready;
    int receiver_mode;
    int applied_manual_gain;
    int applied_gain_tenths;
    int applied_ppm;
    int *supported_gains;
    int supported_gain_count;
    uint32_t applied_frequency;
    uint32_t applied_sample_rate;
    char source_label[320];
    char tuner_label[32];
    struct sigaction old_sigint;
    struct sigaction old_sigterm;

    float i_samples[SAMPLE_BLOCK_PAIRS];
    float q_samples[SAMPLE_BLOCK_PAIRS];
    float spectrum_i[SAMPLE_BLOCK_PAIRS];
    float spectrum_q[SAMPLE_BLOCK_PAIRS];
    float magnitudes[SAMPLE_BLOCK_PAIRS];
    float magnitude_peaks[SAMPLE_BLOCK_PAIRS];
    float magnitude_sorted[SAMPLE_BLOCK_PAIRS];
    size_t pair_count;
    size_t magnitude_bin_count;
    float magnitude_min;
    float magnitude_mean;
    float magnitude_max;
    float magnitude_lower;
    float magnitude_upper;
    struct sdr_signal_stats signal_stats;
    int signal_stats_ready;

    float spectrum_average[SDR_DSP_FFT_SIZE];
    float spectrum_candidate[SDR_DSP_FFT_SIZE];
    float spectrum_peak[SDR_DSP_FFT_SIZE];
    float spectrum_lower_dbfs;
    int spectrum_windows;
    int spectrum_ready;
    int spectrum_peak_ready;
    double spectrum_peak_time;
    size_t scatter_inserted;
    struct scatter_block scatter_history[SCATTER_HISTORY_BLOCKS];
    size_t scatter_history_head;
    size_t scatter_history_count;
    float scatter_axis_limit;
    int have_samples;

    enum active_tab tab;
    enum decode_kind decode;
    enum view_kind view;
    Rectangle plot;
    RenderTexture2D scatter;
    Texture2D waterfall;
    Color *waterfall_pixels;
    float *waterfall_dbfs;
    int waterfall_capacity;
    int waterfall_width;
    int waterfall_height;
    int waterfall_rows;
    float waterfall_lower_dbfs;

    int settings_open;
    int remove_dc;
    char settings_error[160];

    int calibration_open;
    int calibration_technology;
    uint32_t calibration_expected_hz;
    char calibration_status[160];

    int scan_open;
    int scan_running;
    int scan_step;
    int scan_step_count;
    float scan_power[125];
    float scan_bcch_conf[125];
    int scan_selected_arfcn;

    /* GSM decode view: the currently inspected channel and the tuning to
       restore when the view is left. */
    double gsm_selected_hz;         /* carrier of the selected ARFCN (0 = none) */
    uint32_t gsm_return_frequency;  /* view frequency to restore on leave */
    int gsm_return_valid;
    int gsm_autoselect_pending;     /* pick the best BCCH when the open-scan ends */
    struct gsm_sch_continuity gsm_continuity;

    /* Calibration-health indicator and background drift re-check. */
    int auto_drift_check;          /* Settings toggle: enable periodic re-check */
    int gsm_cal_valid;             /* an FCCH-backed GSM calibration exists */
    int gsm_cal_ppm;               /* PPM applied at calibration */
    int gsm_cal_arfcn;             /* channel, for the notice text */
    int drift_health;              /* enum cal_health */
    char drift_notice[160];
    int drift_phase;               /* enum drift_phase */

    /* ADS-B / Mode S decoder tab (the Decoder context). */

    /* GSM SCH decode of the inspected channel. */
    struct gsm_sch_result gsm_sch;
    struct gsm_sch_symbols gsm_sch_symbols;
    int gsm_sch_valid;
    double gsm_sch_time;
    int gsm_const_amplitude; /* constellation: show amplitude vs unit circle */
    int gsm_const_derotated; /* constellation: derotated sample vs differential */
    int gsm_analysis_mode;   /* Burst Analysis Chart: 0=Corr, 1=Soft Bits, 2=Phase */
    
    int gsm_opt_filter;
    int gsm_opt_finecfo;
    int gsm_opt_trellis;

    /* Raw-I/Q recording (to build a GSM test capture). Written by the
       acquisition thread, so record_mutex guards every field here. */
    /* Snapshotted by start_record on the main thread, so the acquisition
       thread never reads live tuning state. */
};

#endif
