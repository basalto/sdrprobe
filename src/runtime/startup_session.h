#ifndef STARTUP_SESSION_H
#define STARTUP_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "calibration_gate.h"
#include "gsm_dsp.h"
#include "lte_dsp.h"
#include "lte_scan.h"
#include "scan_plan.h"

/*
 * Finding a reference and measuring the crystal against it, block by block,
 * with no window and no receiver.
 *
 * `.scratch/startup-installation/issues/01-startup-session-machine.md`. The
 * startup overlay draws this and the command line prints it; neither owns it.
 * ADR-0012 and ADR-0024: a function that draws or reads input may not also
 * decide, and this is the deciding half.
 *
 * The sequence is: scan GSM 900 for a BCCH, measure its FCCH if there is one,
 * fall through to an LTE band scan if there is not, measure a cell's primary
 * sequence, and hold the answer against `calibration_gate.h` until it may be
 * trusted. `survey_session.{c,h}` is the shape, including both of its
 * refusals:
 *
 *   - **It does not touch the receiver.** It says where it wants the tuning
 *     and at what rate (`event.retune_hz`, `event.retune_rate_hz`); the
 *     adapter obeys and reports back with `startup_session_retuned()`, or
 *     says it could not with `startup_session_retune_failed()`.
 *   - **It does not read or write files, and it applies nothing.** It offers
 *     `suggested_ppm` and the source it came from; filing a correction is
 *     `installation_commit()`'s business and it stays the one writer.
 *
 * Why GSM first, and why not for the reason usually given: both references
 * pass through one gate, `calibration_is_stable()`, which will not lock
 * either until the standard error of the centre is under
 * CALIBRATION_MAX_SEM_PPM -- so a lock is a lock at the same tolerance
 * whichever produced it, and the one on-air comparison has them agreeing to
 * about a ppm. GSM leads because **only an FCCH-backed calibration enables
 * the drift re-check** (ADR-0006 gates it on the source), because an FCCH is
 * an unmodulated tone where a PSS phase wraps every 15 kHz, and because
 * finding a GSM reference costs one pass of `scan_plan.h` -- sixteen steps of
 * 0.8 s at 2 MS/s -- against minutes for a band of LTE channels.
 */

/*
 * What one block of samples looks like here: no `struct app`, no receiver
 * handle, no window.
 *
 * The spectrum is the DC-filtered average the frame already holds, in dBFS,
 * and is what the GSM scan reads channel powers out of. The samples are the
 * centred I/Q both detectors run on.
 */
struct startup_block {
    const float *i_samples;
    const float *q_samples;
    size_t pair_count;
    const float *spectrum;      /* SDR_DSP_FFT_SIZE bins of dBFS */
    /*
     * A sort workspace of at least SDR_DSP_FFT_SIZE floats, for the centroid
     * estimator's own floor -- the same arrangement `survey_block` uses, and
     * for the same reason: the machine allocates nothing.
     *
     * May be NULL when the centroid is not allowed, which is the startup
     * form's case.
     */
    float *scratch;
    double centre_hz;           /* where the receiver is tuned */
    double sample_rate;
};

/*
 * The phases, and the names are the report.
 *
 * SCAN_GSM and SCAN_LTE are searches for something to measure; MEASURE_GSM
 * and MEASURE_LTE are the measurement. LOCKED, FAILED and SKIPPED are the
 * three ways it is over, and `startup_session_may_commit()` is true in
 * exactly those three.
 */
enum startup_phase {
    STARTUP_IDLE = 0,
    STARTUP_SCAN_GSM,
    /*
     * Asking the chosen channel to prove it is GSM before measuring against
     * it.
     *
     * A tone is not a broadcast carrier. `GSM_FCCH_SEARCH_HALF_HZ` is 50 kHz,
     * so the detector reports whatever coherent line it finds within that of
     * where an FCCH would be, and nothing about a high coherence says the
     * line is an FCCH. Measured on air, ARFCN 63 here reads **0.97-0.99
     * confidence** and a correction 14 ppm from what ARFCN 113 gives for the
     * same crystal; the scan takes the loudest channel carrying a tone, so it
     * took 63.
     *
     * **This gate did not settle that case** -- 63 passes it, BSIC 42, so it
     * is a real base station and the disagreement is something else
     * (`.scratch/startup-installation/issues/08-*`). What the gate is for is
     * the weaker claim it does settle: that the thing being calibrated
     * against is a GSM transmitter at all. It is demonstrated firing on a
     * synthetic bare tone in `check-startup-session`, and has so far
     * *confirmed* rather than rejected every real channel it has been given
     * -- which is worth knowing about a gate, since a negative from one
     * nobody has seen fire is not a finding.
     *
     * The SCH is what cannot be faked: `gsm_sch_decode()` returns 1 only on a
     * **parity-valid** decode, so a carrier that is no longer GSM does not
     * produce one however coherent its spurs. This is the corroboration the
     * `dsp-validation` skill asks for -- a second, independent statement
     * about the same carrier rather than a louder version of the first.
     */
    STARTUP_VERIFY_GSM,
    /*
     * Asking the tone to prove it is a *signal* by moving the receiver.
     *
     * The SCH gate above proves a GSM base station is transmitting on the
     * channel. It does not prove the line the tone detector locked onto is
     * that station's FCCH: the search is +/-50 kHz wide and returns the
     * strongest thing inside it, so on a channel whose FCCH is weak it
     * returns traffic.
     *
     * A real line has an absolute frequency, and that cannot depend on where
     * the receiver is tuned. An artefact of a search window can. Measured
     * here over two tunings 200 kHz apart: ARFCN 63 repeated to **71 Hz**
     * and ARFCN 113 to **272 Hz** over six recordings, while ARFCN 17 --
     * whose "tone" sat at the very edge of the search window -- moved
     * **3277 Hz** and was not an FCCH at all
     * (`.scratch/startup-installation/issues/09-*`).
     */
    STARTUP_CONFIRM_TONE,
    STARTUP_MEASURE_GSM,
    STARTUP_SCAN_LTE,
    STARTUP_MEASURE_LTE,
    STARTUP_LOCKED,
    STARTUP_FAILED,
    STARTUP_SKIPPED
};

/*
 * Why it ended, in the vocabulary `--calibrate` already prints.
 *
 * The same five words, deliberately: `locked`, `no-cell`,
 * `too-few-measurements`, `sem-too-wide`, `timeout`. A new word here would be
 * a second vocabulary for one outcome, and a grep across the window's log and
 * the headless report would stop working at the seam.
 */
enum startup_reason {
    STARTUP_REASON_NONE = 0,
    STARTUP_REASON_LOCKED,
    STARTUP_REASON_NO_CELL,
    STARTUP_REASON_TOO_FEW,
    STARTUP_REASON_SEM_WIDE,
    STARTUP_REASON_TIMEOUT,
    STARTUP_REASON_NO_TUNE,
    STARTUP_REASON_SKIPPED,
    /*
     * Two references measured this crystal and did not agree.
     *
     * `.scratch/startup-installation/issues/08-*`: at one site here ARFCN 63
     * and ARFCN 113 differ by 15 ppm, both parity-verified GSM cells, both
     * locking the gate, and **no single-channel statistic separates them** --
     * the spreads overlap, the tone coherence is flat across a channel, and
     * the bad one decodes 284 synchronisation bursts in 25 s. A threshold on
     * how far a tone may sit from nominal cannot work either, because on an
     * uncalibrated receiver it legitimately sits tens of kHz out, which is
     * what calibration is for.
     *
     * So this is a refusal rather than a failure, and `app.h` already said so
     * before it was built: "when they agree the correction is worth trusting,
     * and when they do not that is the most useful thing either of them has
     * said."
     */
    STARTUP_REASON_DISAGREE
};

const char *startup_reason_name(enum startup_reason reason);
const char *startup_phase_name(enum startup_phase phase);

/*
 * How long a measurement is given before the gate's failure to open is itself
 * the answer. The headless path's own default, so both adapters give up at
 * the same place.
 */
#define STARTUP_MEASURE_SECONDS 90.0

/*
 * How long one LTE channel is looked at, and how many channels are tried.
 *
 * The look is `lte_scan.h`'s own settle and probe, because a shorter one here
 * would find different cells than the decode view's scan finds on the same
 * band. What is different is where it **stops**: this is looking for a
 * reference, not taking an inventory, so it stops at the first channel whose
 * identity repeats rather than walking the whole raster. The order is
 * `lte_scan_candidate()`'s, which tries whole megahertz first precisely
 * because that is where operators centre carriers, so the early stop usually
 * lands in the first thirty tunings rather than the three hundredth.
 */
/*
 * How many blocks a candidate channel gets to produce a parity-valid SCH.
 *
 * A synchronisation burst comes once every ten TDMA frames -- about 46 ms, so
 * roughly one per 65 ms block -- but the decoder needs the burst to fall
 * inside the block and the channel to be strong enough. Eight blocks is half
 * a second and gives a real BCCH several chances; a channel that says nothing
 * in eight is not one this can calibrate against, and the cost of being wrong
 * is a confident correction tens of ppm out.
 */
#define STARTUP_VERIFY_BLOCKS 8

/*
 * How far two references may disagree and still be one crystal.
 *
 * The gate that produced each already holds its own centre to
 * CALIBRATION_MAX_SEM_PPM (1.0), so two sound measurements of one oscillator
 * land within a couple of ppm of each other -- measured here, GSM and LTE
 * agreed to about one. The fault this separates is 15 ppm wide, so the
 * constant is nowhere near either edge and is not a tuned number: anything
 * from 3 to 10 would make the same call on every case in the ticket.
 *
 * Deliberately **not** tight. A false disagreement costs a calibration that
 * could have been trusted; there is no cost to the operator beyond the form
 * saying so, but sending somebody to the slow path over ordinary measurement
 * noise would be a worse trade than the 15 ppm error it is guarding against.
 */
#define STARTUP_AGREE_PPM 4.0

/*
 * How far the tone may appear to move when the receiver does, and how far to
 * move it.
 *
 * Measured where it breaks, which is what choosing a constant here requires:
 * two real lines repeated to 0.07 and 0.28 ppm across tunings, and the one
 * artefact moved 3.5 ppm. One ppm sits a factor of 3.5 above the worst real
 * case and 3.5 below the artefact -- the geometric middle of the gap, so it
 * is not perched on either edge.
 *
 * The shift is 200 kHz because it has to be large enough to move the line
 * well across the search window and small enough to keep the channel inside
 * the span at 2 MS/s, where the carrier already sits 400 kHz off centre.
 */
#define STARTUP_TONE_REPEAT_PPM 1.0
#define STARTUP_TONE_SHIFT_HZ 200000

/* How many blocks each of the two looks gets. The estimate is a mean over
   them, so this trades half a second against the scatter of a single block --
   four is where the mean stops moving on the carriers measured here. */
#define STARTUP_TONE_BLOCKS 4

#define STARTUP_LTE_LOOKS LTE_SCAN_MIN_LOOKS
#define STARTUP_LTE_AGREE LTE_SCAN_CONFIRMATIONS

struct startup_session {
    enum startup_phase phase;
    enum startup_reason reason;

    /*
     * Where the machine wants the receiver, and whether the adapter has
     * reported getting it there.
     *
     * `tuned` is the whole of the settle rule. A retune flushes the pipeline
     * and costs about a tenth of a second, so a step timed from when the
     * tuning was *asked for* expires before the blocks it is meant to discard
     * have arrived -- and the scan then folds the previous step's signal into
     * this step's answer. The session cannot know when the tuner moved, so
     * the adapter says so and the clock starts there.
     */
    uint32_t want_hz;
    uint32_t want_rate_hz;
    int tuned;
    double step_started_at;

    /* The GSM 900 walk. `plan` is scan_plan.h's; the arrays are indexed by
       ARFCN directly, so index 0 is unused. */
    struct scan_plan plan;
    int step;
    float power[SCAN_ARFCN_LAST + 1];
    float bcch_conf[SCAN_ARFCN_LAST + 1];
    int arfcn;                  /* the BCCH chosen, or 0 */
    float arfcn_confidence;
    /*
     * The verification pass: which candidates have been tried, what the SCH
     * said, and how long the current one has been asked.
     *
     * `bsic` is the proof. A parity-valid SCH carries the Base Station
     * Identity Code, so a channel that passes has not merely avoided failing
     * -- it has said something only a GSM base station could say.
     */
    int verify_blocks;
    int verified;               /* an SCH decoded with valid parity */
    int bsic;
    /*
     * The cross-check: measure a second verified channel and require the two
     * to agree about this one crystal.
     *
     * `cross_check` is on for the search (the startup form, `--calibrate
     * auto`) and off when the caller named the channel -- `--calibrate gsm
     * --arfcn N` is an instruction to measure *that*, and going off to find a
     * second opinion would answer a question nobody asked.
     */
    /*
     * The tone-repeat check: the carrier each of the two looks estimated, how
     * many blocks each has had, and which look is running.
     */
    double tone_first_hz;
    double tone_second_hz;
    int tone_blocks;
    int tone_second_look;
    double tone_moved_hz;       /* what the check measured, for the report */

    int cross_check;
    int references;             /* how many have been measured through */
    int first_ppm;              /* what the first one suggested */
    int first_arfcn;
    int first_bsic;
    int second_ppm;             /* and the second, once there is one */
    int second_arfcn;
    int rejected[SCAN_ARFCN_LAST + 1];  /* channels a verify pass turned down */
    int rejected_arfcn;         /* the most recent one, for the report */
    /* And why, captured **before** the search moves on. Reading `status`
       after a rejection gives the next candidate's line instead, which is
       what the first trace of this printed: "rejected arfcn 63: Checking
       ARFCN 17 is a GSM broadcast carrier". */
    char rejected_why[200];   /* as wide as `status`, so nothing truncates */

    /* The LTE walk, when GSM had nothing. */
    const struct lte_band *band;
    int lte_index;              /* which candidate of the band */
    int lte_looks;              /* looks spent on this channel */
    int lte_agreements;         /* times this channel repeated its identity */
    int lte_pci;                /* what it said, while agreeing */
    unsigned int earfcn;        /* the channel chosen, or 0 */
    int pci;
    float pss;

    /* The measurement, and the gate over it. */
    struct calibration_tracker track;
    uint32_t expected_hz;       /* the carrier being measured against */
    int applied_ppm;            /* the correction in force while measuring */
    int suggested_ppm;
    double measured_hz;
    double offset_hz;
    float quality;              /* FCCH coherence, or PSS correlation */
    double measure_started_at;
    double measure_budget;
    /* How many residuals an adapter has already reported, so a log or a
       report can print each new one exactly once. */
    int reported;

    /*
     * Whether a block with no FCCH tone may contribute a **centroid**
     * residual instead.
     *
     * A real difference between two callers rather than a convenience, which
     * is the test a mode flag has to pass before it earns its place. The
     * startup form files a correction unattended and says no: a centroid
     * residual is a different measurement of a different thing, and a buffer
     * holding both has a centre belonging to neither (ADR-0004). `--calibrate`
     * serves an operator reading every residual as it arrives, where a
     * reading that keeps moving between bursts is worth having and the gate
     * already treats the centroid as the weakest source -- it wants a carrier
     * 8 dB clear of its guard band before the residual counts at all.
     *
     * `calibration_track()` is the same state machine either way; this only
     * decides whether it is ever told a centroid is available.
     */
    int allow_centroid;
    /* What the centroid last measured, for the callers that report it. Zero
       when no centroid has been taken. */
    float peak_dbfs;
    float floor_dbfs;
    float prominence_db;

    /*
     * Why the machine is where it is, in words rather than a code. Both
     * adapters want the same sentence, and the ones a reader acts on are
     * refusals rather than failures.
     */
    char status[200];
};

/* What one tick produced, and what the adapter has to do about it. */
struct startup_session_event {
    /* Where the machine wants the receiver, or 0 for "stay put", and at what
       rate -- 0 meaning "whatever it is now". The rate is here because LTE
       borrows 1.92 MS/s (ADR-0014) and GSM does not, and a machine that said
       only a frequency would leave that decision in the overlay. */
    uint32_t retune_hz;
    uint32_t retune_rate_hz;
    int scan_finished;      /* a search ended; `arfcn` or `earfcn` says what it found */
    /* A candidate was turned down -- `rejected_arfcn` says which, and
       `status` why. Worth an event because a trace that shows only what was
       accepted cannot explain why a search took as long as it did. */
    int candidate_rejected;
    int measure_began;
    int measured;           /* one new residual arrived */
    int finished;           /* LOCKED, FAILED or SKIPPED was reached */
    int release_receiver;   /* nothing is running; the tuning is nobody's */
};

void startup_session_reset(struct startup_session *s);

/*
 * Begin, at `sample_rate`, with `applied_ppm` in force.
 *
 * `band` is the LTE band to fall back to, or NULL when the tuner can reach
 * none -- which a capture and a narrow tuner are both in. `gsm_reachable` is
 * whether this receiver can tune GSM 900 at all; a tuner with a reach hole
 * over the band gets the LTE path directly rather than a scan that would tune
 * where it cannot hear (`device_tuner_reach()`).
 *
 * Returns 0, or -1 with `status` saying why nothing can be measured -- no GSM
 * within reach, no rate wide enough for the plan, and no band either.
 */
int startup_session_begin(struct startup_session *s, double sample_rate,
                          int applied_ppm, int gsm_reachable,
                          const struct lte_band *band, double now,
                          struct startup_session_event *out);

/*
 * One tick. `have_block` says whether `block` holds samples that arrived
 * since the last tick; `now` is monotonic seconds and is the only clock this
 * has.
 *
 * Called every frame with `have_block` as a **parameter rather than a guard**.
 * A search step is over on its own clock once it has heard something, so
 * returning early on "no block" costs a block a step -- which is what a sweep
 * that reported 39 blocks over a 13-step sweep instead of 26 was doing.
 *
 * `block` may be NULL when `have_block` is 0.
 */
void startup_session_tick(struct startup_session *s,
                          const struct startup_block *block, int have_block,
                          double now, struct startup_session_event *out);

/*
 * The tuning the machine asked for has happened, at `now`. The settle starts
 * here; see `tuned` above for why that is not academic.
 */
/*
 * Measure one named channel, with no search at all.
 *
 * What `--calibrate gsm --arfcn N` and `--calibrate lte --earfcn N` do: the
 * operator has said what to measure against, so there is nothing to look for.
 * `allow_centroid` says whether a tone-free block may contribute a centroid
 * residual instead of nothing -- see the field for why that is a real
 * difference between callers and not a convenience.
 *
 * Returns 0, or -1 with `status` saying why the channel is not one.
 */
int startup_session_measure_gsm(struct startup_session *s, int arfcn,
                                int applied_ppm, int allow_centroid,
                                double now,
                                struct startup_session_event *out);
int startup_session_measure_lte(struct startup_session *s, unsigned int earfcn,
                                int applied_ppm, double now,
                                struct startup_session_event *out);

/* How long a measurement is given before the gate's silence is the answer.
   The adapter's, because `--calibrate-seconds` is the operator's. */
void startup_session_set_budget(struct startup_session *s, double seconds);

void startup_session_retuned(struct startup_session *s, double now);

/* The adapter could not tune where the machine asked. A search moves on to
   the next step; a measurement cannot, and ends. */
void startup_session_retune_failed(struct startup_session *s, uint32_t hz,
                                   const char *why,
                                   struct startup_session_event *out);

/* The operator said not to wait. Whatever is running stops, and nothing is
   applied or filed -- a correction that did not pass the gate is wrong by
   however far the estimate had not settled (ADR-0004). */
void startup_session_skip(struct startup_session *s,
                          struct startup_session_event *out);

/* Whether the form may be committed: the three ways it is over. The overlay
   reads this rather than deciding it. */
int startup_session_may_commit(const struct startup_session *s);

/* Whether a search or a measurement is under way. */
int startup_session_running(const struct startup_session *s);

/*
 * Which reference the answer came from, as `calibration_gate.h` spells it --
 * CALIBRATION_SOURCE_FCCH or _LTE. It is what decides whether the drift
 * re-check can run afterwards, so an adapter filing a result must ask.
 */
int startup_session_source(const struct startup_session *s);

#endif
