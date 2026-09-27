#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "model/calibration_view_model.h"
#include "runtime/app.h"
#include "runtime/calibration_gate.h"

/*
 * The Calibration overlay's staged reference, its residual buffer and its
 * verdict, gathered once.
 *
 * In `runtime/` and not beside the model it fills: a builder reads the
 * application's state and a contract may not (ADR-0028).
 */

void calibration_view_model_build(const struct calibration *cal, double now,
                                  int applied_ppm,
                                  struct calibration_view_model *out) {
    const struct calibration_tracker *t = &cal->track;
    double elapsed = cal->started_at > 0.0 ? now - cal->started_at : 0.0;
    const char *unmet;

    memset(out, 0, sizeof(*out));

    out->open = cal->open;
    out->running = cal->running;

    out->technology = cal->technology;
    snprintf(out->channel, sizeof(out->channel), "%s", cal->channel);
    out->band = cal->band;
    out->scanning = cal->lte_scanning;
    out->expected_hz = cal->expected_hz;
    snprintf(out->status, sizeof(out->status), "%s", cal->status);

    /*
     * What measured them. ADR-0004's rule is that a buffer must be
     * source-homogeneous, so naming the source is not a detail: a mixed
     * buffer passes the gate while suggesting a correction belonging to
     * neither reference.
     */
    snprintf(out->source, sizeof(out->source), "%s",
             calibration_source_name(t->source));
    /*
     * This block's residual *and* the median over the ring. Both, because
     * the sequence is what shows whether the scatter is the estimator or
     * the crystal -- which is why `--calibrate` prints every measurement
     * rather than a summary.
     */
    out->observed_ppm = cal->expected_hz
                            ? cal->offset_hz / (double)cal->expected_hz * 1e6
                            : 0.0;
    out->centre_ppm = t->recent_center;
    out->sem_ppm = t->recent_sem;
    out->spread_ppm = t->recent_spread;
    out->measurements = t->measurements;
    out->residuals = t->recent_count;
    /* The quality the gate weighs is the source's own: a tone lock's
       confidence, or a carrier's prominence over its floor. */
    out->quality = t->source == CALIBRATION_SOURCE_FCCH ||
                   t->source == CALIBRATION_SOURCE_LTE
                       ? (double)cal->fcch_confidence
                       : (double)cal->prominence_db;

    out->locked = calibration_is_stable(elapsed, t->measurements,
                                        t->recent_count, t->recent_sem,
                                        t->source, (float)out->quality);
    /*
     * And which clause is still unsatisfied. Empty once it locks, because
     * then there is none -- a reader who sees a name there knows there is
     * something still to wait for or to fix, and which.
     */
    unmet = calibration_gate_unmet(elapsed, t->measurements, t->recent_count,
                                   t->recent_sem, t->source,
                                   (float)out->quality);
    if (unmet)
        snprintf(out->unmet, sizeof(out->unmet), "%s", unmet);

    out->suggested_ppm = cal->suggested_ppm;
    out->applied_ppm = applied_ppm;

    out->gsm_valid = cal->gsm_valid;
    out->gsm_ppm = cal->gsm_ppm;
    out->gsm_arfcn = cal->gsm_arfcn;
    out->lte_valid = cal->lte_valid;
    out->lte_ppm = cal->lte_ppm;
    out->lte_earfcn = cal->lte_earfcn;
    /*
     * How far apart the two references are, and only when there *are* two.
     * One reference that has measured is not a disagreement, and reporting
     * a gap of `0 - 32` as though it were would be the most misleading
     * number on the screen.
     */
    out->have_both = cal->gsm_valid && cal->lte_valid;
    if (out->have_both)
        out->references_apart_ppm = fabs((double)cal->gsm_ppm -
                                         (double)cal->lte_ppm);

    snprintf(out->health, sizeof(out->health), "%s",
             calibration_health_name(cal->drift_health));
    snprintf(out->drift_notice, sizeof(out->drift_notice), "%s",
             cal->drift_notice);
}
