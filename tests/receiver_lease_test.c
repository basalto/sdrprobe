#include "receiver_lease.h"
#include "check.h"

#include <stdio.h>

/*
 * Who gives the receiver back, and in what order.
 *
 * Nine screens each restored their own tuning and no rule connected them. The
 * overlaps are real -- LTE opens calibration, GSM starts a band scan, the
 * survey asks its candidates again -- and the failure they invite is silent:
 * an owner returning out of turn puts back a frequency belonging to somebody
 * else, and the only symptom is an operator finding the receiver somewhere
 * nobody chose.
 *
 * None of that is reachable from a check while it lives in nine view files
 * behind `if (app->receiver_mode)`. Here it is arithmetic (ADR-0012, layer 1).
 */

static struct receiver_tuning at(uint32_t hz, uint32_t rate) {
    struct receiver_tuning t;

    t.center_hz = hz;
    t.sample_rate_hz = rate;
    return t;
}

/* The three tunings the real overlaps move between. */
#define SCOPE_HZ 100300000U
#define GSM_HZ 948400000U
#define SCAN_HZ 935200000U
#define LTE_HZ 806000000U
#define HOUSE_RATE 2000000U
#define LTE_RATE 1920000U

static void test_one_owner_gets_back_what_it_found(void) {
    struct receiver_lease lease;
    struct receiver_lease_token gsm = { 0 };
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    check_int("nothing borrowed yet", receiver_lease_depth(&lease), 0);
    check_true("a zeroed token holds nothing",
               !receiver_lease_token_active(&gsm));

    check_int("GSM borrows the receiver",
              receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &gsm), 0);
    check_true("and now holds something", receiver_lease_token_active(&gsm));
    check_int("one owner deep", receiver_lease_depth(&lease), 1);

    check_int("GSM asks what to put back",
              receiver_lease_begin_return(&lease, &gsm, &back), 0);
    check_int("which is where the scope was", (long)back.center_hz, SCOPE_HZ);
    check_int("at the rate it was using", (long)back.sample_rate_hz, HOUSE_RATE);
    check_int("and gives it up",
              receiver_lease_finish_return(&lease, &gsm), 0);
    check_int("nothing borrowed again", receiver_lease_depth(&lease), 0);
    check_true("the token holds nothing", !receiver_lease_token_active(&gsm));
}

/* GSM enters from the scope, then starts a band scan. Finishing the scan must
   land on GSM's tuning and not on the scope's -- which is the whole point, and
   the case nine separate return fields could not state. */
static void test_gsm_then_band_scan_unwinds_inwards_first(void) {
    struct receiver_lease lease;
    struct receiver_lease_token gsm = { 0 }, scan = { 0 };
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &gsm);
    /* GSM has tuned to its ARFCN; the scan borrows from there. */
    receiver_lease_acquire(&lease, at(GSM_HZ, HOUSE_RATE), &scan);
    check_int("two owners deep", receiver_lease_depth(&lease), 2);

    check_int("the scan returns first",
              receiver_lease_begin_return(&lease, &scan, &back), 0);
    check_int("to GSM's channel, not the scope's",
              (long)back.center_hz, GSM_HZ);
    receiver_lease_finish_return(&lease, &scan);

    check_int("then GSM returns",
              receiver_lease_begin_return(&lease, &gsm, &back), 0);
    check_int("to where the operator had it", (long)back.center_hz, SCOPE_HZ);
    receiver_lease_finish_return(&lease, &gsm);
    check_int("and the receiver is nobody's", receiver_lease_depth(&lease), 0);
}

/* The rate path. LTE borrows the receiver at 1.92 MS/s (ADR-0014) and
   calibration borrows it from LTE; both halves of the snapshot have to come
   back, which is the bug `cal_return_sample_rate` existed to paper over. */
static void test_the_rate_comes_back_with_the_frequency(void) {
    struct receiver_lease lease;
    struct receiver_lease_token lte = { 0 }, cal = { 0 };
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &lte);
    /* LTE is now at 806 MHz and 1.92 MS/s. Calibration borrows from there. */
    receiver_lease_acquire(&lease, at(LTE_HZ, LTE_RATE), &cal);

    check_int("calibration returns to LTE",
              receiver_lease_begin_return(&lease, &cal, &back), 0);
    check_int("at LTE's carrier", (long)back.center_hz, LTE_HZ);
    check_int("and LTE's rate", (long)back.sample_rate_hz, LTE_RATE);
    receiver_lease_finish_return(&lease, &cal);

    check_int("LTE then returns",
              receiver_lease_begin_return(&lease, &lte, &back), 0);
    check_int("to the scope's frequency", (long)back.center_hz, SCOPE_HZ);
    check_int("and the house rate", (long)back.sample_rate_hz, HOUSE_RATE);
    receiver_lease_finish_return(&lease, &lte);
}

static void test_three_levels_unwind_exactly(void) {
    struct receiver_lease lease;
    struct receiver_lease_token a = { 0 }, b = { 0 }, c = { 0 };
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &a);
    receiver_lease_acquire(&lease, at(GSM_HZ, HOUSE_RATE), &b);
    receiver_lease_acquire(&lease, at(SCAN_HZ, HOUSE_RATE), &c);
    check_int("three deep", receiver_lease_depth(&lease), 3);

    receiver_lease_begin_return(&lease, &c, &back);
    check_int("innermost goes back to the middle",
              (long)back.center_hz, SCAN_HZ);
    receiver_lease_finish_return(&lease, &c);
    receiver_lease_begin_return(&lease, &b, &back);
    check_int("middle goes back to the outer", (long)back.center_hz, GSM_HZ);
    receiver_lease_finish_return(&lease, &b);
    receiver_lease_begin_return(&lease, &a, &back);
    check_int("outer goes back to the start", (long)back.center_hz, SCOPE_HZ);
    receiver_lease_finish_return(&lease, &a);
    check_int("empty", receiver_lease_depth(&lease), 0);
}

/* The refusals. Every one of these is a path that used to be expressible and
   silently wrong. */
static void test_out_of_order_is_refused(void) {
    struct receiver_lease lease;
    struct receiver_lease_token outer = { 0 }, inner = { 0 };
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &outer);
    receiver_lease_acquire(&lease, at(GSM_HZ, HOUSE_RATE), &inner);

    check_true("the outer owner may not return first",
               receiver_lease_begin_return(&lease, &outer, &back) < 0);
    check_true("nor finish", receiver_lease_finish_return(&lease, &outer) < 0);
    check_true("nor commit", receiver_lease_commit(&lease, &outer) < 0);
    check_true("nor cancel", receiver_lease_cancel(&lease, &outer) < 0);
    /* And none of that moved anything: a refusal is not a partial return. */
    check_int("still two deep", receiver_lease_depth(&lease), 2);
    check_true("the outer token is still live",
               receiver_lease_token_active(&outer));
    check_int("and the inner one still returns properly",
              receiver_lease_begin_return(&lease, &inner, &back), 0);
    check_int("to the right place", (long)back.center_hz, GSM_HZ);
}

static void test_a_returned_token_cannot_return_again(void) {
    struct receiver_lease lease;
    struct receiver_lease_token first = { 0 }, second = { 0 };
    struct receiver_lease_token stale;
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &first);
    stale = first;                  /* a copy nobody cleared */
    receiver_lease_finish_return(&lease, &first);
    check_true("returning twice is refused",
               receiver_lease_finish_return(&lease, &first) < 0);

    /*
     * The reason the token carries a generation rather than a depth. A later
     * owner now sits where the first one did; a stale copy naming that slot
     * would return somebody else's tuning, and nothing downstream could tell.
     */
    receiver_lease_acquire(&lease, at(LTE_HZ, LTE_RATE), &second);
    check_true("and a stale copy names the old claim, not the new one",
               receiver_lease_begin_return(&lease, &stale, &back) < 0);
    check_int("the live owner is untouched",
              receiver_lease_begin_return(&lease, &second, &back), 0);
    check_int("with its own snapshot", (long)back.center_hz, LTE_HZ);
}

static void test_the_stack_refuses_to_grow(void) {
    struct receiver_lease lease;
    struct receiver_lease_token token[RECEIVER_LEASE_MAX + 1];
    int i;

    receiver_lease_reset(&lease);
    for (i = 0; i < RECEIVER_LEASE_MAX; i++) {
        token[i].generation = 0;
        check_int("a level fits",
                  receiver_lease_acquire(&lease, at(SCOPE_HZ + (uint32_t)i,
                                                    HOUSE_RATE), &token[i]), 0);
    }
    token[RECEIVER_LEASE_MAX].generation = 0;
    check_true("one past the top is refused",
               receiver_lease_acquire(&lease, at(GSM_HZ, HOUSE_RATE),
                                      &token[RECEIVER_LEASE_MAX]) < 0);
    /* A refused acquire has borrowed nothing, so the caller must not go on to
       return it -- and cannot, because the token is inactive. */
    check_true("and hands back nothing",
               !receiver_lease_token_active(&token[RECEIVER_LEASE_MAX]));
    check_int("the stack is unchanged", receiver_lease_depth(&lease),
              RECEIVER_LEASE_MAX);
}

/* A retune that the receiver refused. `retune_receiver()` has already put the
   hardware back, so the snapshot describes a borrowing that never happened. */
static void test_cancel_removes_only_the_newest(void) {
    struct receiver_lease lease;
    struct receiver_lease_token outer = { 0 }, inner = { 0 };
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &outer);
    receiver_lease_acquire(&lease, at(GSM_HZ, HOUSE_RATE), &inner);
    check_int("the failed acquire is cancelled",
              receiver_lease_cancel(&lease, &inner), 0);
    check_int("back to one owner", receiver_lease_depth(&lease), 1);
    check_true("and its token is spent",
               !receiver_lease_token_active(&inner));
    check_int("the survivor still returns properly",
              receiver_lease_begin_return(&lease, &outer, &back), 0);
    check_int("to its own snapshot", (long)back.center_hz, SCOPE_HZ);
}

/* Two-phase, because the receiver can refuse a frequency it accepted a minute
   ago. Popping on the way in would leave nothing to retry with. */
static void test_a_failed_restore_is_retryable(void) {
    struct receiver_lease lease;
    struct receiver_lease_token survey = { 0 };
    struct receiver_tuning back;

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &survey);

    check_int("ask what to put back",
              receiver_lease_begin_return(&lease, &survey, &back), 0);
    check_int("asking does not give it up", receiver_lease_depth(&lease), 1);
    check_true("and the token is still live",
               receiver_lease_token_active(&survey));
    /* Pretend the retune failed. Nothing was popped, so ask again. */
    check_int("ask again after a refusal",
              receiver_lease_begin_return(&lease, &survey, &back), 0);
    check_int("same answer", (long)back.center_hz, SCOPE_HZ);
    check_int("and this time it took",
              receiver_lease_finish_return(&lease, &survey), 0);
    check_int("now it is given up", receiver_lease_depth(&lease), 0);
}

/* "Open waterfall": the operator picked a candidate, the receiver is tuned to
   it on purpose, and putting the sweep's tuning back is the opposite of what
   was asked for. */
static void test_commit_keeps_the_tuning(void) {
    struct receiver_lease lease;
    struct receiver_lease_token survey = { 0 };

    receiver_lease_reset(&lease);
    receiver_lease_acquire(&lease, at(SCOPE_HZ, HOUSE_RATE), &survey);
    check_int("the handoff commits", receiver_lease_commit(&lease, &survey), 0);
    check_int("the receiver is nobody's", receiver_lease_depth(&lease), 0);
    check_true("and the token is spent",
               !receiver_lease_token_active(&survey));
}

/*
 * PPM is not here, and this is the check that it stays that way.
 *
 * Applying a calibration is deliberate persistent state: it must survive every
 * return, including the return of the calibration overlay that measured it.
 * A snapshot with a third field would be rolled back with the tuning by
 * whoever wrote the restore, and the symptom -- a correction silently undone
 * on the way out of the screen that suggested it -- is one nobody would
 * connect to a stack.
 */
static void test_ppm_is_not_leased(void) {
    check_size("a snapshot is a frequency and a rate, and nothing else",
               sizeof(struct receiver_tuning), 2 * sizeof(uint32_t));
}

int main(void) {
    test_one_owner_gets_back_what_it_found();
    test_gsm_then_band_scan_unwinds_inwards_first();
    test_the_rate_comes_back_with_the_frequency();
    test_three_levels_unwind_exactly();
    test_out_of_order_is_refused();
    test_a_returned_token_cannot_return_again();
    test_the_stack_refuses_to_grow();
    test_cancel_removes_only_the_newest();
    test_a_failed_restore_is_retryable();
    test_commit_keeps_the_tuning();
    test_ppm_is_not_leased();
    return check_report("the receiver lease: who borrowed the tuning, "
                        "and in what order they give it back");
}
