#ifndef ACQUISITION_H
#define ACQUISITION_H

#include <pthread.h>
#include <rtl-sdr.h>
#include <stdint.h>
#include <stdio.h>

/* Deliberately dump1090's block size, so timing matches it. */
#define SAMPLE_BLOCK_BYTES (16 * 16384)
#define SAMPLE_BLOCK_PAIRS (SAMPLE_BLOCK_BYTES / 2)

/* Long enough for captures/<name>.bin plus a timestamp. */
#define ACQUISITION_PATH_MAX 256

/* Deliberately dump1090's block size. */

/*
 * Getting samples off the receiver, and out to a file.
 *
 * A worker thread reads 256 KB blocks and hands the newest one to the renderer
 * through a single overwriteable slot -- freshness over continuity, which
 * ADR-0002 explains. Recording tees off the acquisition side rather than the
 * consumed side precisely because that slot drops blocks.
 *
 * This file knows nothing about struct app. What it needs from the rest of the
 * program -- the device handle, the playback file, the sample rate -- is
 * handed to it when acquisition starts.
 */

struct latest_block {
    unsigned char data[SAMPLE_BLOCK_BYTES];
    uint32_t len;
    uint64_t generation;
    uint64_t published_blocks;
    uint64_t processed_blocks;
    uint64_t overwritten_blocks;
    uint64_t malformed_blocks;
    int ready;
    int worker_done;
    int worker_failed;
    int worker_reading;
    int stop;
    char worker_error[160];
    pthread_mutex_t mutex;
};

struct slot_snapshot {
    uint64_t published_blocks;
    uint64_t processed_blocks;
    uint64_t overwritten_blocks;
    uint64_t malformed_blocks;
    int worker_done;
    int worker_failed;
    char worker_error[160];
};

#define SCATTER_SAMPLES 4096
#define SCATTER_HISTORY_BLOCKS 64
#define SCATTER_HISTORY_SECONDS 1.0
#define PEAK_DECAY_DB_PER_SECOND 20.0f
#define PHYSICAL_MAGNITUDE_MAX 180.31223f
#define SPECTRUM_TOP_DBFS 6.0f
#define SCALE_FACTOR 0.8f
#define DB_SCALE_STEP 10.0f
#define CALIBRATION_RECENT 64
#define CALIBRATION_SETTLE_SECONDS 2.0
#define CALIBRATION_MIN_SECONDS 8.0
#define CALIBRATION_MAX_SEM_PPM 1.0
#define CALIBRATION_VIEW_HALF_WIDTH_HZ 250000.0
#define CALIBRATION_SOURCE_CENTROID 0
#define CALIBRATION_SOURCE_FCCH 1
#define CALIBRATION_FCCH_MISS_LIMIT 12
#define SCAN_BAND_LOWER_HZ 935100000.0
#define SCAN_BAND_UPPER_HZ 959900000.0
#define SCAN_EDGE_MARGIN_HZ 200000.0
#define SCAN_STEP_SETTLE_SECONDS 0.35
#define SCAN_STEP_PROBE_SECONDS 0.45
#define SCAN_SENTINEL_DBFS (-300.0f)
#define SCAN_BCCH_MIN_CONF 0.85f
#define DRIFT_CHECK_INTERVAL_SECONDS 300.0
#define DRIFT_CHECK_SETTLE_SECONDS 2.0
#define DRIFT_CHECK_MEASURE_SECONDS 3.0
#define DRIFT_MAX_PPM 2.0
#define DRIFT_MIN_MEASUREMENTS 8
#define DRIFT_RECENT 64

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
#define TAB_COUNT 2

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

/*
 * What acquisition owns.
 *
 * The single overwriteable slot the worker hands blocks through (ADR-0002),
 * the worker itself, and the raw-I/Q recording that tees off the same path.
 * Nothing outside acquisition reads these; the device handle and the tuning
 * stay in struct app, because settings and calibration adjust them too.
 */
struct acquisition {
    struct latest_block latest;
    uint64_t capture_bytes;
    pthread_t worker;
    int mutex_ready;
    int worker_started;
    unsigned char raw[SAMPLE_BLOCK_BYTES];
    unsigned char file_block[SAMPLE_BLOCK_BYTES];
    uint32_t raw_len;
    uint64_t consumed_generation;
    pthread_mutex_t record_mutex;
    FILE *record_file;
    int recording;
    uint64_t record_bytes;
    uint64_t record_limit_bytes;
    uint64_t record_short_blocks;
    char record_path[256];
    uint32_t record_frequency_hz;
    uint32_t record_sample_rate;
    int record_gain_tenths;
    int record_manual_gain;
    int record_ppm;
    int record_arfcn;
    double record_carrier_offset_hz;
    char record_source[320];
    char record_tuner[32];
    char record_started_at[32];

    /* Borrowed for the worker's lifetime; struct app owns these. Acquisition
       reads from them, settings and calibration retune them. */
    rtlsdr_dev_t *dev;
    FILE *capture;
    uint32_t sample_rate;
    const char *capture_path;
};

/* What a capture's sidecar records about the tuning it was taken at. The
   caller supplies it because acquisition does not know how the receiver is
   tuned; it only knows what arrived. */
struct acquisition_record_request {
    uint32_t frequency_hz;
    uint32_t sample_rate;
    int gain_tenths;
    int manual_gain;
    int ppm;
    int arfcn;              /* 0 when not tuned via the GSM view */
    double carrier_offset_hz;
    const char *source;
    const char *tuner;
};

/* Hand acquisition the source to read from. Must be called before starting a
   worker: these handles are borrowed, and nothing else sets them. */
void acquisition_attach_source(struct acquisition *acq, rtlsdr_dev_t *dev,
                               FILE *capture, uint32_t sample_rate,
                               const char *capture_path);

void publish_block(struct acquisition *acq, const unsigned char *data,
                   uint32_t len);
int consume_latest(struct acquisition *acq, struct slot_snapshot *snapshot);
void *receiver_worker(void *arg);
void *file_worker(void *arg);
void receiver_callback(unsigned char *buffer, uint32_t len, void *ctx);
int worker_stop_requested(struct acquisition *acq);
void request_worker_stop(struct acquisition *acq);

/* Recording. start returns 0 on success; the worker closes the file itself
   when the limit is reached and writes the sidecar beside it. */
int acquisition_start_recording(struct acquisition *acq, const char *path,
                                const struct acquisition_record_request *req);
int acquisition_recording_status(struct acquisition *acq, uint64_t *bytes,
                                 char *path, size_t path_size);

/* Lifecycle for the recording mutex; the block slot's own mutex is set up
   by the caller alongside the worker. */
int acquisition_init(struct acquisition *acq);
void acquisition_destroy(struct acquisition *acq);
void acquisition_stop_recording(struct acquisition *acq);

#endif
