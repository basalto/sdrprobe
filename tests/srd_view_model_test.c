#include "check.h"

#include "model/srd_view_model.h"
#include "runtime/app.h"

#include <string.h>

/*
 * What the SRD screen says, decided without a screen.
 *
 * Two things here are decisions rather than readings. Whether the receiver
 * is in the allocation at all -- 430-440 MHz at 1 MS/s or more -- because
 * an empty table means something entirely different otherwise. And the two
 * enums the table's columns print, which now have names: an enum that
 * crossed the wire as an ordinal is what drew the survey's marks swapped
 * for months (`web-visualization/15`).
 */

static struct srd_view srd;

static struct srd_view_model build(uint32_t hz, uint32_t rate) {
    struct srd_view_model out;

    srd_view_model_build(&srd, hz, rate, &out);
    return out;
}

static void blank(void) {
    memset(&srd, 0, sizeof(srd));
}

/*
 * The allocation is ten megahertz and a receiver hears two of it, so
 * "ready" asks whether the tuning is *inside* the band -- not whether the
 * whole band fits, which would need 10 MS/s and refuse everything.
 */
static void test_ready_is_inside_the_band_not_all_of_it(void) {
    struct srd_view_model m;

    blank();
    m = build(434000000u, 2000000u);
    check_int("in the middle of the allocation", m.ready, 1);

    m = build(430000000u, 2000000u);
    check_int("at the lower edge", m.ready, 1);
    m = build(440000000u, 2000000u);
    check_int("at the upper edge", m.ready, 1);

    m = build(429000000u, 2000000u);
    check_int("below it", m.ready, 0);
    m = build(441000000u, 2000000u);
    check_int("above it", m.ready, 0);

    m = build(434000000u, 200000u);
    check_int("in band but far too slow", m.ready, 0);
}

/*
 * "Nothing heard yet" and "a transmitter exactly on the tuning" are
 * different answers, and a bare offset of zero cannot tell them apart.
 */
static void test_no_carrier_is_not_a_carrier_at_zero(void) {
    struct srd_view_model m;

    blank();
    m = build(434000000u, 2000000u);
    check_int("nothing found yet", m.have_carrier, 0);
    check_close("and the offset is not a claim", m.last_carrier_offset_hz,
                0.0, 1e-9);

    srd.session.transmissions_found = 1;
    srd.session.last_carrier_hz = 0.0;
    m = build(434000000u, 2000000u);
    check_int("one found, exactly on the tuning", m.have_carrier, 1);
    check_close("offset zero, and it means it", m.last_carrier_offset_hz,
                0.0, 1e-9);

    srd.session.last_carrier_hz = 616600.0;
    m = build(434000000u, 2000000u);
    check_close("and an offset passes through",
                m.last_carrier_offset_hz, 616600.0, 1e-6);
}

/*
 * Every value of both enums has a name, walked rather than sampled.
 *
 * `unknown` and `UNDECODED` are deliberately different: a row with no kind
 * at all is not the same as a burst that was detected and decoded to
 * nothing, and the window has always drawn them apart.
 */
static void test_every_kind_and_modulation_has_a_name(void) {
    check_str("full", srd_frame_kind_name(SRD_FRAME_FULL), "FULL");
    check_str("repeat", srd_frame_kind_name(SRD_FRAME_REPEAT), "REPEAT");
    check_str("generic", srd_frame_kind_name(SRD_FRAME_GENERIC), "GENERIC");
    check_str("wakeup", srd_frame_kind_name(SRD_FRAME_FSK_DETECTED),
              "WAKEUP");
    check_str("undecoded", srd_frame_kind_name(SRD_FRAME_UNDECODED),
              "UNDECODED");
    check_str("and no kind at all is not 'undecoded'",
              srd_frame_kind_name(SRD_FRAME_UNKNOWN), "unknown");

    check_str("on-off keying", srd_modulation_name(SRD_MOD_OOK), "OOK");
    check_str("two-level FSK", srd_modulation_name(SRD_MOD_FSK2), "2FSK");
}

/* The log travels whole, newest first, clamped to what it holds -- and the
   frequency each row carries is the absolute one fixed when it was
   written, not an offset that a later retune would drag along. */
static void test_the_log_carries_absolute_frequencies(void) {
    struct srd_view_model m;
    int i;

    blank();
    for (i = 0; i < 3; i++) {
        srd.log[i].absolute_hz = 434416600.0 + i;
        srd.log[i].kind = SRD_FRAME_REPEAT;
        srd.log[i].modulation = SRD_MOD_OOK;
        srd.log[i].byte_count = 3;
    }
    srd.log_count = 3;

    m = build(433800000u, 2000000u);
    check_int("three rows", m.log_count, 3);
    check_close("and the first is where it was heard, not where we are now",
                m.log[0].absolute_hz, 434416600.0, 1e-3);
    check_str("its kind, by name",
              srd_frame_kind_name(m.log[0].kind), "REPEAT");
    check_str("its modulation, by name",
              srd_modulation_name(m.log[0].modulation), "OOK");

    srd.log_count = SRD_LOG_CAPACITY * 2;
    m = build(433800000u, 2000000u);
    check_int("an impossible count is clamped", m.log_count,
              SRD_VIEW_MODEL_LOG);
    srd.log_count = -1;
    m = build(433800000u, 2000000u);
    check_int("and a negative one carries nothing", m.log_count, 0);
}

int main(void) {
    test_ready_is_inside_the_band_not_all_of_it();
    test_no_carrier_is_not_a_carrier_at_zero();
    test_every_kind_and_modulation_has_a_name();
    test_the_log_carries_absolute_frequencies();
    return check_report("what the SRD screen says, decided without a screen");
}
