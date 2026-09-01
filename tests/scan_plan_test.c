#include "scan_plan.h"
#include "check.h"

#include <stdio.h>
#include <string.h>

/*
 * The band scan's coverage of the GSM 900 downlink, and the one number it
 * hands back at the end.
 *
 * That number is the whole output of a scan: the operator then spends their
 * time on that channel. Choosing a loud channel with no BCCH over a quieter
 * one that has one means a decode view that can never say anything, and it
 * does not look like a bug -- it looks like a quiet cell (ADR-0012, layer 1).
 */

static float power[SCAN_ARFCN_LAST + 1];
static float confidence[SCAN_ARFCN_LAST + 1];

static void clear(void) {
    for (int i = 0; i <= SCAN_ARFCN_LAST; i++) {
        power[i] = SCAN_SENTINEL_DBFS;
        confidence[i] = 0.0f;
    }
}

static double channel_hz(int arfcn) {
    /* GSM 900 downlink, the same arithmetic gsm_dsp.c uses. */
    return 935000000.0 + (double)arfcn * 200000.0;
}

static void test_the_plan_at_two_megasamples(void) {
    struct scan_plan plan;

    check_int("planning succeeds", scan_plan_make(2000000.0, &plan),
              SCAN_PLAN_OK);
    check_close("the window is the span less a margin at each edge",
                plan.accept_half_hz, 1000000.0 - SCAN_EDGE_MARGIN_HZ, 0.5);
    check_close("steps are one whole window apart", plan.step_hz,
                2.0 * plan.accept_half_hz, 0.5);
    check_close("the first sits a half-window above the band edge",
                plan.first_center_hz,
                SCAN_BAND_LOWER_HZ + plan.accept_half_hz, 0.5);
    /* 24.8 MHz of downlink in 1.6 MHz windows: 16 steps, and the last one
       hangs over the top of the band rather than stopping short of it. */
    check_int("steps to cover the downlink", plan.step_count, 16);
}

/*
 * The property the step count exists for: every one of the 124 channels must
 * fall inside some step's accept window. A step count one too low leaves the
 * top of the band unmeasured, and those channels then read as absent -- which
 * is indistinguishable from a cell that is not transmitting.
 */
static void test_every_channel_is_measured(void) {
    const double rates[] = { 1000000.0, 1200000.0, 2000000.0, 2400000.0,
                             2560000.0 };

    for (size_t r = 0; r < sizeof(rates) / sizeof(*rates); r++) {
        struct scan_plan plan;
        int missed = 0;
        int last_missed = 0;

        if (scan_plan_make(rates[r], &plan) != SCAN_PLAN_OK) {
            check_msg(0, "%.1f MS/s was refused\n", rates[r] / 1e6);
            continue;
        }
        for (int arfcn = SCAN_ARFCN_FIRST; arfcn <= SCAN_ARFCN_LAST; arfcn++) {
            int covered = 0;

            for (int step = 0; step < plan.step_count && !covered; step++)
                covered = scan_plan_covers(&plan,
                                           scan_plan_step_centre(&plan, step),
                                           channel_hz(arfcn));
            if (!covered) {
                missed++;
                last_missed = arfcn;
            }
        }
        check_msg(missed == 0,
                  "%.2f MS/s: %d channels fall in no step (e.g. ARFCN %d)\n",
                  rates[r] / 1e6, missed, last_missed);
    }
}

/* No channel may be measured twice either: the windows abut, they do not
   overlap, and a channel measured in two steps gets whichever answer came
   last rather than the one from the step that saw it best. */
static void test_no_channel_is_measured_twice(void) {
    struct scan_plan plan;
    int doubled = 0;

    check_int("planning succeeds", scan_plan_make(2000000.0, &plan),
              SCAN_PLAN_OK);
    for (int arfcn = SCAN_ARFCN_FIRST; arfcn <= SCAN_ARFCN_LAST; arfcn++) {
        int seen = 0;

        for (int step = 0; step < plan.step_count; step++)
            if (scan_plan_covers(&plan, scan_plan_step_centre(&plan, step),
                                 channel_hz(arfcn)))
                seen++;
        if (seen > 1)
            doubled++;
    }
    check_int("no channel is in two windows", doubled, 0);
}

static void test_a_rate_too_low_is_refused(void) {
    struct scan_plan plan;

    /* The margin eats 200 kHz of each half-span, so anything under 600 kS/s
       leaves an accept window narrower than 100 kHz -- half the 200 kHz
       channel spacing, and not enough to measure a channel in. */
    check_int("500 kS/s is refused", scan_plan_make(500000.0, &plan),
              SCAN_PLAN_RATE_TOO_LOW);
    check_int("just under the boundary is refused",
              scan_plan_make(2.0 * (100000.0 + SCAN_EDGE_MARGIN_HZ) - 2.0,
                             &plan),
              SCAN_PLAN_RATE_TOO_LOW);
    check_int("600 kS/s is exactly the boundary, and allowed",
              scan_plan_make(2.0 * (100000.0 + SCAN_EDGE_MARGIN_HZ), &plan),
              SCAN_PLAN_OK);
}

static void test_the_accept_window(void) {
    struct scan_plan plan;
    double centre;

    scan_plan_make(2000000.0, &plan);
    centre = scan_plan_step_centre(&plan, 3);
    check_int("the centre channel is covered",
              scan_plan_covers(&plan, centre, centre), 1);
    check_int("the edge of the window is covered",
              scan_plan_covers(&plan, centre, centre + plan.accept_half_hz),
              1);
    check_int("just past it is not",
              scan_plan_covers(&plan, centre,
                               centre + plan.accept_half_hz + 1.0),
              0);
    /* The margin is the point: the tuner rolls off out here, so a channel
       measured at the edge of the span reads low. */
    check_int("the edge of the tuned span is outside the window",
              scan_plan_covers(&plan, centre, centre + 1000000.0 - 1.0), 0);
}

static void test_step_phases(void) {
    const double settle = SCAN_STEP_SETTLE_SECONDS;
    const double probe = SCAN_STEP_PROBE_SECONDS;

    check_int("settling", scan_step_phase_at(settle / 2.0, 0, 16),
              SCAN_STEP_SETTLING);
    check_int("probing starts the instant the settle ends",
              scan_step_phase_at(settle, 0, 16), SCAN_STEP_PROBING);
    check_int("still probing", scan_step_phase_at(settle + probe - 0.01, 0, 16),
              SCAN_STEP_PROBING);
    check_int("then on to the next", scan_step_phase_at(settle + probe, 0, 16),
              SCAN_STEP_NEXT);
    check_int("the last step finishes",
              scan_step_phase_at(settle + probe, 15, 16), SCAN_STEP_FINISHED);
    check_int("a one-step scan finishes",
              scan_step_phase_at(settle + probe, 0, 1), SCAN_STEP_FINISHED);
}

/*
 * FCCH confidence is held at its best across the step. The tone is sent ten
 * times a multiframe, so most blocks in a probe window see nothing; taking the
 * latest reading rather than the best would make a channel's BCCH flag depend
 * on when the last block happened to arrive.
 */
static void test_confidence_is_held_at_its_best(void) {
    float held = 0.0f;
    const float blocks[] = { 0.1f, 0.2f, 0.95f, 0.1f, 0.05f, 0.3f };

    for (size_t i = 0; i < sizeof(blocks) / sizeof(*blocks); i++)
        held = scan_hold_confidence(held, blocks[i]);
    check_close("the burst is remembered after five quiet blocks", held, 0.95,
                1e-6);
    check_msg(held >= SCAN_BCCH_MIN_CONF,
              "a channel with one clear FCCH burst did not reach the "
              "threshold (%.2f)\n",
              held);
}

/* Picking the loudest channel. */
static void test_strongest(void) {
    clear();
    check_int("an empty scan chooses nothing", scan_select_strongest(power), 0);

    power[7] = -60.0f;
    power[42] = -35.0f;
    power[100] = -50.0f;
    check_int("the loudest wins", scan_select_strongest(power), 42);

    /* A tie keeps the first: arbitrary, but it must be stable, or the same
       scan hands back a different channel each run. */
    clear();
    power[10] = -40.0f;
    power[20] = -40.0f;
    check_int("a tie keeps the lower channel", scan_select_strongest(power),
              10);

    /* Only 1..124 are downlink channels. */
    clear();
    power[0] = 0.0f;
    check_int("channel 0 is not a channel", scan_select_strongest(power), 0);
}

/*
 * The choice that matters: a BCCH carrier beats a louder one without a BCCH,
 * however much louder. This is the preference the scan exists to express, and
 * it is one careless simplification away from disappearing.
 */
static void test_bcch_beats_loud(void) {
    clear();
    power[42] = -20.0f;  /* very loud, no FCCH: a TCH carrier, say */
    power[73] = -55.0f;  /* far quieter, but it has the tone */
    confidence[73] = 0.95f;

    check_int("the loud one is the loudest", scan_select_strongest(power), 42);
    check_int("but the BCCH is chosen", scan_choose(power, confidence), 73);
    check_int("and the BCCH selector agrees",
              scan_select_bcch(power, confidence), 73);

    /* Among BCCH carriers, the loudest still wins. */
    confidence[42] = 0.9f;
    check_int("two BCCHs: the loudest", scan_choose(power, confidence), 42);
}

static void test_the_fallback(void) {
    clear();
    power[42] = -20.0f;
    power[73] = -55.0f;
    /* Nothing confidently carried a tone. Something to look at beats
       nothing. */
    check_int("with no BCCH anywhere, the loudest", scan_choose(power,
                                                                confidence),
              42);
    check_int("and no BCCH is reported", scan_select_bcch(power, confidence),
              0);

    /* A confidence just under the threshold is not a BCCH. The threshold is
       what stops a noise correlation from being called a carrier. */
    confidence[73] = SCAN_BCCH_MIN_CONF - 0.01f;
    check_int("just under the threshold does not count",
              scan_select_bcch(power, confidence), 0);
    confidence[73] = SCAN_BCCH_MIN_CONF;
    check_int("exactly at it does", scan_select_bcch(power, confidence), 73);

    /* An empty scan must hand back 0 rather than a channel nobody measured:
       0 is what the caller tests for. */
    clear();
    check_int("an empty scan chooses nothing", scan_choose(power, confidence),
              0);
}

int main(void) {
    test_the_plan_at_two_megasamples();
    test_every_channel_is_measured();
    test_no_channel_is_measured_twice();
    test_a_rate_too_low_is_refused();
    test_the_accept_window();
    test_step_phases();
    test_confidence_is_held_at_its_best();
    test_strongest();
    test_bcch_beats_loud();
    test_the_fallback();

    return check_report("band scan");
}
