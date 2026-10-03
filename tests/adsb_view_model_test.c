#include "check.h"

#include "model/adsb_view_model.h"
#include "runtime/app.h"

#include <string.h>

/*
 * What the ADS-B screen says, decided without a screen.
 *
 * Two things here are worth a check rather than a reading. Whether Mode S
 * could be present at all -- off 1090 MHz or under 2 MS/s nothing that
 * arrives can be a frame -- because an empty table means two entirely
 * different things depending on the answer. And how much of the log
 * travels, which is a decision about a link that drops messages.
 *
 * One `struct adsb_view` in, one model out; `-lm` alone.
 */

static struct adsb_view adsb;
static int receiver_mode = 1;
static int have_samples = 1;

static struct adsb_view_model build(uint32_t hz, uint32_t rate) {
    struct adsb_view_model out;

    adsb_view_model_build(&adsb, hz, rate, receiver_mode, have_samples,
                          &out);
    return out;
}

static void blank(void) {
    memset(&adsb, 0, sizeof(adsb));
    receiver_mode = 1;
    have_samples = 1;
}

/* A row the log would hold, numbered so a test can tell them apart. */
static void put(int slot, int n) {
    snprintf(adsb.log[slot].icao, sizeof(adsb.log[slot].icao), "%06X", n);
    snprintf(adsb.log[slot].label, sizeof(adsb.log[slot].label), "POS");
    adsb.log[slot].time = (double)n;
}

/*
 * The distinction the whole funnel exists for: a quiet sky and a receiver
 * pointed somewhere else both produce nothing, and only this tells them
 * apart. 1090 MHz at 2 MS/s is where Mode S is; a pulse is half a
 * microsecond, so a slower rate cannot resolve one whatever is on air.
 */
static void test_ready_is_about_the_receiver_not_the_sky(void) {
    struct adsb_view_model m;

    blank();
    m = build(1090000000u, 2000000u);
    check_int("on frequency and fast enough", m.ready, 1);

    m = build(1090000000u, 1000000u);
    check_int("too slow to resolve a pulse", m.ready, 0);

    m = build(948400000u, 2000000u);
    check_int("fast enough, but pointed at GSM", m.ready, 0);

    m = build(1090000000u, 2048000u);
    check_int("faster than needed is still ready", m.ready, 1);
}

/*
 * The log travels whole and bounded, and both halves matter.
 *
 * Bounded because all 256 entries every block is about 570 KB/s, more than
 * ADR-0027 budgets for every derived stream put together. Whole -- rather
 * than the rows new since last time -- because the Viewer link drops
 * messages by design, and a dropped increment would lose those decoded
 * aircraft permanently with nothing to say they had existed.
 */
static void test_the_log_is_bounded_and_newest_first(void) {
    struct adsb_view_model m;
    int i;

    blank();
    m = build(1090000000u, 2000000u);
    check_int("an empty log carries nothing", m.log_count, 0);

    for (i = 0; i < 10; i++)
        put(i, 100 + i);
    adsb.log_count = 10;
    m = build(1090000000u, 2000000u);
    check_int("a short log carries all of it", m.log_count, 10);
    check_str("newest first, unreordered", m.log[0].icao, "000064");
    check_str("and the oldest of the ten last", m.log[9].icao, "00006D");

    /* Now more than fits. `adsb->log` is newest-first, so the cap keeps the
       newest -- the opposite would show a reader the oldest 48 of 256 and
       look like a decode that had stopped. */
    for (i = 0; i < ADSB_LOG_CAPACITY; i++)
        put(i, 1000 + i);
    adsb.log_count = ADSB_LOG_CAPACITY;
    m = build(1090000000u, 2000000u);
    check_int("a full log is capped", m.log_count, ADSB_VIEW_MODEL_LOG);
    check_str("and it is the newest that survive", m.log[0].icao, "0003E8");
    check_str("down to the 48th", m.log[ADSB_VIEW_MODEL_LOG - 1].icao,
              "000417");
}

/* A count larger than the array is a refusal, not a read past the end. The
   log's count and its contents are written on different lines in
   `adsb_runtime.c`, so a count that outran them would be a buffer overrun
   here rather than a wrong number. */
static void test_an_impossible_count_is_clamped(void) {
    struct adsb_view_model m;

    blank();
    adsb.log_count = ADSB_LOG_CAPACITY * 4;
    m = build(1090000000u, 2000000u);
    check_int("clamped to what travels", m.log_count, ADSB_VIEW_MODEL_LOG);

    adsb.log_count = -5;
    m = build(1090000000u, 2000000u);
    check_int("and a negative count carries nothing", m.log_count, 0);
}

/*
 * The funnel, for the run and for the block, carried separately.
 *
 * Both, because they answer different questions: the totals say what this
 * run has managed, and the block says whether it is still managing it. A
 * receiver that was decoding and stopped shows healthy totals beside an
 * empty block, which no single pair of numbers could say.
 */
static void test_the_funnel_carries_both_clocks(void) {
    struct adsb_view_model m;

    blank();
    adsb.session.frames_total = 24;
    adsb.session.positions_total = 7;
    adsb.session.totals.preambles = 609;
    adsb.session.totals.attempts = 36;
    adsb.session.totals.crc_failed = 12;
    adsb.session.totals.decoded = 24;
    adsb.session.block_stats.preambles = 2;

    m = build(1090000000u, 2000000u);
    check_int("frames", (long)m.frames_total, 24);
    check_int("positions", (long)m.positions_total, 7);
    check_int("preambles seen this run", (long)m.totals.preambles, 609);
    check_int("of which DF17/18-shaped", (long)m.totals.attempts, 36);
    check_int("of which failed parity", (long)m.totals.crc_failed, 12);
    check_int("of which decoded", (long)m.totals.decoded, 24);
    check_int("and this block's own preambles", (long)m.block.preambles, 2);
    check_int("with nothing decoded in it", (long)m.block.decoded, 0);
}

/*
 * Whose problem an empty table is, and the one reading of the funnel a
 * reader acts on.
 *
 * Both were decided inside `draw_adsb()`: the window drew a Retune button
 * for one case and a sentence for the other, and coloured its funnel line
 * amber when frames were arriving and none decoded. The browser could see
 * neither -- it had one sentence for both cases and a funnel in one colour.
 */
static void test_not_ready_says_whose_problem_it_is(void) {
    struct adsb_view_model m;

    blank();
    m = build(1090000000u, 2000000u);
    check_str("on frequency", adsb_readiness_name(m.readiness), "ready");

    m = build(100000000u, 2000000u);
    check_str("a receiver pointed elsewhere",
              adsb_readiness_name(m.readiness), "receiver-elsewhere");

    receiver_mode = 0;
    m = build(100000000u, 2000000u);
    check_str("a capture taken elsewhere",
              adsb_readiness_name(m.readiness), "capture-elsewhere");
}

static void test_frames_arriving_and_none_decoding_is_its_own_answer(void) {
    struct adsb_view_model m;

    blank();
    m = build(1090000000u, 2000000u);
    check_int("a quiet band is not a fault", m.funnel_warn, 0);

    /*
     * Preambles alone do not raise it: a preamble is a correlation peak and
     * noise produces those. An *attempt* is a preamble that survived
     * shaping, so it is a frame that was really there.
     */
    adsb.session.totals.preambles = 4000;
    m = build(1090000000u, 2000000u);
    check_int("correlation peaks on noise are not frames", m.funnel_warn, 0);

    adsb.session.totals.attempts = 120;
    m = build(1090000000u, 2000000u);
    check_int("frames arriving and none decoding", m.funnel_warn, 1);

    adsb.session.totals.decoded = 1;
    m = build(1090000000u, 2000000u);
    check_int("one decode is enough to clear it", m.funnel_warn, 0);
}

/* An empty log means "listening" once samples have arrived and "waiting"
   before, which is the difference between a quiet sky and a receiver that
   is not running. */
static void test_an_empty_log_says_which_kind_of_empty(void) {
    struct adsb_view_model m;

    blank();
    m = build(1090000000u, 2000000u);
    check_int("samples are arriving", m.have_samples, 1);
    have_samples = 0;
    m = build(1090000000u, 2000000u);
    check_int("and before any have", m.have_samples, 0);
}

/* The analysis charts behind "Show charts": the shown frame trace's four
   arrays, carried when the trace is valid. The scatter's y centres on the
   expected amplitude (amplitude*2-1, clamped). */
static void test_the_analysis_charts_are_carried(void) {
    struct adsb_view_model m;
    struct adsb_frame_trace *t = &adsb.session.trace;
    int i;

    blank();
    t->valid = 1;
    t->bit_count = 56;
    for (i = 0; i < ADSB_TRACE_LANDSCAPE; i++)
        t->landscape[i] = (float)i;
    for (i = 0; i < 56; i++) {
        t->confidence[i] = 0.7f;
        t->margin[i] = (float)i - 28.0f;
        t->amplitude[i] = 0.5f;        /* -> scatter_y 0.0 */
    }
    for (i = 0; i < ADSB_TRACE_SAMPLES; i++)
        t->envelope[i] = 0.1f;

    m = build(1090000000u, 2000000u);

    check_int("the trace is carried", m.trace_valid, 1);
    check_int("the bit count is carried", m.trace_bits, 56);
    check_int("the landscape is full", m.landscape_count,
              ADSB_TRACE_LANDSCAPE);
    check_close("the confidence is carried", m.confidence[0], 0.7, 1e-6);
    check_int("the envelope count is preamble plus the bits",
              m.envelope_count, ADSB_PREAMBLE_SAMPLES + 56 * ADSB_SAMPLES_PER_BIT);
    check_close("the scatter x is the margin", m.scatter_x[0], -28.0, 1e-6);
    check_close("the scatter y centres on the expected amplitude",
                m.scatter_y[0], 0.0, 1e-6);
}

int main(void) {
    test_not_ready_says_whose_problem_it_is();
    test_the_analysis_charts_are_carried();
    test_frames_arriving_and_none_decoding_is_its_own_answer();
    test_an_empty_log_says_which_kind_of_empty();
    test_ready_is_about_the_receiver_not_the_sky();
    test_the_log_is_bounded_and_newest_first();
    test_an_impossible_count_is_clamped();
    test_the_funnel_carries_both_clocks();
    return check_report("what the ADS-B screen says, decided without a "
                        "screen");
}
