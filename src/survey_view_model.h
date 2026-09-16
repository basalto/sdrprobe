#ifndef SURVEY_VIEW_MODEL_H
#define SURVEY_VIEW_MODEL_H

#include "reading_origin.h"
#include "site_history.h"
#include "survey_carrier.h"
#include "survey_sweep.h"

struct app;

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
};

struct survey_view_model {
    struct survey_candidate_view candidates[SURVEY_MAX_PEAKS];
    int candidate_count;
};

/*
 * Fills `out` from the current sweep in `app->survey.session`. `out` is not a
 * snapshot to keep past this frame: a candidate's carrier and history mark
 * are read fresh every call, the same as the drawing they replace did.
 */
void survey_view_model_build(const struct app *app,
                             struct survey_view_model *out);

#endif
