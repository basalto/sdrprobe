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
#include "band_plan.h"
#include "config.h"
#include "adsb_dsp.h"
#include "gsm_bcch.h"
#include "gsm_dsp.h"
#include "lte_dsp.h"
#include "lte_mib.h"
#include "lte_scan.h"
#include "options.h"
#include "sdr_dsp.h"
#include "survey_sweep.h"


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
/*
 * Sub-views of the Decode tab, selected by number keys like the Scope views.
 *
 * The order is the key order and the order the labels are drawn in, so the
 * three cannot drift apart. ADS-B is first, and so is the zero-initialised
 * default, which is the one that agrees with the receiver: nothing has been
 * tuned yet at startup, and the default tuning is 1090 MHz.
 */
enum decode_kind {
    DECODE_ADSB,
    DECODE_GSM,
    DECODE_LTE
};
/* What the ADS-B view decides -- the log row, which frame the charts are
   drawn from, and the funnel -- is in a header the checks can reach. */
#include "adsb_analysis.h"
struct scatter_block {
    float i[SCATTER_SAMPLES];
    float q[SCATTER_SAMPLES];
    size_t count;
    double time;
};
/* The SCH decode is reported as it comes off the burst; the running memory
   kept to notice a decode that cannot be right is in gsm_continuity.h, where
   it can be checked. It flags, it never substitutes. */
#include "gsm_continuity.h"

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
    VIEW_WATERFALL,
    VIEW_SURVEY
};
/* Top-level tabs. TAB_SCOPE must be 0 (zero-initialised default). See
   docs/adr/0008-top-level-tab-navigation.md. */
/* The tabs, and the precedence chain that decides which control a key press
   reaches, are in a header the checks can reach. */
#include "input_route.h"

struct band_scan {
    double step_started_at;
    struct scan_plan plan;      /* how the downlink is covered, in scan_plan.h */
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
    float peak_dbfs;
    float floor_dbfs;
    float prominence_db;
    double peak_hz;
    double started_at;
    float fcch_confidence;
    /* Which source the residuals came from, the ring of them, and whether the
       gate is satisfied -- all in calibration_gate.h, where it can be
       checked. */
    struct calibration_tracker track;
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

    /* Analysis mode: the charts drawn from one frame's trace, and the funnel
       counters that say what never became a message. `trace` is the most
       recent attempt whatever its outcome, because a frame that failed its CRC
       is the one worth looking at; `good_trace` is the last one that passed,
       for the Hold last good toggle to pin. */
    int analysis_mode;
    int hold_last_good;
    struct adsb_frame_trace trace;
    struct adsb_frame_trace good_trace;
    struct adsb_demod_stats block_stats;   /* the latest block alone */
    struct adsb_demod_stats totals;        /* accumulated over the session */
};

/*
 * The LTE decode screen's own state: the cell the synchronisation signals
 * found, and the Master Information Block behind it.
 *
 * Both are kept rather than replaced each block, because a cell that is found
 * and then missed for a second is worth still showing -- with `age` saying so
 * -- and because the block is broadcast once per frame while a sample block
 * covers seven. The counters are the honest record of how often each stage
 * actually succeeded, which is the difference between a marginal cell and a
 * strong one.
 */
/* One cell a scan found, and enough about it to be worth listing. */
/*
 * A walk along a band's channels, looking for a cell at each.
 *
 * It has to tune to every one of them -- see lte_scan.h for why -- so it is
 * slow enough that the reader watches it happen, which is why the results
 * accumulate in a list rather than being replaced, and why the list is what
 * gets picked from rather than a single winner being chosen for them. The GSM
 * scan hands back one ARFCN; this hands back everything it saw.
 */
struct lte_band_scan {
    int running;
    int band;                   /* index into lte_band_at() */
    int candidate;              /* how far along the order */
    int total;
    double step_started;
    int settled;                /* past the tuner's settling time */
    /* This channel so far: how many blocks have been looked at, and the
       identity they keep agreeing on. An identity is only believed once it
       has repeated -- see lte_scan.h. */
    int looks;
    int pending_pci;
    int pending_hits;
    struct lte_cell pending_cell;
    struct lte_found_cell found[LTE_SCAN_MAX_FOUND];
    int found_count;
    int selected;               /* the row the reader picked, -1 for none */
    /*
     * The confirmation pass, which runs once the sweep has walked the band:
     * every entry above is revisited and asked again, and one that does not
     * repeat its identity is dropped. `confirming` says the pass is under
     * way, `confirm_index` which entry is being revisited, and
     * `confirm_total` how many there were when it began -- the list shortens
     * as entries fail, so it cannot be recovered afterwards, and the caption
     * needs it to say how far along the pass is.
     */
    int confirming;
    int confirm_index;
    int confirm_total;
    int confirm_dropped;
};

struct lte_view {
    int earfcn;                 /* 0 when the tuning is not on the raster */
    struct lte_cell cell;
    int cell_valid;
    double cell_time;
    struct lte_mib mib;
    int mib_valid;
    double mib_time;
    int mib_ports_used;         /* the combining the message decoded under */

    /*
     * A message is believed only once it repeats, and this is the one waiting
     * to be believed.
     *
     * Sixteen bits of parity sound decisive until you count the attempts:
     * four scrambling offsets against three masks, for each of three
     * antenna-port hypotheses, is thirty-six chances per block, and a session
     * that finds a cell nine thousand times over half an hour will see one
     * pass by chance. That is not a rare accident to be tolerated -- it is
     * the *expected* number, so a counter that believes the first pass is
     * reporting noise as a message.
     *
     * What a real cell has and chance does not is consistency: a cell's
     * bandwidth, acknowledgement channel and antenna count do not change
     * between one frame and the next, and two random parity passes agree on
     * all three about once in a hundred and forty-four times.
     */
    struct lte_mib pending_mib;
    int pending_mib_hits;

    uint64_t blocks_seen;
    uint64_t cells_found;
    uint64_t mib_parity_passes;  /* before the repeat is required */
    uint64_t mibs_decoded;
    /* The identity a scripted run has already printed. The search finds the
       same cell in every block, and a line each would bury the message. */
    int announced_pci;

    /* Why the last block produced nothing, when it produced nothing. The
       common answer is that the receiver is not on LTE's sample grid, and
       saying so beats an empty screen. */
    char status[160];

    /*
     * The receiver's tuning and rate before this view took them.
     *
     * LTE is the only view that changes the sample rate, and it has to: its
     * arithmetic is the 1.92 MS/s grid and nothing else will do (ADR-0014).
     * It borrows the receiver the way the GSM view borrows the tuning, and
     * gives both back on the way out.
     */
    uint32_t return_frequency;
    uint32_t return_sample_rate;
    int return_valid;

    struct lte_band_scan scan;

    /* Analysis mode: the charts behind the numbers, as the GSM view has for
       its bursts and the ADS-B view for its frames, and what the search saw
       on its way to them. The trace is only collected while the charts are
       up, which is what keeps the cost off the ordinary path. */
    int analysis_mode;
    struct lte_trace trace;
};

/*
 * The GSM decode screen's own state: which channel is being inspected, the
 * last SCH decode and the symbols behind it, and the decode options the
 * feature toggles set.
 *
 * The handoff fields stay in struct app: the scan tells this view which
 * channel to open, and calibration publishes the result this view displays.
 */
/*
 * What the cell has said about itself so far.
 *
 * One System Information message carries some of these and not others -- the
 * identity comes from type 3, the neighbours from types 1 and 2 -- so they
 * accumulate across blocks rather than being replaced by each. Cleared when
 * the view tunes elsewhere, because then it is a different cell.
 */
struct gsm_cell {
    int blocks;                 /* System Information messages read */
    enum gsm_si_type last_type;
    int have_lai;
    int mcc;
    int mnc;
    int mnc_digits;
    int lac;
    int have_cell_id;
    int cell_id;
    int neighbour_count;
    int neighbours[GSM_SI_MAX_NEIGHBOURS];
};

struct gsm_view {
    double selected_hz;         /* carrier of the selected ARFCN (0 = none) */
    uint32_t return_frequency;  /* view frequency to restore on leave */
    int return_valid;
    struct gsm_sch_continuity continuity;
    struct gsm_sch_result sch;
    struct gsm_sch_symbols sch_symbols;
    int sch_valid;
    double sch_time;
    struct gsm_cell cell;
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
    /* The tuning its history was gathered at. When the receiver moves, every
       row above the newest belongs to another frequency, so the view notices
       and clears -- the same way it notices a resize. */
    uint32_t waterfall_tuned_hz;
    int waterfall_width;
    int waterfall_height;
    int waterfall_rows;
};

/*
 * The help overlay's own state: whether it is open, which topic it is showing,
 * and how far that topic is scrolled. content_height is measured during the
 * input phase so the scroll can be clamped there, which is what lets drawing
 * stay read-only.
 */
struct help_overlay {
    int open;
    int topic;
    float scroll;
    float content_height;
};

/*
 * The band survey's own state: the range being swept, the power found across
 * it, the candidates standing above the local floor, and the measurement of
 * whichever one is selected.
 *
 * The sweep's own arithmetic -- how many steps, how many bins, when a step is
 * done, what a measurement adds up to -- lives in survey_sweep.h, with the
 * constants that go with it; the window's lives in survey_window.h. What is
 * left here is this view's state and the few numbers only the view uses.
 */
#define SURVEY_SCAN_HALF_SPAN_HZ 2000000.0  /* "scan this frequency" window */
#define SURVEY_ZOOM_STEP 1.6        /* per key press */
#define SURVEY_PAN_FRACTION 0.25    /* of the visible span, per key press */
#define SURVEY_MIN_SPAN_HZ 100000.0 /* no closer than this */
#define SURVEY_MAX_BANDS 48         /* allocations drawn behind the trace */

/*
 * What a sweep produced, kept so that drilling into a region can be undone.
 * "Sweep region" narrows the swept range to the region, which throws away
 * everything outside it -- and re-sweeping to get it back costs minutes. A
 * copy costs 44 KB and comes back instantly.
 */
struct survey_snapshot {
    int valid;
    double lower_hz;
    double upper_hz;
    int bins;
    float power[SURVEY_BINS];
    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    int peak_count;
    char from[24];
    char to[24];
};

struct survey_view {
    char from[24];
    int from_length;
    char to[24];
    int to_length;
    char dwell[12];
    int dwell_length;
    int focus;                  /* 0 = from, 1 = to, 2 = dwell */
    double dwell_seconds;       /* parsed at the start of a sweep */

    double lower_hz;            /* the range actually swept */
    double upper_hz;
    /* The range currently typed in the fields. Before the first sweep there is
       no swept range, and the chart, the zoom and the drag all need something
       to work against -- so they work against this. */
    double field_lower_hz;
    double field_upper_hz;
    /* What of it is on screen. Zooming narrows this window without resampling
       the array, so the same measurements are simply drawn larger. */
    double view_lower_hz;
    double view_upper_hz;
    int bins;                   /* of SURVEY_BINS, in use for this range */
    float power[SURVEY_BINS];

    struct survey_plan plan;    /* what the running sweep is working through */
    int sweeping;
    int step;
    int step_count;
    double step_started_at;
    int step_folded;            /* a block has been folded into this step */
    uint32_t return_frequency;  /* tuning to restore when the view is left */
    int return_valid;

    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    int peak_count;
    int selected;               /* index into peaks, -1 for none */
    int hover;

    /* Dragging a rectangle across the chart to zoom into it. A press only
       becomes a drag once the pointer has moved; below that it is a click,
       and a click selects a candidate. */
    int drag_active;
    float drag_from_x;
    double drag_from_hz;
    double drag_to_hz;

    /* Measuring the selected candidate, once retuned to it. */
    int measuring;
    double measure_started_at;
    double measure_expected_hz;
    struct survey_measurement measure;
    struct sdr_carrier_report report;
    int report_valid;

    struct survey_snapshot previous;

    char status[200];
};

struct app {
    struct scope_view sv;
    struct survey_view survey;
    struct gsm_view gsm;
    struct adsb_view adsb;
    struct lte_view lte;
    struct settings_panel set;
    struct help_overlay help;
    struct calibration cal;
    struct band_scan bandscan;
    struct acquisition acq;
    struct options options;
    /* Antenna and site: what makes one sweep comparable to another, loaded
       once at startup and reported by anything that measures. */
    struct config config;
    struct sdr_dsp dsp;
    rtlsdr_dev_t *dev;
    FILE *capture;
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
