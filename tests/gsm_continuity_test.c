#include "gsm_continuity.h"
#include "check.h"

#include <stdio.h>

/*
 * Whether consecutive SCH decodes hang together, which is what tells an
 * operator the frame number on screen can be believed.
 *
 * The wrap is the case this was written for. T1 comes round every 3 h 28 m, so
 * a rule that treats 2047 -> 0 as a jump of 2047 backwards is wrong for a few
 * seconds out of every afternoon -- and the only symptom is a warning nobody
 * was there to see (ADR-0012, layer 1).
 */

/* One tick of T1: 1326 frames of 60/13 ms. */
#define TICK GSM_T1_SECONDS

static void test_the_advance_counts_forwards(void) {
    check_int("standing still", gsm_t1_advance(100, 100), 0);
    check_int("one tick on", gsm_t1_advance(100, 101), 1);
    check_int("ten ticks on", gsm_t1_advance(100, 110), 10);
    /* The hyperframe rolls over. This is an advance of one, and reading it as
       anything else is the bug. */
    check_int("2047 to 0 is one tick",
              gsm_t1_advance(GSM_T1_MODULUS - 1, 0), 1);
    check_int("2046 to 1 is three", gsm_t1_advance(GSM_T1_MODULUS - 2, 1), 3);
    /* Genuinely backwards, which the modular reading turns into an enormous
       forward advance -- and an enormous advance is exactly what the check
       below refuses. */
    check_int("a step backwards is nearly a whole hyperframe",
              gsm_t1_advance(100, 99), GSM_T1_MODULUS - 1);
}

static void test_what_the_clock_allows(void) {
    check_int("no time at all still allows a tick",
              gsm_t1_allowed_advance(0.0), 1);
    check_int("a block later, one tick", gsm_t1_allowed_advance(0.0655), 1);
    check_int("just under a tick", gsm_t1_allowed_advance(TICK - 0.1), 1);
    check_int("just over one", gsm_t1_allowed_advance(TICK + 0.1), 2);
    /* Away for a minute: ten ticks have gone by and none of them is a fault. */
    check_int("a minute away", gsm_t1_allowed_advance(60.0), 10);
}

/* The ordinary case: decodes arriving many times a second off one cell. */
static void test_a_steady_run(void) {
    struct gsm_sch_continuity c;
    double now = 0.0;
    int t1 = 500;
    int flagged = 0;

    gsm_continuity_reset(&c);
    for (int block = 0; block < 300; block++) {
        now += 0.0655;
        /* T1 ticks every 6.12 s; at 65.5 ms a block that is every 93rd. */
        t1 = 500 + (int)(now / GSM_T1_SECONDS);
        if (!gsm_continuity_observe(&c, t1, 59, now))
            flagged++;
    }
    check_int("nothing implausible in a steady run", flagged, 0);
    check_int("and the flag is clear at the end", c.implausible, 0);
    check_int("the first decode is never implausible", c.have_last, 1);
}

/* The first decode has nothing to be inconsistent with. */
static void test_the_first_decode(void) {
    struct gsm_sch_continuity c;

    gsm_continuity_reset(&c);
    check_int("accepted", gsm_continuity_observe(&c, 1234, 59, 10.0), 1);
    check_int("and not flagged", c.implausible, 0);
    check_int("remembered", c.last_t1, 1234);
}

/*
 * The wrap end to end: a decode at T1 = 2047 followed 6 s later by one at
 * T1 = 0 is a cell doing exactly what it should.
 */
static void test_the_hyperframe_wrap(void) {
    struct gsm_sch_continuity c;

    gsm_continuity_reset(&c);
    gsm_continuity_observe(&c, GSM_T1_MODULUS - 1, 59, 100.0);
    check_int("the wrap is not a jump",
              gsm_continuity_observe(&c, 0, 59, 100.0 + TICK + 0.5), 1);
    check_int("and nothing is flagged", c.implausible, 0);
    check_int("the new T1 is remembered", c.last_t1, 0);

    /* And on round the far side. */
    check_int("still fine a tick later",
              gsm_continuity_observe(&c, 1, 59, 100.0 + 2.0 * TICK + 0.5), 1);
    check_int("clear", c.implausible, 0);
}

/* A frame number that moved further than the clock allows is a misread field:
   T1 advances at a fixed rate and cannot skip. */
static void test_a_jump_is_flagged(void) {
    struct gsm_sch_continuity c;

    gsm_continuity_reset(&c);
    gsm_continuity_observe(&c, 500, 59, 10.0);
    check_int("a hundred ticks in one block",
              gsm_continuity_observe(&c, 600, 59, 10.0655), 0);
    check_int("flagged", c.implausible, 1);

    /* Backwards is the same fault seen from the other side: modularly it is an
       enormous advance, far more than any gap allows. */
    gsm_continuity_reset(&c);
    gsm_continuity_observe(&c, 500, 59, 10.0);
    check_int("a step backwards", gsm_continuity_observe(&c, 499, 59, 10.0655),
              0);
    check_int("flagged", c.implausible, 1);

    /* And the flag clears again once decodes make sense: it describes the
       last decode, not the session. */
    check_int("the next consistent decode",
              gsm_continuity_observe(&c, 499, 59, 10.2), 1);
    check_int("clears it", c.implausible, 0);
}

/*
 * A long gap is not a fault. This is what the old rule got wrong: it required
 * an advance of at most one whatever the elapsed time, so leaving the view for
 * a minute and coming back reported a jump on a cell that had done nothing but
 * keep time.
 */
static void test_a_long_gap_is_not_a_jump(void) {
    struct gsm_sch_continuity c;

    gsm_continuity_reset(&c);
    gsm_continuity_observe(&c, 500, 59, 10.0);
    check_int("a minute away, ten ticks on",
              gsm_continuity_observe(&c, 510, 59, 70.0), 1);
    check_int("not flagged", c.implausible, 0);

    /* But eleven ticks in a minute is still too many. */
    gsm_continuity_reset(&c);
    gsm_continuity_observe(&c, 500, 59, 10.0);
    check_int("more ticks than the minute allows",
              gsm_continuity_observe(&c, 520, 59, 70.0), 0);
    check_int("flagged", c.implausible, 1);
}

/*
 * A BSIC that changes means a different cell on the same channel, or a decode
 * that was wrong. Either way the identity on screen is not what it claims, and
 * the frame number beside it belongs to something else. Retuning resets this,
 * so a channel change does not trip it.
 */
static void test_a_changed_bsic_is_flagged(void) {
    struct gsm_sch_continuity c;

    gsm_continuity_reset(&c);
    gsm_continuity_observe(&c, 500, 59, 10.0);
    check_int("a different BSIC on the same channel",
              gsm_continuity_observe(&c, 500, 56, 10.0655), 0);
    check_int("flagged", c.implausible, 1);

    gsm_continuity_reset(&c);
    check_int("after retuning, the first decode of the new cell is fine",
              gsm_continuity_observe(&c, 900, 56, 11.0), 1);
    check_int("not flagged", c.implausible, 0);
}

int main(void) {
    test_the_advance_counts_forwards();
    test_what_the_clock_allows();
    test_a_steady_run();
    test_the_first_decode();
    test_the_hyperframe_wrap();
    test_a_jump_is_flagged();
    test_a_long_gap_is_not_a_jump();
    test_a_changed_bsic_is_flagged();

    return check_report("SCH continuity");
}
