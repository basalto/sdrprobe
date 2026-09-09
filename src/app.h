#ifndef APP_H
#define APP_H

#include <pthread.h>
#include <raylib.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "acquisition.h"
#include "band_plan.h"
#include "config.h"
#include "site_history.h"
#include "survey_carrier.h"
#include "survey_confirm.h"
#include "adsb_dsp.h"
#include "gsm_bcch.h"
#include "gsm_dsp.h"
#include "lte_dsp.h"
#include "lte_stats.h"
#include "lte_mib.h"
#include "lte_scan.h"
#include "receiver_lease.h"
#include "options.h"
#include "gsm_session.h"
#include "tetra_session.h"
#include "lte_session.h"
#include "adsb_session.h"
#include "fm_session.h"
#include "installation.h"
#include "device_backend.h"
#include "sdr_dsp.h"
#include "signal_findings.h"
#include "survey_sweep.h"
#include "survey_session.h"


/*
 * The application's shared state.
 *
 * Acquisition now owns its own (struct acquisition, in acquisition.h). What
 * is left here is still one record every view reads, so the view files are an
 * organisation of that coupling rather than modules in their own right.
 *
 * One exception is worth naming, because it used to be nine: no view stores
 * the tuning it means to put back. `struct receiver_lease` below owns every
 * temporary borrowing of the receiver, each owner holds a token rather than a
 * frequency, and receiver_lease.h is the rule they unwind by.
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
/*
 * The Decode tab's screens, in the order they are offered.
 *
 * The order is load-bearing: the header draws the row from this enum's values,
 * so the numbers a reader presses are these positions. FM leads because it is
 * the one that always has something to show here -- broadcast is on the air
 * every hour of every day, where a GSM capture needs a cell that has not been
 * refarmed and ADS-B needs an aircraft overhead.
 */
enum decode_kind {
    DECODE_FM,
    DECODE_ADSB,
    DECODE_GSM,
    DECODE_LTE,
    DECODE_TETRA
};
/* What the ADS-B view decides -- the log row, which frame the charts are
   drawn from, and the funnel -- is in a header the checks can reach. */
#include "adsb_analysis.h"
#include "tetra_dsp.h"
#include "tetra_sync.h"
#include "chart_window.h"
#include "fm_dsp.h"
#include "fm_scan.h"
#include "rds.h"

/*
 * Walking band II.
 *
 * Two passes, for the reason fm_scan.h sets out: a coarse sweep says where
 * the carriers are and only those get the quarter second a pilot needs. The
 * state is one struct because it is one activity -- which pass, how far
 * through it, and what has been found.
 */
#define FM_SCAN_MAX_FOUND 48

struct fm_found_station {
    int channel;                /* index into the 100 kHz raster */
    double frequency_hz;
    float power_dbfs;
    int stereo;                 /* a pilot: the station is broadcasting in
                                   stereo, since one is sent for no other
                                   reason */
    int rds;                    /* groups arrived */
    int pi_valid;
    uint16_t pi;
    char ps[9];                 /* empty unless a whole name repeated */
};

struct fm_scan {
    int running;
    int sweeping;               /* pass one: the coarse spectrum sweep */
    int step;
    struct fm_scan_plan plan;
    double step_started_at;
    float power[206];           /* one per channel of the raster */
    struct fm_found_station found[FM_SCAN_MAX_FOUND];
    int found_count;
    int list_scroll;            /* row_list.h holds the arithmetic */
    int visiting;               /* pass two: which candidate */
    /*
     * Pass three: back to the ones that answered, for long enough to read
     * their name. Only those -- a name costs seconds where deciding whether
     * there is RDS at all costs under one, so spending it on every carrier
     * would turn a scan of band II into a scan of the whole tuner.
     */
    int naming;
    int naming_pass;            /* pass two is done; this one is running */
    /* Band II is walked in thirteen tunings and then revisited; this is where
       the operator had the receiver before any of that (receiver_lease.h). */
    struct receiver_lease_token lease_token;
    char status[160];
};

/*
 * FM broadcast, and what a station's RDS says about it.
 *
 * The one decode view here that cannot work a block at a time. The pilot loop
 * needs a quarter of a second before it may be believed -- four blocks at the
 * house rate -- and the symbol clock, the subcarrier phase and the bitstream
 * all run continuously through a block boundary. So the front end is kept
 * across blocks rather than rebuilt from each, which is the whole reason this
 * state lives here instead of on a stack somewhere in the draw call.
 *
 * What is kept across blocks is the *baseband*, not the bits, and that
 * distinction cost an afternoon. Soft bits are only meaningful relative to a
 * symbol grid and a subcarrier axis, and both are worked out from whatever
 * span they are worked out over: a block's worth of baseband is 77 symbols
 * that do not begin on the previous block's grid, and the axis carries a 180
 * degree ambiguity that the differential decode absorbs happily within one
 * run and not at all across a join. Bits decoded per block and then
 * concatenated look perfect and synchronise to nothing -- 2020 of them gave
 * zero groups where the same capture decoded offline gives eighteen.
 *
 * So the window holds complex baseband, one timing search and one axis are
 * done over the whole of it, and the result is bit-for-bit what the offline
 * decode of the same span produces. The view and a script cannot disagree
 * about what a station said, because they are running the same function over
 * the same samples.
 */
#define FM_VIEW_BASEBAND 65536      /* about 3.4 s at the pilot rate */
/* Half a second of sound. Enough to ride out the jitter between a block
   arriving and a frame being drawn, and short enough that what is heard is
   what the receiver is on rather than where it was. */
#define FM_AUDIO_RING 32768
/* What is handed to the sound card at once. */
#define FM_AUDIO_CHUNK 2048
/* How much of it the charts keep: enough for two windows of the transform,
   which at 50 kHz is about a tenth of a second -- long enough to average and
   short enough to still be what is happening now. */
#define FM_AUDIO_TRACE 4096
#define FM_VIEW_SOFT_BITS (FM_VIEW_BASEBAND / FM_RDS_SAMPLES_PER_SYMBOL + 8)
/*
 * And a much longer memory of the *bits*, which is a different thing.
 *
 * The baseband window is short because one timing search and one axis have to
 * cover it, and both cost work proportional to its length. Radio text needs
 * far longer than that: sixty-four characters in sixteen segments at roughly
 * one group a second is a quarter of a minute, and a three-second window can
 * never hold it -- the name arrived and the text never did.
 *
 * Bits are cheap where baseband is not. Thirty thousand of them is
 * twenty-five seconds and 128 KB, and the block synchroniser walks them in
 * one pass. So the baseband is decoded in chunks and the bits accumulate.
 *
 * Chunks, not a sliding window, and the difference cost an attempt. A sliding
 * window re-derives its timing offset and drops a leading symbol every time,
 * so which absolute symbol a given index means moves under you -- appending
 * "the newest few" from each pass gave a stream that decoded worse than the
 * window alone had. Fixed chunks that never overlap have one seam each and no
 * ambiguity about what has already been counted.
 */
#define FM_VIEW_BIT_MEMORY 32768
/* One chunk: 2048 symbols, about 1.7 s. Long enough for a solid timing and
   axis estimate, short enough that the view is never far behind. */


struct fm_view {
    /* What the waterfall draws: the reader's zoom and pan over the received
       span, shared with every other frequency chart (chart_window.h). */
    struct chart_window window;

    char frequency[16];             /* megahertz, as typed */
    int frequency_length;
    int typing;

    /* The RDS decode: the front end, the baseband accumulator, the chunking,
       the bits and the station (fm_session.h). The sound and the chart spectra
       stay below, because neither is a decode -- one writes into a raylib
       stream and the other exists to be drawn. */
    struct fm_session session;

    /*
     * The audio, and the ring between the two rates that produce and consume
     * it. A block arrives every 65 ms carrying 65 ms of sound, so production
     * and consumption match exactly and the ring only has to absorb jitter --
     * except when the renderer falls behind and the acquisition slot drops a
     * block (ADR-0002), which is a gap in the sound and cannot be anything
     * else.
     */
    struct fm_audio audio;
    int audio_ready;                /* the device opened */
    char audio_error[80];
    int playing;
    AudioStream audio_stream;
    /* Interleaved left and right, so a frame is two entries. Always two
       channels even on a mono station -- switching the stream's format when
       a pilot comes and goes would mean reopening the device mid-song. */
    int16_t audio_ring[FM_AUDIO_RING * 2];
    size_t audio_head, audio_tail;  /* tail writes, head reads */

    /*
     * The most recent audio, as floats, for the two charts that draw it. Kept
     * whether or not anything is playing: the charts say what *would* be
     * heard, and a chart that only works once a button has been pressed is a
     * chart that answers the question after it stops being asked.
     */
    float audio_trace[FM_AUDIO_TRACE];
    size_t audio_trace_count;
    float audio_spectrum[FM_MPX_SPECTRUM_BINS];
    size_t audio_spectrum_bins;
    double audio_spectrum_bin_hz;

    int analysis_mode;              /* charts instead of the waterfall */
    /* The multiplex spectrum the charts draw, refreshed a few times a second
       rather than per block: it averages 32 windows and nobody reads a chart
       at sixty frames a second. */
    float spectrum[FM_MPX_SPECTRUM_BINS];
    size_t spectrum_bins;
    double spectrum_bin_hz;
    double spectrum_at;

    /* The timing search's own answer, kept so the chart can show how clearly
       the winning offset won -- a peak barely above its neighbours is a
       symbol clock about to slip. */
    float timing_energy[FM_RDS_SAMPLES_PER_SYMBOL];

    struct fm_scan scan;
};
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
    VIEW_WATERFALL
};
/* Top-level tabs, in input_route.h so a check can reach the precedence
   without app.h. TAB_SURVEY is 0 and so is the zero-initialised default,
   which is deliberate: it is the screen a session starts on.
   See docs/adr/0008-top-level-tab-navigation.md. */
/* The tabs, and the precedence chain that decides which control a key press
   reaches, are in a header the checks can reach. */
#include "input_route.h"

struct band_scan {
    /*
     * Whether the overlay is up, and whether a sweep is actually walking.
     *
     * Two things and not one: the GSM view runs the same scan *inline*, with
     * the overlay closed, so `open` without `running` is the overlay sitting
     * on a finished scan and `running` without `open` is the GSM view's own.
     * `open` is nested like `cal.open` and `help.open`; `settings_open` is
     * the last overlay flag that is not.
     */
    int open;
    int running;
    int step;
    double step_started_at;
    struct scan_plan plan;      /* how the downlink is covered, in scan_plan.h */
    /*
     * What the sweep measured, per ARFCN: the channel's power, and how
     * confident the FCCH tone detector is that it carries a BCCH.
     *
     * Measurements, which is why they belong here rather than beside the
     * drawing -- the same argument the survey's snapshot settled
     * (`survey_session_keep()`, ticket 04).
     *
     * Indexed by ARFCN directly, so index 0 is unused and the bound is
     * scan_plan.h's own `SCAN_ARFCN_LAST` rather than the bare 125 these were
     * declared with.
     */
    float power[SCAN_ARFCN_LAST + 1];
    float bcch_conf[SCAN_ARFCN_LAST + 1];
    /*
     * Pick the best BCCH when this scan finishes, rather than leaving the
     * reader on a chart.
     *
     * A scan option and not a flag of the GSM view's, even though the GSM
     * view is the only thing that sets it: it changes what the scan does when
     * it ends, so the scan is what has to know. The same shape as
     * `gsm_session.options`, which the view sets and the decode obeys.
     */
    int autoselect;
    /* Borrowed from whatever the GSM view had tuned, and given back to it --
       not to whatever was on screen before GSM (receiver_lease.h). */
    struct receiver_lease_token lease_token;
};

struct calibration {
    /* What the waterfall draws: the reader's zoom and pan over the received
       span, shared with every other frequency chart (chart_window.h). */
    struct chart_window window;

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
    /* The receiver, while a measurement or the band scan looking for one owns
       it. One claim covers both: they are phases of one borrowing, and Back
       ends whichever is running (receiver_lease.h). */
    struct receiver_lease_token lease_token;
    int suggested_ppm;

    /*
     * Which overlay is up.
     *
     * Nested like `help.open`, which set the precedent: the precedence chain
     * reads it through `input_state_now()` and does not care where it lives.
     * All four overlay flags are nested now.
     */
    int open;
    int technology;                /* 0 = 2G, 1 = 4G */
    uint32_t expected_hz;          /* the carrier being measured against */
    /*
     * What the overlay's status line says.
     *
     * The overlay's, and only since this ticket: `retune_receiver()` and the
     * two beside it wrote all their failures here from every screen in the
     * program, prefixed "Calibration", so the reason a survey step or an FM
     * retune failed arrived on this line. That is `app->receiver_error` now,
     * and the two paths here that used to depend on the shared buffer quote
     * it deliberately.
     */
    char status[160];

    /* What each reference last measured, and whether it is worth trusting.
       Two references measuring one crystal: when they agree the correction is
       worth trusting, and when they do not that is the most useful thing
       either of them has said. */
    int gsm_valid;                 /* an FCCH-backed GSM calibration exists */
    int gsm_ppm;                   /* PPM applied at calibration */
    int gsm_arfcn;                 /* channel, for the notice text */
    uint32_t gsm_expected_hz;      /* calibrated carrier */
    uint32_t gsm_tune_hz;          /* receiver center used for the re-check */
    int lte_valid;
    int lte_earfcn;
    int lte_ppm;                   /* what the LTE reference suggested */
    int lte_band;                  /* which band the 4G scan will walk */
    int lte_scanning;              /* a calibration-driven band scan is up */

    /* The health indicator and the background re-check that feeds it. */
    int auto_drift;                /* Settings toggle: enable periodic re-check */
    int drift_health;              /* enum cal_health */
    int drift_phase;               /* enum drift_phase */
    char drift_notice[160];
    int drift_health_prev;         /* restored if a re-check is inconclusive */
    double drift_ppm;              /* last measured residual drift */
    double drift_last_check_at;
    double drift_phase_started_at;
    /* The automatic drift check's own claim. It interrupts whatever decode
       view is on screen, so it nests inside that view's -- and it never runs
       while calibration is open, so it is not a third level inside this. */
    struct receiver_lease_token drift_token;
    double drift_recent_ppm[DRIFT_RECENT];
    int drift_recent_count;
};

/*
 * What the Settings panel is holding while it is open: the text being
 * typed and which control has focus. Applying it writes through to the
 * applied_* fields; until then this is the panel's own draft.
 */
struct settings_panel {
    /*
     * Whether the panel is up. Nested like `cal.open`, `bandscan.open` and
     * `help.open`, and it was the last of the four that was not.
     */
    int open;
    /*
     * What went wrong applying the draft, on the panel's own line.
     *
     * The panel's, and only since ticket 08's follow-up: the acquisition
     * lifecycle wrote here too -- "Acquisition is already running", "Cannot
     * block worker signals", "Could not join acquisition worker" -- from
     * `start_acquisition()` and `stop_acquisition()`, which every retune and
     * every rate change go through. The headless startup read it back for its
     * "Cannot start acquisition" line, with a fallback to "unknown" that was
     * the tell. Those are `app->receiver_error` now, the same split ticket 07
     * made for `calibration_status`.
     */
    char error[160];
    /* PPM is the only text field here. The centre frequency was beside it
       until the Scope header grew one, and a value with two homes has two
       parsers. */
    char ppm[16];
    int ppm_length;
    int gain_choice;
    int remove_dc;
    int auto_drift;       /* Settings-panel working copy */
    /* Which transform size the Scope's two frequency charts use. An index
       into sdr_dsp_fft_choice, working copy until Apply. */
    int fft_choice;
};

/*
 * The ADS-B screen's own state: the decoder's position-pairing cache and
 * the newest-first log of messages it has recovered.
 */
struct adsb_view {
    /* The decode: the demodulator, its even/odd pairing cache, this block's
       messages, the funnel and the traces the charts draw
       (adsb_session.h). */
    struct adsb_session session;
    struct adsb_log_entry log[ADSB_LOG_CAPACITY]; /* newest first */
    int log_count;

    /* Analysis mode: which charts are up, and whether the reader has pinned
       the last frame that passed. Both are about looking, not decoding. */
    int analysis_mode;
    int hold_last_good;
};

/*
 * The TETRA decode screen's own state.
 *
 * Stateless per block underneath: a TETRA downlink is continuous and this base
 * station puts a synchronization burst in every timeslot, about seventy a
 * second, so a block carries several and there is nothing to accumulate across
 * blocks. What is kept here is the last identity read, the totals behind it,
 * and enough of the last block to draw.
 */
#define TETRA_LOG_CAPACITY 64

struct tetra_log_entry {
    double at;                  /* seconds since the run started */
    int mcc, mnc, colour, la;
    int bursts, blocks, broadcast;
};

struct tetra_view {
    int analysis_mode;
    /* The decode: identity, lock, counters and the last block's symbols. It
       takes samples and gives back events (tetra_session.h). */
    struct tetra_session session;
    /* What the charts are drawn from: the last block's phase steps as points
       on a circle, and how much of each 255-symbol slot repeated. Derived from
       the session's symbols each block, because a point on a circle is a
       drawing and not a decode. */
    float point_x[TETRA_MAX_SYMBOLS];
    float point_y[TETRA_MAX_SYMBOLS];
    unsigned char point_bit[TETRA_MAX_SYMBOLS];
    int point_count;
    float profile[TETRA_SLOT_SYMBOLS];
    int profile_valid;
    int profile_fixed;
    struct tetra_log_entry log[TETRA_LOG_CAPACITY];   /* newest first */
    int log_count;
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
    /* What the waterfall draws: the reader's zoom and pan over the received
       span, shared with every other frequency chart (chart_window.h). */
    struct chart_window window;

    int earfcn;                 /* 0 when the tuning is not on the raster */
    /* The decode itself: the cell, its measurements, the run's statistics and
       the rule that decides when a broadcast is believed. It takes samples and
       gives back events (lte_session.h). */
    struct lte_session session;
    /* The identity a scripted run has already printed. The search finds the
       same cell in every block, and a line each would bury the message. This
       is the printer's business, not the decode's, so it stays here. */
    int announced_pci;

    /*
     * The receiver's tuning and rate before this view took them.
     *
     * LTE is the only view that changes the sample rate, and it has to: its
     * arithmetic is the 1.92 MS/s grid and nothing else will do (ADR-0014).
     * It borrows the receiver the way the GSM view borrows the tuning, and
     * gives both back on the way out.
     */
    /* Where the receiver was and at what rate, before this view took it to
       1.92 MS/s. One snapshot holds both, which is what retires the separate
       cal_return_sample_rate the calibration overlay used to need. */
    struct receiver_lease_token lease_token;

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
struct gsm_view {
    /* What the waterfall draws: the reader's zoom and pan over the received
       span, shared with every other frequency chart (chart_window.h). */
    struct chart_window window;

    /*
     * The channel being inspected, as a channel number and as its carrier.
     *
     * Two spellings of one fact, always written on the same two lines --
     * `gsm_tune_selected()` and `--arfcn` are the only writers. The number
     * used to be `scan_selected_arfcn` in `struct app`, which named the wrong
     * owner: no scan is involved when the operator picks a channel or when
     * `--arfcn` names one at startup, and a recording's sidecar reads it to
     * say which channel the capture is of. Fourteen of its twenty uses were
     * already in `view_gsm.c` (ticket 08).
     */
    int selected_arfcn;         /* 0 = none */
    double selected_hz;         /* carrier of the selected ARFCN (0 = none) */
    /* The tuning this view borrowed on the way in (receiver_lease.h). The
       band scan nests inside it, so the two unwind in order. */
    struct receiver_lease_token lease_token;
    /* The decode itself: what was read, the continuity, the cell, and the
       front-end options. It takes samples and gives back events, and knows
       nothing about a window (gsm_session.h). */
    struct gsm_session session;
    /*
     * Charts instead of the waterfall. A boolean: 0 waterfall, 1 burst
     * analysis, only ever set to those or toggled between them.
     *
     * Nested like every other technology's -- `fm`, `lte`, `tetra` and `adsb`
     * all have one, and this was the last that did not: it sat loose at the
     * top of `struct app` as `gsm_analysis_mode`, read from five files, which
     * is the straggler `.scratch/deepening/issues/06-*` names.
     *
     * Its comment there read "Burst Analysis Chart: 0=Corr, 1=Soft Bits,
     * 2=Phase", describing a three-way selector the view has not had for some
     * time. Impeccable arithmetic beside false prose, in a field nobody could
     * see because it was loose in a thousand-line struct.
     */
    int analysis_mode;
    int const_amplitude; /* constellation: show amplitude vs unit circle */
    int const_derotated; /* constellation: derotated sample vs differential */
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
    /*
     * What part of the received span the two frequency charts are showing.
     *
     * One window for the spectrum and the waterfall together: they are two
     * drawings of the same samples, and a reader who zoomed one and not the
     * other would have been given two answers to one question.
     *
     * The rules, decided rather than assumed (.scratch/frequency-window/):
     * dragging a region is a zoom and never a retune -- what is on screen is
     * already received and looking closer at it costs nothing. Left and Right
     * pan inside it, and only when the window runs out of received span does
     * panning become retuning, which is the point at which carrying on is
     * impossible any other way.
     */
    /* The transform size the two frequency charts ask for. Only honoured
       while the Scope owns the spectrum (input_scope_owns_spectrum); the
       survey, both band scans and calibration always get the default. */
    int fft_size;

    /* The drawn range, the drag, and the tuning it is all anchored against.
       Shared with every decode view's waterfall (chart_window.h), so what a
       drag means is decided in one place. */
    struct chart_window window;
    /*
     * The control row's three fields. They hold text rather than numbers
     * because a half-typed "94." is not a frequency, and the value is only
     * parsed when the reader commits with Enter. While a field has focus it
     * is left alone; the rest of the time it follows the receiver and the
     * window, so it always says what is actually on screen.
     */
    char centre_text[32];
    char start_text[32];
    char end_text[32];
    int field_focus;            /* SCOPE_FIELD_* below, or -1 for none */

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
 * constants that go with it; the window's lives in freq_window.h. What is
 * left here is this view's state and the few numbers only the view uses.
 */
#define SURVEY_SCAN_HALF_SPAN_HZ 2000000.0  /* "scan this frequency" window */
#define SURVEY_ZOOM_STEP 1.6        /* per key press */
#define SURVEY_PAN_FRACTION 0.25    /* of the visible span, per key press */
#define SURVEY_MIN_SPAN_HZ 100000.0 /* no closer than this */
#define SURVEY_MAX_BANDS 48         /* allocations drawn behind the trace */

/*
 * The survey screen: the text fields, the menus, the frequency window, and
 * what is selected.
 *
 * **The sweep itself is not here.** `survey_session.h` owns the machine --
 * plan, fold, candidates, carriers, confirmation verdicts, history marks,
 * watch summary -- and this is what the drawing needs on top of it
 * (`.scratch/deepening/issues/04-survey-session.md`). The split is the point:
 * everything in this struct is about a window and nothing in it decides.
 */
struct survey_view {
    struct survey_session session;

    char from[24];
    int from_length;
    char to[24];
    int to_length;
    char dwell[12];
    int dwell_length;
    /* Where the receiver is and what it is listening with. Edited here and
       written straight back to the configuration, because they describe the
       installation and should still be true next session. */
    char site[CONFIG_VALUE_MAX];
    int site_length;
    char antenna[CONFIG_VALUE_MAX];
    int antenna_length;
    /* The site is a combo: type a new one, or pick one this receiver has been
       to before. Spelling an existing site differently is how one place
       quietly becomes two, and the list is the cheapest guard against it. */
    int site_menu_open;
    /* The band list, which is long enough to need scrolling where the site
       and antenna lists are not -- row_list.h holds that arithmetic. */
    int band_menu_open;
    int band_scroll;
    int antenna_menu_open;

    int focus;                  /* 0 from, 1 to, 2 dwell, 3 site, 4 antenna */

    /* The range currently typed in the fields. Before the first sweep there is
       no swept range, and the chart, the zoom and the drag all need something
       to work against -- so they work against this. */
    double field_lower_hz;
    double field_upper_hz;
    /* What of it is on screen. Zooming narrows this window without resampling
       the array, so the same measurements are simply drawn larger. */
    double view_lower_hz;
    double view_upper_hz;

    /* Where the operator had the receiver before a sweep walked it across a
       band. Held for as long as the view is up, because it still owns the
       right to sweep again; given up for good by "Open waterfall", which is
       a deliberate handoff rather than a forgotten restore. */
    struct receiver_lease_token lease_token;
    /* Nested inside that claim: a confirmation pass walks the receiver across
       the candidates and hands it back to the sweep's tuning, not to whatever
       was on screen before the survey. */
    struct receiver_lease_token confirm_lease_token;
    /* Whether the pass running was asked for on the command line, and so has
       to report on stdout: a verdict nobody can read is a verdict that may as
       well not have been reached (ADR-0012). */
    int confirm_printed;

    int selected;               /* index into the session's peaks, -1 for none */
    /* How far down the candidate list is scrolled, in rows. The list used to
       draw the first seventeen and stop, which put the rest of a full-tuner
       sweep somewhere no pointer or key could reach. survey_list.h holds the
       arithmetic. */
    int list_scroll;
    int hover;

    /* Dragging a rectangle across the chart to zoom into it. A press only
       becomes a drag once the pointer has moved; below that it is a click,
       and a click selects a candidate. */
    int drag_active;
    float drag_from_x;
    double drag_from_hz;
    double drag_to_hz;

    /*
     * The spelling of the range a narrowing sweep replaced, so Reset zoom can
     * put the fields back the way they were typed. The *measurements* it puts
     * back are the session's (`survey_session_keep()`), because a measurement
     * is not a property of a window -- and keeping them here is how the
     * restore came to be written twice and broken twice.
     */
    char kept_from[24];
    char kept_to[24];
};

struct app {
    struct scope_view sv;
    struct survey_view survey;
    struct gsm_view gsm;
    struct adsb_view adsb;
    struct tetra_view tetra;
    struct lte_view lte;
    struct fm_view fm;
    struct settings_panel set;
    struct help_overlay help;
    struct calibration cal;
    struct band_scan bandscan;
    struct acquisition acq;
    struct options options;
    /* Antenna and site: what makes one sweep comparable to another, loaded
       once at startup and reported by anything that measures. The file
       format; `installation` below is what decides. */
    struct config config;
    /*
     * The receiving setup -- receiver, site, antenna -- and what has been
     * calibrated for it (installation.h, ADR-0018 and ADR-0022). Views set
     * fields on it and never save; `installation_commit()` is the one writer.
     */
    struct installation installation;
    struct sdr_dsp dsp;
    /* The open source, whatever kind it is: a receiver, a capture, later a
       UHD device. `device_backend.h` owns the handle; nothing here looks at
       it. This was an `rtlsdr_dev_t *`, which is why `<rtl-sdr.h>` used to be
       included by a header every view reads. */
    struct device_session source;
    FILE *capture;
    int window_ready;
    int signals_ready;
    int receiver_mode;
    int applied_manual_gain;
    int applied_gain_tenths;
    int applied_ppm;
    /* The gain list was here, as a pointer and a count. It is
       `device.gain_list` / `gain_count`, reached through
       `device_gain_option_count()` and `device_gain_option_value()` so a
       continuous range and a discrete list read the same (ticket 06). */
    uint32_t applied_frequency;
    uint32_t applied_sample_rate;
    /*
     * What this run's samples came out of: the container, its full scale, the
     * tuner's reach, the gain model (device_profile.h). One profile per
     * source -- a receiver run and a file run fill it in differently, and
     * everything that needs to know what a count means asks it rather than
     * assuming eight bits.
     */
    struct device_profile device;
    /* Who borrowed the tuning above, and what they put back when they give it
       up. The truth about where the receiver *is* stays in the two fields
       above; this is only the stack of where it was (receiver_lease.h). */
    struct receiver_lease lease;
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

    /*
     * One spectrum, sized to the largest transform the Scope may ask for.
     * `spectrum_bins` is how many of them are currently filled -- everything
     * that reads this must use that rather than the array's length, which is
     * a capacity and not a count.
     */
    float spectrum_average[SDR_DSP_FFT_MAX];
    float spectrum_candidate[SDR_DSP_FFT_MAX];
    float spectrum_peak[SDR_DSP_FFT_MAX];
    int spectrum_bins;
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

    int remove_dc;

    /*
     * Why the receiver last refused, in words, from `retune_receiver()`, the
     * two beside it, and the acquisition lifecycle underneath them.
     *
     * A handoff and not calibration's, which is what it used to be: every
     * screen retunes through one function, and all five of its failure
     * messages were written into `calibration_status` and prefixed
     * "Calibration" -- so a survey step that would not tune, or an FM
     * station, or a Mode S retune, reported its reason into the calibration
     * overlay's status line, on a screen nobody was looking at, describing
     * something that had nothing to do with calibration. `view_lte.c` knew
     * and quoted it by hand to find out why 1.92 MS/s had been refused, which
     * is the tell.
     *
     * `start_acquisition()` and `stop_acquisition()` were writing their
     * failures into `settings_error` for the same kind of reason, which left
     * this buffer *stale* on the path that matters most: `retune_receiver()`
     * stops and restarts acquisition, so a caller quoting this after a failed
     * retune could read a message from something else entirely. One buffer,
     * one meaning.
     *
     * The caller decides whether to show it. What matters is that the reason
     * exists under an honest name instead of being thrown away.
     */
    char receiver_error[160];


};

#endif
