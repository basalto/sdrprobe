#define _POSIX_C_SOURCE 200809L

#include "survey_session.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The hour of the day, for the clock the history keeps. A wall-clock read and
   nothing else: the diurnal statistics are the only thing that depends on it,
   and no verdict or mark does. */
static int survey_hour_now(void) {
    time_t now = time(NULL);
    struct tm local;

    localtime_r(&now, &local);
    return local.tm_hour;
}

static void survey_session_mark(struct survey_session *s);

static void event_clear(struct survey_session_event *out) {
    if (out)
        memset(out, 0, sizeof(*out));
}

void survey_session_clear(struct survey_session *s) {
    int i;

    if (!s)
        return;
    for (i = 0; i < s->bins && i < SURVEY_BINS; i++)
        s->power[i] = SURVEY_SENTINEL_DBFS;
    s->peak_count = 0;
    s->carrier_count = 0;
    s->missing_count = 0;
    memset(s->carrier_status, 0, sizeof(s->carrier_status));
    s->report_valid = 0;
    s->carrier_valid = 0;
    s->held_valid = 0;
    memset(&s->bursts, 0, sizeof(s->bursts));
    memset(&s->envelope, 0, sizeof(s->envelope));
    survey_measure_reset(&s->measure);
    if (s->state == SURVEY_SESSION_MEASURING)
        s->state = SURVEY_SESSION_IDLE;
}

void survey_session_keep(struct survey_session *s) {
    struct survey_kept *keep;

    if (!s || s->bins <= 0)
        return;
    keep = &s->kept;
    keep->valid = 1;
    keep->plan = s->plan;
    keep->lower_hz = s->lower_hz;
    keep->upper_hz = s->upper_hz;
    keep->bins = s->bins;
    memcpy(keep->power, s->power, (size_t)s->bins * sizeof(*s->power));
    memcpy(keep->peaks, s->peaks,
           (size_t)s->peak_count * sizeof(*s->peaks));
    keep->peak_count = s->peak_count;
}

int survey_session_restore(struct survey_session *s) {
    const struct survey_kept *keep;

    if (!s || !s->kept.valid)
        return -1;
    keep = &s->kept;
    /*
     * The measurement first, and the order is the whole of it: what was being
     * measured belongs to the narrowed sweep this replaces, and forgetting it
     * *after* the copy empties the survey being put back -- which is the one
     * thing this exists to avoid, and which it did once.
     */
    survey_session_forget_measurement(s);
    s->plan = keep->plan;
    s->lower_hz = keep->lower_hz;
    s->upper_hz = keep->upper_hz;
    s->bins = keep->bins;
    memcpy(s->power, keep->power, (size_t)keep->bins * sizeof(*s->power));
    memcpy(s->peaks, keep->peaks,
           (size_t)keep->peak_count * sizeof(*s->peaks));
    s->peak_count = keep->peak_count;
    /*
     * The carriers and the marks are derived, so they are derived again rather
     * than left over: the narrowed sweep's carriers under the restored sweep's
     * peaks would be one sweep's signals drawn at another's frequencies, and
     * the list would look up a bump's width in a carrier that is not there.
     */
    s->carrier_count = survey_carriers_from(
        s->power, s->bins, SURVEY_SENTINEL_DBFS,
        survey_plan_bin_centre(&s->plan, 0),
        s->plan.bin_hz > 0.0 ? s->plan.bin_hz : 1.0, SURVEY_BANDWIDTH_DB,
        s->peaks, s->peak_count, s->carriers, SURVEY_CARRIER_MAX);
    survey_session_mark(s);
    /* And the verdicts, which were about the sweep being undone. */
    s->confirm.count = 0;
    s->confirm.confirmed = 0;
    s->confirm.intermittent = 0;
    s->confirm.refuted = 0;
    s->kept.valid = 0;
    return 0;
}

void survey_session_reset(struct survey_session *s) {
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->state = SURVEY_SESSION_IDLE;
    s->dwell_seconds = SURVEY_DWELL_DEFAULT;
    survey_measure_reset(&s->measure);
}

double survey_session_bin_hz(const struct survey_session *s, int bin) {
    if (!s || s->bins <= 0)
        return s ? s->lower_hz : 0.0;
    return s->lower_hz +
           ((double)bin + 0.5) * (s->upper_hz - s->lower_hz) /
               (double)s->bins;
}

double survey_session_bin_width_hz(const struct survey_session *s) {
    if (!s || s->bins <= 0)
        return 0.0;
    return (s->upper_hz - s->lower_hz) / (double)s->bins;
}

const struct survey_carrier *
survey_session_carrier_at(const struct survey_session *s, double hz) {
    int i;

    if (!s)
        return NULL;
    for (i = 0; i < s->carrier_count; i++)
        if (hz >= s->carriers[i].lower_hz && hz <= s->carriers[i].upper_hz)
            return &s->carriers[i];
    return NULL;
}

unsigned survey_session_confirmed_flags_at(const struct survey_session *s,
                                           double hz) {
    const struct survey_confirm_target *target;
    double tolerance;

    if (!s || s->confirm.count <= 0)
        return 0u;
    tolerance = s->plan.bin_hz > 0.0 ? s->plan.bin_hz : 1e5;
    target = survey_confirm_for(s->confirm.target, s->confirm.count, hz,
                                tolerance);
    return target ? target->suspicion : 0u;
}

int survey_session_change_count(const struct survey_session *s) {
    int i, changes = 0;

    if (!s)
        return 0;
    for (i = 0; i < s->carrier_count; i++)
        if (s->carrier_status[i] == SITE_STATUS_NEW)
            changes++;
    return changes + s->missing_count;
}

/*
 * How this sweep compares with what the site has heard.
 *
 * Recomputed from the history the session already holds -- no file, no
 * transform. This used to read the history off disk on every folded block,
 * because the marks were refreshed inside the peak finder and the peak finder
 * runs during the dwell.
 */
static void survey_session_mark(struct survey_session *s) {
    double hz[SURVEY_CARRIER_MAX];
    double tolerance;
    int i;

    if (!s)
        return;
    s->missing_count = 0;
    memset(s->carrier_status, 0, sizeof(s->carrier_status));
    if (!s->history_loaded)
        return;

    tolerance = s->plan.bin_hz > 0.0 ? s->plan.bin_hz : 1e5;
    for (i = 0; i < s->carrier_count; i++) {
        hz[i] = s->carriers[i].centre_hz;
        s->carrier_status[i] =
            (signed char)site_history_status(&s->history, hz[i], tolerance);
    }
    if (s->carrier_count > 0)
        s->missing_count = site_history_missing(
            &s->history, hz, s->carrier_count, s->plan.lower_hz,
            s->plan.upper_hz, tolerance, s->missing,
            SURVEY_SESSION_MISSING_MAX);
}

void survey_session_set_history(struct survey_session *s,
                                const struct site_history *history,
                                int loaded) {
    if (!s)
        return;
    if (history && loaded) {
        s->history = *history;
        s->history_loaded = 1;
    } else {
        site_history_init(&s->history, "");
        s->history_loaded = 0;
    }
    survey_session_mark(s);
}

/*
 * Fold the usable middle of this block's spectrum into the survey array. The
 * outer fifth of each step is discarded: the tuner's response rolls off at the
 * edges of its span, so a signal there reads low, and the next step covers it
 * properly anyway.
 *
 * The block's own spectrum is peak-held too. A survey that came from one
 * tuning can measure its own candidates out of that without retuning, which is
 * what makes a capture's report complete -- and held rather than latest,
 * because the latest is whatever happened to be transmitting at the end.
 */
static void survey_session_fold(struct survey_session *s,
                                const struct survey_block *block) {
    double rate = block->sample_rate;
    double centre = block->centre_hz;
    double bin_hz = rate / (double)SDR_DSP_FFT_SIZE;
    double lower = centre - rate / 2.0;
    int i;

    for (i = 0; i < SDR_DSP_FFT_SIZE; i++) {
        double hz = lower + ((double)i + 0.5) * bin_hz;
        int bin;

        if (!survey_fold_keeps(hz, centre, rate))
            continue;
        bin = survey_plan_bin_at(&s->plan, hz);
        if (bin < 0)
            continue;
        s->power[bin] = survey_fold_hold(s->power[bin], block->spectrum[i]);
    }
    for (i = 0; i < SDR_DSP_FFT_SIZE; i++)
        s->held_spectrum[i] =
            s->held_valid ? survey_fold_hold(s->held_spectrum[i],
                                             block->spectrum[i])
                          : block->spectrum[i];
    s->held_valid = 1;
    s->step_folded++;
    s->blocks_folded++;
}

const float *survey_session_spectrum(const struct survey_session *s) {
    if (!s || !s->held_valid || s->plan.step_count > 1)
        return NULL;
    return s->held_spectrum;
}

/*
 * The maxima, and the signals they are maxima of.
 *
 * Two bars, and the second is chosen from how deeply this sweep folded into
 * each bin rather than from a constant (ADR-0013). The grouping happens here
 * rather than at the end, because everything above the measurement -- what is
 * new, what is missing, what to ask again about -- works on carriers, and a
 * station with four maxima would otherwise arrive as four things.
 */
static void survey_session_find_peaks(struct survey_session *s,
                                      const struct survey_block *block) {
    struct sdr_peak_gate gate;

    gate.topographic_db = SURVEY_MIN_PROMINENCE_DB;
    gate.floor_db = SURVEY_FLOOR_THRESHOLD_DB;
    gate.bandwidth_db = SURVEY_BANDWIDTH_DB;
    s->peak_count = sdr_dsp_find_peaks(s->power, s->bins,
                                       SURVEY_SENTINEL_DBFS, &gate,
                                       block->scratch, s->peaks,
                                       SURVEY_MAX_PEAKS);
    s->carrier_count = survey_carriers_from(
        s->power, s->bins, SURVEY_SENTINEL_DBFS,
        survey_plan_bin_centre(&s->plan, 0),
        s->plan.bin_hz > 0.0 ? s->plan.bin_hz : 1.0, SURVEY_BANDWIDTH_DB,
        s->peaks, s->peak_count, s->carriers, SURVEY_CARRIER_MAX);
    survey_session_mark(s);
}

/* ------------------------------------------------------------------ *
 * The sweep
 * ------------------------------------------------------------------ */

static enum survey_plan_status survey_session_arm(struct survey_session *s,
                                                  double from_hz, double to_hz,
                                                  double sample_rate,
                                                  double dwell, double now,
                                                  struct survey_session_event *out) {
    enum survey_plan_status result;

    result = survey_plan_make(from_hz, to_hz, sample_rate, SDR_DSP_FFT_SIZE,
                              dwell, &s->plan);
    switch (result) {
    case SURVEY_PLAN_BAD_RANGE:
        snprintf(s->status, sizeof(s->status),
                 "The high edge must be above the low one.");
        return result;
    case SURVEY_PLAN_BAD_DWELL:
        snprintf(s->status, sizeof(s->status),
                 "Dwell must be between %.2f and %.0f seconds.",
                 SURVEY_DWELL_MIN, SURVEY_DWELL_MAX);
        return result;
    case SURVEY_PLAN_BAD_RATE:
        snprintf(s->status, sizeof(s->status),
                 "Sample rate is too low to sweep.");
        return result;
    case SURVEY_PLAN_OK:
        break;
    }

    s->dwell_seconds = dwell;
    s->lower_hz = s->plan.lower_hz;
    s->upper_hz = s->plan.upper_hz;
    s->bins = s->plan.bins;
    survey_session_clear(s);
    s->step_count = s->plan.step_count;
    s->step = 0;
    s->step_folded = 0;
    s->blocks_folded = 0;
    s->blocks_discarded = 0;
    s->source_ended = 0;
    /* A new sweep has not been asked about yet. Leaving the last one's
       verdicts here would attach them to whatever this sweep finds at those
       frequencies, which is a claim nobody made. */
    s->confirm.count = 0;
    s->confirm.confirmed = 0;
    s->confirm.intermittent = 0;
    s->confirm.refuted = 0;
    s->state = SURVEY_SESSION_SWEEPING;
    s->step_started_at = now;
    if (out)
        out->peaks_changed = 1;
    return SURVEY_PLAN_OK;
}

enum survey_plan_status survey_session_sweep(struct survey_session *s,
                                             double from_hz, double to_hz,
                                             double sample_rate, double dwell,
                                             double now,
                                             struct survey_session_event *out) {
    enum survey_plan_status result;

    event_clear(out);
    if (!s)
        return SURVEY_PLAN_BAD_RANGE;
    s->source = SURVEY_SOURCE_SWEEP;
    result = survey_session_arm(s, from_hz, to_hz, sample_rate, dwell, now,
                                out);
    if (result != SURVEY_PLAN_OK) {
        s->state = SURVEY_SESSION_IDLE;
        return result;
    }
    if (out)
        out->retune_hz =
            (uint32_t)llround(survey_plan_step_centre(&s->plan, 0));
    /* What this is going to cost, before it is spent: a long dwell over a wide
       range is minutes, and knowing that up front is the difference between
       patience and pressing Stop. */
    snprintf(s->status, sizeof(s->status),
             "Sweeping %.3f - %.3f MHz in %d steps, %.2f s each: about %s.",
             s->lower_hz / 1e6, s->upper_hz / 1e6, s->step_count,
             s->dwell_seconds,
             s->plan.seconds < 90.0 ? "a minute" : "a few minutes");
    return SURVEY_PLAN_OK;
}

enum survey_plan_status
survey_session_one_tuning(struct survey_session *s, double centre_hz,
                          double sample_rate,
                          struct survey_session_event *out) {
    double usable = sample_rate * SURVEY_USABLE_SPAN;
    enum survey_plan_status result;

    event_clear(out);
    if (!s)
        return SURVEY_PLAN_BAD_RANGE;
    result = survey_session_arm(s, centre_hz - usable / 2.0,
                                centre_hz + usable / 2.0, sample_rate,
                                SURVEY_DWELL_MIN, 0.0, out);
    if (result != SURVEY_PLAN_OK) {
        s->state = SURVEY_SESSION_IDLE;
        return result;
    }
    /* A capture's one step never ends on a clock, so nothing it delivers is
       stale and every block counts. */
    s->source = SURVEY_SOURCE_ONE_TUNING;
    snprintf(s->status, sizeof(s->status),
             "Surveying the %.3f MHz this capture holds.", centre_hz / 1e6);
    return SURVEY_PLAN_OK;
}

/* The sentence a finished sweep leaves behind. */
static void survey_session_swept(struct survey_session *s) {
    snprintf(s->status, sizeof(s->status),
             "Swept %.3f - %.3f MHz in %d steps; %d candidates%s."
             "   Up/Down or click to inspect one.",
             s->lower_hz / 1e6, s->upper_hz / 1e6, s->step_count,
             s->peak_count,
             s->peak_count >= SURVEY_MAX_PEAKS
                 ? " (as many as this view holds)"
                 : " found");
}

/*
 * One sweep of a watch: fold what was found into what the site knows, and
 * count what changed.
 *
 * The folding is the point. A watch that only looked would learn nothing --
 * every sweep would find the same signals "new", because new means "this site
 * has not heard it" and nothing would ever have been written down. It is also
 * the only way an hour accumulates enough sweeps for a daily pattern to be
 * visible at all.
 */
static void survey_session_watch_fold(struct survey_session *s,
                                      struct survey_session_event *out) {
    double hz[SURVEY_CARRIER_MAX];
    float level[SURVEY_CARRIER_MAX], prom[SURVEY_CARRIER_MAX];
    int i;

    if (!s->history_loaded)
        return;
    for (i = 0; i < s->carrier_count; i++) {
        hz[i] = s->carriers[i].centre_hz;
        level[i] = s->carriers[i].peak_dbfs;
        prom[i] = s->carriers[i].prominence_db;
    }
    s->watch_appeared = site_history_merge(&s->history, hz, level, prom,
                                           s->carrier_count, s->plan.bin_hz,
                                           survey_hour_now());
    s->watch_lost = site_history_lost_now(&s->history);
    s->watch_carriers = s->carrier_count;
    s->watch_total_appeared += s->watch_appeared;
    s->watch_total_lost += s->watch_lost;
    s->watch_sweeps++;
    if (out) {
        out->watch_swept = 1;
        out->history_dirty = 1;
    }
}

static void survey_session_sweep_done(struct survey_session *s, double now,
                                      struct survey_session_event *out) {
    s->state = SURVEY_SESSION_IDLE;
    survey_session_swept(s);
    if (out) {
        out->sweep_finished = 1;
        out->peaks_changed = 1;
    }
    /*
     * A watch folds the sweep in, says what changed, and goes round again. It
     * does not park the receiver back where it started -- it is about to move
     * it anyway.
     */
    if (s->watching) {
        survey_session_watch_fold(s, out);
        if (s->watch_limit > 0 && s->watch_sweeps >= s->watch_limit) {
            s->watching = 0;
            if (out)
                out->watch_finished = 1;
        }
        snprintf(s->status, sizeof(s->status),
                 "Watching %s: sweep %d, %d appeared, %d went quiet"
                 "  (%d and %d since the watch began)",
                 s->history.site[0] ? s->history.site : "nowhere",
                 s->watch_sweeps, s->watch_appeared, s->watch_lost,
                 s->watch_total_appeared, s->watch_total_lost);
        if (s->watching && s->source == SURVEY_SOURCE_SWEEP &&
            s->lower_hz < s->upper_hz) {
            /* Round again over the same range. The plan is unchanged, so this
               re-arms rather than re-plans. */
            survey_session_clear(s);
            s->step = 0;
            s->step_folded = 0;
            s->blocks_folded = 0;
            s->blocks_discarded = 0;
            s->state = SURVEY_SESSION_SWEEPING;
            s->step_started_at = now;
            if (out)
                out->retune_hz =
                    (uint32_t)llround(survey_plan_step_centre(&s->plan, 0));
            return;
        }
        s->watching = 0;
    }
    /* Back where the operator was, until they pick a candidate. */
    if (out)
        out->release_receiver = 1;
}

static void survey_session_sweep_tick(struct survey_session *s,
                                      const struct survey_block *block,
                                      int have_block, double now,
                                      struct survey_session_event *out) {
    enum survey_step_phase phase;

    /*
     * A capture holds one tuning and never retunes, so nothing it delivers is
     * stale and every block counts; its step ends when the source does.
     */
    if (s->source == SURVEY_SOURCE_ONE_TUNING) {
        if (have_block) {
            survey_session_fold(s, block);
            survey_session_find_peaks(s, block);
            if (out)
                out->peaks_changed = 1;
        }
        return;
    }

    phase = survey_step_phase_at(now - s->step_started_at, s->dwell_seconds,
                                 s->step, s->step_count);
    if (have_block) {
        if (phase == SURVEY_STEP_SETTLING) {
            /*
             * Discarded rather than folded: it was in the pipeline while the
             * tuner was moving, so it holds the previous step's samples, and
             * folding it writes that step's signal into this step's bins.
             */
            s->blocks_discarded++;
        } else {
            /* Every block that arrives during the dwell is folded in, and the
               fold is a peak hold, so a burst anywhere inside the dwell
               leaves its mark even though the blocks either side of it were
               quiet. */
            survey_session_fold(s, block);
            survey_session_find_peaks(s, block);
            if (out)
                out->peaks_changed = 1;
        }
    }
    /*
     * Asked whether a block arrived or not.
     *
     * A step is over on its own clock once it has heard something, and
     * waiting for one more block to say so costs a block per step -- 39
     * blocks over a 13-step sweep of band II instead of 26, which is the
     * sweep taking half again as long for signal it has already measured.
     * What it may not do is leave a step it has heard nothing on:
     * survey_step_may_advance() carries that reason.
     */
    if (!survey_step_may_advance(phase, s->step_folded))
        return;

    if (phase == SURVEY_STEP_FINISHED) {
        survey_session_sweep_done(s, now, out);
        return;
    }
    s->step++;
    s->step_folded = 0;
    s->step_started_at = now;
    if (out)
        out->retune_hz =
            (uint32_t)llround(survey_plan_step_centre(&s->plan, s->step));
}

void survey_session_stop(struct survey_session *s,
                         struct survey_session_event *out) {
    event_clear(out);
    if (!s)
        return;
    s->watching = 0;
    /* A pass in flight is stopped with everything else. Left running it would
       be a machine nothing ticks and a view nothing reaches, since the input
       handler waits on a pass rather than fighting it for the receiver. */
    s->confirm.running = 0;
    if (s->state != SURVEY_SESSION_SWEEPING) {
        s->state = SURVEY_SESSION_IDLE;
        return;
    }
    s->state = SURVEY_SESSION_IDLE;
    snprintf(s->status, sizeof(s->status),
             "Stopped after %d of %d steps; %d candidates so far.", s->step,
             s->step_count, s->peak_count);
    if (out) {
        out->sweep_stopped = 1;
        out->release_receiver = 1;
    }
}

void survey_session_source_ended(struct survey_session *s, const char *why,
                                 struct survey_session_event *out) {
    event_clear(out);
    if (!s)
        return;
    s->source_ended = 1;
    if (s->state == SURVEY_SESSION_SWEEPING &&
        s->source == SURVEY_SOURCE_ONE_TUNING) {
        /* The capture is the sweep. Running out of it is the ordinary end. */
        s->state = SURVEY_SESSION_IDLE;
        survey_session_swept(s);
        if (out) {
            out->sweep_finished = 1;
            out->peaks_changed = 1;
        }
        return;
    }
    s->watching = 0;
    if (s->state == SURVEY_SESSION_SWEEPING) {
        s->state = SURVEY_SESSION_IDLE;
        snprintf(s->status, sizeof(s->status),
                 "Acquisition ended during step %d of %d: %s", s->step + 1,
                 s->step_count, why && why[0] ? why : "no more blocks");
        if (out)
            out->sweep_stopped = 1;
    } else {
        s->state = SURVEY_SESSION_IDLE;
        snprintf(s->status, sizeof(s->status), "Acquisition ended: %s",
                 why && why[0] ? why : "no more blocks");
    }
    if (out)
        out->release_receiver = 1;
}

void survey_session_retuned(struct survey_session *s, double now) {
    if (!s)
        return;
    switch (s->state) {
    case SURVEY_SESSION_SWEEPING:
        s->step_started_at = now;
        break;
    case SURVEY_SESSION_CONFIRMING:
        s->confirm.started_at = now;
        s->confirm.settled = 0;
        break;
    case SURVEY_SESSION_MEASURING:
        s->measure_started_at = now;
        break;
    case SURVEY_SESSION_IDLE:
        break;
    }
}

void survey_session_retune_failed(struct survey_session *s, double hz,
                                  struct survey_session_event *out) {
    event_clear(out);
    if (!s)
        return;
    s->watching = 0;
    if (s->state == SURVEY_SESSION_CONFIRMING) {
        s->confirm.running = 0;
        s->state = SURVEY_SESSION_IDLE;
        if (out)
            out->confirm_finished = 1;
    } else if (s->state == SURVEY_SESSION_SWEEPING) {
        s->state = SURVEY_SESSION_IDLE;
        if (out)
            out->sweep_stopped = 1;
    } else {
        s->state = SURVEY_SESSION_IDLE;
    }
    snprintf(s->status, sizeof(s->status),
             "The receiver would not tune to %.4f MHz.", hz / 1e6);
    if (out)
        out->release_receiver = 1;
}

/* ------------------------------------------------------------------ *
 * Watching
 * ------------------------------------------------------------------ */

int survey_session_watch(struct survey_session *s, int have_site, int limit,
                         double now, struct survey_session_event *out) {
    event_clear(out);
    if (!s)
        return -1;
    if (!have_site) {
        /* Without a site there is nothing to fold into, so a watch would
           sweep for hours and learn nothing. */
        s->watching = 0;
        snprintf(s->status, sizeof(s->status),
                 "Name the site first -- a watch has nowhere to put what it "
                 "learns.");
        return -1;
    }
    if (s->source != SURVEY_SOURCE_SWEEP || s->lower_hz >= s->upper_hz) {
        s->watching = 0;
        snprintf(s->status, sizeof(s->status),
                 "Watching needs a live receiver and a range swept once.");
        return -1;
    }
    s->watching = 1;
    s->watch_limit = limit > 0 ? limit : 0;
    s->watch_sweeps = 0;
    s->watch_appeared = s->watch_lost = 0;
    s->watch_total_appeared = s->watch_total_lost = 0;
    s->watch_started_at = now;
    if (s->state == SURVEY_SESSION_SWEEPING)
        return 0;
    /* Not sweeping: go round now, over the range the last sweep covered. */
    survey_session_clear(s);
    s->step = 0;
    s->step_folded = 0;
    s->blocks_folded = 0;
    s->blocks_discarded = 0;
    s->state = SURVEY_SESSION_SWEEPING;
    s->step_started_at = now;
    if (out)
        out->retune_hz =
            (uint32_t)llround(survey_plan_step_centre(&s->plan, 0));
    return 0;
}

void survey_session_watch_stop(struct survey_session *s) {
    if (!s)
        return;
    s->watching = 0;
    snprintf(s->status, sizeof(s->status),
             "Watch stopped after %d sweeps: %d appeared, %d went quiet.",
             s->watch_sweeps, s->watch_total_appeared, s->watch_total_lost);
}

/* ------------------------------------------------------------------ *
 * Asking again
 * ------------------------------------------------------------------ */

static void survey_session_confirm_begin_target(struct survey_session *s) {
    if (!s)
        return;
    s->confirm.measured = 0;
    s->confirm.hits = 0;
    s->confirm.looks = 0;
    /* Cleared, not carried over: a target the pass never catches must read as
       never measured rather than inheriting the previous one's answer, which
       is the quietest way to attribute one signal's kind to another. */
    s->confirm.kind_measured = 0;
    memset(&s->confirm.carrier, 0, sizeof(s->confirm.carrier));
    memset(&s->confirm.bursts, 0, sizeof(s->confirm.bursts));
    memset(&s->confirm.envelope, 0, sizeof(s->confirm.envelope));
}

/*
 * What kind of thing this look found, from the raw samples.
 *
 * Only on a look that found the signal at all. Measuring the kind of a block
 * the carrier was absent from measures the noise -- and it would then
 * overwrite a look that had caught it, which for anything bursty is most of
 * them.
 *
 * The search window is centred where the target was *put* rather than where
 * the spectrum says it is: the pass tunes SURVEY_CONFIRM_OFFSET_HZ below it so
 * the signal lands clear of the receiver's own DC spike, and that offset is
 * also the guard signal_find_carrier() needs.
 */
static void survey_session_confirm_kind(struct survey_session *s,
                                        const struct survey_block *block,
                                        const struct survey_confirm_target *target,
                                        const struct sdr_carrier_report *found) {
    double at = SURVEY_CONFIRM_OFFSET_HZ +
                (target->power_centre_hz > 0.0
                     ? target->power_centre_hz - target->hz : 0.0);
    double channel = found->bandwidth_hz;
    double search = survey_carrier_search_hz(found->bandwidth_hz);

    if (channel < SURVEY_CARRIER_MIN_CHANNEL_HZ)
        channel = SURVEY_CARRIER_MIN_CHANNEL_HZ;
    if (!signal_find_carrier(block->i_samples, block->q_samples,
                             block->pair_count, block->sample_rate,
                             at - search, at + search,
                             SURVEY_CONFIRM_OFFSET_HZ / 2.0, channel,
                             &s->confirm.carrier))
        return;
    signal_find_bursts(block->i_samples, block->q_samples, block->pair_count,
                       block->sample_rate, SIGNAL_BURST_GAP_DEFAULT,
                       &s->confirm.bursts);
    signal_envelope_stats(block->i_samples, block->q_samples,
                          block->pair_count, block->sample_rate,
                          s->confirm.carrier.offset_hz, channel,
                          &s->confirm.envelope);
    s->confirm.kind_measured = 1;
}

/*
 * One look: measure this block on its own, and keep it if it is the best yet.
 *
 * Per block, because that is the only way to count *how often* the signal was
 * there, which is the difference between a transmitter and a burst. And the
 * best single look rather than a peak-held spectrum, because holding raises
 * the noise floor along with the signal: a burst present in two blocks of six
 * measured 4.7 dB against the hold, under the 6 dB bar it had cleared twice,
 * so the reported number contradicted the verdict beside it.
 */
static void survey_session_confirm_look(struct survey_session *s,
                                        const struct survey_block *block,
                                        const struct survey_confirm_target *target) {
    struct sdr_carrier_report look;

    if (!s || !block || !target)
        return;
    s->confirm.looks++;
    if (!sdr_dsp_characterise_carrier(block->spectrum, SDR_DSP_FFT_SIZE,
                                      block->centre_hz, block->sample_rate,
                                      target->hz, 200000.0, 20.0f,
                                      block->scratch, &look))
        return;
    if (survey_confirm_present(look.prominence_db))
        s->confirm.hits++;
    /* Kept even when it did not clear the bar: a refuted target saying how
       close it came is worth more than one saying nothing. */
    if (survey_confirm_better(s->confirm.measured,
                              s->confirm.best.prominence_db,
                              look.prominence_db)) {
        s->confirm.best = look;
        s->confirm.measured = 1;
        if (survey_confirm_present(look.prominence_db))
            survey_session_confirm_kind(s, block, target, &look);
    }
}

/*
 * Read the answer out and fill in the verdict. Returns whether the carrier was
 * measurable at all; `report` is left with the best look when it was.
 *
 * The verdict comes from the count of looks and the level from the best of
 * them. A signal up in one look of six is intermittent however loud that look
 * was, and one up in all six is continuous however quiet.
 */
static int survey_session_confirm_decide(struct survey_session *s,
                                         const struct survey_block *block,
                                         struct survey_confirm_target *target,
                                         struct sdr_carrier_report *report) {
    if (!s || !target)
        return 0;
    if (s->confirm.measured && report)
        *report = s->confirm.best;
    target->hits = s->confirm.hits;
    target->looks = s->confirm.looks;
    target->prominence_db =
        s->confirm.measured ? s->confirm.best.prominence_db : 0.0f;
    /*
     * The width and the suspicion at the pass's own resolution, which is three
     * orders finer than the sweep that raised the candidate. This is the only
     * place either can be measured honestly: a 212 kHz bin cannot resolve a
     * 25 kHz carrier, and the comb test refuses to run at all when the bin is
     * that wide.
     */
    target->bandwidth_hz =
        s->confirm.measured ? s->confirm.best.bandwidth_hz : 0.0;
    target->kind_measured = s->confirm.kind_measured;
    target->carrier = s->confirm.carrier;
    target->bursts = s->confirm.bursts;
    target->envelope = s->confirm.envelope;
    /*
     * And a frequency where the closer look found a prominence and nothing
     * else is flagged as empty, whatever the count of looks said. The two live
     * in the same field because they are the same kind of statement -- "do not
     * believe this at face value" -- and different flags because a reader acts
     * differently on each.
     */
    if (survey_confirm_is_empty(s->confirm.kind_measured, &s->confirm.carrier,
                                &s->confirm.envelope))
        target->suspicion |= SURVEY_SUSPECT_NO_CARRIER;
    if (s->confirm.measured && block)
        target->suspicion |= survey_suspect_confirmed(
            block->reference_clock_hz, s->confirm.best.centre_hz,
            s->confirm.best.bandwidth_hz, block->sample_rate,
            SDR_DSP_FFT_SIZE);
    target->verdict = (signed char)survey_confirm_verdict_from(
        target->claim, s->confirm.hits, s->confirm.looks);
    return s->confirm.measured;
}

static int survey_session_confirm_arm(struct survey_session *s, int count,
                                      double now,
                                      struct survey_session_event *out) {
    if (count <= 0)
        return -1;
    s->confirm.count = count;
    s->confirm.index = 0;
    s->confirm.confirmed = 0;
    s->confirm.intermittent = 0;
    s->confirm.refuted = 0;
    s->confirm.running = 1;
    s->confirm.settled = 0;
    s->confirm.looks = 0;
    s->confirm.started_at = now;
    s->state = SURVEY_SESSION_CONFIRMING;
    survey_session_confirm_begin_target(s);
    if (out)
        out->retune_hz = (uint32_t)llround(s->confirm.target[0].hz -
                                           SURVEY_CONFIRM_OFFSET_HZ);
    snprintf(s->status, sizeof(s->status), "Asking again about %d %s, %.0f s.",
             count, count == 1 ? "change" : "changes",
             survey_confirm_seconds(count));
    return count;
}

int survey_session_confirm_changes(struct survey_session *s, double now,
                                   struct survey_session_event *out) {
    int i, count = 0;

    event_clear(out);
    if (!s || s->confirm.running)
        return -1;
    for (i = 0; i < s->carrier_count && count < SURVEY_CONFIRM_MAX; i++) {
        if (s->carrier_status[i] != SITE_STATUS_NEW)
            continue;
        memset(&s->confirm.target[count], 0,
               sizeof(s->confirm.target[count]));
        s->confirm.target[count].hz = s->carriers[i].centre_hz;
        s->confirm.target[count].power_centre_hz =
            s->carriers[i].power_centre_hz;
        s->confirm.target[count].claim = SURVEY_CLAIM_NEW;
        s->confirm.target[count].verdict = SURVEY_VERDICT_PENDING;
        count++;
    }
    for (i = 0; i < s->missing_count && count < SURVEY_CONFIRM_MAX; i++) {
        memset(&s->confirm.target[count], 0,
               sizeof(s->confirm.target[count]));
        s->confirm.target[count].hz = s->missing[i]->hz;
        s->confirm.target[count].claim = SURVEY_CLAIM_MISSING;
        s->confirm.target[count].verdict = SURVEY_VERDICT_PENDING;
        count++;
    }
    return survey_session_confirm_arm(s, count, now, out);
}

int survey_session_confirm_all(struct survey_session *s, double now,
                               struct survey_session_event *out) {
    int i, count;

    event_clear(out);
    if (!s || s->confirm.running)
        return -1;
    count = s->carrier_count < SURVEY_CONFIRM_MAX ? s->carrier_count
                                                  : SURVEY_CONFIRM_MAX;
    for (i = 0; i < count; i++) {
        memset(&s->confirm.target[i], 0, sizeof(s->confirm.target[i]));
        s->confirm.target[i].hz = s->carriers[i].centre_hz;
        s->confirm.target[i].power_centre_hz = s->carriers[i].power_centre_hz;
        s->confirm.target[i].claim = SURVEY_CLAIM_NEW;
        s->confirm.target[i].verdict = SURVEY_VERDICT_PENDING;
    }
    if (count <= 0) {
        /* "asked 0" is a pass that had nothing to ask about, which a reader
           has to be able to tell from a pass that did not run. */
        s->confirm.count = 0;
        return 0;
    }
    return survey_session_confirm_arm(s, count, now, out);
}

static void survey_session_confirm_finish(struct survey_session *s,
                                          struct survey_session_event *out) {
    s->confirm.running = 0;
    s->state = SURVEY_SESSION_IDLE;
    snprintf(s->status, sizeof(s->status),
             "Asked again about %d: %d held up, %d came and went, %d did not."
             "  %s",
             s->confirm.count, s->confirm.confirmed, s->confirm.intermittent,
             s->confirm.refuted,
             s->confirm.intermittent
                 ? "Bursty is a finding, not a mistake."
                 : (s->confirm.refuted
                        ? "A sweep step is a tenth of a second; this was six "
                          "blocks."
                        : "The sweep had them right."));
    if (out) {
        out->confirm_finished = 1;
        out->release_receiver = 1;
    }
}

void survey_session_confirm_abandon(struct survey_session *s,
                                    struct survey_session_event *out) {
    event_clear(out);
    if (!s || !s->confirm.running)
        return;
    s->confirm.running = 0;
    s->state = SURVEY_SESSION_IDLE;
    snprintf(s->status, sizeof(s->status),
             "Stopped asking after %d of %d: %d held up, %d came and went, "
             "%d did not.", s->confirm.index, s->confirm.count,
             s->confirm.confirmed, s->confirm.intermittent,
             s->confirm.refuted);
    if (out) {
        out->confirm_finished = 1;
        out->release_receiver = 1;
        out->marks_stale = 1;
    }
}

static void survey_session_confirm_tick(struct survey_session *s,
                                        const struct survey_block *block,
                                        int have_block, double now,
                                        struct survey_session_event *out) {
    struct survey_confirm_target *target;
    struct sdr_carrier_report report;
    int measured;

    if (!s->confirm.running)
        return;
    target = &s->confirm.target[s->confirm.index];

    if (!s->confirm.settled) {
        if (now - s->confirm.started_at >= SURVEY_CONFIRM_SETTLE_SECONDS) {
            s->confirm.settled = 1;
            s->confirm.started_at = now;
        }
        return;                    /* still the previous target's spectrum */
    }
    if (have_block) {
        survey_session_confirm_look(s, block, target);  /* counts the look */
        if (s->confirm.looks < SURVEY_CONFIRM_LOOKS)
            return;
    } else if (now - s->confirm.started_at < 3.0) {
        return;                    /* still waiting for blocks */
    }

    memset(&report, 0, sizeof(report));
    measured = survey_session_confirm_decide(s, block, target, &report);
    if (target->verdict == SURVEY_VERDICT_CONFIRMED)
        s->confirm.confirmed++;
    else if (target->verdict == SURVEY_VERDICT_INTERMITTENT)
        s->confirm.intermittent++;
    else
        s->confirm.refuted++;
    if (out)
        out->target_finished = s->confirm.index + 1;

    /*
     * Teach the site what the closer look found, not what the sweep guessed. A
     * "new" that held up is worth remembering; one that did not was noise and
     * must not enter the history, or the next sweep will call it missing and
     * the noise becomes a permanent ghost.
     */
    if (s->history_loaded &&
        survey_confirm_should_record(target->claim, target->verdict,
                                     target->suspicion)) {
        /*
         * Recorded at the carrier's measured centre, not at the peak that
         * pointed here. A 200 kHz FM signal has several local maxima and the
         * sweep reports each; remembering each would fill the history with
         * five entries for one station, all of them "new" next time something
         * moved by a bin.
         */
        site_history_record_one(&s->history,
                                measured ? report.centre_hz : target->hz,
                                report.peak_dbfs, report.prominence_db,
                                block ? block->sample_rate /
                                            (double)SDR_DSP_FFT_SIZE
                                      : s->plan.bin_hz,
                                survey_hour_now());
        if (out)
            out->history_dirty = 1;
    }

    s->confirm.index++;
    if (s->confirm.index >= s->confirm.count) {
        survey_session_confirm_finish(s, out);
        return;
    }
    s->confirm.settled = 0;
    s->confirm.looks = 0;
    s->confirm.started_at = now;
    survey_session_confirm_begin_target(s);
    if (out)
        out->retune_hz =
            (uint32_t)llround(s->confirm.target[s->confirm.index].hz -
                              SURVEY_CONFIRM_OFFSET_HZ);
}

/* ------------------------------------------------------------------ *
 * Measuring one candidate
 * ------------------------------------------------------------------ */

/*
 * Is anything riding it? Measured from the raw samples, not the spectrum.
 *
 * The search window is centred on where the candidate was *put* rather than on
 * where the spectrum says it is: the session asks for SURVEY_OFFSET_HZ below
 * the candidate precisely so it lands clear of the receiver's own DC spike, and
 * that offset is also the guard this needs. A guard of zero would find the DC
 * spike every time -- it is the strongest thing in any capture, at an empty
 * frequency as readily as an occupied one -- and then measure its sidebands.
 */
static void survey_session_measure_carrier(struct survey_session *s,
                                           const struct survey_block *block,
                                           const struct sdr_carrier_report *found) {
    double at = SURVEY_OFFSET_HZ;
    double channel = found->bandwidth_hz;

    if (channel < SURVEY_CARRIER_MIN_CHANNEL_HZ)
        channel = SURVEY_CARRIER_MIN_CHANNEL_HZ;
    signal_find_bursts(block->i_samples, block->q_samples, block->pair_count,
                       block->sample_rate, SIGNAL_BURST_GAP_DEFAULT,
                       &s->bursts);
    s->carrier_valid = signal_find_carrier(
        block->i_samples, block->q_samples, block->pair_count,
        block->sample_rate, at - SURVEY_CARRIER_SEARCH_HZ,
        at + SURVEY_CARRIER_SEARCH_HZ, SURVEY_OFFSET_HZ / 2.0, channel,
        &s->carrier);
    /* The envelope's shape, in the channel the carrier search just located
       rather than at the frequency the sweep guessed -- and only when it
       located one, since isolating a channel around nothing measures noise. */
    if (s->carrier_valid)
        signal_envelope_stats(block->i_samples, block->q_samples,
                              block->pair_count, block->sample_rate,
                              s->carrier.offset_hz, channel, &s->envelope);
    else
        memset(&s->envelope, 0, sizeof(s->envelope));
}

void survey_session_forget_measurement(struct survey_session *s) {
    if (!s)
        return;
    s->report_valid = 0;
    s->carrier_valid = 0;
    memset(&s->bursts, 0, sizeof(s->bursts));
    memset(&s->envelope, 0, sizeof(s->envelope));
    survey_measure_reset(&s->measure);
    if (s->state == SURVEY_SESSION_MEASURING)
        s->state = SURVEY_SESSION_IDLE;
}

void survey_session_measure(struct survey_session *s, double hz, double now,
                            struct survey_session_event *out) {
    event_clear(out);
    if (!s)
        return;
    survey_session_forget_measurement(s);
    s->measure_expected_hz = hz;
    s->measure_started_at = now;
    s->state = SURVEY_SESSION_MEASURING;
    if (out)
        out->retune_hz = (uint32_t)llround(hz - SURVEY_OFFSET_HZ);
    snprintf(s->status, sizeof(s->status), "Measuring %.4f MHz", hz / 1e6);
}

static void survey_session_measure_tick(struct survey_session *s,
                                        const struct survey_block *block,
                                        int have_block, double now,
                                        struct survey_session_event *out) {
    /* Not until the tuner has settled: the blocks before that are the previous
       tuning's, and measuring them measures the wrong frequency.
       survey_measure_settled() carries the reason. */
    if (have_block && survey_measure_settled(now - s->measure_started_at)) {
        struct sdr_carrier_report report;
        int found = sdr_dsp_characterise_carrier(
            block->spectrum, SDR_DSP_FFT_SIZE, block->centre_hz,
            block->sample_rate, s->measure_expected_hz, 200000.0,
            SURVEY_BANDWIDTH_DB, block->scratch, &report);

        /* The duty rule -- what counts as the candidate being up in this
           block -- is in survey_sweep.h with the rest of the arithmetic. */
        if (survey_measure_observe(&s->measure, found, report.prominence_db,
                                   report.centre_hz)) {
            s->report = report;
            s->report_valid = 1;
            survey_session_measure_carrier(s, block, &report);
        }
    }
    if (now - s->measure_started_at >= SURVEY_MEASURE_SECONDS) {
        s->state = SURVEY_SESSION_IDLE;
        snprintf(s->status, sizeof(s->status),
                 "Measured %.4f MHz over %d blocks.",
                 s->measure_expected_hz / 1e6, s->measure.blocks);
        if (out)
            out->measure_finished = 1;
    }
}

/* ------------------------------------------------------------------ *
 * The tick
 * ------------------------------------------------------------------ */

void survey_session_tick(struct survey_session *s,
                         const struct survey_block *block, int have_block,
                         double now, struct survey_session_event *out) {
    event_clear(out);
    if (!s)
        return;
    if (have_block && (!block || !block->spectrum))
        have_block = 0;
    switch (s->state) {
    case SURVEY_SESSION_CONFIRMING:
        survey_session_confirm_tick(s, block, have_block, now, out);
        break;
    case SURVEY_SESSION_SWEEPING:
        survey_session_sweep_tick(s, block, have_block, now, out);
        break;
    case SURVEY_SESSION_MEASURING:
        survey_session_measure_tick(s, block, have_block, now, out);
        break;
    case SURVEY_SESSION_IDLE:
        break;
    }
}
