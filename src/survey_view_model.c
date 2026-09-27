#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "survey_view_model.h"
#include "survey_session.h"
#include "survey_suspect.h"


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
