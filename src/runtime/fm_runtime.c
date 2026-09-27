/*
 * FM's runtime: everything the per-block step does for this technology, with
 * nothing that draws and nothing that reads input.
 *
 * `.scratch/layer-boundaries/issues/02-*` is the "why". `frame_advance()` is
 * the one per-block step the window, `headless` and `server` all drive, and
 * sixteen of the eighteen functions it called were defined in files that
 * draw -- so `server` could not be built without every view, every overlay
 * and raylib. This is FM's share of that, moved out of `view_fm.c`: the
 * discriminator and the RDS chain (`update_fm_flush`), the band scan and its
 * steps, tuning, and entering the view.
 *
 * What stayed behind in `view_fm.c` is the drawing, the input handling, and
 * the three functions that work the sound card -- `fm_play()`,
 * `fm_audio_close()` and `update_fm_audio()` -- because an `AudioStream` is
 * a window resource and only the window has one.
 *
 * This file includes no raylib header and must not gain one. `view.h` still
 * does, which is why this includes `app.h` and the FM headers directly
 * rather than reaching for it; ticket 02 splits `view.h` in turn.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "debug_log.h"
#include "fm_scan.h"
#include "runtime.h"
#include "sdr_dsp.h"
void view_fm_defaults(struct app *app) {
    memset(&app->fm, 0, sizeof(app->fm));
    rds_station_init(&app->fm.session.station);
    /* Empty until the scan says what is out there. Nothing here knows the
       band's occupants and guessing one wires this site into the source. */
    app->fm.frequency[0] = '\0';
    app->fm.frequency_length = 0;
}

/*
 * A block of samples into the FM chain: audio, the multiplex, the RDS
 * subcarrier and whatever bits fall out of it.
 *
 * `flush` is for the band scan, which leaves a carrier after less than a
 * second and would otherwise discard every partial chunk unread. See the
 * chunk length below.
 */
void update_fm_flush(struct app *app, double now, int flush) {
    static float multiplex[SAMPLE_BLOCK_PAIRS];
    struct fm_view *fm = &app->fm;
    size_t n;

    if (app->frame.pair_count < 2)
        return;
    /* The multiplex, which the sound, the charts and the decode all read. One
       discriminator pass for all three. */
    n = fm_discriminate_f(app->frame.i_samples, app->frame.q_samples, app->frame.pair_count,
                          multiplex, SAMPLE_BLOCK_PAIRS);
    if (n == 0)
        return;

    /*
     * The multiplex spectrum, a few times a second. It averages thirty-two
     * windows of a 2048-point transform, which is far more work than a frame
     * needs and produces a picture that does not change at frame rate anyway.
     *
     * It used to be gated on `analysis_mode` as well -- the window's "Show
     * charts" toggle -- which made a *computation* answer to a question about
     * what one reader happened to be drawing. That was harmless while the
     * window was the only reader and became a silent hole the moment it was
     * not: `fm_spectrum` (ticket 14's Phase 4) published nothing at all under
     * `server`, where nothing draws and the toggle is therefore always off,
     * so a browser's FM view would have had a permanently empty chart with no
     * error to explain it. Found by running it, not by reading it.
     *
     * The gate is gone rather than widened, and the cost is why: measured at
     * **0.832 ms a call** on this machine, which is 1.27% of the 65.5 ms of
     * signal a block covers and, at the 0.25 s rate limit below, **0.33% of
     * one core**. There is no saving here worth a reader-specific condition.
     */
    if (now - fm->spectrum_at > 0.25) {
        double rate = (double)app->applied.sample_rate_hz;
        size_t want;

        /* Ask for the whole transform, then keep the part of it a multiplex
           actually occupies -- the bin width is only known afterwards. */
        fm->spectrum_bins = fm_multiplex_spectrum(multiplex, n, rate,
                                                  fm->spectrum,
                                                  FM_MPX_SPECTRUM_BINS,
                                                  &fm->spectrum_bin_hz);
        if (fm->spectrum_bin_hz > 0.0) {
            want = (size_t)(FM_MPX_SPECTRUM_TOP_HZ / fm->spectrum_bin_hz);
            if (want > 0 && want < fm->spectrum_bins)
                fm->spectrum_bins = want;
        }
        fm->spectrum_at = now;

        /*
         * And the sound's own spectrum. The multiplex chart shows this band
         * too, at its left edge, but not as it is heard: de-emphasis has not
         * been applied there and the pilot has not been taken out, and both
         * of those are most of the difference between what is transmitted and
         * what comes out of a speaker.
         *
         * This one keeps the `analysis_mode` gate, and the difference is the
         * whole point rather than an oversight: the multiplex now has two
         * readers and so is nobody's to gate, while the audio spectrum still
         * has exactly one -- the chart beside it -- and there is no second
         * reader to leave holding an empty array. It also only ever has
         * anything to measure once something is playing.
         */
        if (fm->analysis_mode && fm->audio_trace_count > 0) {
            fm->audio_spectrum_bins =
                fm_multiplex_spectrum(fm->audio_trace, fm->audio_trace_count,
                                      fm_audio_rate(&fm->audio),
                                      fm->audio_spectrum,
                                      FM_MPX_SPECTRUM_BINS,
                                      &fm->audio_spectrum_bin_hz);
            if (fm->audio_spectrum_bin_hz > 0.0) {
                size_t want = (size_t)(16000.0 / fm->audio_spectrum_bin_hz);
                if (want > 0 && want < fm->audio_spectrum_bins)
                    fm->audio_spectrum_bins = want;
            }
        }
    }

    /*
     * The sound, from the same multiplex the decode reads. One block in is
     * one block of audio out, so the ring neither fills nor empties except
     * when a block is dropped.
     */
    {
        static int16_t pcm[FM_AUDIO_RING];
        static int16_t frames[FM_AUDIO_RING * 2];
        size_t made, k;

        /* The path is built once and kept, so the charts have something to
           draw before anything is played and the level follower is already
           settled when Play is pressed. */
        if (fm->audio.decimate < 1 ||
            fm->audio.sample_rate != (double)app->applied.sample_rate_hz)
            fm_audio_init(&fm->audio, (double)app->applied.sample_rate_hz);

        /* One pass gives both: the sum signal for the charts and the two
           channels for the card. */
        made = fm_audio_decode(&fm->audio, multiplex, n, pcm, frames,
                               FM_AUDIO_RING);

        if (fm->playing) {
            for (k = 0; k < made; k++) {
                size_t next = (fm->audio_tail + 1) & (FM_AUDIO_RING - 1);
                if (next == fm->audio_head)
                    break;  /* the card is not keeping up; drop the newest */
                fm->audio_ring[fm->audio_tail * 2] = frames[k * 2];
                fm->audio_ring[fm->audio_tail * 2 + 1] = frames[k * 2 + 1];
                fm->audio_tail = next;
            }
        }

        /* The tail of it, as floats, for the charts. */
        if (made > 0) {
            size_t keep = made > FM_AUDIO_TRACE ? FM_AUDIO_TRACE : made;
            for (k = 0; k < keep; k++)
                fm->audio_trace[k] = (float)pcm[made - keep + k] / 32768.0f;
            fm->audio_trace_count = keep;
        }
    }

    /*
     * And the decode, which is all `fm_session.h`'s: one chunk at a time, a
     * timing search and an axis over each, its bits appended and the baseband
     * it came from discarded. Nothing is decoded twice and nothing straddles
     * two estimates.
     */
    {
        struct fm_session_event event;

        fm_session_feed(&fm->session, multiplex, n,
                        (double)app->applied.sample_rate_hz, now, flush, &event);
        if (event.groups_advanced) {
            debug_log_write("fm-rds", "pi 0x%04X ps \"%s\" groups %ld",
                            fm->session.station.pi, fm->session.station.ps,
                            fm->session.groups_total);
        }
        if (fm->analysis_mode && fm->session.bb_count > 0)
            fm_rds_timing_scores(fm->session.bb_i, fm->session.bb_q,
                                 fm_rds_chunk_length(fm->session.bb_count, 1),
                                 fm->timing_energy);
    }
}

/*
 * Entering the view puts the receiver in the band.
 *
 * Without this the FM view opens on whatever the last screen was pointed at
 * -- 1090 MHz by default, which is ADS-B -- so the waterfall shows a megahertz
 * of nothing, the panels report no pilot, and the frequency field says 89.6
 * while the receiver is nine hundred megahertz away. A scan then "returns" to
 * 1090 when it finishes, which is correct by the convention every other scan
 * follows and useless here.
 *
 * Only when it is outside band II, so switching away and back does not throw
 * away a station the operator tuned by hand.
 */
void enter_fm(struct app *app, double now) {
    if (!app->receiver_mode)
        return;
    /*
     * Only when there is nothing to show and nowhere to listen.
     *
     * Not the first time only: switching tabs away and back would otherwise
     * throw away both the list and whichever station was being listened to,
     * and start half a minute of tuning nobody asked for. And not when the
     * receiver is already in band II, which is how arriving from the survey's
     * Inspect works -- it has tuned to a station the reader picked, and
     * walking the whole band before playing it would answer a question they
     * did not ask.
     */
    if (app->fm.scan.found_count > 0 || app->fm.scan.running)
        return;
    if ((double)app->applied.frequency_hz >= FM_BAND_LOWER_HZ &&
        (double)app->applied.frequency_hz <= FM_BAND_UPPER_HZ)
        return;
    fm_scan_begin(app, now);
}

int fm_scan_showing(const struct app *app) {
    return app->fm.scan.running || app->fm.scan.found_count > 0;
}

int fm_editing(const struct app *app) {
    return app->fm.typing;
}

/*
 * Retune, and start the decode again from nothing.
 *
 * Everything in the view describes one carrier: the pilot the loop is tracking,
 * the bits in the window, the station assembled from them. Carrying any of it
 * across a retune would put the last station's name over the new one's signal
 * for as long as the window took to empty.
 */
void fm_tune(struct app *app, double hz) {
    if (retune_receiver(app, (uint32_t)llround(hz), app->applied.ppm) < 0)
        return;
    app->fm.session.soft_count = 0;
    app->fm.session.bit_count = 0;
    app->fm.session.bb_consumed = 0;
    app->fm.session.bb_count = 0;
    app->fm.session.front_rate = 0;
    app->fm.session.blocks_seen = 0;
    app->fm.session.groups_total = 0;
    rds_station_init(&app->fm.session.station);
    /*
     * The audio path too, and its pilot with it.
     *
     * It was left running across a retune because its rate had not changed,
     * so the loop arrived at each new station already locked to the last
     * one's pilot -- which during a band scan means every carrier inherits
     * its predecessor's verdict, and the first stereo station makes the rest
     * of the band look stereo.
     */
    fm_audio_init(&app->fm.audio, (double)app->applied.sample_rate_hz);
}

/*
 * Walking band II.
 *
 * Two passes, and fm_scan.h has the arithmetic and the argument. The first
 * sweeps the band as a spectrum -- thirteen tunings, under three seconds --
 * and marks every channel standing over the local floor. The second visits
 * only those, for the three quarters of a second a pilot needs plus enough
 * for an identification.
 *
 * It does not try to read a *name* while scanning. Four segments arriving and
 * repeating is a second or two of groups on a good signal and never on a
 * marginal one, so a scan that waited for names would spend most of its time
 * on the stations it was never going to read. The scan says where to stop;
 * stopping is what reads the name.
 */
void fm_scan_begin(struct app *app, double now) {
    struct fm_scan *scan = &app->fm.scan;
    int i;

    if (!app->receiver_mode) {
        snprintf(scan->status, sizeof(scan->status),
                 "A band scan needs a live receiver.");
        return;
    }
    if (fm_scan_plan_for((double)app->applied.sample_rate_hz, &scan->plan) !=
        FM_SCAN_OK) {
        snprintf(scan->status, sizeof(scan->status),
                 "The sample rate is too low to sweep the band.");
        return;
    }
    for (i = 0; i < (int)(sizeof(scan->power) / sizeof(scan->power[0])); i++)
        scan->power[i] = FM_SCAN_SENTINEL_DBFS;
    scan->found_count = 0;
    scan->visiting = 0;
    scan->naming = 0;
    scan->naming_pass = 0;
    scan->step = 0;
    scan->sweeping = 1;
    /*
     * Rescanning while the claim is still held keeps it. There is no
     * leave_fm(), so a tab switch during a sweep leaves this view still
     * owning the receiver -- exactly as the return_valid flag it replaced
     * did -- and acquiring again each time would stack claims nobody returns.
     */
    if (receiver_lease_token_active(&scan->lease_token)) {
        if (retune_receiver(app, (uint32_t)llround(scan->plan.first_center_hz),
                            app->applied.ppm) < 0) {
            scan->sweeping = 0;
            return;
        }
    } else if (receiver_borrow_at(app, &scan->lease_token,
                                  (uint32_t)llround(scan->plan.first_center_hz),
                                  0) < 0) {
        scan->sweeping = 0;
        return;
    }
    /*
     * The caller's clock, never `GetTime()`.
     *
     * raylib's clock is exactly 0.0 before `InitWindow()`, and this is
     * reachable with no window: a Viewer's `view fm` runs `set_decode()` ->
     * `enter_fm()` -> here, and with a live receiver outside band II that
     * starts a scan. Stamped from a 0.0 origin while every later tick
     * measures against the serve loop's own `now`, each step reads as long
     * expired and the scan races its whole plan in one pass.
     *
     * Exactly the fault ticket 07 found in the survey, mirrored: there the
     * command handler used a raw `monotonic_seconds()` where the loop used
     * a relative one, and the sweep never advanced. Same cause -- two
     * callers of one function disagreeing about what `now` means.
     */
    scan->step_started_at = now;
    scan->running = 1;
    snprintf(scan->status, sizeof(scan->status),
             "Sweeping %d steps, about %.0f s, then the carriers it finds",
             scan->plan.step_count, fm_scan_sweep_seconds(&scan->plan));
    debug_log_write("fm-scan", "begin, %d steps", scan->plan.step_count);
}

void fm_scan_stop(struct app *app) {
    struct fm_scan *scan = &app->fm.scan;

    if (!scan->running)
        return;
    scan->running = 0;
    scan->sweeping = 0;
    receiver_return(app, &scan->lease_token);
    snprintf(scan->status, sizeof(scan->status),
             "Stopped; %d carrier%s found.", scan->found_count,
             scan->found_count == 1 ? "" : "s");
    debug_log_write("fm-scan", "stopped, %d found", scan->found_count);
}

/* Turn the swept powers into the list of channels worth visiting. */
static void fm_scan_choose(struct app *app) {
    struct fm_scan *scan = &app->fm.scan;
    int count = fm_scan_channel_count();
    double floor_dbfs;
    int i, measured = 0;

    /* The floor is the median of what was measured, so a band with a dozen
       strong stations in it does not raise its own threshold out of reach of
       the weak ones -- which a mean would. */
    {
        static float sorted[206];
        for (i = 0; i < count; i++)
            if (scan->power[i] > FM_SCAN_SENTINEL_DBFS)
                sorted[measured++] = scan->power[i];
        if (measured == 0) {
            snprintf(scan->status, sizeof(scan->status),
                     "The sweep measured nothing.");
            return;
        }
        {
            int a, b;
            for (a = 1; a < measured; a++) {
                float key = sorted[a];
                for (b = a - 1; b >= 0 && sorted[b] > key; b--)
                    sorted[b + 1] = sorted[b];
                sorted[b + 1] = key;
            }
        }
        floor_dbfs = sorted[measured / 2];
    }

    for (i = 0; i < count && scan->found_count < FM_SCAN_MAX_FOUND; i++) {
        struct fm_found_station *f;

        if (!fm_scan_is_carrier(scan->power[i], floor_dbfs))
            continue;
        /*
         * One entry per carrier, not per channel over the threshold. An FM
         * carrier is 200 kHz wide on a 100 kHz raster, so a strong station
         * puts its neighbours over the floor too and a list built channel by
         * channel reports every station two or three times.
         */
        if (scan->found_count > 0) {
            f = &scan->found[scan->found_count - 1];
            if (i - f->channel <= 2) {
                if (scan->power[i] > f->power_dbfs) {
                    f->channel = i;
                    f->frequency_hz = fm_scan_channel_hz(i);
                    f->power_dbfs = scan->power[i];
                }
                continue;
            }
        }
        f = &scan->found[scan->found_count++];
        memset(f, 0, sizeof(*f));
        f->channel = i;
        f->frequency_hz = fm_scan_channel_hz(i);
        f->power_dbfs = scan->power[i];
    }
    snprintf(scan->status, sizeof(scan->status),
             "%d carrier%s over the floor; visiting each for %.1f s",
             scan->found_count, scan->found_count == 1 ? "" : "s",
             FM_SCAN_VISIT_SECONDS);
    debug_log_write("fm-scan", "swept, %d carriers, floor %.1f dBFS",
                    scan->found_count, floor_dbfs);
}

void update_fm(struct app *app, double now) {
    update_fm_flush(app, now, 0);
}

/*
 * The scan is over: count what was found, say so, and stay on the loudest
 * station.
 *
 * Reached from two places -- the end of pass two when nothing carried RDS,
 * and the end of pass three once every station that did has had its chance to
 * say its name.
 */
static void fm_scan_finish(struct app *app, struct fm_scan *scan, double now) {
    int with_rds = 0, with_stereo = 0, named = 0, i, best = 0;

    (void)now;
    for (i = 0; i < scan->found_count; i++) {
        if (scan->found[i].rds)
            with_rds++;
        if (scan->found[i].ps[0])
            named++;
        if (scan->found[i].stereo)
            with_stereo++;
        if (scan->found[i].power_dbfs > scan->found[best].power_dbfs)
            best = i;
    }
    scan->running = 0;
    scan->naming_pass = 0;
    if (scan->found_count == 0) {
        snprintf(scan->status, sizeof(scan->status), "No carriers found.");
        return;
    }
    /*
     * It ends on the loudest station rather than back where it started. Every
     * other scan here restores the tuning it borrowed, because the operator
     * was listening to something and asked a question about the band; this
     * one *is* how a station gets chosen, so returning would put the receiver
     * on whatever the previous screen happened to be pointed at -- 1090 MHz
     * on a fresh start, which is not in the band at all.
     */
    fm_tune(app, scan->found[best].frequency_hz);
    snprintf(app->fm.frequency, sizeof(app->fm.frequency), "%.1f",
             scan->found[best].frequency_hz / 1e6);
    app->fm.frequency_length = (int)strlen(app->fm.frequency);
    snprintf(scan->status, sizeof(scan->status),
             "%d carrier%s, %d in stereo, %d carrying RDS, %d named. "
             "Listening to the strongest.", scan->found_count,
             scan->found_count == 1 ? "" : "s", with_stereo, with_rds, named);
    debug_log_write("fm-scan", "done, %d found, %d with RDS, %d named, "
                    "tuned %.1f MHz", scan->found_count, with_rds, named,
                    scan->found[best].frequency_hz / 1e6);
}

void update_fm_scan(struct app *app, double now, int have_block) {
    struct fm_scan *scan = &app->fm.scan;
    double elapsed;

    if (!scan->running)
        return;
    elapsed = now - scan->step_started_at;

    if (scan->sweeping) {
        if (elapsed < FM_SCAN_STEP_SETTLE_SECONDS || !have_block)
            return;
        if (elapsed < FM_SCAN_STEP_SETTLE_SECONDS +
                      FM_SCAN_STEP_MEASURE_SECONDS) {
            /* Measure into the channel grid while the step is still open, so
               a step contributes every block it saw rather than only its
               last. */
            double centre = (double)app->applied.frequency_hz;
            sdr_dsp_channel_powers(app->frame.spectrum_average, SDR_DSP_FFT_SIZE,
                                   centre - app->applied.sample_rate_hz / 2.0,
                                   centre + app->applied.sample_rate_hz / 2.0,
                                   centre - scan->plan.accept_half_hz,
                                   centre + scan->plan.accept_half_hz,
                                   FM_BAND_LOWER_HZ, FM_CHANNEL_SPACING_HZ,
                                   0, fm_scan_channel_count() - 1,
                                   scan->power);
            return;
        }
        scan->step++;
        if (scan->step >= scan->plan.step_count) {
            scan->sweeping = 0;
            fm_scan_choose(app);
            if (scan->found_count == 0) {
                fm_scan_stop(app);
                return;
            }
            scan->visiting = 0;
            fm_tune(app, scan->found[0].frequency_hz);
            scan->step_started_at = now;
            return;
        }
        retune_receiver(app,
                        (uint32_t)llround(scan->plan.first_center_hz +
                                          (double)scan->step *
                                              scan->plan.step_hz),
                        app->applied.ppm);
        scan->step_started_at = now;
        return;
    }

    /*
     * Pass three: back to the ones that answered, until each says its name.
     *
     * Only the ones that answered, because a name costs seconds where
     * deciding whether there is RDS at all costs under one -- and it stops
     * the moment the name is confirmed, so a station that names itself
     * quickly costs what it takes rather than what it was budgeted.
     */
    if (scan->naming_pass) {
        struct fm_found_station *f;

        while (scan->naming < scan->found_count &&
               (!scan->found[scan->naming].rds ||
                scan->found[scan->naming].ps[0]))
            scan->naming++;
        if (scan->naming >= scan->found_count) {
            fm_scan_finish(app, scan, now);
            return;
        }
        f = &scan->found[scan->naming];
        if (elapsed < FM_SCAN_VISIT_SETTLE_SECONDS)
            return;
        if (have_block)
            update_fm(app, now);
        if (app->fm.session.station.ps_valid) {
            snprintf(f->ps, sizeof(f->ps), "%s", app->fm.session.station.ps);
            debug_log_write("fm-scan", "%.1f MHz named \"%s\" after %.1f s",
                            f->frequency_hz / 1e6, f->ps,
                            elapsed - FM_SCAN_VISIT_SETTLE_SECONDS);
        } else if (elapsed <
                   FM_SCAN_VISIT_SETTLE_SECONDS + FM_SCAN_NAME_SECONDS) {
            return;
        } else {
            update_fm_flush(app, now, 1);
            if (app->fm.session.station.ps_valid)
                snprintf(f->ps, sizeof(f->ps), "%s", app->fm.session.station.ps);
            debug_log_write("fm-scan", "%.1f MHz unnamed after %.1f s",
                            f->frequency_hz / 1e6, FM_SCAN_NAME_SECONDS);
        }
        scan->naming++;
        while (scan->naming < scan->found_count &&
               (!scan->found[scan->naming].rds ||
                scan->found[scan->naming].ps[0]))
            scan->naming++;
        if (scan->naming >= scan->found_count) {
            fm_scan_finish(app, scan, now);
            return;
        }
        fm_tune(app, scan->found[scan->naming].frequency_hz);
        scan->step_started_at = now;
        snprintf(scan->status, sizeof(scan->status),
                 "Reading the name at %.1f MHz",
                 scan->found[scan->naming].frequency_hz / 1e6);
        return;
    }

    /* Pass two: one carrier at a time, reading whatever arrives. */
    if (elapsed < FM_SCAN_VISIT_SETTLE_SECONDS)
        return;
    if (elapsed < FM_SCAN_VISIT_SETTLE_SECONDS + FM_SCAN_VISIT_SECONDS) {
        if (have_block)
            update_fm(app, now);
        return;
    }
    {
        struct fm_found_station *f = &scan->found[scan->visiting];
        const struct rds_station *s = &app->fm.session.station;

        /* The visit is over: decode the baseband it gathered rather than
           moving on and dropping it. Without this the scan reported every
           station as carrying no RDS, because a visit is shorter than a
           chunk. */
        update_fm_flush(app, now, 1);

        f->stereo = fm_audio_is_stereo(&app->fm.audio);
        f->rds = s->funnel.groups > 0;
        f->pi_valid = s->pi_valid;
        f->pi = s->pi;
        if (s->ps_valid)
            snprintf(f->ps, sizeof(f->ps), "%s", s->ps);
        /* The funnel, not just the verdict: which stage stops is the
           diagnosis, exactly as it is for the LTE chain. A visit that reads
           no RDS because the pilot never locked is a different fault from one
           that demodulated symbols and never synchronised. */
        debug_log_write("fm-scan",
                        "%.1f MHz %s bb %zu consumed %zu bits %zu "
                        "soft %ld matched %ld groups %ld pi 0x%04X",
                        f->frequency_hz / 1e6,
                        f->stereo ? "stereo" : "mono",
                        app->fm.session.bb_count, app->fm.session.bb_consumed,
                        app->fm.session.bit_count, s->funnel.bits,
                        s->funnel.blocks_matched, s->funnel.groups, f->pi);

        scan->visiting++;
        if (scan->visiting >= scan->found_count) {
            /*
             * Pass two is done: every carrier has been asked whether it
             * carries RDS. Now go back to the ones that said yes and wait for
             * each to say its name.
             */
            int any = 0, i;

            for (i = 0; i < scan->found_count; i++)
                if (scan->found[i].rds && !scan->found[i].ps[0])
                    any = 1;
            if (!any) {
                fm_scan_finish(app, scan, now);
                return;
            }
            scan->naming_pass = 1;
            scan->naming = 0;
            while (scan->naming < scan->found_count &&
                   (!scan->found[scan->naming].rds ||
                    scan->found[scan->naming].ps[0]))
                scan->naming++;
            fm_tune(app, scan->found[scan->naming].frequency_hz);
            scan->step_started_at = now;
            snprintf(scan->status, sizeof(scan->status),
                     "Reading the name at %.1f MHz",
                     scan->found[scan->naming].frequency_hz / 1e6);
            return;
        }
        fm_tune(app, scan->found[scan->visiting].frequency_hz);
        scan->step_started_at = now;
        snprintf(scan->status, sizeof(scan->status),
                 "Visiting %d of %d: %.1f MHz", scan->visiting + 1,
                 scan->found_count,
                 scan->found[scan->visiting].frequency_hz / 1e6);
    }
}
