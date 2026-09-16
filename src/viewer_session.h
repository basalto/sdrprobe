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

#endif
