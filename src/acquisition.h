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
    /* Off by default: a slow renderer drops blocks rather than lagging behind
       the receiver (ADR-0002). Set only for file playback that is being read
       by a script, where dropping a block silently changes the answer. */
    int lossless;
    char worker_error[160];
    pthread_mutex_t mutex;
    /* Signalled when the slot is emptied; only a lossless publisher waits. */
    pthread_cond_t drained;
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
/* The calibration constants and the stability gate live in a header that
   depends on nothing, so they can be checked without a window (ADR-0012). */
#include "calibration_gate.h"
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

#define TAB_COUNT 2

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
    char record_technology[16];
    char record_source[320];
    char record_tuner[32];
    char record_started_at[32];

    /* Borrowed for the worker's lifetime; struct app owns these. Acquisition
       reads from them, settings and calibration retune them. */
    rtlsdr_dev_t *dev;
    FILE *capture;
    uint32_t sample_rate;
    const char *capture_path;
    int capture_loop;       /* 0 = stop at the end instead of wrapping */
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
    /* Which technology the capture was taken for ("gsm", "adsb"). The reader
       of a raw .bin cannot tell from the samples, and the GSM fields below
       being absent says only that no channel was selected. */
    const char *technology;
    const char *source;
    const char *tuner;
    /* How long to record for. The buttons pass
       ACQUISITION_RECORD_BUTTON_SECONDS; --record-seconds passes its own. */
    double seconds;
};

/* What the Record buttons capture. Long enough for a GSM multiframe and for
   Mode S traffic to show up, short enough to keep a test capture in the tens
   of megabytes. */
#define ACQUISITION_RECORD_BUTTON_SECONDS 2.0

/* Hand acquisition the source to read from. Must be called before starting a
   worker: these handles are borrowed, and nothing else sets them. */
void acquisition_attach_source(struct acquisition *acq, rtlsdr_dev_t *dev,
                               FILE *capture, uint32_t sample_rate,
                               const char *capture_path, int capture_loop);

/* Deliver every block instead of overwriting the slot: the worker waits for
   the consumer to take one before publishing the next. For *file playback
   only* -- a live receiver must never be made to wait, because the callback
   blocking stalls the USB stream and loses samples for real. With it set, the
   file worker also stops pacing to real time: nothing is watching, and the
   consumer sets the rate.

   This is what makes a headless decode deterministic. Without it, the same
   capture decodes a different number of frames on a loaded machine than on an
   idle one, and a check that asserts on the count is a coin toss (ADR-0012,
   layer 2). Call before starting the worker. */
void acquisition_set_lossless(struct acquisition *acq, int lossless);

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
