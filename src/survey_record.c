#include "survey_record.h"

#include <string.h>

#include "band_plan.h"
#include "survey_suspect.h"

#define SURVEY_BANDWIDTH_DB 20.0f

const char *survey_flag_text(unsigned int flags, char *buffer, size_t size) {
    size_t used = 0;

    buffer[0] = '\0';
    if (flags & SURVEY_SUSPECT_REFERENCE)
        used += (size_t)snprintf(buffer + used, size - used, "reference");
    if (flags & SURVEY_SUSPECT_STEP_CENTRE)
        used += (size_t)snprintf(buffer + used, size - used, "%sstep-centre",
                                 used ? "," : "");
    if (flags & SURVEY_SUSPECT_UNRESOLVED)
        used += (size_t)snprintf(buffer + used, size - used, "%sunresolved",
                                 used ? "," : "");
    if (flags & SURVEY_SUSPECT_NO_CARRIER)
        used += (size_t)snprintf(buffer + used, size - used, "%sno-carrier",
                                 used ? "," : "");
    return used ? buffer : "-";
}

static void copy_id(char *out, size_t size, const char *in) {
    if (!in) {
        out[0] = '\0';
        return;
    }
    snprintf(out, size, "%s", in);
}

void survey_record_setup_from(struct survey_record_setup *out,
                              const struct installation *inst,
                              int gain_tenths) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (inst) {
        if (installation_identified(inst))
            copy_id(out->receiver, sizeof(out->receiver), inst->receiver);
        copy_id(out->antenna, sizeof(out->antenna), inst->antenna);
        copy_id(out->site, sizeof(out->site), inst->site);
    }
    out->gain_tenths = gain_tenths;
}

struct tm survey_record_now(void) {
    time_t now = time(NULL);
    struct tm when;

    localtime_r(&now, &when);
    return when;
}

/*
 * What each maximum is, from facts rather than from an application.
 *
 * Moved here from `survey_store.c` unchanged in arithmetic: this is what a
 * candidate *means*, and it was living in the module that spells JSON, where
 * the headless report reached it only by calling into the file writer's
 * translation unit.
 */
int survey_record_candidates(const struct survey_record_tuning *tuning,
                             const struct survey_plan *plan,
                             const struct sdr_peak *peaks, int count,
                             const float *spectrum, float *scratch,
                             struct survey_candidate *out, int max) {
    int i, filled = 0;

    if (!tuning || !plan || !peaks || !out)
        return 0;
    for (i = 0; i < count && filled < max; i++) {
        struct survey_candidate *c = &out[filled];
        struct sdr_carrier_report report;
        const struct band_plan_entry *entry;

        memset(c, 0, sizeof(*c));
        c->found_hz = survey_plan_bin_centre(plan, peaks[i].index);
        c->power_dbfs = peaks[i].power_dbfs;
        c->prominence_db = peaks[i].prominence_db;
        c->extent_hz = (double)(peaks[i].upper_index - peaks[i].lower_index +
                                1) * plan->bin_hz;

        if (spectrum && scratch &&
            sdr_dsp_characterise_carrier(spectrum, SDR_DSP_FFT_SIZE,
                                         tuning->centre_hz,
                                         tuning->sample_rate_hz, c->found_hz,
                                         200000.0, SURVEY_BANDWIDTH_DB,
                                         scratch, &report)) {
            c->measured = 1;
            c->centre_hz = report.centre_hz;
            c->width_hz = report.bandwidth_hz;
        }
        /*
         * The measurement is the better frequency, so the comb test is applied
         * to it -- but the candidate is still reported where the survey found
         * it. Several peaks inside one wide carrier all measure to the same
         * centre, and reporting that centre in place of each would hide the
         * fact that the peak finder returned several.
         */
        /*
         * Measured width where there is one, the survey array's extent where
         * there is not. It used to pass zero for a swept survey, which made
         * every narrowness test answer "no" and left the one observation that
         * tells a bare carrier from a service unavailable in exactly the case
         * that needs it.
         */
        c->suspect = survey_suspect(plan, tuning->reference_clock_hz,
                                    c->measured ? c->centre_hz : c->found_hz,
                                    c->measured ? c->width_hz : c->extent_hz,
                                    tuning->sample_rate_hz, SDR_DSP_FFT_SIZE,
                                    tuning->remove_dc);
        entry = band_plan_lookup(c->measured ? c->centre_hz : c->found_hz);
        c->allocation = entry ? entry->name : NULL;
        filled++;
    }
    return filled;
}

int survey_record_build(struct survey_record *out,
                        const struct survey_record_input *in) {
    static struct survey_candidate candidates[SURVEY_RECORD_CANDIDATE_MAX];
    int count;

    if (!out || !in)
        return -1;
    count = survey_record_candidates(&in->tuning, in->plan, in->peaks,
                                     in->peak_count, in->spectrum, in->scratch,
                                     candidates,
                                     SURVEY_RECORD_CANDIDATE_MAX);
    return survey_record_form(out, in->plan, in->dwell_seconds, &in->setup,
                              &in->recorded_at, candidates, count,
                              in->carriers, in->carrier_count, in->targets,
                              in->target_count);
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
