#ifndef CALIBRATION_VIEW_MODEL_H
#define CALIBRATION_VIEW_MODEL_H

#include <stdint.h>

/*
 * What the Calibration overlay says, as plain data -- no raylib type
 * anywhere (`web-visualization/17`).
 *
 * The screen is a staged reference, a residual buffer filling, and a verdict.
 * Three things on it are decisions rather than readings:
 *
 *  - **What measured the residuals**, by name. ADR-0004's whole point is
 *    that a buffer mixing two sources passes the gate while suggesting a
 *    correction belonging to neither, so the source is not a detail -- and
 *    it was spelled in exactly one place, a ternary chain inside
 *    `headless_run.c`'s `cal-measure` line, where the overlay could not
 *    reach it.
 *  - **Which clause of the gate is still unsatisfied.**
 *    `calibration_is_stable()` returns one bit, and a reader watching a
 *    calibration that will not lock cannot tell "it needs four more seconds"
 *    from "the scatter is too wide to ever settle" -- different situations,
 *    and only one is worth waiting out.
 *  - **What the two references make of each other.** Two references
 *    measuring one crystal: when they agree the correction is worth
 *    trusting, and when they do not, that is the most useful thing either
 *    of them has said.
 *
 * Takes the structs it reads rather than a `const struct app *`, so
 * `check-calibration-view-model` links `-lm` alone.
 */

/*
 * What a correction is worth, and how long it has been standing.
 *
 * Nothing here dismisses itself on a timer: a correction is a standing fact
 * about the receiver, and a surface that erased itself could not answer
 * "what am I corrected by?" a minute later. So the health travels as a name
 * and the notice travels whole.
 */
static inline const char *calibration_health_name(int health) {
    switch (health) {
    case 1:  return "good";      /* CAL_HEALTH_GOOD */
    case 2:  return "drifting";  /* CAL_HEALTH_DRIFT */
    case 3:  return "checking";  /* CAL_HEALTH_CHECKING */
    }
    return "unknown";            /* CAL_HEALTH_UNKNOWN */
}

struct calibration_view_model {
    /* Whether the overlay is up, and whether a measurement is under way.
       They are different: the overlay opens on a staged reference and
       nothing is measured until Start. */
    int open;
    int running;

    /* The staged reference. `technology` is 0 for 2G and 1 for 4G, and
       `channel` is the ARFCN or EARFCN as typed -- text, because it is text
       until it parses. */
    int technology;
    char channel[16];
    int band;
    int scanning;               /* a calibration-driven band scan is up */
    uint32_t expected_hz;       /* the carrier being measured against */
    char status[160];

    /* What the gate holds. `observed_ppm` is this block's residual and
       `centre_ppm` the median over the ring -- both, because the sequence is
       what shows whether the scatter is the estimator or the crystal, which
       a summary cannot. */
    char source[16];
    double observed_ppm;
    double centre_ppm;
    double sem_ppm;
    double spread_ppm;
    int measurements;
    int residuals;
    double quality;

    /*
     * The verdict, and -- when it is not a lock -- which clause is still
     * unsatisfied, by the name `calibration_gate_unmet()` gives it. Empty
     * once it locks, because then there is no clause to name.
     */
    int locked;
    char unmet[24];

    /* What it would suggest, and what is in force now. */
    int suggested_ppm;
    int applied_ppm;

    /* The two references. `agree` is 0 when fewer than two have measured,
       which is its own answer and not "they disagree". */
    int gsm_valid, gsm_ppm, gsm_arfcn;
    int lte_valid, lte_ppm, lte_earfcn;
    int have_both;
    double references_apart_ppm;

    /* The health indicator, which is where the verdict lives after the
       overlay closes. */
    char health[16];
    char drift_notice[160];
};

struct calibration;

/* Fills `out`. Reads plain fields only -- no I/O, no raylib call.
   `now` and `applied_ppm` are the caller's: the elapsed time the gate
   measures against, and what the receiver is actually corrected by. */
void calibration_view_model_build(const struct calibration *cal, double now,
                                  int applied_ppm,
                                  struct calibration_view_model *out);

#endif
