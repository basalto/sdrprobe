#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "model/survey_view_model.h"
#include "runtime/survey_session.h"
#include "core/survey_suspect.h"
#include "core/signal_findings.h"
#include "core/band_plan.h"


/* The sweep's own suspicion at a frequency -- the frequency-only half of a
   candidate's flags, available whether or not anything has asked again. */
static unsigned survey_view_model_suspect(const struct survey_session *ss,
                                          const struct survey_record_tuning *t,
                                          double hz) {
    return survey_suspect(&ss->plan, t->reference_clock_hz, hz,
                          0.0, t->sample_rate_hz, SDR_DSP_FFT_SIZE,
                          t->remove_dc);
}

void survey_view_model_build(const struct survey_session *ss,
                             const struct survey_record_tuning *tuning,
                             struct survey_view_model *out) {
    int i;

    memset(out, 0, sizeof(*out));

    out->sweeping = survey_session_sweeping(ss);
    out->watching = ss->watching;
    snprintf(out->status, sizeof(out->status), "%s", ss->status);
    out->lower_hz = ss->lower_hz;
    out->upper_hz = ss->upper_hz;
    out->bins = ss->bins;
    if (out->bins > SURVEY_VIEW_MODEL_MAX_BINS)
        out->bins = SURVEY_VIEW_MODEL_MAX_BINS;
    if (out->bins > 0)
        memcpy(out->power, ss->power, (size_t)out->bins * sizeof(*out->power));

    out->candidate_count = ss->peak_count;
    if (out->candidate_count > SURVEY_MAX_PEAKS)
        out->candidate_count = SURVEY_MAX_PEAKS;

    for (i = 0; i < out->candidate_count; i++) {
        struct survey_candidate_view *c = &out->candidates[i];
        double hz = survey_session_bin_hz(ss, ss->peaks[i].index);
        const struct survey_carrier *carrier =
            survey_session_carrier_at(ss, hz);
        /*
         * Through the carrier this maximum belongs to, not through its own
         * frequency: the confirmation pass asks about carriers at their
         * measured centre, and a station's shoulders are maxima of the same
         * signal several kilohertz away.
         */
        double asked_hz = carrier ? carrier->centre_hz : hz;

        c->hz = hz;
        c->power_dbfs = ss->peaks[i].power_dbfs;
        c->has_carrier = carrier != NULL;
        if (carrier) {
            c->carrier_centre_hz = carrier->centre_hz;
            c->width_hz = carrier->width_hz;
            c->shape = survey_carrier_shape(carrier->width_hz);
        }
        c->flags = survey_view_model_suspect(ss, tuning, hz) |
                   survey_session_confirmed_flags_at(ss, asked_hz);
        c->mark = survey_mark_of(c->flags);
        c->seen = ss->history_loaded
                      ? site_history_seen(
                            &ss->history,
                            site_history_find(&ss->history, asked_hz,
                                              ss->plan.bin_hz > 0.0
                                                  ? ss->plan.bin_hz
                                                  : 1e5),
                            1)
                      : SITE_SEEN_UNKNOWN;
    }
}

static void detail_add(struct survey_detail_view *out, int indent,
                       enum survey_detail_tone tone, const char *text) {
    struct survey_detail_line *l;

    if (out->line_count >= SURVEY_DETAIL_MAX_LINES)
        return;
    l = &out->line[out->line_count++];
    snprintf(l->text, sizeof(l->text), "%s", text);
    l->indent = indent;
    l->tone = tone;
}

/*
 * The Selected-candidate panel's lines, decided once. This mirrors
 * `draw_detail()` in `view_survey.c` -- the same order, the same sentences,
 * through the same shared functions (`signal_findings_from`, `survey_suspect_*`,
 * the band plan) -- but as a list a drawing only renders. The window draws it
 * and the browser renders it, so the panel is one decision and not two.
 */
void survey_detail_view_build(const struct survey_session *ss,
                              const struct survey_record_tuning *tuning,
                              int selected, double selected_hz,
                              struct survey_detail_view *out) {
    char text[SURVEY_DETAIL_TEXT];
    double shown_hz;
    unsigned suspect;

    memset(out, 0, sizeof(*out));
    if (selected < 0)
        return;              /* out->selected stays 0: "click a candidate" */
    out->selected = 1;

    shown_hz = ss->report_valid ? ss->report.centre_hz : selected_hz;
    snprintf(text, sizeof(text), "Selected  %.4f MHz%s", shown_hz / 1e6,
             ss->report_valid ? "  (measured)" : "");
    detail_add(out, 0, SURVEY_DETAIL_BRIGHT, text);

    if (survey_session_measuring(ss))
        detail_add(out, 0, SURVEY_DETAIL_ACTIVE, "measuring...");

    if (ss->report_valid) {
        snprintf(text, sizeof(text), "peak power         %.1f dBFS",
                 (double)ss->report.peak_dbfs);
        detail_add(out, 0, SURVEY_DETAIL_VALUE, text);
        snprintf(text, sizeof(text), "above local floor  %.1f dB",
                 (double)ss->report.prominence_db);
        detail_add(out, 0, SURVEY_DETAIL_VALUE, text);
        snprintf(text, sizeof(text), "occupied bandwidth %.1f kHz  (-%.0f dB)",
                 ss->report.bandwidth_hz / 1e3,
                 (double)ss->report.bandwidth_ref_db);
        detail_add(out, 0, SURVEY_DETAIL_VALUE, text);
        if (ss->measure.blocks > 0) {
            double duty = survey_measure_duty(&ss->measure);

            snprintf(text, sizeof(text), "duty               %s  (%d/%d blocks)",
                     survey_measure_duty_label(duty), ss->measure.hits,
                     ss->measure.blocks);
            detail_add(out, 0, SURVEY_DETAIL_VALUE, text);
        }
        if (ss->measure.hits > 1) {
            snprintf(text, sizeof(text), "stability          +/- %.1f kHz",
                     survey_measure_spread_hz(&ss->measure) / 1e3);
            detail_add(out, 0, SURVEY_DETAIL_VALUE, text);
        }
    } else if (!survey_session_measuring(ss)) {
        detail_add(out, 0, SURVEY_DETAIL_ACTIVE,
                   "nothing measurable at that frequency now");
    }

    if (ss->report_valid || ss->carrier_valid) {
        struct signal_findings findings;
        int k;

        signal_findings_from(ss->carrier_valid ? &ss->carrier : NULL,
                             &ss->bursts, &ss->envelope,
                             survey_measure_duty(&ss->measure),
                             ss->measure.hits, ss->measure.blocks,
                             survey_measure_spread_hz(&ss->measure),
                             &findings);
        for (k = 0; k < findings.count; k++)
            detail_add(out, k ? 1 : 0,
                       k ? SURVEY_DETAIL_MUTED : SURVEY_DETAIL_VALUE,
                       findings.line[k]);
    }

    suspect = survey_suspect(&ss->plan, tuning->reference_clock_hz, shown_hz,
                             ss->report_valid ? ss->report.bandwidth_hz : 0.0,
                             tuning->sample_rate_hz, SDR_DSP_FFT_SIZE,
                             tuning->remove_dc) |
              survey_session_confirmed_flags_at(ss, shown_hz);
    if (survey_suspect_empty(suspect)) {
        detail_add(out, 0, SURVEY_DETAIL_ACTIVE,
                   "the closer look found nothing here");
        detail_add(out, 1, SURVEY_DETAIL_WARN,
                   "no carrier, and the envelope varies like noise");
        detail_add(out, 1, SURVEY_DETAIL_MUTED,
                   "however many looks saw it: a bar is cleared by noise");
    }
    if (survey_suspect_warns(suspect)) {
        detail_add(out, 0, SURVEY_DETAIL_ACTIVE,
                   "this looks like the receiver, not the band");
        detail_add(out, 1, SURVEY_DETAIL_WARN, survey_suspect_reason(suspect));
        if (suspect & SURVEY_SUSPECT_UNRESOLVED)
            detail_add(out, 1, SURVEY_DETAIL_WARN,
                       "and too narrow for this FFT to resolve: a bare carrier");
        detail_add(out, 1, SURVEY_DETAIL_MUTED,
                   "unplug the antenna and sweep again: what stays is the "
                   "receiver");
    } else if (suspect & SURVEY_SUSPECT_UNRESOLVED) {
        detail_add(out, 0, SURVEY_DETAIL_MUTED,
                   "too narrow for this FFT to resolve: the width above is its "
                   "floor");
    }

    {
        const struct band_plan_entry *entry = band_plan_lookup(shown_hz);

        if (entry) {
            snprintf(text, sizeof(text), "band plan: %s", entry->name);
            detail_add(out, 0, SURVEY_DETAIL_INFO, text);
            if (entry->note)
                detail_add(out, 1, SURVEY_DETAIL_MUTED, entry->note);
            detail_add(out, 1, SURVEY_DETAIL_MUTED,
                       "a frequency lookup, not a detection");
        } else {
            detail_add(out, 0, SURVEY_DETAIL_MUTED,
                       "band plan: nothing allocated here that this table "
                       "knows");
        }
    }
}
