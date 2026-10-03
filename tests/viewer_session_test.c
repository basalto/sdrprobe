#include "check.h"

#include "server/viewer_session.h"

/*
 * When a metadata State update is due.
 *
 * This is one `if` in the serve loop, and it is out here because that loop
 * takes `struct app` and nothing windowless can reach it (ADR-0012). It is
 * also the whole of a measured fault: `viewer_link_publish_*()` queues rather
 * than sends, so a publish always leaves a slot with unsent bytes,
 * `viewer_link_poll()` then registers the client for writing, and `select()`
 * returns at once because a loopback socket is writable. The 20 ms poll
 * timeout is the loop's only pacing, so publishing unconditionally meant it
 * could never be reached.
 *
 * Measured before the fix: ~100 000 loop iterations a second and 98.6% of a
 * core for a client subscribed to `receiver_state` alone, against 9.8% with
 * no client at all (`.scratch/web-visualization/issues/10-*`). The numbers
 * are in that ticket; what is pinned here is the decision that produced them.
 */

/* Never published is not "published at time zero", and a loop timing from its
   own start reaches a real 0.0 -- so the sentinel has to be outside the range
   a clock can take, and a check that used 0.0 for both would pass either
   way. */
static void test_a_first_update_is_always_due(void) {
    check_int("never published, at the very start of the clock",
              viewer_update_due(0.0, -1.0, 0.25, 0), 1);
    check_int("never published, later",
              viewer_update_due(9999.0, -1.0, 0.25, 0), 1);
    check_int("and 0.0 is a real time, not a sentinel",
              viewer_update_due(0.0, 0.0, 0.25, 0), 0);
}

/* The interval, from both sides of it. */
static void test_the_heartbeat(void) {
    check_int("just after publishing, nothing is due",
              viewer_update_due(10.0, 10.0, 0.25, 0), 0);
    check_int("a tenth of a second later, still not",
              viewer_update_due(10.1, 10.0, 0.25, 0), 0);
    check_int("at the interval exactly, it is",
              viewer_update_due(10.25, 10.0, 0.25, 0), 1);
    check_int("and past it",
              viewer_update_due(10.9, 10.0, 0.25, 0), 1);
}

/*
 * **A change does not wait for the interval.** This is what makes a retune
 * immediate: the caller's `changed` is the tuning generation differing from
 * the one last published, and a Viewer must not draw a quarter second of
 * measurements under the previous frequency.
 */
static void test_a_change_beats_the_interval(void) {
    check_int("a change in the same instant it was published",
              viewer_update_due(10.0, 10.0, 0.25, 1), 1);
    check_int("and with no time passed at all",
              viewer_update_due(0.0, 0.0, 1000.0, 1), 1);
}

/*
 * The two intervals the session uses, asserted here rather than restated:
 * a check naming 0.25 of its own would agree with itself while the program
 * published at some other rate.
 */
static void test_the_intervals_the_session_uses(void) {
    check_true("the receiver's state is well under a block at 2 MS/s",
               VIEWER_SESSION_STATE_INTERVAL_SECONDS < 1.0 / 15.26 * 10.0);
    check_true("and not so fast that it becomes the loop's clock",
               VIEWER_SESSION_STATE_INTERVAL_SECONDS >= 1.0 / 15.26);
    check_true("health is no faster than its own CPU sample",
               VIEWER_SESSION_HEALTH_INTERVAL_SECONDS >= 1.0);

    /* What the fault was: at these intervals a loop that idles between
       updates publishes tens per second, not tens of thousands. */
    check_true("under a hundred receiver_state updates a second",
               1.0 / VIEWER_SESSION_STATE_INTERVAL_SECONDS < 100.0);
    check_true("and under ten of link_health",
               1.0 / VIEWER_SESSION_HEALTH_INTERVAL_SECONDS < 10.0);
}

/* A loop that publishes on every pass is the fault; walk one and count. */
static void test_a_run_of_the_loop_publishes_at_the_interval(void) {
    double published_at = -1.0;
    int sent = 0;
    int pass;

    /*
     * Ten seconds of a loop free-running at 100 kHz -- what was measured.
     * The clock is `pass * 10 us` rather than a `now += 0.00001`
     * accumulator, which lands on 1000001 passes and would have made this a
     * check about floating-point addition.
     */
    for (pass = 0; pass < 1000000; pass++) {
        double now = pass * 0.00001;

        if (viewer_update_due(now, published_at,
                              VIEWER_SESSION_STATE_INTERVAL_SECONDS, 0)) {
            sent++;
            published_at = now;
        }
    }
    check_int("a million passes send forty updates, not a million", sent, 40);
}

/*
 * `--duration`: never read inside this loop until a live test for a
 * separate ticket sat past it -- `sdrprobe.c`'s other headless loop
 * (decode/playback) has always honoured its own duration, and this one
 * checked only `stop_requested()` (SIGINT/SIGTERM). Every `server
 * --duration N` run before that fix outlived N silently.
 */
static void test_no_budget_means_no_duration_limit(void) {
    check_int("0 (options.h's own \"unset\") never elapses",
              viewer_duration_elapsed(9999.0, 0.0), 0);
    check_int("neither does a negative value",
              viewer_duration_elapsed(9999.0, -1.0), 0);
}

static void test_the_duration_budget(void) {
    check_int("well before the budget, not elapsed",
              viewer_duration_elapsed(1.0, 5.0), 0);
    check_int("just before it, still not",
              viewer_duration_elapsed(4.999, 5.0), 0);
    check_int("at it exactly, elapsed",
              viewer_duration_elapsed(5.0, 5.0), 1);
    check_int("and past it",
              viewer_duration_elapsed(5.1, 5.0), 1);
    check_int("at the very start of the clock, a budget of 0 never trips",
              viewer_duration_elapsed(0.0, 0.0), 0);
}

/*
 * Every stream in the enum says what paces it.
 *
 * This is the enumeration an inline `if` cannot have, and it is the check
 * ticket 07's publish spin got past: the fault was fixed once for
 * `receiver_state` (ticket 10), the fix produced a named predicate covered
 * by this very suite, and the two new streams were then written with a bare
 * inline `if` instead. A guideline is weaker than a fix that was read,
 * understood and still not applied -- so this walks the enum.
 *
 * `VIEWER_PACED_UNKNOWN` is what a stream nobody has paced gets, so adding
 * one to `enum viewer_stream` without a row in `viewer_stream_pacing()`
 * fails here rather than shipping.
 */
static void test_every_stream_says_what_paces_it(void) {
    int s;
    int on_data = 0, on_time = 0, on_demand = 0;

    for (s = 0; s < VIEWER_STREAM_COUNT; s++) {
        enum viewer_stream_pacing p =
            viewer_stream_pacing((enum viewer_stream)s);

        /* By index rather than by name: linking viewer_link.c for the
           spelling would pull sockets and the embedded page into a suite of
           pure predicates. check-viewer-link pins the names. */
        check_msg(p != VIEWER_PACED_UNKNOWN,
                  "stream %d says what paces it\n", s);
        if (p == VIEWER_PACED_ON_DATA)
            on_data++;
        else if (p == VIEWER_PACED_ON_TIME)
            on_time++;
        else
            on_demand++;
    }

    /*
     * And the split, because the two families are not paced the same way
     * and one predicate that flattened them would either spin the metadata
     * streams or stall the data ones.
     */
    /*
     * Stated as counts rather than as `COUNT - n`, so adding a stream makes
     * somebody say which family it joined. That is not hypothetical: adding
     * `settings_state` failed this check until the on-time count was raised
     * from 2 to 3, which is the moment to think about whether a panel that
     * changes when somebody *types* should wait for a sample block. It
     * should not.
     */
    check_int("the eleven view streams are paced on data", on_data, 11);
    /* receiver_state, link_health, the Settings and Calibration panels, and
       the three FM analysis charts -- the charts are measurement arrays but
       paced on a 4 Hz heartbeat rather than per block, deliberately (a human
       reading a chart does not need 15 Hz, and the audio spectrum recomputes
       only at that cadence). That is the "which family did it join" decision
       this count forces. */
    check_int("the panels and the FM charts are paced on time", on_time, 7);
    check_int("and command_result is a reply, not a stream", on_demand, 1);
    check_int("which is all of them", on_data + on_time + on_demand,
              VIEWER_STREAM_COUNT);
    check_str("which is what it is called",
              viewer_pacing_name(
                  viewer_stream_pacing(VIEWER_STREAM_COMMAND_RESULT)),
              "on-demand");
}

/*
 * What the two reasons do, and the one they must not do.
 *
 * A data stream is deliberately not also given an interval: a timer there
 * would republish numbers no new block produced, and a publish **queues into
 * a replaceable slot rather than sending**, so the loop's `select()` stays
 * ready and it never waits. Measured at **234216 messages in 10 seconds**
 * against the 15.26/s a live receiver's block rate caps it at.
 */
static void test_a_data_stream_is_not_also_on_a_timer(void) {
    int s;

    for (s = 0; s < VIEWER_STREAM_COUNT; s++) {
        enum viewer_stream stream = (enum viewer_stream)s;

        if (viewer_stream_pacing(stream) != VIEWER_PACED_ON_DATA)
            continue;
        /* No block this pass, an hour since the last publish, and the
           caller shouting that something changed: still not due. */
        check_msg(!viewer_publish_due(stream, 0, 3600.0, 0.0, 0.25, 1),
                  "stream %d waits for a block, whatever the clock says\n", s);
        check_msg(viewer_publish_due(stream, 1, 3600.0, 3599.99, 0.25, 0),
                  "stream %d goes out when one arrives\n", s);
    }
}

/* The on-time streams keep `viewer_update_due()`'s three reasons -- changed,
   never sent, or the interval has passed -- and are not held back by a pass
   with no block in it, which is the whole reason they are not on-data. */
static void test_an_on_time_stream_does_not_wait_for_a_block(void) {
    check_true("a retune goes out at once, block or no block",
               viewer_publish_due(VIEWER_STREAM_RECEIVER_STATE, 0, 10.0, 9.99,
                                  0.25, 1));
    check_true("and so does the first one ever",
               viewer_publish_due(VIEWER_STREAM_RECEIVER_STATE, 0, 10.0, -1.0,
                                  0.25, 0));
    check_true("and the quarter-second heartbeat",
               viewer_publish_due(VIEWER_STREAM_RECEIVER_STATE, 0, 10.0, 9.5,
                                  0.25, 0));
    check_true("but not twice inside it",
               !viewer_publish_due(VIEWER_STREAM_RECEIVER_STATE, 0, 10.0,
                                   9.9, 0.25, 0));
}

/* A reply is sent by whoever answers the command. The loop never publishes
   it, whatever it is asked. */
static void test_a_reply_is_never_due(void) {
    check_true("not on a block",
               !viewer_publish_due(VIEWER_STREAM_COMMAND_RESULT, 1, 10.0,
                                   -1.0, 0.0, 0));
    check_true("not on a timer",
               !viewer_publish_due(VIEWER_STREAM_COMMAND_RESULT, 0, 3600.0,
                                   0.0, 0.25, 1));
}

int main(void) {
    test_every_stream_says_what_paces_it();
    test_a_data_stream_is_not_also_on_a_timer();
    test_an_on_time_stream_does_not_wait_for_a_block();
    test_a_reply_is_never_due();
    test_a_first_update_is_always_due();
    test_the_heartbeat();
    test_a_change_beats_the_interval();
    test_the_intervals_the_session_uses();
    test_a_run_of_the_loop_publishes_at_the_interval();
    test_no_budget_means_no_duration_limit();
    test_the_duration_budget();
    return check_report("what paces each Viewer stream, and when one is due");
}
