#ifndef SCAN_PLAN_H
#define SCAN_PLAN_H

/*
 * The GSM 900 band scan's arithmetic: how the downlink is covered in steps,
 * which channels a step can see, when a step is done, and which channel the
 * operator is handed at the end.
 *
 * The last one is the reason this is worth checking. The scan's whole output
 * is a single number -- an ARFCN -- and the operator then spends their time on
 * it. Choosing wrongly does not look like an error; it looks like a quiet
 * neighbourhood. Nothing else in the program would say otherwise.
 *
 * Plain arrays and doubles, no raylib and no receiver (ADR-0012), checked by
 * tests/scan_plan_test.c.
 */

/* The GSM 900 downlink, inset from the band edges so a channel is never
   measured on the shoulder of the tuner's response. */
#define SCAN_BAND_LOWER_HZ 935100000.0
#define SCAN_BAND_UPPER_HZ 959900000.0
#define SCAN_EDGE_MARGIN_HZ 200000.0

/* How long a step waits for the tuner, and then how long it keeps probing.
   FCCH is sent ten times a multiframe, so a step that only looked once would
   miss most bursts; the probe window is long enough to catch one. */
#define SCAN_STEP_SETTLE_SECONDS 0.35
#define SCAN_STEP_PROBE_SECONDS 0.45

#define SCAN_SENTINEL_DBFS (-300.0f)

/* How sure the FCCH detector must be before a channel counts as carrying a
   BCCH. Slightly relaxed from the calibration threshold: a strong neighbour
   lowers coherence, and here the answer is only which channel to look at
   next, not what correction to apply. */
#define SCAN_BCCH_MIN_CONF 0.85f

/* ARFCNs 1..124 are the GSM 900 downlink; index 0 is unused so an ARFCN can
   index the arrays directly. */
#define SCAN_ARFCN_FIRST 1
#define SCAN_ARFCN_LAST 124

enum scan_plan_status {
    SCAN_PLAN_OK,
    SCAN_PLAN_RATE_TOO_LOW /* the accept window would be under 100 kHz */
};

struct scan_plan {
    double accept_half_hz;  /* half the window a step measures in */
    double step_hz;         /* how far the receiver moves between steps */
    double first_center_hz;
    int step_count;
};

/*
 * Work out how many tunings it takes to cover the downlink at this sample
 * rate. The accept window is the span minus a margin at each edge, and steps
 * are exactly one window apart, so the band is covered once with no overlap
 * and no gap.
 */
static inline enum scan_plan_status
scan_plan_make(double sample_rate, struct scan_plan *plan) {
    double accept_half = sample_rate / 2.0 - SCAN_EDGE_MARGIN_HZ;
    double centre;

    if (accept_half < 100000.0)
        return SCAN_PLAN_RATE_TOO_LOW;
    plan->accept_half_hz = accept_half;
    plan->step_hz = 2.0 * accept_half;
    plan->first_center_hz = SCAN_BAND_LOWER_HZ + accept_half;
    plan->step_count = 0;
    for (centre = plan->first_center_hz;
         centre - accept_half < SCAN_BAND_UPPER_HZ; centre += plan->step_hz)
        plan->step_count++;
    return SCAN_PLAN_OK;
}

static inline double scan_plan_step_centre(const struct scan_plan *plan,
                                           int step) {
    return plan->first_center_hz + (double)step * plan->step_hz;
}

/* Whether a channel falls inside the window this step measures. Outside it the
   channel is either on the tuner's shoulder or another step's business. */
static inline int scan_plan_covers(const struct scan_plan *plan,
                                   double centre_hz, double channel_hz) {
    return channel_hz >= centre_hz - plan->accept_half_hz &&
           channel_hz <= centre_hz + plan->accept_half_hz;
}

enum scan_step_phase {
    SCAN_STEP_SETTLING, /* the tuner has not caught up */
    SCAN_STEP_PROBING,  /* measure power, look for FCCH, stay here */
    SCAN_STEP_NEXT,
    SCAN_STEP_FINISHED
};

static inline enum scan_step_phase scan_step_phase_at(double elapsed, int step,
                                                      int step_count) {
    if (elapsed < SCAN_STEP_SETTLE_SECONDS)
        return SCAN_STEP_SETTLING;
    if (elapsed < SCAN_STEP_SETTLE_SECONDS + SCAN_STEP_PROBE_SECONDS)
        return SCAN_STEP_PROBING;
    if (step + 1 >= step_count)
        return SCAN_STEP_FINISHED;
    return SCAN_STEP_NEXT;
}

/*
 * Keep the best FCCH coherence seen at this channel across the step's blocks,
 * never clearing it. FCCH is intermittent: a block between bursts reads low,
 * and taking the latest rather than the best would turn a BCCH carrier into an
 * ordinary one depending on when the block happened to arrive.
 */
static inline float scan_hold_confidence(float held, float seen) {
    return seen > held ? seen : held;
}

/* The loudest channel measured, or 0 if none was. */
static inline int scan_select_strongest(const float *power) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    int arfcn;

    for (arfcn = SCAN_ARFCN_FIRST; arfcn <= SCAN_ARFCN_LAST; arfcn++) {
        if (power[arfcn] > best_power) {
            best_power = power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

/* The loudest channel that also carries an FCCH tone, or 0 if none does. */
static inline int scan_select_bcch(const float *power,
                                   const float *confidence) {
    int best = 0;
    float best_power = SCAN_SENTINEL_DBFS;
    int arfcn;

    for (arfcn = SCAN_ARFCN_FIRST; arfcn <= SCAN_ARFCN_LAST; arfcn++) {
        if (confidence[arfcn] >= SCAN_BCCH_MIN_CONF &&
            power[arfcn] > best_power) {
            best_power = power[arfcn];
            best = arfcn;
        }
    }
    return best;
}

/*
 * What the scan hands back when it finishes.
 *
 * A BCCH carrier is preferred over a merely loud one, however much louder the
 * loud one is, and that preference is the whole value of the scan: a channel
 * with no BCCH has no FCCH to calibrate against and no SCH to decode, so
 * landing on one means an operator watching a chart that can never say
 * anything. The fallback exists for a band where nothing was confidently a
 * BCCH -- something to look at beats nothing.
 */
static inline int scan_choose(const float *power, const float *confidence) {
    int bcch = scan_select_bcch(power, confidence);

    return bcch > 0 ? bcch : scan_select_strongest(power);
}

#endif
