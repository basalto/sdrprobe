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
/*
 * The band scan's own state: where the sweep started, how wide each step is,
 * and the frequency to restore when it finishes.
 *
 * It shares nothing with the calibration it feeds. Choosing a channel goes
 * through calibration_select_channel(); the results the GSM view reads --
 * per-channel power and BCCH confidence -- stay in struct app, because two
 * screens read them.
 */
/* Sub-views of the Decode tab, selected by number keys like the Scope views. */
enum decode_kind {
    DECODE_GSM,
    DECODE_ADSB
};
#define ADSB_LOG_CAPACITY 256
/* One row of the decoded-message log, formatted for display at decode time. */
struct adsb_log_entry {
    char stamp[16];
    char icao[8];
    char label[6];
    char detail[96];
    char raw[32];
    double time;
    int highlight;
};
struct scatter_block {
    float i[SCATTER_SAMPLES];
    float q[SCATTER_SAMPLES];
    size_t count;
    double time;
};
/* The SCH decode is reported as it comes off the burst. The only running
   memory kept is the previous T1, to notice a decode that cannot be right:
   T1 advances once per 1326 frames (~6.1 s), so consecutive decodes seconds
   apart must agree to within 1. This flags, it never substitutes. */
struct gsm_sch_continuity {
    int have_last;
    int last_t1;
    int implausible;
};

/* Calibration-health indicator states. UNKNOWN must be 0 (zero-initialised). */
enum cal_health {
    CAL_HEALTH_UNKNOWN = 0, /* grey: never GSM-calibrated, or PPM changed manually */
    CAL_HEALTH_GOOD,        /* green: applied PPM backed by a stable FCCH lock */
    CAL_HEALTH_DRIFT,       /* red: a periodic re-check found drift */
    CAL_HEALTH_CHECKING     /* amber: a re-check is in progress */
};
/* Phases of one background drift re-check. */
enum drift_phase {
    DRIFT_IDLE = 0,
    DRIFT_SETTLE,
    DRIFT_MEASURE
};
enum view_kind {
    VIEW_MAGNITUDE,
    VIEW_SPECTRUM,
    VIEW_SCATTER,
    VIEW_WATERFALL
};
/* Top-level tabs. TAB_SCOPE must be 0 (zero-initialised default). See
   docs/adr/0008-top-level-tab-navigation.md. */
enum active_tab {
    TAB_SCOPE,
    TAB_DECODE
};

struct band_scan {
    double step_started_at;
    double first_center_hz;
    double step_hz;
    double accept_half_hz;
    uint32_t return_frequency;
};

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

/*
 * The GSM decode screen's own state: which channel is being inspected, the
 * last SCH decode and the symbols behind it, and the decode options the
 * feature toggles set.
 *
 * The handoff fields stay in struct app: the scan tells this view which
 * channel to open, and calibration publishes the result this view displays.
 */
struct gsm_view {
    double selected_hz;         /* carrier of the selected ARFCN (0 = none) */
    uint32_t return_frequency;  /* view frequency to restore on leave */
    int return_valid;
    struct gsm_sch_continuity continuity;
    struct gsm_sch_result sch;
    struct gsm_sch_symbols sch_symbols;
    int sch_valid;
    double sch_time;
    int const_amplitude; /* constellation: show amplitude vs unit circle */
    int const_derotated; /* constellation: derotated sample vs differential */
    int opt_filter;
    int opt_finecfo;
    int opt_trellis;
};

/*
 * The Scope tab's own state: the GPU textures behind the scatter and waterfall,
 * the history each keeps between frames, and the per-view scales that Up/Down
 * adjust.
 *
 * The frame loop used to rebuild these itself, which meant it had to know that
 * the scatter's size lives on a RenderTexture and the waterfall's in two ints.
 * It calls view_scope_resize_if_needed() now and knows neither.
 */
struct scope_view {
    int scatter_ready;
    int waterfall_ready;
    float magnitude_peaks[SAMPLE_BLOCK_PAIRS];
    size_t magnitude_bin_count;
    float magnitude_lower;
    float magnitude_upper;
    float spectrum_lower_dbfs;
    size_t scatter_inserted;
    struct scatter_block scatter_history[SCATTER_HISTORY_BLOCKS];
    size_t scatter_history_head;
    size_t scatter_history_count;
    float scatter_axis_limit;
    RenderTexture2D scatter;
    Texture2D waterfall;
    Color *waterfall_pixels;
    float *waterfall_dbfs;
    int waterfall_capacity;
    int waterfall_width;
    int waterfall_height;
    int waterfall_rows;
};

struct app {
    struct scope_view sv;
    struct gsm_view gsm;
    struct adsb_view adsb;
    struct settings_panel set;
    struct calibration cal;
    struct band_scan bandscan;
    struct acquisition acq;
    struct options options;
    struct sdr_dsp dsp;
    rtlsdr_dev_t *dev;
    FILE *capture;
    int record_mutex_ready;
    int window_ready;
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
    float magnitude_sorted[SAMPLE_BLOCK_PAIRS];
    size_t pair_count;
    float magnitude_min;
    float magnitude_mean;
    float magnitude_max;
    struct sdr_signal_stats signal_stats;
    int signal_stats_ready;

    float spectrum_average[SDR_DSP_FFT_SIZE];
    float spectrum_candidate[SDR_DSP_FFT_SIZE];
    float spectrum_peak[SDR_DSP_FFT_SIZE];
    int spectrum_windows;
    int spectrum_ready;
    int spectrum_peak_ready;
    double spectrum_peak_time;
    int have_samples;

    enum active_tab tab;
    enum decode_kind decode;
    enum view_kind view;
    Rectangle plot;
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
    int gsm_autoselect_pending;     /* pick the best BCCH when the open-scan ends */

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
    int gsm_analysis_mode;   /* Burst Analysis Chart: 0=Corr, 1=Soft Bits, 2=Phase */
    

    /* Raw-I/Q recording (to build a GSM test capture). Written by the
       acquisition thread, so record_mutex guards every field here. */
    /* Snapshotted by start_record on the main thread, so the acquisition
       thread never reads live tuning state. */
};

#endif
