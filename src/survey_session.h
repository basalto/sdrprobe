#ifndef SURVEY_SESSION_H
#define SURVEY_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "sdr_dsp.h"
#include "signal_probe.h"
#include "site_history.h"
#include "survey_carrier.h"
#include "survey_confirm.h"
#include "survey_suspect.h"
#include "survey_sweep.h"

/*
 * A band survey, block by block, with no window and no receiver.
 *
 * `.scratch/deepening/issues/04-survey-session.md`, and the Probe-side twin of
 * the decode sessions in 02. The helpers underneath this were already deep and
 * checked -- `survey_sweep.h` for the step plan and the fold, `survey_carrier.h`
 * for maxima into signals, `survey_confirm.h` for the verdicts, `site_history`
 * for what a place has heard -- but the decisions that *sequence* them lived in
 * `view_survey.c` beside the drawing, and a second copy of the same sequence
 * lived in `survey_report.c`. ADR-0012: a function that draws or reads input
 * may not also decide.
 *
 * So the machine is here: idle -> sweeping -> confirming, with watching as a
 * sweep that goes round again and measuring as a look at one candidate.
 * **The view draws a session; the headless report prints one.** Both are
 * adapters, and neither owns the sweep.
 *
 * Two things it deliberately does not do, because they are what made the two
 * copies diverge in the first place:
 *
 *   - **It does not touch the receiver.** It says where it wants the tuning
 *     (`event.retune_hz`) and the adapter obeys or reports that it could not
 *     (`survey_session_retune_failed`). One machine, two ways of holding a
 *     tuner: the window through a lease, the headless path through a straight
 *     call.
 *   - **It does not read or write files.** It owns a `struct site_history` and
 *     says when the history it holds has changed (`event.history_dirty`) or
 *     needs reloading (`event.marks_stale`); the adapter, which knows the
 *     installation, does the loading and saving.
 */

/*
 * What one block of samples looks like to a survey: no `struct app`, no
 * receiver handle, no window.
 *
 * `scratch` is a sort workspace of at least SDR_DSP_FFT_SIZE floats -- the
 * peak finder and the carrier characterisation both need somewhere to sort a
 * copy of the spectrum, and the program hands them `magnitude_sorted`.
 * `reference_clock_hz` is 0 for a source with no crystal to blame, which is
 * the case a capture is in, and the comb tests then run on nothing.
 */
struct survey_block {
    const float *i_samples;
    const float *q_samples;
    size_t pair_count;
    const float *spectrum;      /* SDR_DSP_FFT_SIZE bins of dBFS */
    float *scratch;
    double centre_hz;           /* where the receiver is tuned */
    double sample_rate;
    double reference_clock_hz;
    int remove_dc;
};

enum survey_session_state {
    SURVEY_SESSION_IDLE,
    SURVEY_SESSION_SWEEPING,
    SURVEY_SESSION_CONFIRMING,
    SURVEY_SESSION_MEASURING
};

/*
 * How the session is being driven, which changes one decision and only one:
 * when a step is over.
 *
 * A **sweep** walks the tuner and each step ends on its own clock. A
 * **capture** holds one tuning for as long as the file lasts, so its single
 * step ends when the source does and every block it delivers is folded --
 * there is no stale block, because nothing ever moved.
 */
enum survey_session_source {
    SURVEY_SOURCE_SWEEP,
    SURVEY_SOURCE_ONE_TUNING
};

/* How many remembered signals the marks can name as absent. */
#define SURVEY_SESSION_MISSING_MAX 32

/*
 * The survey a narrowing sweep is about to replace.
 *
 * Sweeping the window throws away everything outside it and getting that back
 * costs minutes, so Reset zoom restores it from here rather than re-sweeping.
 * 44 KB, which is what a minute of a receiver's time is worth.
 *
 * It lives with the machine rather than with the drawing because it holds
 * *measurements*, and that is not a distinction without a difference: kept in
 * the view, the restore was written twice and broken twice -- once restoring
 * the range fields and not the chart, and once clearing the sweep it had just
 * put back.
 */
struct survey_kept {
    int valid;
    /*
     * The plan as well as the range.
     *
     * Everything downstream of a sweep is scaled by `plan.bin_hz` -- the
     * carrier grouping, the history's matching tolerance, whether an extent is
     * a measurement or the instrument's floor -- and restoring the bins
     * without it left a wide sweep's peaks being read with a narrow sweep's
     * bin width.
     */
    struct survey_plan plan;
    double lower_hz;
    double upper_hz;
    int bins;
    float power[SURVEY_BINS];
    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    int peak_count;
};

/*
 * Asking again about what the sweep claimed.
 *
 * `best` and `hits` are the current target's looks, held rather than averaged:
 * a peak-holding spectrum raises the noise floor along with the signal, so a
 * burst present in two blocks of six came back under the bar it had cleared
 * twice and the verdict contradicted the number beside it.
 */
struct survey_session_confirm {
    int running;
    int index;
    int count;
    int looks;
    int settled;
    double started_at;
    int confirmed;
    int intermittent;
    int refuted;
    struct survey_confirm_target target[SURVEY_CONFIRM_MAX];

    struct sdr_carrier_report best;
    int measured;       /* whether `best` holds anything at all */
    int hits;
    /* What kind of thing the best look found. Scratch until
       survey_session_confirm_decide() copies it into the target, so that a
       target the pass never caught reads as never measured rather than
       inheriting the previous one's answer. */
    int kind_measured;
    struct signal_carrier carrier;
    struct signal_bursts bursts;
    struct signal_envelope envelope;
};

struct survey_session {
    enum survey_session_state state;
    enum survey_session_source source;

    struct survey_plan plan;
    double dwell_seconds;
    double lower_hz;            /* the range actually swept */
    double upper_hz;
    int bins;                   /* of SURVEY_BINS, in use for this range */
    float power[SURVEY_BINS];
    /*
     * And the block spectrum itself, peak-held the same way.
     *
     * A survey that came from one tuning can measure its own candidates out of
     * this without retuning, which is what makes a capture's report complete.
     * Held rather than latest: the latest is whatever happened to be
     * transmitting when the source ran out. Meaningless across a swept range,
     * where it belongs to whichever step was last -- which is exactly what
     * survey_session_spectrum() refuses to hand over.
     */
    float held_spectrum[SDR_DSP_FFT_SIZE];
    int held_valid;

    /* The step being walked. */
    int step;
    int step_count;
    double step_started_at;
    int step_folded;            /* blocks folded into this step */
    int blocks_folded;          /* and over the whole sweep */
    int blocks_discarded;       /* arrived while the tuner was still moving */
    int source_ended;

    struct sdr_peak peaks[SURVEY_MAX_PEAKS];
    int peak_count;
    struct survey_carrier carriers[SURVEY_CARRIER_MAX];
    int carrier_count;

    /*
     * What this site heard before, and how this sweep compares.
     *
     * The session holds the history and never loads it: `carrier_status` runs
     * alongside `carriers` so the chart and the list can show which are new
     * here without asking again per frame, and `missing` points into
     * `history`, which outlives a frame.
     */
    struct site_history history;
    int history_loaded;
    signed char carrier_status[SURVEY_CARRIER_MAX];
    const struct site_entry *missing[SURVEY_SESSION_MISSING_MAX];
    int missing_count;

    struct survey_session_confirm confirm;

    struct survey_kept kept;

    /*
     * Watching: sweep, fold what was found into what the site knows, say what
     * changed, and sweep again. A survey answers "what is out there"; running
     * it round the clock answers "what changed while nobody was looking",
     * which is the question a single sweep can never reach.
     */
    int watching;
    int watch_limit;            /* stop after this many; 0 for no limit */
    int watch_sweeps;
    int watch_appeared;         /* what the last sweep of the watch changed */
    int watch_lost;
    /*
     * And how many signals that sweep found.
     *
     * Kept rather than read off `carrier_count` when the report is written: a
     * watch folds the sweep in and immediately clears the array to go round
     * again, so by the time an adapter prints the line the count is zero. It
     * did, and the first sweep of every watch reported "carriers 0 appeared 5"
     * -- two numbers about the same sweep, one of them from after it had been
     * thrown away.
     */
    int watch_carriers;
    int watch_total_appeared;
    int watch_total_lost;
    double watch_started_at;

    /* Measuring one candidate, once retuned to it. */
    double measure_started_at;
    double measure_expected_hz;
    struct survey_measurement measure;
    struct sdr_carrier_report report;
    int report_valid;
    struct signal_carrier carrier;
    int carrier_valid;
    struct signal_bursts bursts;
    struct signal_envelope envelope;

    /*
     * Why the machine is where it is, in words. A sentence rather than a code,
     * for `lte_session`'s reason: both adapters want the same words, and the
     * ones a reader acts on -- name the site first, a sweep needs a live
     * receiver -- are refusals rather than failures.
     */
    char status[200];
};

/* What one tick produced, and what the adapter has to do about it. */
struct survey_session_event {
    /* Where the machine wants the receiver, or 0 for "stay put". The adapter
       tunes and, if it cannot, says so with survey_session_retune_failed(). */
    uint32_t retune_hz;
    int release_receiver;   /* the sweep is over; the tuning is nobody's */
    int sweep_finished;
    int sweep_stopped;      /* it ended without reaching the last step */
    int confirm_finished;
    int measure_finished;
    int watch_swept;        /* one watch sweep folded into the history */
    int watch_finished;     /* the sweeps that were asked for are done */
    int history_dirty;      /* what the session holds changed; write it */
    int marks_stale;        /* reload the history and re-mark against it */
    int peaks_changed;      /* the candidate list moved under the drawing */
    /*
     * Which confirmation target just got its verdict, counting from one, or 0.
     *
     * Per target rather than all at the end, because that is what both
     * adapters want: a scripted pass prints each verdict as it is reached, so
     * a sweep that is stopped half way has still reported what it settled.
     */
    int target_finished;
};

void survey_session_reset(struct survey_session *s);

/*
 * Plan a sweep of `from_hz` to `to_hz` at `sample_rate`, dwelling `dwell`
 * seconds on each step, and arm its first step. Returns SURVEY_PLAN_OK, or
 * the plan's complaint with `status` saying it in words.
 *
 * `event.retune_hz` is where the first step wants the receiver.
 */
enum survey_plan_status survey_session_sweep(struct survey_session *s,
                                             double from_hz, double to_hz,
                                             double sample_rate, double dwell,
                                             double now,
                                             struct survey_session_event *out);

/*
 * Survey what one tuning covers -- a capture, which holds exactly that.
 *
 * `usable` is the fraction of the span to keep, SURVEY_USABLE_SPAN, and the
 * step never ends on a clock: every block is folded and the sweep finishes
 * when the caller says the source has (survey_session_source_ended).
 */
enum survey_plan_status
survey_session_one_tuning(struct survey_session *s, double centre_hz,
                          double sample_rate,
                          struct survey_session_event *out);

/*
 * One tick of whatever the session is doing. `have_block` says whether
 * `block` holds a spectrum that arrived since the last tick; `now` is
 * monotonic seconds and is the only clock the session has.
 *
 * `block` may be NULL when `have_block` is 0.
 */
void survey_session_tick(struct survey_session *s,
                         const struct survey_block *block, int have_block,
                         double now, struct survey_session_event *out);

/* The source stopped delivering. A capture's survey is finished by this and a
   receiver's is stopped by it, which are different verdicts. */
void survey_session_source_ended(struct survey_session *s, const char *why,
                                 struct survey_session_event *out);

/*
 * The tuning the machine asked for has happened, at `now`.
 *
 * **The settle starts here and not when the retune was asked for**, and the
 * difference is not academic: a retune flushes the receiver's pipeline and
 * takes about a tenth of a second, which is the whole of
 * SURVEY_SETTLE_SECONDS. Timed from the request, every block of the step
 * measures as post-settle and the stale one among them is folded -- a real
 * carrier written into bins at a frequency nothing is transmitting on. A
 * 13-step sweep of band II folded 40 blocks and discarded none, where it
 * should fold 26 and discard 13, one per step.
 *
 * The session cannot know when the tuner moved, so the adapter says so.
 */
void survey_session_retuned(struct survey_session *s, double now);

/* The adapter could not tune where the machine asked. Whatever was running
   stops, and `status` says where it would not go. */
void survey_session_retune_failed(struct survey_session *s, double hz,
                                  struct survey_session_event *out);

/* Stop a sweep where it stands, keeping what was folded so far. */
void survey_session_stop(struct survey_session *s,
                         struct survey_session_event *out);

/*
 * Hand the session what the site has heard, and re-mark this sweep against it.
 * `loaded` is 0 for a site with no history, or none named -- the marks then
 * say nothing rather than saying "new", because "this site has never heard
 * it" and "nobody asked" are different claims.
 */
void survey_session_set_history(struct survey_session *s,
                                const struct site_history *history,
                                int loaded);

/*
 * Start a confirmation pass over what the marks called new or absent, which is
 * what the window's "Ask again" asks. Returns the number of targets, or -1
 * when there is nothing to ask about.
 */
int survey_session_confirm_changes(struct survey_session *s, double now,
                                   struct survey_session_event *out);

/*
 * Start one over *every* carrier the sweep found, which is what a scripted
 * sweep needs: its output is the report, so there is no history to lean on and
 * every claim it prints has to have been asked about.
 */
int survey_session_confirm_all(struct survey_session *s, double now,
                               struct survey_session_event *out);

/*
 * Watch: keep sweeping the same range, folding each sweep into the history.
 * `limit` is how many sweeps to take, or 0 for until stopped. Returns 0 when
 * it began, -1 when it refused -- with `status` saying why, which for a
 * nameless site is the whole point: a watch with nowhere to put what it learns
 * would sweep for hours and learn nothing.
 */
int survey_session_watch(struct survey_session *s, int have_site, int limit,
                         double now, struct survey_session_event *out);
void survey_session_watch_stop(struct survey_session *s);

/*
 * Give up a confirmation pass part way through.
 *
 * The verdicts already reached stand: a target that was asked about and
 * answered has been answered, and throwing that away because the operator
 * stopped the pass would lose the only evidence the sweep's claims ever get.
 */
void survey_session_confirm_abandon(struct survey_session *s,
                                    struct survey_session_event *out);

/*
 * Measure one candidate at `hz`. The receiver is asked for
 * `hz - SURVEY_OFFSET_HZ` so the signal lands clear of its own DC spike, and
 * that offset is also the guard the carrier search needs.
 */
void survey_session_measure(struct survey_session *s, double hz, double now,
                            struct survey_session_event *out);

/*
 * Forget what was measured, keeping the sweep.
 *
 * For a candidate that cannot be measured -- a capture holds one tuning, so
 * nothing can be pointed at anything. It must not take the candidates with
 * it: they are what the sweep found, and a click that emptied the list it was
 * a click *in* would be the worst kind of wrong.
 */
void survey_session_forget_measurement(struct survey_session *s);

/*
 * The spectrum a candidate may be measured out of, or NULL.
 *
 * Non-NULL only when the whole survey came from one tuning. Across a swept
 * range the held spectrum belongs to whichever step was last, and a bandwidth
 * read from it would be a number about the wrong signal.
 */
const float *survey_session_spectrum(const struct survey_session *s);

/* The frequency at the middle of a survey bin, and a bin's width. */
double survey_session_bin_hz(const struct survey_session *s, int bin);
double survey_session_bin_width_hz(const struct survey_session *s);

/* Clear the sweep: a different range is a different measurement, and drawing
   one range's peaks under another's axis puts every one of them at a
   frequency it was not measured at. */
void survey_session_clear(struct survey_session *s);

/*
 * Keep this sweep, so a narrowing one can be undone without re-sweeping.
 * A no-op when there is no sweep to keep.
 */
void survey_session_keep(struct survey_session *s);

/*
 * Put the kept sweep back, measurements and all. Returns 0 when it did, -1
 * when there was nothing kept. The measurement being taken goes -- it belongs
 * to the sweep this replaces -- and the kept copy is spent.
 */
int survey_session_restore(struct survey_session *s);

static inline int survey_session_has_kept(const struct survey_session *s) {
    return s && s->kept.valid;
}

/* Which signal a maximum belongs to, or NULL: a reader points at a bump and
   wants to know about the carrier, not about one of its shoulders. */
const struct survey_carrier *survey_session_carrier_at(
    const struct survey_session *s, double hz);

/* The suspicion flags a confirmation pass settled at `hz`, or 0. */
unsigned survey_session_confirmed_flags_at(const struct survey_session *s,
                                           double hz);

/* How many of the marks are changes worth asking again about. */
int survey_session_change_count(const struct survey_session *s);

static inline int survey_session_sweeping(const struct survey_session *s) {
    return s->state == SURVEY_SESSION_SWEEPING;
}

static inline int survey_session_confirming(const struct survey_session *s) {
    return s->state == SURVEY_SESSION_CONFIRMING;
}

static inline int survey_session_measuring(const struct survey_session *s) {
    return s->state == SURVEY_SESSION_MEASURING;
}

#endif /* SURVEY_SESSION_H */
