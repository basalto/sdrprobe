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

static struct adsb_view_model build(uint32_t hz, uint32_t rate) {
    struct adsb_view_model out;

    adsb_view_model_build(&adsb, hz, rate, &out);
    return out;
}

static void blank(void) {
    memset(&adsb, 0, sizeof(adsb));
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

int main(void) {
    test_ready_is_about_the_receiver_not_the_sky();
    test_the_log_is_bounded_and_newest_first();
    test_an_impossible_count_is_clamped();
    test_the_funnel_carries_both_clocks();
    return check_report("what the ADS-B screen says, decided without a "
                        "screen");
}
