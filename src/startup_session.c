#include "startup_session.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sdr_dsp.h"

/*
 * The machine described in startup_session.h. Nothing here touches a
 * receiver, a file or a window; see the header for the two refusals and why
 * they are what keep two copies of this from drifting apart.
 */

const char *startup_reason_name(enum startup_reason reason) {
    switch (reason) {
    case STARTUP_REASON_LOCKED:   return "locked";
    case STARTUP_REASON_NO_CELL:  return "no-cell";
    case STARTUP_REASON_TOO_FEW:  return "too-few-measurements";
    case STARTUP_REASON_SEM_WIDE: return "sem-too-wide";
    case STARTUP_REASON_TIMEOUT:  return "timeout";
    case STARTUP_REASON_NO_TUNE:  return "no-tune";
    case STARTUP_REASON_SKIPPED:  return "skipped";
    case STARTUP_REASON_DISAGREE: return "references-disagree";
    default:                      return "none";
    }
}

const char *startup_phase_name(enum startup_phase phase) {
    switch (phase) {
    case STARTUP_SCAN_GSM:     return "scan-gsm";
    case STARTUP_VERIFY_GSM:   return "verify-gsm";
    case STARTUP_MEASURE_GSM:  return "measure-gsm";
    case STARTUP_SCAN_LTE:     return "scan-lte";
    case STARTUP_MEASURE_LTE:  return "measure-lte";
    case STARTUP_LOCKED:       return "locked";
    case STARTUP_FAILED:       return "failed";
    case STARTUP_SKIPPED:      return "skipped";
    default:                   return "idle";
    }
}

void startup_session_reset(struct startup_session *s) {
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    calibration_tracker_init(&s->track);
}

/*
 * Committable exactly when nothing is running, which includes a session that
 * never began.
 *
 * Written as the three finished phases first, and that was wrong in a way the
 * check caught: every skip condition in ticket 04 -- a capture, a stated
 * --ppm, a scripted run -- leaves the machine IDLE, and a form whose Continue
 * is gated on a calibration that was never started would never open again.
 */
int startup_session_may_commit(const struct startup_session *s) {
    return !startup_session_running(s);
}

int startup_session_running(const struct startup_session *s) {
    if (!s)
        return 0;
    return s->phase == STARTUP_SCAN_GSM || s->phase == STARTUP_VERIFY_GSM ||
           s->phase == STARTUP_MEASURE_GSM ||
           s->phase == STARTUP_SCAN_LTE || s->phase == STARTUP_MEASURE_LTE;
}

int startup_session_source(const struct startup_session *s) {
    if (!s)
        return CALIBRATION_SOURCE_CENTROID;
    return s->track.source;
}

static void event_clear(struct startup_session_event *out) {
    if (out)
        memset(out, 0, sizeof(*out));
}

/* Ask the adapter for a tuning, and stop believing blocks until it reports
   back. Every step in this file goes through here, so there is one place the
   settle rule can be got wrong. */
static void want_tuning(struct startup_session *s, uint32_t hz, uint32_t rate,
                        struct startup_session_event *out) {
    s->want_hz = hz;
    s->want_rate_hz = rate;
    s->tuned = 0;
    if (out) {
        out->retune_hz = hz;
        out->retune_rate_hz = rate;
    }
}

static void finish(struct startup_session *s, enum startup_phase phase,
                   enum startup_reason reason,
                   struct startup_session_event *out) {
    s->phase = phase;
    s->reason = reason;
    if (out) {
        out->finished = 1;
        out->release_receiver = 1;
    }
}

/* -------------------------------------------------------------------------
 * The GSM 900 walk
 * ---------------------------------------------------------------------- */

static void gsm_step_begin(struct startup_session *s,
                           struct startup_session_event *out) {
    double centre = scan_plan_step_centre(&s->plan, s->step);

    want_tuning(s, (uint32_t)centre, 0, out);
    snprintf(s->status, sizeof(s->status),
             "Looking for a GSM 900 reference: step %d of %d",
             s->step + 1, s->plan.step_count);
}

static void gsm_scan_finished(struct startup_session *s,
                              struct startup_session_event *out,
                              double now);

static void lte_scan_begin_at(struct startup_session *s,
                              struct startup_session_event *out, double now);

static void measure_gsm_begin(struct startup_session *s,
                              struct startup_session_event *out, double now) {
    uint32_t channel_hz = 0;

    if (s->arfcn < 1 || s->arfcn > SCAN_ARFCN_LAST ||
        !gsm_downlink_hz((unsigned int)s->arfcn, &channel_hz)) {
        finish(s, STARTUP_FAILED, STARTUP_REASON_NO_CELL, out);
        snprintf(s->status, sizeof(s->status),
                 "ARFCN %d is not a GSM 900 downlink channel", s->arfcn);
        return;
    }
    s->expected_hz = channel_hz;
    s->phase = STARTUP_MEASURE_GSM;
    s->measure_started_at = now;
    calibration_tracker_init(&s->track);
    s->reported = 0;
    /* Tuned 400 kHz below the channel, as every GSM path here does, so the
       carrier sits inside the span rather than on the tuner's DC spike. */
    want_tuning(s, channel_hz - 400000U, 0, out);
    if (out)
        out->measure_began = 1;
    snprintf(s->status, sizeof(s->status),
             "Measuring GSM 900 ARFCN %d at %.3f MHz", s->arfcn,
             channel_hz / 1e6);
}

/*
 * Ask the loudest channel that still carries a tone to prove it is GSM.
 *
 * `scan_select_bcch()` and **not** `scan_choose()`, which is the trap this
 * call is one keystroke away from: `scan_choose()` falls back to the loudest
 * channel when nothing carried a broadcast carrier at all, which is right for
 * the GSM view -- something to look at beats nothing -- and wrong here, where
 * a channel with no tone has nothing to calibrate against.
 *
 * And **a tone is not enough either**, which is what the verification below
 * is for: the detector reports any coherent line within 50 kHz of where an
 * FCCH would be, and a high coherence says the line is steady rather than
 * that it is an FCCH.
 *
 * `scan_select_bcch()` runs over the candidates this pass has not already
 * turned down -- a rejected channel is zeroed in the confidence array, so the next
 * best takes its place and the search continues rather than giving up on the
 * band because its loudest carrier is no longer GSM.
 */
static void verify_gsm_begin(struct startup_session *s,
                             struct startup_session_event *out, double now) {
    uint32_t channel_hz = 0;

    s->arfcn = scan_select_bcch(s->power, s->bcch_conf);
    if (s->arfcn <= 0 ||
        !gsm_downlink_hz((unsigned int)s->arfcn, &channel_hz)) {
        s->arfcn = 0;
        lte_scan_begin_at(s, out, now);
        return;
    }
    s->arfcn_confidence = s->bcch_conf[s->arfcn];
    s->phase = STARTUP_VERIFY_GSM;
    s->verify_blocks = 0;
    s->verified = 0;
    s->bsic = -1;
    s->expected_hz = channel_hz;
    want_tuning(s, channel_hz - 400000U, 0, out);
    snprintf(s->status, sizeof(s->status),
             "Checking ARFCN %d is a GSM broadcast carrier", s->arfcn);
}

/* This candidate did not say anything only a base station could say. Drop it
   and let the next best take its place. */
static void verify_gsm_rejected(struct startup_session *s,
                                struct startup_session_event *out,
                                double now) {
    if (s->arfcn > 0 && s->arfcn <= SCAN_ARFCN_LAST) {
        s->rejected[s->arfcn] = 1;
        s->bcch_conf[s->arfcn] = 0.0f;
    }
    verify_gsm_begin(s, out, now);
}

static void gsm_scan_finished(struct startup_session *s,
                              struct startup_session_event *out, double now) {
    if (out)
        out->scan_finished = 1;
    if (scan_select_bcch(s->power, s->bcch_conf) > 0) {
        verify_gsm_begin(s, out, now);
        return;
    }
    s->arfcn = 0;
    /* No channel anywhere in the band reached SCAN_BCCH_MIN_CONF. That is the
       fall-through condition, and it is "no BCCH" rather than "no power" for
       the reason above. */
    lte_scan_begin_at(s, out, now);
}

static void gsm_scan_tick(struct startup_session *s,
                          const struct startup_block *block, int have_block,
                          double now, struct startup_session_event *out) {
    double elapsed;
    enum scan_step_phase phase;
    int arfcn;

    if (!s->tuned)
        return;
    elapsed = now - s->step_started_at;
    phase = scan_step_phase_at(elapsed, s->step, s->plan.step_count);
    if (phase == SCAN_STEP_SETTLING)
        return;

    /*
     * Measure while probing, and again on the step that ends -- the block
     * that arrives on the last tick is as good as any other. `have_block` is
     * a parameter and not a guard for the reason the header gives: a step is
     * over on its own clock, so returning early here would cost a block a
     * step.
     */
    if (have_block && block && block->spectrum && block->pair_count) {
        double centre = block->centre_hz;
        double lower = centre - block->sample_rate / 2.0;
        double upper = centre + block->sample_rate / 2.0;

        sdr_dsp_channel_powers(block->spectrum, SDR_DSP_FFT_SIZE, lower, upper,
                               centre - s->plan.accept_half_hz,
                               centre + s->plan.accept_half_hz,
                               GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ,
                               SCAN_ARFCN_FIRST, SCAN_ARFCN_LAST, s->power);

        for (arfcn = SCAN_ARFCN_FIRST; arfcn <= SCAN_ARFCN_LAST; arfcn++) {
            double channel = GSM900_BASE_HZ +
                             (double)arfcn * GSM900_ARFCN_SPACING_HZ;
            struct gsm_fcch_result fcch;
            double target;

            if (!scan_plan_covers(&s->plan, centre, channel))
                continue;
            target = channel - centre + GSM_FCCH_TONE_HZ;
            gsm_fcch_detect(block->i_samples, block->q_samples,
                            block->pair_count, block->sample_rate, target,
                            GSM_FCCH_SEARCH_HALF_HZ, &fcch);
            /* Held across the step's blocks, never cleared: FCCH is
               intermittent and a block between bursts reads low. */
            s->bcch_conf[arfcn] = scan_hold_confidence(s->bcch_conf[arfcn],
                                                       fcch.confidence);
        }
    }

    if (phase == SCAN_STEP_PROBING)
        return;
    if (phase == SCAN_STEP_FINISHED) {
        gsm_scan_finished(s, out, now);
        return;
    }
    s->step++;
    gsm_step_begin(s, out);
}

/*
 * One block of the verification pass.
 *
 * `gsm_sch_decode()` returns 1 only on a parity-valid decode, which is the
 * whole point: a coherent line that is not an FCCH cannot go on to produce a
 * Base Station Identity Code that checks out.
 */
static void verify_gsm_tick(struct startup_session *s,
                            const struct startup_block *block, int have_block,
                            double now, struct startup_session_event *out) {
    struct gsm_sch_result sch;

    if (!s->tuned)
        return;
    if (!have_block || !block || !block->pair_count)
        return;
    s->verify_blocks++;
    memset(&sch, 0, sizeof(sch));
    if (gsm_sch_decode(block->i_samples, block->q_samples, block->pair_count,
                       block->sample_rate,
                       (double)s->expected_hz - block->centre_hz, 0, &sch,
                       NULL) == 1 && sch.decoded) {
        s->verified = 1;
        s->bsic = sch.bsic;
        snprintf(s->status, sizeof(s->status),
                 "ARFCN %d is GSM: BSIC %d (NCC %d, BCC %d)", s->arfcn,
                 sch.bsic, sch.ncc, sch.bcc);
        measure_gsm_begin(s, out, now);
        return;
    }
    if (s->verify_blocks >= STARTUP_VERIFY_BLOCKS) {
        snprintf(s->status, sizeof(s->status),
                 "ARFCN %d carries a tone but no GSM synchronisation burst",
                 s->arfcn);
        verify_gsm_rejected(s, out, now);
    }
}

/* -------------------------------------------------------------------------
 * The LTE walk, when GSM had nothing to offer
 * ---------------------------------------------------------------------- */

static void measure_lte_begin(struct startup_session *s,
                              struct startup_session_event *out, double now) {
    uint32_t carrier = 0;

    if (!lte_earfcn_downlink_hz(s->earfcn, &carrier)) {
        finish(s, STARTUP_FAILED, STARTUP_REASON_NO_CELL, out);
        snprintf(s->status, sizeof(s->status),
                 "EARFCN %u is not a downlink channel this band table knows",
                 s->earfcn);
        return;
    }
    s->expected_hz = carrier;
    s->phase = STARTUP_MEASURE_LTE;
    s->measure_started_at = now;
    calibration_tracker_init(&s->track);
    calibration_tracker_use(&s->track, CALIBRATION_SOURCE_LTE);
    s->reported = 0;
    /* Tuned to the carrier's centre, not beside it: LTE never transmits on
       the middle subcarrier, so the DC spike lands in a hole the standard
       already leaves. And at LTE's own rate (ADR-0014). */
    want_tuning(s, carrier, LTE_SAMPLE_RATE_HZ, out);
    if (out)
        out->measure_began = 1;
    snprintf(s->status, sizeof(s->status),
             "Measuring LTE cell %d on EARFCN %u at %.3f MHz", s->pci,
             s->earfcn, carrier / 1e6);
}

static void lte_step_begin(struct startup_session *s,
                           struct startup_session_event *out) {
    unsigned int earfcn = lte_scan_candidate(s->band, s->lte_index);
    uint32_t carrier = 0;

    s->lte_looks = 0;
    s->lte_agreements = 0;
    s->lte_pci = -1;
    if (!earfcn || !lte_earfcn_downlink_hz(earfcn, &carrier)) {
        s->earfcn = 0;
        return;                 /* the caller reads earfcn 0 as "band walked" */
    }
    s->earfcn = earfcn;
    want_tuning(s, carrier, LTE_SAMPLE_RATE_HZ, out);
    snprintf(s->status, sizeof(s->status),
             "Looking for a 4G reference in band %d: channel %d of %d",
             s->band ? s->band->band : 0, s->lte_index + 1,
             lte_scan_count(s->band));
}

static void lte_scan_begin_at(struct startup_session *s,
                              struct startup_session_event *out, double now) {
    if (!s->band) {
        finish(s, STARTUP_FAILED, STARTUP_REASON_NO_CELL, out);
        snprintf(s->status, sizeof(s->status),
                 "No GSM 900 broadcast carrier, and no LTE band this "
                 "receiver can reach");
        return;
    }
    s->phase = STARTUP_SCAN_LTE;
    s->lte_index = 0;
    s->step_started_at = now;
    lte_step_begin(s, out);
    if (!s->earfcn)
        finish(s, STARTUP_FAILED, STARTUP_REASON_NO_CELL, out);
}

static void lte_scan_tick(struct startup_session *s,
                          const struct startup_block *block, int have_block,
                          double now, struct startup_session_event *out) {
    double elapsed;

    if (!s->tuned)
        return;
    elapsed = now - s->step_started_at;
    if (elapsed < LTE_SCAN_SETTLE_SECONDS)
        return;

    if (have_block && block && block->pair_count >=
            (size_t)(LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE)) {
        struct lte_cell cell;

        s->lte_looks++;
        if (lte_cell_search(block->i_samples, block->q_samples,
                            block->pair_count, block->sample_rate, &cell,
                            NULL) == 1) {
            /* An identity that repeats is a cell; one that does not is the
               noise the sweep's loose gate lets through. `lte_scan.h` owns
               that rule and this is the same one, not a second. */
            if (s->lte_pci == cell.pci) {
                s->lte_agreements++;
            } else {
                s->lte_pci = cell.pci;
                s->lte_agreements = 1;
            }
            s->pci = cell.pci;
            s->pss = cell.pss_correlation;
            if (s->lte_agreements >= STARTUP_LTE_AGREE) {
                if (out)
                    out->scan_finished = 1;
                measure_lte_begin(s, out, now);
                return;
            }
        }
    }

    if (elapsed < LTE_SCAN_SETTLE_SECONDS +
                      LTE_SCAN_PROBE_SECONDS * (double)STARTUP_LTE_LOOKS)
        return;

    s->lte_index++;
    s->step_started_at = now;
    lte_step_begin(s, out);
    if (!s->earfcn) {
        finish(s, STARTUP_FAILED, STARTUP_REASON_NO_CELL, out);
        snprintf(s->status, sizeof(s->status),
                 "No 4G cell found in band %d, and no GSM 900 broadcast "
                 "carrier either", s->band ? s->band->band : 0);
    }
}

/* -------------------------------------------------------------------------
 * The measurement, and the gate over it
 * ---------------------------------------------------------------------- */

/* Which clause of the gate is still unsatisfied, so a calibration that will
   not lock is a diagnosis rather than a shrug. The same three words the
   headless report prints. */
static enum startup_reason unmet_clause(const struct startup_session *s) {
    if (s->track.measurements < CALIBRATION_MIN_MEASUREMENTS)
        return STARTUP_REASON_TOO_FEW;
    if (s->track.recent_sem > CALIBRATION_MAX_SEM_PPM)
        return STARTUP_REASON_SEM_WIDE;
    return STARTUP_REASON_TIMEOUT;
}

static void observe(struct startup_session *s, double measured_hz,
                    float quality, double now,
                    struct startup_session_event *out) {
    double observed_ppm;

    s->measured_hz = measured_hz;
    s->offset_hz = measured_hz - (double)s->expected_hz;
    s->quality = quality;
    observed_ppm = s->offset_hz / (double)s->expected_hz * 1000000.0;
    calibration_tracker_observe(&s->track, observed_ppm);

    s->suggested_ppm = sdr_dsp_corrected_ppm(
        s->applied_ppm,
        (double)s->expected_hz *
            (1.0 + s->track.recent_center / 1000000.0),
        (double)s->expected_hz);
    if (s->suggested_ppm < -1000)
        s->suggested_ppm = -1000;
    if (s->suggested_ppm > 1000)
        s->suggested_ppm = 1000;

    s->track.stable = calibration_is_stable(
        now - s->measure_started_at, s->track.measurements,
        s->track.recent_count, s->track.recent_sem, s->track.source, quality);
    if (out)
        out->measured = 1;
}

static void measure_status(struct startup_session *s) {
    snprintf(s->status, sizeof(s->status),
             "%s (%s): %d measurements, +/- %.2f ppm, suggested %+d ppm",
             s->track.stable ? "Stable lock" : "Acquiring",
             s->track.source == CALIBRATION_SOURCE_FCCH ? "FCCH tone"
                                                        : "LTE cell",
             s->track.measurements, s->track.recent_sem, s->suggested_ppm);
}

static void measure_gsm_tick(struct startup_session *s,
                             const struct startup_block *block, int have_block,
                             double now, struct startup_session_event *out) {
    struct gsm_fcch_result fcch;
    struct sdr_channel_estimate estimate;
    double target;
    int have_fcch;
    int have_centroid = 0;

    if (!s->tuned || !have_block || !block || !block->pair_count)
        return;
    target = (double)s->expected_hz - block->centre_hz + GSM_FCCH_TONE_HZ;
    /* The detector returns 1 when it found the tone, which is the same test
       the calibration overlay makes -- one convention, not two. */
    have_fcch = gsm_fcch_detect(block->i_samples, block->q_samples,
                                block->pair_count, block->sample_rate, target,
                                GSM_FCCH_SEARCH_HALF_HZ, &fcch);

    /*
     * The centroid, and only when the caller allows it.
     *
     * Independent of the tone rather than an else-branch, exactly as the
     * calibration overlay has it: a dip in centroid prominence must not wipe
     * an FCCH accumulation, and the peak/floor/prominence are worth reporting
     * whichever source the residual comes from.
     *
     * The startup form says no. It files a correction unattended, and a
     * centroid residual is a different measurement of a different thing --
     * ADR-0004's whole subject is a buffer that held two. `--calibrate` says
     * yes, because an operator is reading every residual as it arrives and a
     * reading that keeps moving between bursts is worth having.
     */
    if (s->allow_centroid && block->spectrum && block->scratch) {
        double lower = block->centre_hz - block->sample_rate / 2.0;
        double upper = block->centre_hz + block->sample_rate / 2.0;

        have_centroid = sdr_dsp_estimate_channel_center(
            block->spectrum, SDR_DSP_FFT_SIZE, lower, upper,
            (double)s->expected_hz, 100000.0, 50000.0, block->scratch,
            &estimate);
        if (have_centroid) {
            s->peak_dbfs = estimate.peak_dbfs;
            s->floor_dbfs = estimate.floor_dbfs;
            s->prominence_db = estimate.prominence_db;
        }
    }

    switch (calibration_track(&s->track, have_fcch, have_centroid)) {
    case CALIBRATION_USE_FCCH:
        observe(s, block->centre_hz + fcch.tone_frequency_hz - GSM_FCCH_TONE_HZ,
                fcch.confidence, now, out);
        break;
    case CALIBRATION_USE_CENTROID:
        /*
         * The quality the gate reads is the **prominence** here, not the
         * tone's coherence: `calibration_is_stable()` asks a different
         * question of each source, and handing it a stale FCCH confidence
         * would gate a centroid residual on how good a tone was some blocks
         * ago.
         */
        observe(s, estimate.measured_frequency_hz, s->prominence_db, now, out);
        break;
    case CALIBRATION_HOLD_TONE:
    default:
        break;
    }
    measure_status(s);
}

static void measure_lte_tick(struct startup_session *s,
                             const struct startup_block *block, int have_block,
                             double now, struct startup_session_event *out) {
    struct lte_cell cell;

    if (!s->tuned || !have_block || !block)
        return;
    if (block->pair_count < (size_t)(LTE_HALF_FRAME_SAMPLES + LTE_FFT_SIZE))
        return;
    if (lte_cell_search(block->i_samples, block->q_samples, block->pair_count,
                        block->sample_rate, &cell, NULL) != 1) {
        snprintf(s->status, sizeof(s->status),
                 "No 4G cell in this block on EARFCN %u", s->earfcn);
        return;
    }
    calibration_tracker_use(&s->track, CALIBRATION_SOURCE_LTE);
    s->pci = cell.pci;
    observe(s, (double)s->expected_hz + cell.frequency_offset_hz,
            cell.pss_correlation, now, out);
    measure_status(s);
}

/* -------------------------------------------------------------------------
 * The public surface
 * ---------------------------------------------------------------------- */

int startup_session_begin(struct startup_session *s, double sample_rate,
                          int applied_ppm, int gsm_reachable,
                          const struct lte_band *band, double now,
                          struct startup_session_event *out) {
    enum scan_plan_status status;

    event_clear(out);
    if (!s)
        return -1;
    startup_session_reset(s);
    s->applied_ppm = applied_ppm;
    s->suggested_ppm = applied_ppm;
    s->band = band;
    s->measure_budget = STARTUP_MEASURE_SECONDS;
    s->step_started_at = now;
    /*
     * A search asks the band what to calibrate against, so it owes the
     * operator a second opinion (issue 08). A caller that names the channel
     * does not: that is an instruction to measure *that* one.
     */
    s->cross_check = 1;

    status = scan_plan_make(sample_rate, &s->plan);
    if (!gsm_reachable || status != SCAN_PLAN_OK) {
        /*
         * No GSM to look at: either the tuner cannot reach the band, or the
         * rate is too narrow for a step to measure a whole channel. Straight
         * to the fall-back rather than a scan that would tune where it cannot
         * hear and report an absence it never tested.
         */
        lte_scan_begin_at(s, out, now);
        return s->phase == STARTUP_FAILED ? -1 : 0;
    }
    s->phase = STARTUP_SCAN_GSM;
    s->step = 0;
    gsm_step_begin(s, out);
    return 0;
}


/*
 * One reference has settled. Take a second opinion, or deliver the verdict.
 *
 * The whole of issue 08 is here: two parity-verified GSM cells at one site
 * measured this crystal 15 ppm apart, and nothing about either measurement on
 * its own said which to believe. So the search does not lock on one.
 */
static void reference_settled(struct startup_session *s, double now,
                              struct startup_session_event *out) {
    const char *what = s->track.source == CALIBRATION_SOURCE_FCCH
                           ? "a GSM broadcast carrier" : "a 4G cell";

    if (!s->cross_check || s->track.source != CALIBRATION_SOURCE_FCCH) {
        /*
         * A named channel is an instruction, and the LTE path has only one
         * reference to offer -- a band scan that stopped at the first cell
         * whose identity repeated has no second one in hand, and finding one
         * costs another walk of the raster.
         */
        finish(s, STARTUP_LOCKED, STARTUP_REASON_LOCKED, out);
        snprintf(s->status, sizeof(s->status),
                 "Calibrated %+d ppm from %s, +/- %.2f ppm over %d "
                 "measurements", s->suggested_ppm, what, s->track.recent_sem,
                 s->track.measurements);
        return;
    }

    if (s->references == 0) {
        s->references = 1;
        s->first_ppm = s->suggested_ppm;
        s->first_arfcn = s->arfcn;
        s->first_bsic = s->bsic;
        /* Take this channel out of the running and ask the next best. */
        if (s->arfcn > 0 && s->arfcn <= SCAN_ARFCN_LAST)
            s->bcch_conf[s->arfcn] = 0.0f;
        if (scan_select_bcch(s->power, s->bcch_conf) > 0) {
            verify_gsm_begin(s, out, now);
            snprintf(s->status, sizeof(s->status),
                     "ARFCN %d gave %+d ppm; checking it against another "
                     "carrier", s->first_arfcn, s->first_ppm);
            return;
        }
        /*
         * Only one broadcast carrier within reach. The correction stands --
         * it is the best this site can offer -- and the status says it was
         * not cross-checked, because an operator deciding whether to keep it
         * should know which of the two cases they are in.
         */
        finish(s, STARTUP_LOCKED, STARTUP_REASON_LOCKED, out);
        snprintf(s->status, sizeof(s->status),
                 "Calibrated %+d ppm from ARFCN %d, +/- %.2f ppm. Only one "
                 "broadcast carrier here, so nothing checked it",
                 s->suggested_ppm, s->first_arfcn, s->track.recent_sem);
        return;
    }

    s->references = 2;
    s->second_ppm = s->suggested_ppm;
    s->second_arfcn = s->arfcn;
    if (fabs((double)s->second_ppm - (double)s->first_ppm) <=
        STARTUP_AGREE_PPM) {
        /* The first is kept rather than the mean or the second: averaging two
           numbers that agree buys nothing measurable and would make the
           correction depend on the order the band happened to be scanned. */
        s->suggested_ppm = s->first_ppm;
        finish(s, STARTUP_LOCKED, STARTUP_REASON_LOCKED, out);
        snprintf(s->status, sizeof(s->status),
                 "Calibrated %+d ppm. ARFCN %d and ARFCN %d agree (%+d and "
                 "%+d ppm)", s->suggested_ppm, s->first_arfcn,
                 s->second_arfcn, s->first_ppm, s->second_ppm);
        return;
    }
    finish(s, STARTUP_FAILED, STARTUP_REASON_DISAGREE, out);
    snprintf(s->status, sizeof(s->status),
             "ARFCN %d says %+d ppm and ARFCN %d says %+d. One of them is not "
             "on frequency; nothing applied",
             s->first_arfcn, s->first_ppm, s->second_arfcn, s->second_ppm);
}

void startup_session_tick(struct startup_session *s,
                          const struct startup_block *block, int have_block,
                          double now, struct startup_session_event *out) {
    event_clear(out);
    if (!s || !startup_session_running(s))
        return;

    switch (s->phase) {
    case STARTUP_SCAN_GSM:
        gsm_scan_tick(s, block, have_block, now, out);
        break;
    case STARTUP_VERIFY_GSM:
        verify_gsm_tick(s, block, have_block, now, out);
        break;
    case STARTUP_SCAN_LTE:
        lte_scan_tick(s, block, have_block, now, out);
        break;
    case STARTUP_MEASURE_GSM:
        measure_gsm_tick(s, block, have_block, now, out);
        break;
    case STARTUP_MEASURE_LTE:
        measure_lte_tick(s, block, have_block, now, out);
        break;
    default:
        break;
    }

    if (s->phase != STARTUP_MEASURE_GSM && s->phase != STARTUP_MEASURE_LTE)
        return;
    if (s->track.stable) {
        reference_settled(s, now, out);
        return;
    }
    if (now - s->measure_started_at > s->measure_budget) {
        enum startup_reason why = unmet_clause(s);
        finish(s, STARTUP_FAILED, why, out);
        snprintf(s->status, sizeof(s->status),
                 "Could not settle on a correction: %s after %d measurements",
                 startup_reason_name(why), s->track.measurements);
    }
}

/* -------------------------------------------------------------------------
 * Measuring a channel the caller named, with no search
 * ---------------------------------------------------------------------- */

void startup_session_set_budget(struct startup_session *s, double seconds) {
    if (s && seconds > 0.0)
        s->measure_budget = seconds;
}

int startup_session_measure_gsm(struct startup_session *s, int arfcn,
                                int applied_ppm, int allow_centroid,
                                double now,
                                struct startup_session_event *out) {
    event_clear(out);
    if (!s)
        return -1;
    startup_session_reset(s);
    s->applied_ppm = applied_ppm;
    s->suggested_ppm = applied_ppm;
    s->allow_centroid = allow_centroid;
    s->measure_budget = STARTUP_MEASURE_SECONDS;
    s->arfcn = arfcn;
    measure_gsm_begin(s, out, now);
    return s->phase == STARTUP_MEASURE_GSM ? 0 : -1;
}

int startup_session_measure_lte(struct startup_session *s, unsigned int earfcn,
                                int applied_ppm, double now,
                                struct startup_session_event *out) {
    event_clear(out);
    if (!s)
        return -1;
    startup_session_reset(s);
    s->applied_ppm = applied_ppm;
    s->suggested_ppm = applied_ppm;
    /*
     * No centroid on the LTE side, and not as a policy: the offset comes from
     * `lte_cell_search`, which either found a cell or did not. There is no
     * second estimator to fall back to, so the flag would name a choice that
     * does not exist.
     */
    s->allow_centroid = 0;
    s->measure_budget = STARTUP_MEASURE_SECONDS;
    s->earfcn = earfcn;
    measure_lte_begin(s, out, now);
    return s->phase == STARTUP_MEASURE_LTE ? 0 : -1;
}

void startup_session_retuned(struct startup_session *s, double now) {
    if (!s || s->tuned)
        return;
    s->tuned = 1;
    /*
     * The settle starts here, and not when the tuning was asked for. A retune
     * flushes the pipeline and costs about a tenth of a second -- the whole of
     * a scan step's settle -- so timed from the request every block of the
     * step measures as post-settle and the stale one among them is believed.
     */
    s->step_started_at = now;
    if (s->phase == STARTUP_MEASURE_GSM || s->phase == STARTUP_MEASURE_LTE)
        s->measure_started_at = now;
}

void startup_session_retune_failed(struct startup_session *s, uint32_t hz,
                                   const char *why,
                                   struct startup_session_event *out) {
    event_clear(out);
    if (!s || !startup_session_running(s))
        return;
    /*
     * A search can go on: the next step is a different frequency and the
     * tuner may well take it. A measurement cannot -- there is exactly one
     * carrier it is measuring against.
     */
    if (s->phase == STARTUP_SCAN_GSM) {
        if (s->step + 1 >= s->plan.step_count) {
            gsm_scan_finished(s, out, 0.0);
            return;
        }
        s->step++;
        gsm_step_begin(s, out);
        return;
    }
    if (s->phase == STARTUP_SCAN_LTE) {
        s->lte_index++;
        lte_step_begin(s, out);
        if (!s->earfcn)
            finish(s, STARTUP_FAILED, STARTUP_REASON_NO_CELL, out);
        return;
    }
    finish(s, STARTUP_FAILED, STARTUP_REASON_NO_TUNE, out);
    snprintf(s->status, sizeof(s->status),
             "The receiver would not tune to %.3f MHz: %.120s", hz / 1e6,
             why ? why : "no reason given");
}

void startup_session_skip(struct startup_session *s,
                          struct startup_session_event *out) {
    event_clear(out);
    if (!s || !startup_session_running(s))
        return;
    finish(s, STARTUP_SKIPPED, STARTUP_REASON_SKIPPED, out);
    snprintf(s->status, sizeof(s->status),
             "Calibration skipped; the correction is unchanged");
}
