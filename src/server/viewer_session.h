#ifndef VIEWER_SESSION_H
#define VIEWER_SESSION_H

/* For `enum viewer_stream`, which the pacing table below is a statement
   about: a table that could not name every value of its enum is the shape
   of fault this ticket exists to stop. */
#include "server/viewer_link.h"

struct app;

/* Where a Viewer link listens when nothing names a port -- ADR-0027's
   loopback, an arbitrary but memorable high port nothing else on this
   machine is likely to want. */
#define VIEWER_SESSION_DEFAULT_PORT 8765

/*
 * `server`/`web`: drives ticket 02's advance step with no window,
 * builds ticket 03's view model each block, and publishes it over ticket
 * 04's WebSocket server (joined by src/server/viewer_link.c) instead of drawing
 * it. Runs until Ctrl-C (`stop_requested()`) or, for file playback, until
 * the capture ends.
 *
 * Does not start or stop acquisition -- like `survey_report_run()`, that is
 * the caller's (`run_headless()`, src/app/sdrprobe.c) to do around this call,
 * so a failure here still gets a clean shutdown from the one place that
 * already knows how.
 */
int viewer_session_run(struct app *app);

/*
 * How often a metadata State update goes out when nothing about it changed.
 *
 * These exist because publishing them on every loop iteration made the serve
 * loop spin. `viewer_link_publish_*()` **queues** rather than sends, so a
 * publish always leaves a slot with unsent bytes; `viewer_link_poll()` then
 * registers the client for writing, `select()` returns at once because a
 * loopback socket is writable, and the 20 ms poll timeout that is the loop's
 * only pacing is never reached. Publishing unconditionally therefore
 * guaranteed the loop could never idle -- measured at ~100 000 iterations a
 * second and 98.6% of a core for a client subscribed to `receiver_state`
 * alone, against 9.8% with no client at all
 * (`.scratch/web-visualization/issues/10-*`).
 *
 * A quarter second for the receiver's state: every field in it -- centre,
 * rate, correction, tuning generation, full scale -- changes only on a
 * retune, and a retune says so through the generation below rather than
 * waiting for this. So this interval is not how fresh the value is, it is how
 * long a Viewer that has just connected waits to be told where the receiver
 * is pointed, and how long a Viewer waits to learn the link is still alive.
 * It is also comfortably under a block at any rate this program uses (15.26
 * blocks a second at 2 MS/s), so it can never become the loop's clock.
 *
 * A second for the link's health: `server_cpu_percent` is sampled once a
 * second and cannot be fresher than that.
 */
#define VIEWER_SESSION_STATE_INTERVAL_SECONDS 0.25
#define VIEWER_SESSION_HEALTH_INTERVAL_SECONDS 1.0

/*
 * Whether a periodic State update is due: because something it carries
 * changed, because it has never been sent, or because the interval has
 * passed.
 *
 * `published_at` is when this stream last went out, and **negative means
 * never** -- a caller cannot use 0.0 for that, since a loop timing from its
 * own start has a real 0.0. `changed` is the caller's own comparison: for
 * `receiver_state` it is the tuning generation differing from the one last
 * published, which is what makes a retune immediate rather than something a
 * Viewer waits a quarter second to hear about.
 *
 * Out here rather than inside the loop because the loop takes `struct app`
 * and nothing windowless can reach it (ADR-0012), and this is a decision.
 */
static inline int viewer_update_due(double now, double published_at,
                                    double interval_seconds, int changed) {
    if (changed)
        return 1;
    if (published_at < 0.0)
        return 1;
    return now - published_at >= interval_seconds;
}

/*
 * What paces a stream -- and there are two answers, which is why one "is
 * this due?" cannot serve both.
 *
 * **On data.** The eleven view streams carry what a sample block produced.
 * Republishing between blocks sends the same numbers again, and a publish
 * **queues into a replaceable slot rather than sending**, so an ungated
 * republish leaves `select()` permanently ready and the loop never waits.
 * Measured twice, at four and five orders of magnitude over the block rate:
 * ticket 10 for `receiver_state`, ticket 07 for the survey pair, at
 * **234216 messages in 10 seconds** against the 15.26/s a live receiver's
 * block rate caps it at.
 *
 * **On time.** `receiver_state` and `link_health` change without a block
 * arriving -- a retune, a reconnect, a CPU sample -- so they are paced by an
 * interval, with `changed` making a retune immediate rather than something a
 * Viewer waits a quarter second to hear about.
 *
 * **Neither.** `command_result` is a reply. It goes to whoever sent the
 * command, when they sent it, and the loop never publishes it at all.
 *
 * `VIEWER_PACED_UNKNOWN` is what a stream nobody has paced gets, and it is
 * the point of the table: `check-viewer-session` walks every value of
 * `enum viewer_stream` and fails on it, so adding a stream without saying
 * what paces it fails the gate rather than shipping. That is the
 * enumeration an inline `if` cannot have -- and it is the whole of what a
 * unit check can see here. It cannot see the *shape of the loop*, so a
 * publish call written outside the gate is still invisible to it;
 * `make bench-serve` is what catches that, and it is a number rather than an
 * opinion (`.scratch/web-visualization/issues/12-*`).
 */
enum viewer_stream_pacing {
    VIEWER_PACED_UNKNOWN = 0,
    VIEWER_PACED_ON_DATA,
    VIEWER_PACED_ON_TIME,
    VIEWER_PACED_ON_DEMAND
};

static inline const char *viewer_pacing_name(enum viewer_stream_pacing p) {
    switch (p) {
    case VIEWER_PACED_ON_DATA:   return "on-data";
    case VIEWER_PACED_ON_TIME:   return "on-time";
    case VIEWER_PACED_ON_DEMAND: return "on-demand";
    case VIEWER_PACED_UNKNOWN:   break;
    }
    return "unpaced";
}

/*
 * No `default:` and no range fallthrough: a stream this does not name falls
 * off the end and gets `VIEWER_PACED_UNKNOWN`, which the check refuses.
 */
static inline enum viewer_stream_pacing
viewer_stream_pacing(enum viewer_stream stream) {
    switch (stream) {
    case VIEWER_STREAM_RECEIVER_STATE:
    case VIEWER_STREAM_LINK_HEALTH:
    /*
     * The Settings panel is **on time**, not on data: it changes when
     * somebody types into a field or presses Apply, and a block arriving
     * says nothing about it. Pacing it on data would leave a staged value
     * unreported on a source that had stopped delivering -- which is
     * exactly when a reader is most likely to be changing settings.
     */
    case VIEWER_STREAM_SETTINGS_STATE:
        return VIEWER_PACED_ON_TIME;
    case VIEWER_STREAM_COMMAND_RESULT:
        return VIEWER_PACED_ON_DEMAND;
    case VIEWER_STREAM_SPECTRUM:
    case VIEWER_STREAM_WATERFALL:
    case VIEWER_STREAM_SURVEY_SPECTRUM:
    case VIEWER_STREAM_SURVEY_STATE:
    case VIEWER_STREAM_FM_SPECTRUM:
    case VIEWER_STREAM_FM_STATE:
    case VIEWER_STREAM_GSM_STATE:
    case VIEWER_STREAM_ADSB_STATE:
    case VIEWER_STREAM_TETRA_STATE:
    case VIEWER_STREAM_SRD_STATE:
    case VIEWER_STREAM_LTE_STATE:
        return VIEWER_PACED_ON_DATA;
    case VIEWER_STREAM_COUNT:
        break;
    }
    return VIEWER_PACED_UNKNOWN;
}

/*
 * Whether the serve loop may publish this stream on this pass. One decision,
 * two reasons, both named -- a single "is this due?" that ignored the
 * difference would either spin the metadata streams or stall the data ones.
 *
 * `block_arrived` is the caller's `spectrum_updated`. The other three
 * arguments are `viewer_update_due()`'s and are read only by the on-time
 * streams; a data stream is deliberately **not** also given an interval,
 * because a timer would republish numbers no new block produced, which is
 * the spin this exists to stop.
 */
static inline int viewer_publish_due(enum viewer_stream stream,
                                     int block_arrived, double now,
                                     double published_at,
                                     double interval_seconds, int changed) {
    switch (viewer_stream_pacing(stream)) {
    case VIEWER_PACED_ON_DATA:
        return block_arrived != 0;
    case VIEWER_PACED_ON_TIME:
        return viewer_update_due(now, published_at, interval_seconds,
                                 changed);
    case VIEWER_PACED_ON_DEMAND:
    case VIEWER_PACED_UNKNOWN:
        break;
    }
    /* A reply is sent by whoever answers the command, and a stream nobody
       has paced is not published at all rather than published wrongly. */
    return 0;
}

/*
 * Whether a `--duration` budget has run out. `duration_seconds <= 0` means
 * "no budget, run until Ctrl-C" -- options.h's own convention for the
 * field -- so this reads 0 (never elapsed) in that case rather than every
 * caller needing its own guard first.
 *
 * Out here for the same reason `viewer_update_due()` is: it is a decision
 * (ADR-0012), not a loop, and the loop it belongs to takes `struct app`.
 * The one caller, `viewer_session_run()`, had this inline as a bare
 * comparison until a live test for a separate ticket sat past its own
 * `--duration` and kept running -- `sdrprobe.c`'s *other* headless loop
 * (decode/playback) has always checked its duration; this one, added
 * afterwards, checked only `stop_requested()` (SIGINT/SIGTERM) and never
 * this. Pulled out and pinned here so it cannot regress unnoticed a
 * second time.
 */
static inline int viewer_duration_elapsed(double now, double duration_seconds) {
    return duration_seconds > 0.0 && now >= duration_seconds;
}

#endif
