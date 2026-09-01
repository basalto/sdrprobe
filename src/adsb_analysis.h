#ifndef ADSB_ANALYSIS_H
#define ADSB_ANALYSIS_H

#include <string.h>

#include "adsb_dsp.h"

/*
 * What the ADS-B view decides: whether Mode S could be there at all, which
 * frame the analysis charts are describing, what the message log holds, and
 * what the funnel counters add up to.
 *
 * The charts have no caption that proves which frame they are drawn from, so a
 * latch that quietly follows the newest frame is invisible -- the envelope and
 * the confidence bars keep looking plausible while describing something else
 * entirely. The funnel is the operator's evidence about whether the antenna or
 * the demodulator is the problem, and a counter that double-counts sends them
 * after the wrong one.
 *
 * Plain data, no raylib (ADR-0012); checked by tests/adsb_analysis_test.c.
 */

#define ADSB_LOG_CAPACITY 256

/* How far off 1090 MHz the receiver may be tuned and still be looking at Mode
   S. Wider than the signal: a frame is 1 MHz wide either side of the carrier
   and the whole point is that a nearly-right tuning still decodes. */
#define ADSB_TUNED_TOLERANCE_HZ 200000
#define ADSB_MIN_SAMPLE_RATE 2000000U

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

/*
 * Whether the receiver is where Mode S is. Off 1090 MHz, or below 2 MS/s,
 * nothing that arrives can be a frame -- a pulse is half a microsecond, so a
 * slower rate cannot resolve one -- and the view says so instead of drawing
 * empty charts that look like a quiet sky.
 */
static inline int adsb_receiver_ready(uint32_t frequency_hz,
                                      uint32_t sample_rate,
                                      uint32_t mode_s_hz) {
    long delta = (long)frequency_hz - (long)mode_s_hz;

    if (delta < 0)
        delta = -delta;
    return delta < ADSB_TUNED_TOLERANCE_HZ &&
           sample_rate >= ADSB_MIN_SAMPLE_RATE;
}

/* Analysis panels are worth drawing only when Mode S could actually be there;
   off frequency the retune affordance is the useful thing to show, not three
   empty charts. */
static inline int adsb_analysis_visible(int analysis_mode, int ready) {
    return analysis_mode && ready;
}

/*
 * Keep an incoming trace. `latest` is the most recent attempt whatever its
 * outcome, because a frame that failed its CRC is the one worth looking at;
 * `good` is the last one that passed, for the Hold toggle to pin.
 *
 * A trace that is not valid is not kept at all: a block with no preamble in it
 * would otherwise blank the charts, and there are many such blocks between
 * frames even under a busy approach path.
 */
static inline void adsb_trace_keep(struct adsb_frame_trace *latest,
                                   struct adsb_frame_trace *good,
                                   const struct adsb_frame_trace *incoming) {
    if (!incoming->valid)
        return;
    *latest = *incoming;
    if (incoming->crc_ok)
        *good = *incoming;
}

/*
 * Which trace the charts draw. Hold pins the last frame that passed its CRC;
 * without it, or before any frame has passed, the charts follow the latest
 * attempt.
 *
 * Hold must survive new frames arriving -- that is the entire point of it. An
 * operator holds a frame precisely because traffic is coming in too fast to
 * read, and a latch that lets the next frame through has silently done
 * nothing.
 */
static inline const struct adsb_frame_trace *
adsb_trace_shown(const struct adsb_frame_trace *latest,
                 const struct adsb_frame_trace *good, int hold) {
    if (hold && good->valid)
        return good;
    return latest;
}

/*
 * Push a row onto the front of the log, which is newest-first and drops the
 * oldest row when full.
 */
static inline void adsb_log_push(struct adsb_log_entry *log, int *count,
                                 const struct adsb_log_entry *entry) {
    int keep = *count < ADSB_LOG_CAPACITY ? *count : ADSB_LOG_CAPACITY - 1;

    memmove(&log[1], &log[0], (size_t)keep * sizeof(*log));
    log[0] = *entry;
    if (*count < ADSB_LOG_CAPACITY)
        (*count)++;
}

/* Clear the highlight on every row, so only the rows added next stand out. */
static inline void adsb_log_fade(struct adsb_log_entry *log, int count) {
    for (int i = 0; i < count; i++)
        log[i].highlight = 0;
}

/* Add one block's funnel counters to the running totals. */
static inline void adsb_totals_add(struct adsb_demod_stats *totals,
                                   const struct adsb_demod_stats *block) {
    totals->preambles += block->preambles;
    totals->attempts += block->attempts;
    totals->crc_failed += block->crc_failed;
    totals->decoded += block->decoded;
}

/*
 * Whether a set of funnel counters can be believed. Every stage is a subset of
 * the one before it: preambles found, of those the ones worth demodulating, of
 * those the ones that failed CRC, and the rest decoded. Counters that break
 * this describe an impossible run, and the operator would read them as
 * evidence about their antenna.
 */
static inline int adsb_funnel_is_consistent(
    const struct adsb_demod_stats *totals) {
    return totals->attempts <= totals->preambles &&
           totals->crc_failed <= totals->attempts &&
           totals->decoded <= totals->attempts &&
           totals->crc_failed + totals->decoded <= totals->attempts;
}

#endif
