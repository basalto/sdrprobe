#include "check.h"

#include "model/tetra_view_model.h"
#include "runtime/app.h"

#include <string.h>

/*
 * What the TETRA screen says, decided without a screen.
 *
 * The funnel is what this is really about. Three things produce an empty
 * identity table and they are not the same: a rate that cannot decode TETRA
 * at all, a carrier that is not TETRA, and a carrier that is TETRA but too
 * weak for its parity to pass. The middle term of the funnel --
 * synchronisation matched, parity did not -- is the only one that separates
 * the last two.
 */

static struct tetra_view tetra;

static struct tetra_view_model build(int rate_supported) {
    struct tetra_view_model out;

    tetra.rate_unsupported = !rate_supported;
    tetra_view_model_build(&tetra, &out);
    return out;
}

static void blank(void) {
    memset(&tetra, 0, sizeof(tetra));
}

/* The identity is carried only once parity has established it, and zeroed
   otherwise -- not left at whatever the session still holds. */
static void test_an_unestablished_identity_is_not_carried(void) {
    struct tetra_view_model m;

    blank();
    tetra.session.mcc = 268;
    tetra.session.mnc = 3;
    tetra.session.colour = 17;
    tetra.session.la = 4375;

    m = build(1);
    check_int("no parity yet, so no identity", m.have_identity, 0);
    check_int("and the colour code is not last minute's", m.colour, 0);
    check_int("nor the location area", m.la, 0);

    tetra.session.have_identity = 1;
    m = build(1);
    check_int("established", m.have_identity, 1);
    check_int("MCC", m.mcc, 268);
    check_int("MNC", m.mnc, 3);
    check_int("colour code", m.colour, 17);
    check_int("location area", m.la, 4375);
}

/*
 * The funnel's three answers to "nothing decoded".
 *
 * Bursts with no blocks at all is a carrier that is not TETRA. Bursts whose
 * synchronisation matched and whose parity failed is TETRA too weak to
 * read. And an unsupported rate is neither -- it is the receiver, and
 * nothing on air could have decoded.
 */
static void test_the_funnel_separates_three_kinds_of_nothing(void) {
    struct tetra_view_model m;

    blank();
    m = build(0);
    check_int("an unsupported rate says so", m.rate_supported, 0);

    tetra.session.bursts_total = 40;
    m = build(1);
    check_int("bursts found", (long)m.bursts_total, 40);
    check_int("but none carried a block", (long)m.blocks_total, 0);
    check_int("and none failed parity either -- not TETRA",
              (long)m.blocks_failed, 0);

    tetra.session.blocks_failed = 40;
    m = build(1);
    check_int("synchronisation matched and parity did not -- TETRA, weak",
              (long)m.blocks_failed, 40);

    tetra.session.blocks_total = 101;
    tetra.session.broadcast_total = 87;
    m = build(1);
    check_int("blocks with parity", (long)m.blocks_total, 101);
    check_int("of which broadcast", (long)m.broadcast_total, 87);
}

/* `lock` is the answer to "is this TETRA at all": 0.80 on a real carrier
   here against under 0.035 for empty spectrum *and* for an FM station. It
   travels as the number, not as a verdict -- a scale is not a classifier. */
static void test_lock_travels_as_a_number(void) {
    struct tetra_view_model m;

    blank();
    tetra.session.lock = 0.80f;
    tetra.session.offset_hz = -1234.5;
    m = build(1);
    check_close("lock", m.lock, 0.80, 1e-6);
    check_close("and the residual offset", m.offset_hz, -1234.5, 1e-6);
}

/* The whole log travels, newest first, clamped to what it holds. */
static void test_the_log_is_whole_and_clamped(void) {
    struct tetra_view_model m;
    int i;

    blank();
    m = build(1);
    check_int("an empty log carries nothing", m.log_count, 0);

    for (i = 0; i < 5; i++) {
        tetra.log[i].colour = 10 + i;
        tetra.log[i].at = (double)i;
    }
    tetra.log_count = 5;
    m = build(1);
    check_int("five rows", m.log_count, 5);
    check_int("newest first, unreordered", m.log[0].colour, 10);
    check_int("and the fifth last", m.log[4].colour, 14);

    tetra.log_count = TETRA_LOG_CAPACITY;
    m = build(1);
    check_int("a full log is carried whole", m.log_count,
              TETRA_VIEW_MODEL_LOG);

    /* A count past the array is a refusal, not a read past the end: the
       count and the contents are written on different lines. */
    tetra.log_count = TETRA_LOG_CAPACITY * 3;
    m = build(1);
    check_int("an impossible count is clamped", m.log_count,
              TETRA_VIEW_MODEL_LOG);
    tetra.log_count = -2;
    m = build(1);
    check_int("and a negative one carries nothing", m.log_count, 0);
}

int main(void) {
    test_an_unestablished_identity_is_not_carried();
    test_the_funnel_separates_three_kinds_of_nothing();
    test_lock_travels_as_a_number();
    test_the_log_is_whole_and_clamped();
    return check_report("what the TETRA screen says, decided without a "
                        "screen");
}
