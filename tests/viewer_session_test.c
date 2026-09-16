#include "check.h"

#include "viewer_session.h"

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

int main(void) {
    test_a_first_update_is_always_due();
    test_the_heartbeat();
    test_a_change_beats_the_interval();
    test_the_intervals_the_session_uses();
    test_a_run_of_the_loop_publishes_at_the_interval();
    return check_report("when a Viewer metadata update is due");
}
