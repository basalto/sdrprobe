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
static int receiver_mode = 1;

static struct srd_view_model build(uint32_t hz, uint32_t rate) {
    struct srd_view_model out;

    srd_view_model_build(&srd, hz, rate, receiver_mode, &out);
    return out;
}

static void blank(void) {
    memset(&srd, 0, sizeof(srd));
    receiver_mode = 1;
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
                m.log[0].entry.absolute_hz, 434416600.0, 1e-3);
    check_str("its kind, by name",
              srd_frame_kind_name(m.log[0].entry.kind), "REPEAT");
    check_str("its modulation, by name",
              srd_modulation_name(m.log[0].entry.modulation), "OOK");

    srd.log_count = SRD_LOG_CAPACITY * 2;
    m = build(433800000u, 2000000u);
    check_int("an impossible count is clamped", m.log_count,
              SRD_VIEW_MODEL_LOG);
    srd.log_count = -1;
    m = build(433800000u, 2000000u);
    check_int("and a negative one carries nothing", m.log_count, 0);
}

/*
 * Whose problem an empty table is.
 *
 * Two answers and only one is actionable, which is why they are two: a
 * receiver pointed elsewhere can be retuned from this screen, and a capture
 * holds the one tuning it was taken at. The window drew two sentences here
 * and the browser drew one for both, which is the drift
 * `web-visualization/16` exists to stop.
 */
static void test_not_ready_says_whose_problem_it_is(void) {
    struct srd_view_model m;

    blank();
    m = build(434000000u, 2000000u);
    check_str("in band", srd_readiness_name(m.readiness), "ready");

    m = build(100000000u, 2000000u);
    check_str("a receiver pointed elsewhere",
              srd_readiness_name(m.readiness), "receiver-elsewhere");

    receiver_mode = 0;
    m = build(100000000u, 2000000u);
    check_str("a capture taken elsewhere",
              srd_readiness_name(m.readiness), "capture-elsewhere");
    /* And a capture that happens to be in band is simply ready: there is
       nothing to retune, and nothing to apologise for either. */
    m = build(434000000u, 2000000u);
    check_str("a capture taken here", srd_readiness_name(m.readiness),
              "ready");
}

/*
 * The parameters panel, and the one guard in it worth having once.
 *
 * `have_parameters` is separate from a zero chip period because "nothing
 * heard yet" and "a measurement that came back zero" are different answers,
 * and the chip *rate* is carried rather than divided out by each reader --
 * the division by zero is the part that must not be written twice.
 */
static void test_the_parameters_panel_is_decided_here(void) {
    struct srd_view_model m;

    blank();
    m = build(434000000u, 2000000u);
    check_int("nothing heard, nothing to describe", m.have_parameters, 0);

    srd.session.last_chip_us = 500.0;
    srd.session.last_over_floor_db = 47.3;
    m = build(434000000u, 2000000u);
    check_int("a measurement is something to describe", m.have_parameters, 1);
    check_close("500 us is 2000 chips a second", m.last_chip_rate_hz, 2000.0,
                1e-9);
    check_close("and how far over the floor it was", m.last_over_floor_db,
                47.3, 1e-6);
    check_str("the line code, worded once", m.line_code,
              "Manchester (Thomas)");

    srd.polarity = SRD_MANCHESTER_IEEE;
    m = build(434000000u, 2000000u);
    check_str("the other polarity", m.line_code, "Manchester (IEEE)");

    /* A period of zero is a refusal, not a division. */
    srd.session.last_chip_us = 0.0;
    srd.log_count = 1;
    m = build(434000000u, 2000000u);
    check_int("a row alone is also something to describe",
              m.have_parameters, 1);
    check_close("no period, no rate", m.last_chip_rate_hz, 0.0, 1e-12);
}

/*
 * What a row says about itself.
 *
 * Seven cases, chosen here rather than inside two drawings -- three of them
 * read protocol fields out of the payload by byte offset, and a byte offset
 * written into a drawing is protocol knowledge no check can reach.
 */
static void test_a_row_says_what_it_is(void) {
    struct srd_view_model m;
    static const uint8_t full[10] = { 0x3F, 0x04, 0x0B, 0x69, 0xBB,
                                      0xCC, 0x9F, 0x42, 0xF2, 0xD4 };
    /* The 2-FSK remote's prefix A, 0x27E57B, and fourteen bytes -- which is
       what `srd_device_type_of()` requires and what the marker's own copy of
       this test did *not*: it checked two bytes and ignored the kind. */
    static const uint8_t fsk[14] = { 0x27, 0xE5, 0x7B, 0x11, 0xDE, 0xAD,
                                     0xBE, 0xEF, 0x00, 0x2A, 0, 0, 0, 0 };

    blank();
    srd.log_count = 1;
    srd.log[0].kind = SRD_FRAME_FULL;
    srd.log[0].byte_count = sizeof(full);
    memcpy(srd.log[0].bytes, full, sizeof(full));
    m = build(434000000u, 2000000u);
    check_str("a full frame names its delimiters", m.log[0].detail,
              "hdr 3F  tag 04  trailer D4");
    check_str("and the frame proves the device", m.log[0].device,
              "SRD remote control");
    check_str("the marker carries the kind", m.log[0].marker_label, "FULL");
    check_str("and the payload as hex, no trailing space", m.log[0].hex,
              "3F 04 0B 69 BB CC 9F 42 F2 D4");

    blank();
    srd.log_count = 1;
    srd.log[0].kind = SRD_FRAME_GENERIC;
    srd.log[0].byte_count = sizeof(fsk);
    memcpy(srd.log[0].bytes, fsk, sizeof(fsk));
    m = build(434000000u, 2000000u);
    check_str("the 2-FSK protocol's own fields", m.log[0].detail,
              "id DEADBEEF  seq 002A  flg 11");
    check_str("the marker carries its sequence number",
              m.log[0].marker_label, "seq 002A");

    /*
     * The same fourteen bytes under a kind the prefix test does not apply
     * to. The marker's own copy of that test checked two bytes and never
     * looked at the kind, so it would have labelled this "seq 002A" while
     * the table beside it called the device a remote control.
     */
    srd.log[0].kind = SRD_FRAME_FULL;
    m = build(434000000u, 2000000u);
    check_str("a full frame is not the 2-FSK protocol whatever it opens with",
              m.log[0].marker_label, "FULL");

    blank();
    srd.log_count = 1;
    srd.log[0].kind = SRD_FRAME_UNDECODED;
    m = build(434000000u, 2000000u);
    check_str("a burst with no frame says so", m.log[0].detail,
              "detected burst (no frame)");
    check_str("and establishes nothing about the device", m.log[0].device,
              "unknown");
    /*
     * And carries **no** marker label. It is the commonest thing on this
     * band by a wide margin -- one live sweep put 35 on screen at once --
     * and every one said the same word, burying the markers that carry a
     * decode. An empty label is a decision, which is why it is asserted.
     */
    check_str("and no label on the waterfall", m.log[0].marker_label, "");

    blank();
    srd.log_count = 1;
    srd.log[0].kind = SRD_FRAME_FSK_DETECTED;
    srd.log[0].chip_us = 64.2;
    srd.log[0].bit_count = 96;
    m = build(434000000u, 2000000u);
    check_str("a wakeup burst reports its preamble", m.log[0].detail,
              "preamble 96 chips (64us, 15.6 kbd)");
    check_str("and is labelled as one", m.log[0].marker_label, "WAKEUP");

    /* A kind with no name at all reads "unknown" rather than falling
       through to the last branch of a ternary chain, which is how the
       window's marker used to label it GENERIC. */
    blank();
    srd.log_count = 1;
    srd.log[0].kind = SRD_FRAME_UNKNOWN;
    m = build(434000000u, 2000000u);
    check_str("no kind is not the last kind", m.log[0].marker_label,
              "unknown");
}

int main(void) {
    test_ready_is_inside_the_band_not_all_of_it();
    test_not_ready_says_whose_problem_it_is();
    test_the_parameters_panel_is_decided_here();
    test_a_row_says_what_it_is();
    test_no_carrier_is_not_a_carrier_at_zero();
    test_every_kind_and_modulation_has_a_name();
    test_the_log_carries_absolute_frequencies();
    return check_report("what the SRD screen says, decided without a screen");
}
