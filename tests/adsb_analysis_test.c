#include "adsb_analysis.h"
#include "check.h"

#include <stdio.h>
#include <string.h>

/*
 * What the ADS-B view decides, as opposed to what it draws.
 *
 * The analysis charts carry no proof of which frame they are describing, so a
 * Hold that quietly lets the next frame through is invisible: the envelope and
 * the confidence bars go on looking plausible. And the funnel is the
 * operator's evidence about whether the antenna or the demodulator is losing
 * frames, so counters that describe an impossible run send them after the
 * wrong problem (ADR-0012, layer 1).
 */

#define MODE_S_HZ 1090000000U

static struct adsb_frame_trace trace_of(int valid, int crc_ok, int bits) {
    struct adsb_frame_trace t;

    memset(&t, 0, sizeof(t));
    t.valid = valid;
    t.crc_ok = crc_ok;
    t.bit_count = bits;
    return t;
}

/* Whether Mode S could be there at all. */
static void test_receiver_ready(void) {
    check_int("on frequency at 2 MS/s",
              adsb_receiver_ready(MODE_S_HZ, 2000000U, MODE_S_HZ), 1);
    check_int("a little off is still fine",
              adsb_receiver_ready(MODE_S_HZ + 100000U, 2000000U, MODE_S_HZ), 1);
    check_int("and a little off the other way",
              adsb_receiver_ready(MODE_S_HZ - 100000U, 2000000U, MODE_S_HZ), 1);
    check_int("exactly at the tolerance is not",
              adsb_receiver_ready(MODE_S_HZ + ADSB_TUNED_TOLERANCE_HZ,
                                  2000000U, MODE_S_HZ),
              0);
    check_int("the FM band is not Mode S",
              adsb_receiver_ready(100000000U, 2000000U, MODE_S_HZ), 0);
    /* A pulse is half a microsecond. Below 2 MS/s there is less than one
       sample per pulse, so nothing that arrives can be a frame however well
       the receiver is tuned. */
    check_int("on frequency but too slow",
              adsb_receiver_ready(MODE_S_HZ, 1000000U, MODE_S_HZ), 0);
    check_int("exactly 2 MS/s is enough",
              adsb_receiver_ready(MODE_S_HZ, ADSB_MIN_SAMPLE_RATE, MODE_S_HZ),
              1);

    /* The panels follow: analysis mode alone is not enough. */
    check_int("analysis mode off", adsb_analysis_visible(0, 1), 0);
    check_int("analysis mode on but off frequency",
              adsb_analysis_visible(1, 0), 0);
    check_int("both", adsb_analysis_visible(1, 1), 1);
}

/*
 * Which trace is kept. The latest is whatever last arrived, good or bad,
 * because a frame that failed its CRC is the one worth looking at; the good
 * one only moves when a frame passes.
 */
static void test_keeping_traces(void) {
    struct adsb_frame_trace latest = trace_of(0, 0, 0);
    struct adsb_frame_trace good = trace_of(0, 0, 0);
    struct adsb_frame_trace passed = trace_of(1, 1, 112);
    struct adsb_frame_trace failed = trace_of(1, 0, 56);
    struct adsb_frame_trace nothing = trace_of(0, 0, 0);

    adsb_trace_keep(&latest, &good, &passed);
    check_int("a good frame becomes the latest", latest.bit_count, 112);
    check_int("and the last good one", good.bit_count, 112);

    adsb_trace_keep(&latest, &good, &failed);
    check_int("a failed frame becomes the latest", latest.bit_count, 56);
    check_int("but does not replace the last good one", good.bit_count, 112);
    check_int("which is still marked valid", good.valid, 1);

    /* Most blocks contain no preamble at all. Those must leave the charts
       alone rather than blanking them between frames. */
    adsb_trace_keep(&latest, &good, &nothing);
    check_int("a block with nothing in it leaves the latest alone",
              latest.bit_count, 56);
    check_int("and the last good one", good.bit_count, 112);
}

/*
 * The latch. Holding pins the last frame that *passed* its CRC, and the flood
 * of failed attempts between good frames must not get through it -- which is
 * the whole reason it exists. Most attempts fail; without the hold the charts
 * flick to noise between one readable frame and the next, which is exactly
 * when an operator wants to look at one.
 */
static void test_hold_keeps_failed_frames_off_the_charts(void) {
    struct adsb_frame_trace latest = trace_of(0, 0, 0);
    struct adsb_frame_trace good = trace_of(0, 0, 0);
    struct adsb_frame_trace first = trace_of(1, 1, 112);
    const struct adsb_frame_trace *shown;

    adsb_trace_keep(&latest, &good, &first);
    shown = adsb_trace_shown(&latest, &good, 0);
    check_int("not holding: the latest", shown->bit_count, 112);

    for (int i = 0; i < 20; i++) {
        struct adsb_frame_trace failed = trace_of(1, 0, 56 + i);
        adsb_trace_keep(&latest, &good, &failed);
        shown = adsb_trace_shown(&latest, &good, 1);
        check_msg(shown->bit_count == 112,
                  "failed attempt %d got through the hold: charts show %d "
                  "bits\n",
                  i, shown->bit_count);
        check_msg(shown->crc_ok,
                  "the held trace stopped being a frame that passed\n");
    }
    shown = adsb_trace_shown(&latest, &good, 0);
    check_int("letting go returns to the latest attempt", shown->bit_count, 75);

    /* A *good* frame does replace it: "hold last good" means the last good
       one, not the one that was good when the button was pressed. Pinning a
       single frame across a whole session would be a different feature, and
       the button does not claim it. */
    {
        struct adsb_frame_trace better = trace_of(1, 1, 88);
        adsb_trace_keep(&latest, &good, &better);
        shown = adsb_trace_shown(&latest, &good, 1);
        check_int("a newer good frame is what is held", shown->bit_count, 88);
    }
}

/* Holding before anything has passed its CRC must show the latest rather than
   an empty trace: a blank chart with the button lit reads as a broken view. */
static void test_hold_with_nothing_to_hold(void) {
    struct adsb_frame_trace latest = trace_of(1, 0, 56);
    struct adsb_frame_trace good = trace_of(0, 0, 0);
    const struct adsb_frame_trace *shown = adsb_trace_shown(&latest, &good, 1);

    check_int("holding nothing shows the latest attempt", shown->bit_count, 56);
    check_int("and it is a real trace", shown->valid, 1);
}

/* The message log: newest first, oldest dropped. */
static void test_the_log(void) {
    static struct adsb_log_entry log[ADSB_LOG_CAPACITY];
    int count = 0;

    for (int i = 0; i < 5; i++) {
        struct adsb_log_entry entry;
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.icao, sizeof(entry.icao), "%06X", i);
        entry.highlight = 1;
        adsb_log_push(log, &count, &entry);
    }
    check_int("five rows", count, 5);
    check_str("the newest is first", log[0].icao, "000004");
    check_str("the oldest is last", log[4].icao, "000000");

    adsb_log_fade(log, count);
    check_int("fading clears the newest", log[0].highlight, 0);
    check_int("and the oldest", log[4].highlight, 0);

    /* Fill it past capacity: the count stops, the oldest falls off the end,
       and nothing is written past the array. */
    for (int i = 5; i < ADSB_LOG_CAPACITY + 40; i++) {
        struct adsb_log_entry entry;
        memset(&entry, 0, sizeof(entry));
        snprintf(entry.icao, sizeof(entry.icao), "%06X", i);
        adsb_log_push(log, &count, &entry);
    }
    check_int("the log stops at its capacity", count, ADSB_LOG_CAPACITY);
    {
        char newest[8];
        char oldest[8];
        snprintf(newest, sizeof(newest), "%06X", ADSB_LOG_CAPACITY + 39);
        snprintf(oldest, sizeof(oldest), "%06X", 40);
        check_str("the newest is still first", log[0].icao, newest);
        check_str("and the oldest surviving row is last",
                  log[ADSB_LOG_CAPACITY - 1].icao, oldest);
    }
}

/* The funnel: preambles found, attempts made, CRCs failed, messages decoded. */
static void test_the_funnel(void) {
    struct adsb_demod_stats totals;
    struct adsb_demod_stats block;

    memset(&totals, 0, sizeof(totals));
    memset(&block, 0, sizeof(block));
    block.preambles = 40;
    block.attempts = 12;
    block.crc_failed = 9;
    block.decoded = 3;

    for (int i = 0; i < 10; i++)
        adsb_totals_add(&totals, &block);
    check_int("preambles accumulate", (long)totals.preambles, 400);
    check_int("attempts accumulate", (long)totals.attempts, 120);
    check_int("failures accumulate", (long)totals.crc_failed, 90);
    check_int("decodes accumulate", (long)totals.decoded, 30);
    check_int("and the funnel narrows the way it should",
              adsb_funnel_is_consistent(&totals), 1);

    /* An empty run is consistent -- nothing arrived, nothing is claimed. */
    memset(&totals, 0, sizeof(totals));
    check_int("nothing seen is consistent", adsb_funnel_is_consistent(&totals),
              1);

    /* The shapes that would be a lie. Each of these has been drawn on screen
       as a funnel and read as evidence about an antenna. */
    memset(&totals, 0, sizeof(totals));
    totals.preambles = 10;
    totals.attempts = 12;
    check_int("more attempts than preambles",
              adsb_funnel_is_consistent(&totals), 0);

    memset(&totals, 0, sizeof(totals));
    totals.preambles = 100;
    totals.attempts = 10;
    totals.crc_failed = 11;
    check_int("more failures than attempts",
              adsb_funnel_is_consistent(&totals), 0);

    memset(&totals, 0, sizeof(totals));
    totals.preambles = 100;
    totals.attempts = 10;
    totals.crc_failed = 6;
    totals.decoded = 6;
    check_int("a frame counted as both failed and decoded",
              adsb_funnel_is_consistent(&totals), 0);

    memset(&totals, 0, sizeof(totals));
    totals.preambles = 100;
    totals.attempts = 10;
    totals.crc_failed = 4;
    totals.decoded = 6;
    check_int("and the same run adding up exactly",
              adsb_funnel_is_consistent(&totals), 1);
}

int main(void) {
    test_receiver_ready();
    test_keeping_traces();
    test_hold_keeps_failed_frames_off_the_charts();
    test_hold_with_nothing_to_hold();
    test_the_log();
    test_the_funnel();

    return check_report("ADS-B analysis");
}
