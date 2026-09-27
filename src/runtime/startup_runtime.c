/*
 * The startup form's machine, one frame of it, out of the overlay that
 * draws the form.
 *
 * `startup_session.{c,h}` is the machine itself and already knows nothing
 * about a screen; this is the adapter around it that `frame_advance()` calls
 * -- taking and giving back the receiver, obeying a retune it asked for.
 * ADR-0024's form is opt-in and a scripted run never sees it, but
 * `--calibrate auto` drives the same machine with no window at all.
 *
 * Out of `overlay_startup.c` by `.scratch/layer-boundaries/issues/02-*`.
 */

#include <stdio.h>
#include <string.h>

#include "runtime/app.h"
#include "runtime/debug_log.h"
#include "runtime/runtime.h"

/* Give the receiver back, whatever happened to the measurement. */
void startup_release(struct app *app) {
    struct startup_view *s = &app->startup;

    if (receiver_lease_token_active(&s->lease_token))
        receiver_return(app, &s->lease_token);
    s->retune_pending = 0;
}

/*
 * One frame of the machine, and the adapter work around it.
 *
 * Called every frame with `have_block` as a **parameter rather than a guard**:
 * a scan step is over on its own clock, so skipping the tick on a frame with
 * no block costs a block a step -- which is the fault a 13-step sweep that
 * folded 39 blocks instead of 26 was made of.
 */
void update_startup(struct app *app, int have_block) {
    struct startup_view *s = &app->startup;
    struct startup_block block;
    struct startup_session_event ev;
    double now = monotonic_seconds();

    if (!s->open)
        return;

    /*
     * The tuning the machine asked for, obeyed here -- it never touches the
     * receiver itself. The settle starts when the tuner has actually moved,
     * which is what `startup_session_retuned()` is told and why a retune that
     * takes a tenth of a second does not silently cost a step its settle.
     */
    if (s->retune_pending) {
        int ok;

        s->retune_pending = 0;
        if (s->pending_rate_hz)
            ok = retune_receiver_at_rate(app, s->pending_hz,
                                         s->pending_rate_hz,
                                         app->applied.ppm) == 0;
        else
            ok = retune_receiver(app, s->pending_hz, app->applied.ppm) == 0;
        if (ok)
            startup_session_retuned(&s->session, monotonic_seconds());
        else
            startup_session_retune_failed(&s->session, s->pending_hz,
                                          app->receiver_error, &ev);
    }

    memset(&block, 0, sizeof(block));
    if (have_block) {
        block.i_samples = app->frame.i_samples;
        block.q_samples = app->frame.q_samples;
        block.pair_count = app->frame.pair_count;
        block.spectrum = app->frame.spectrum_average;
        block.centre_hz = (double)app->applied.frequency_hz;
        block.sample_rate = (double)app->applied.sample_rate_hz;
    }
    startup_session_tick(&s->session, &block,
                         have_block && app->frame.spectrum_ready, now, &ev);

    if (ev.scan_finished)
        debug_log_write("startup", "scan done, %s, arfcn %d, earfcn %u",
                        startup_phase_name(s->session.phase), s->session.arfcn,
                        s->session.earfcn);
    if (ev.measured)
        debug_log_write("startup",
                        "measure %d observed_ppm %.2f centre_ppm %.2f "
                        "sem_ppm %.2f spread_ppm %.2f source %s quality %.2f",
                        s->session.track.measurements,
                        s->session.expected_hz
                            ? s->session.offset_hz /
                                  (double)s->session.expected_hz * 1e6
                            : 0.0,
                        s->session.track.recent_center,
                        s->session.track.recent_sem,
                        s->session.track.recent_spread,
                        startup_session_source(&s->session) ==
                                CALIBRATION_SOURCE_FCCH ? "fcch" : "lte",
                        (double)s->session.quality);
    if (ev.finished)
        debug_log_write("startup",
                        "result %s reason %s measurements %d sem_ppm %.2f "
                        "suggested_ppm %d",
                        startup_phase_name(s->session.phase),
                        startup_reason_name(s->session.reason),
                        s->session.track.measurements,
                        s->session.track.recent_sem,
                        s->session.suggested_ppm);

    if (ev.retune_hz) {
        s->pending_hz = ev.retune_hz;
        s->pending_rate_hz = ev.retune_rate_hz;
        s->retune_pending = 1;
    }
    if (ev.release_receiver)
        startup_release(app);
}
