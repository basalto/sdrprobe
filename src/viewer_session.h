#ifndef VIEWER_SESSION_H
#define VIEWER_SESSION_H

struct app;

/* Where a Viewer link listens when nothing names a port -- ADR-0027's
   loopback, an arbitrary but memorable high port nothing else on this
   machine is likely to want. */
#define VIEWER_SESSION_DEFAULT_PORT 8765

/*
 * `--headless --serve`: drives ticket 02's advance step with no window,
 * builds ticket 03's view model each block, and publishes it over ticket
 * 04's WebSocket server (joined by src/viewer_link.c) instead of drawing
 * it. Runs until Ctrl-C (`stop_requested()`) or, for file playback, until
 * the capture ends.
 *
 * Does not start or stop acquisition -- like `survey_report_run()`, that is
 * the caller's (`run_headless()`, src/sdrprobe.c) to do around this call,
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

#endif
