#ifndef SURVEY_VIEW_MODEL_H
#define SURVEY_VIEW_MODEL_H

#include "core/reading_origin.h"
#include "model/survey_mark.h"
#include "model/site_seen.h"
#include "core/survey_carrier.h"
#include "model/survey_tuning.h"
#include "core/survey_sweep.h"

/* What this reads, and all of it: the sweep, and the four tuning facts
   `survey_tuning_from()` already gathers. It took a `const struct app *`,
   which made a contract depend on everything the application does
   (`.scratch/layer-boundaries/issues/03-*`). */
struct survey_session;

/*
 * What a survey candidate is, decided once rather than by every drawing that
 * reads one.
 *
 * `draw_survey()`'s per-peak flag word (for the chart's marks) and
 * `draw_peak_list()`'s per-row flags, carrier, shape and history mark were a
 * line-for-line second copy of each other -- both computed
 * `survey_suspect_at(...) | survey_confirmed_flags_at(...)` through the same
 * carrier lookup, and the list went on to look the carrier up a *second* time
 * for its width and shape. ADR-0012: a function that draws may not also
 * decide, and two drawings computing the same decision are one decision with
 * two chances to disagree.
 *
 * `survey_view_model_build()` fills one candidate per swept peak, in the same
 * order as `struct survey_session`'s own `peaks[]` -- so `draw_survey()` reads
 * `candidates[i].flags` where it used to compute `flags[i]`, and
 * `draw_peak_list()` reads a candidate by the same index a visible row names.
 *
 * Reads only plain fields -- no GL or raylib call, no I/O -- so
 * `check-survey-view-model` links `-lm` alone against it, the same as
 * `scope_view_model.h`.
 */
struct survey_candidate_view {
    double hz;             /* the peak's own frequency */
    float power_dbfs;

    /* The carrier this peak belongs to, or none: a step's shoulder can be a
       peak with no carrier grouping ever having found it worth one. */
    int has_carrier;
    double carrier_centre_hz;  /* valid only when has_carrier */
    double width_hz;           /* valid only when has_carrier */
    enum survey_shape shape;   /* valid only when has_carrier */

    /* The sweep's own suspicion, OR'd with whatever a confirmation pass
       settled at the carrier's centre (or the peak's own frequency, absent a
       carrier) -- survey_suspect_warns/_empty/_contested() read this. */
    unsigned flags;

    /* What this site has heard of it before, SITE_SEEN_UNKNOWN with no
       history loaded. */
    enum site_seen seen;

    /*
     * Which of the four marks this candidate wears, decided here from the
     * flags above rather than by each reader. The chart and the wire both
     * take it: two readers deriving a mark from one flag word is two
     * chances to disagree, and the browser did disagree for months
     * (`web-visualization/15`).
     */
    enum survey_peak_mark mark;
};

/*
 * Everything ticket 07's own comment names as still missing, on
 * `survey_candidate_view`'s day: `ss->power`, `bins`, the sweep's step and
 * status. **Not the drag/zoom window** -- that is an input-mapping concern
 * (the chart's own frequency-to-pixel arithmetic under a narrowed view), and
 * this ticket's browser reader has no drag to map yet; a faithful copy of
 * the window's zoom state with nothing on the far end to use it would be
 * exactly the "half a screen modelled" fault `CLAUDE.md` names about layout
 * headers, moved to a view model instead.
 *
 * `power` is capped at `SURVEY_VIEW_MODEL_MAX_BINS` -- `SURVEY_BINS` itself,
 * so nothing is ever truncated; the cap exists so a reader sizing a buffer
 * from this header needs no other one.
 */
#define SURVEY_VIEW_MODEL_MAX_BINS SURVEY_BINS

struct survey_view_model {
    struct survey_candidate_view candidates[SURVEY_MAX_PEAKS];
    int candidate_count;

    /* Whether a sweep is currently walking the range below, and what the
       window's own status line would say -- "sweeping...", "nothing found",
       a count of candidates, a refusal. One string rather than the several
       booleans a reader would otherwise have to recombine into the same
       sentence the window already wrote. */
    int sweeping;
    char status[200];

    /* The range actually swept, and its own spectrum -- power against
       frequency, the chart's whole data. Zero bins and zero-valued bounds
       before anything has ever swept, which is a fact rather than a
       placeholder: nothing has been measured yet. */
    double lower_hz;
    double upper_hz;
    int bins;
    float power[SURVEY_VIEW_MODEL_MAX_BINS];
};

/*
 * Fills `out` from the current sweep in `app->survey.session`. `out` is not a
 * snapshot to keep past this frame: a candidate's carrier and history mark
 * are read fresh every call, the same as the drawing they replace did.
 */
void survey_view_model_build(const struct survey_session *ss,
                             const struct survey_record_tuning *tuning,
                             struct survey_view_model *out);

#endif
