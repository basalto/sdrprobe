#define _POSIX_C_SOURCE 200809L

#include <string.h>

#include "survey_view_model.h"
#include "app.h"
#include "installation.h"
#include "survey_session.h"
#include "survey_suspect.h"
#include "view.h"

/*
 * The four facts a candidate needs, read out of `struct app` once here rather
 * than by every caller that builds a `struct survey_record` or a view model
 * of its own -- `survey_report.c` and this file are the two.
 *
 * Declared in view.h rather than here: both callers already include it, and a
 * survey record's tuning is not only this view model's business. What moved
 * is which file defines the bodies, so that this one compiles with no
 * raylib call in it and check-survey-view-model can link `-lm` alone --
 * they used to live in view_survey.c, which draws.
 */
struct reading_clock survey_reading_clock(const struct app *app) {
    struct reading_clock clock = { 0.0, 0.0 };
    int calibrated = 0;

    if (!app)
        return clock;
    if (installation_ppm(&app->installation, &calibrated))
        clock.crystal_ppm = (double)calibrated;
    clock.applied_ppm = (double)app->applied.ppm;
    return clock;
}

void survey_tuning_from(struct survey_record_tuning *out,
                        const struct app *app) {
    memset(out, 0, sizeof(*out));
    out->centre_hz = (double)app->applied.frequency_hz;
    out->sample_rate_hz = (double)app->applied.sample_rate_hz;
    out->reference_clock_hz = app->device.reference_clock_hz;
    out->remove_dc = app->remove_dc;
    out->clock = survey_reading_clock(app);
}

/* The sweep's own suspicion at a frequency -- the frequency-only half of a
   candidate's flags, available whether or not anything has asked again. */
static unsigned survey_view_model_suspect(const struct app *app, double hz) {
    struct survey_record_tuning t;

    survey_tuning_from(&t, app);
    return survey_suspect(&app->survey.session.plan, t.reference_clock_hz, hz,
                          0.0, t.sample_rate_hz, SDR_DSP_FFT_SIZE,
                          t.remove_dc);
}

void survey_view_model_build(const struct app *app,
                             struct survey_view_model *out) {
    const struct survey_session *ss = &app->survey.session;
    int i;

    memset(out, 0, sizeof(*out));

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
        c->flags = survey_view_model_suspect(app, hz) |
                   survey_session_confirmed_flags_at(ss, asked_hz);
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
