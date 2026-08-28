/* rtl_tui: plots RTL-SDR signal magnitude over time.
 *
 * Tunes to 1090 MHz at 2 MS/s with max manual gain (the dump1090/ADS-B
 * setup), then streams samples via the async librtlsdr callback. Each
 * incoming block is converted to one display frame of per-bin peak
 * magnitudes on the USB callback thread; the main thread just pops the
 * newest frame and hands it to the rendering layer (tui.c), which draws
 * it as a scrolling line of ASCII ("waterfall turned sideways": x = time
 * within the block, glyph = signal strength). Runs until 'q' or Ctrl-C.
 *
 * With --file <path>, reads raw 8-bit interleaved I/Q from a file instead
 * of the dongle (for hardware-free testing with testfiles/modes1.bin).
 * File mode uses the exact same producer/consumer path, fed by a worker
 * thread, so the renderer cannot tell the two sources apart.
 *
 * Build: make rtl_tui
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <rtl-sdr.h>

#include "tui.h"

/* Signal conventions shared with dump1090 (see AGENTS.md):
 * 1090 MHz is the ADS-B downlink; at 2 MS/s one I/Q pair spans 0.5 us,
 * which matches the 0.5 us chip of Mode-S Manchester encoding, so each
 * ADS-B pulse lands in a single sample. */
#define FREQ        1090000000  /* ADS-B frequency. */
#define SAMPLE_RATE 2000000     /* 2 MS/s: 1 sample = 0.5 us. */
#define BLOCK_BYTES (16*16384)  /* 256 KB, like dump1090's block; at 2 MS/s
                                 * this is 131072 I/Q pairs = ~65 ms. */

/* One peak per rendered column; the cap itself lives in tui.h. */
#define MAX_COLS TUI_MAX_COLS

/* Set by SIGINT/SIGTERM and by the 'q' key; polled by every thread.
 * sig_atomic_t so the signal handler can write it safely. */
static volatile sig_atomic_t stop = 0;
static void on_signal(int sig) { (void)sig; stop = 1; }

/* Convert one raw I/Q byte pair to a magnitude on the 127.5-centered scale.
 *
 * The dongle delivers unsigned 8-bit samples where 127/127.5 means "zero
 * signal", so we subtract 127.5 to get signed I and Q in [-127.5, 127.5]
 * and return |I + jQ| = sqrt(I^2 + Q^2) in [0, ~180.4]. This deliberately
 * ignores the phase; for a "is there energy here?" display, magnitude is
 * all we need, and it is exactly what dump1090 computes per sample before
 * looking for the ADS-B preamble. */
static float iq_magnitude(unsigned char i, unsigned char q) {
    float fi = (float)i - 127.5f, fq = (float)q - 127.5f;
    return sqrtf(fi*fi + fq*fq);
}

/* --- producer/consumer state ---------------------------------------------
 *
 * Threading model (mirrors the planned raylib spec in docs/):
 *
 *   USB thread (librtlsdr's own)          main thread
 *   --------------------------            ------------------------
 *   rtlsdr_callback():                    render_loop():
 *     block -> per-bin peaks                pop newest frame
 *     -> push into single slot              tui_frame() + poll keyboard
 *
 * The hand-off is a single mutex-guarded slot, NOT a queue: if the
 * renderer is lagging, stale frames are overwritten and counted in
 * dropped_frames. A magnitude-over-time display strictly prefers the
 * freshest block over every block, and a queue could grow unboundedly
 * (frames arrive every ~131 ms; a slow tty could easily render slower
 * than that).
 *
 * Locking discipline: the callback computes peaks into a LOCAL array
 * first and holds the mutex only for the final memcpy/publish, so the
 * USB thread is never stalled by slow rendering for more than a few
 * hundred nanoseconds -- librtlsdr drops USB packets if its callback
 * blocks, which would show up as gaps in the time axis.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static float    frame_peak[MAX_COLS]; /* per-bin peak magnitudes */
static int      frame_nbins = 0;      /* valid entries in frame_peak */
static int      frame_ready = 0;      /* 1 when frame_peak holds a new frame */
static unsigned long long total_pairs = 0;   /* I/Q pairs seen, for stats */
static unsigned long long dropped_frames = 0;/* frames overwritten unread */
static int      cols = 80;            /* terminal width; written once by
                                       * main before streaming starts,
                                       * read-only afterwards, so the
                                       * callback can use it lock-free */
static rtlsdr_dev_t *dev = NULL;      /* dongle handle, for cancel_async */
static int      use_file = 0;

/* librtlsdr async callback: convert one block to per-bin peaks and publish.
 *
 * Why PEAK per bin and not average: an 80-column frame bins 131072 samples
 * into groups of ~1638 (~0.8 ms each). An ADS-B pulse is 0.5 us wide, so
 * averaging would dilute it ~1600x into the noise floor and the display
 * would stay blank even while rtl_adsb decodes messages (verified on
 * testfiles/modes1.bin: strongest bin average was 47.7 vs peaks of 180).
 * Peak-hold preserves any pulse that landed anywhere inside the bin. */
static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
    (void)ctx;
    int pairs = (int)len / 2;        /* interleaved I/Q -> pairs */
    if (pairs <= 0 || stop) return;
    int c = cols;
    if (c > MAX_COLS) c = MAX_COLS;
    /* Samples per column, rounded up so every column gets some data and
     * the last bin may simply be short. */
    int bin = (pairs + c - 1) / c;
    int nbins = 0;
    float pk[MAX_COLS];              /* local: computed WITHOUT the lock */
    for (int i = 0; i < c; i++) {
        int start = i * bin;
        if (start >= pairs) break;
        int end = start + bin;
        if (end > pairs) end = pairs;
        float m = 0.0f;
        for (int j = start; j < end; j++) {
            float v = iq_magnitude(buf[j*2], buf[j*2+1]);
            if (v > m) m = v;
        }
        pk[i] = m;
        nbins = i + 1;
    }
    /* Publish: brief critical section only. Overwriting an unread frame
     * is intentional (see header comment). */
    pthread_mutex_lock(&lock);
    if (frame_ready)
        dropped_frames++;
    memcpy(frame_peak, pk, (size_t)nbins * sizeof(float));
    frame_nbins = nbins;
    frame_ready = 1;
    total_pairs += (unsigned long long)pairs;
    pthread_mutex_unlock(&lock);
}

/* File-mode producer thread: feed the capture through the same callback,
 * looping the file forever, paced so the display scrolls at wall-clock
 * speed instead of free-running at fread() speed. One 256 KB block is
 * 131072 I/Q pairs = ~131 ms at 2 MS/s, so sleep that long per block. */
static void *file_streamer(void *arg) {
    FILE *fp = arg;
    static unsigned char buf[BLOCK_BYTES]; /* static: 256 KB off the stack */
    while (!stop) {
        size_t got = fread(buf, 1, sizeof(buf), fp);
        if (got < sizeof(buf)) {
            if (got == 0) {
                if (feof(fp)) { rewind(fp); continue; } /* loop forever */
                break; /* genuine read error: give up */
            }
            /* Short read at EOF: top the block up from the start of the
             * file so every frame is a full 256 KB. */
            rewind(fp);
            size_t more = fread(buf + got, 1, sizeof(buf) - got, fp);
            if (more == 0) continue;
            got += more;
        }
        rtlsdr_callback(buf, (uint32_t)got, NULL);
        struct timespec ts = { 0, 131072000L };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* Dongle-mode worker thread: run the blocking async reader.
 *
 * rtlsdr_read_async() blocks until rtlsdr_cancel_async() is called from
 * another thread; while running, it fires rtlsdr_callback() on librtlsdr's
 * internal USB thread for every block. Parameters: dev, callback, ctx,
 * buf_num 0 = library default (15), buf_len = 256 KB per block. */
static void *async_reader(void *arg) {
    rtlsdr_dev_t *d = arg;
    if (rtlsdr_read_async(d, rtlsdr_callback, NULL, 0, BLOCK_BYTES) < 0)
        fprintf(stderr, "Async read failed.\n");
    return NULL;
}

/* Render loop (main thread): pop the newest frame and draw it.
 *
 * Polls at ~50 fps. Most polls find no new frame (frames arrive every
 * ~131 ms, i.e. ~7.6/s) and just check the keyboard; when a frame is
 * available it is handed to tui_frame(), which draws it as a single line
 * that scrolls upward under the previous ones, giving a
 * magnitude-over-time history for free. */
static void render_loop(const char *source) {
    int frame = 0; /* frame counter, only used to animate the spinner */
    char header[256];
    snprintf(header, sizeof(header),
             "rtl_tui  %.3f MHz  %d S/s  %s", FREQ/1e6, SAMPLE_RATE, source);
    while (!stop) {
        /* Grab the freshest frame, if any. Copy it out under the lock so
         * the producer can start filling the next one immediately. */
        pthread_mutex_lock(&lock);
        int have = frame_ready;
        int nbins = frame_nbins;
        float pk[MAX_COLS];
        unsigned long long tp = total_pairs;
        if (have) {
            memcpy(pk, frame_peak, (size_t)nbins * sizeof(float));
            frame_ready = 0;
        }
        pthread_mutex_unlock(&lock);

        if (have && nbins > 0)
            tui_frame(pk, nbins, header, tp, frame++);

        if (tui_quit_pressed())
            stop = 1;

        struct timespec ts = { 0, 20000000L }; /* 20 ms: ~50 fps poll */
        nanosleep(&ts, NULL);
    }
}

/* --- main --------------------------------------------------------------- */

int main(int argc, char **argv) {
    FILE *fp = NULL;
    const char *path = NULL;
    int numgains, gains[100], gain;

    if (argc == 3 && strcmp(argv[1], "--file") == 0) {
        use_file = 1;
        path = argv[2];
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [--file capture.bin]\n", argv[0]);
        return 1;
    }

    if (use_file) {
        fp = fopen(path, "rb");
        if (!fp) {
            fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
            return 1;
        }
    } else {
        /* Find and open the first device. */
        int device_count = rtlsdr_get_device_count();
        if (!device_count) {
            fprintf(stderr, "No supported RTLSDR devices found.\n");
            return 1;
        }
        if (rtlsdr_open(&dev, 0) < 0) {
            fprintf(stderr, "Error opening the RTLSDR device: %s\n",
                    strerror(errno));
            return 1;
        }
        /* Manual gain mode, set to the maximum available gain -- the
         * dump1090 policy: ADS-B messages are strong and brief, and AGC
         * hunting during a message can corrupt it. (rtl_adsb uses AGC;
         * that is why its gain line reads "automatic".) */
        rtlsdr_set_tuner_gain_mode(dev, 1);
        numgains = rtlsdr_get_tuner_gains(dev, gains);
        gain = gains[numgains-1]; /* gains are in tenths of a dB */
        rtlsdr_set_tuner_gain(dev, gain);
        rtlsdr_set_center_freq(dev, FREQ);
        rtlsdr_set_sample_rate(dev, SAMPLE_RATE);
        rtlsdr_reset_buffer(dev); /* drop stale pre-tune samples */
    }

    /* The TUI needs a real terminal: raw input mode + ANSI escapes. */
    if (tui_setup() < 0) {
        fprintf(stderr, "Not a terminal; cannot run TUI.\n");
        if (fp) fclose(fp);
        if (dev) rtlsdr_close(dev);
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* Publish the render width BEFORE any producer thread starts, so the
     * callback can read it without taking the lock (see state comment). */
    cols = tui_cols();
    if (cols < 10) cols = 10;
    if (cols > MAX_COLS) cols = MAX_COLS;

    if (use_file) {
        /* File mode: same producer/consumer split, but "streamed" from a
         * worker thread so the render loop is identical for both sources. */
        pthread_t tid;
        if (pthread_create(&tid, NULL, file_streamer, fp) != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(errno));
            fclose(fp);
            return 1;
        }
        render_loop(path);
        stop = 1;              /* make the producer exit its loop */
        pthread_join(tid, NULL);
    } else {
        /* Async streaming: rtlsdr_read_async() blocks until cancelled, so
         * run it on a worker thread and render on the main thread.
         * Shutdown order: renderer notices 'q'/signal -> stop=1 ->
         * cancel_async() -> read_async returns on the worker -> join.
         * (The callback also checks `stop` so it stops publishing frames
         * during teardown.) */
        pthread_t tid;
        if (pthread_create(&tid, NULL, async_reader, dev) != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(errno));
            rtlsdr_close(dev);
            return 1;
        }
        render_loop("rtl-sdr");
        stop = 1;
        rtlsdr_cancel_async(dev);
        pthread_join(tid, NULL);
    }

    /* Informational only: nonzero drops just mean the producer outran
     * the tty at some point; the display kept showing the newest data. */
    if (dropped_frames)
        fprintf(stderr, "(dropped %llu stale frames)\n", dropped_frames);

    if (fp) fclose(fp);
    if (dev) rtlsdr_close(dev);
    /* Terminal restore runs via the atexit handler installed by
     * tui_setup(). */
    fprintf(stderr, "\n");
    return 0;
}
