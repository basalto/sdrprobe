#include "survey_record.h"

#include <string.h>

#include "survey_suspect.h"

static void copy_id(char *out, size_t size, const char *in) {
    if (!in) {
        out[0] = '\0';
        return;
    }
    snprintf(out, size, "%s", in);
}

int survey_record_form(struct survey_record *out,
                       const struct survey_plan *plan, double dwell_seconds,
                       const struct survey_record_setup *setup,
                       const struct tm *recorded_at,
                       const struct survey_candidate *candidates, int count,
                       const struct survey_carrier *carriers,
                       int carrier_count,
                       const struct survey_confirm_target *targets,
                       int target_count) {
    int i;

    if (!out || !plan || !recorded_at)
        return -1;
    memset(out, 0, sizeof(*out));
    out->plan = *plan;
    out->dwell_seconds = dwell_seconds;
    out->recorded_at = *recorded_at;
    if (setup) {
        copy_id(out->setup.receiver, sizeof(out->setup.receiver),
                setup->receiver);
        copy_id(out->setup.antenna, sizeof(out->setup.antenna),
                setup->antenna);
        copy_id(out->setup.site, sizeof(out->setup.site), setup->site);
        out->setup.gain_tenths = setup->gain_tenths;
    }
    /* The tolerance every match below is made with, so nothing re-derives
       it. A plan with no bins gives zero, which matches exactly. */
    out->match_hz = plan->bin_hz / 2.0;

    if (candidates && count > 0) {
        if (count > SURVEY_RECORD_CANDIDATE_MAX)
            count = SURVEY_RECORD_CANDIDATE_MAX;
        memcpy(out->candidates, candidates,
               (size_t)count * sizeof(*candidates));
        out->candidate_count = count;
    }
    if (carriers && carrier_count > 0) {
        if (carrier_count > SURVEY_CARRIER_MAX)
            carrier_count = SURVEY_CARRIER_MAX;
        memcpy(out->carriers, carriers,
               (size_t)carrier_count * sizeof(*carriers));
        out->carrier_count = carrier_count;
    }
    if (targets && target_count > 0) {
        if (target_count > SURVEY_CONFIRM_MAX)
            target_count = SURVEY_CONFIRM_MAX;
        memcpy(out->targets, targets,
               (size_t)target_count * sizeof(*targets));
        out->target_count = target_count;
    }

    /*
     * A candidate takes the verdict of the carrier holding it.
     *
     * The pass asks about carriers, and several maxima of one signal sit a
     * few bins apart inside one; asking at each maximum's own frequency would
     * find no target for the shoulders and mark most of a confirmed station's
     * list unconfirmed. Falling back to the candidate's own frequency when no
     * carrier holds it is what makes a lone peak still answerable.
     */
    for (i = 0; i < out->candidate_count; i++) {
        const struct survey_candidate *c = &out->candidates[i];
        int holder = survey_carrier_holding(out->carriers,
                                            out->carrier_count, c->found_hz);
        double asked_at = holder >= 0 ? out->carriers[holder].centre_hz
                                      : c->found_hz;

        out->candidate_verdict[i] =
            (signed char)survey_confirm_verdict_at(out->targets,
                                                   out->target_count,
                                                   asked_at, out->match_hz);
        if (survey_suspect_warns(c->suspect))
            out->suspicious++;
    }
    for (i = 0; i < out->carrier_count; i++) {
        const struct survey_carrier *c = &out->carriers[i];
        const struct survey_confirm_target *kind;
        int t;

        out->carrier_verdict[i] =
            (signed char)survey_confirm_verdict_at(out->targets,
                                                   out->target_count,
                                                   c->centre_hz,
                                                   out->match_hz);
        /*
         * Which target measured this carrier's kind, as an index.
         *
         * `survey_confirm_kind_at()` is the decision and stays the decision;
         * this turns its answer into a position in the record's own array, so
         * a copied record still points at its own targets rather than at the
         * ones it was formed from.
         */
        out->carrier_kind[i] = -1;
        kind = survey_confirm_kind_at(out->targets, out->target_count,
                                      c->centre_hz, out->match_hz);
        if (kind)
            for (t = 0; t < out->target_count; t++)
                if (&out->targets[t] == kind) {
                    out->carrier_kind[i] = t;
                    break;
                }
    }
    for (i = 0; i < out->target_count; i++) {
        if (out->targets[i].verdict == SURVEY_VERDICT_CONFIRMED)
            out->confirmed++;
        else if (out->targets[i].verdict == SURVEY_VERDICT_INTERMITTENT)
            out->intermittent++;
        else if (out->targets[i].verdict == SURVEY_VERDICT_REFUTED)
            out->refuted++;
    }
    return 0;
}

const struct survey_confirm_target *
survey_record_carrier_kind(const struct survey_record *record, int carrier) {
    if (!record || carrier < 0 || carrier >= record->carrier_count)
        return NULL;
    if (record->carrier_kind[carrier] < 0)
        return NULL;
    return &record->targets[record->carrier_kind[carrier]];
}
