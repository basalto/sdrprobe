/*
 * check-gsm-session -- a GSM decode replayed block by block, with no window,
 * no receiver and no process launch.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`. These are the assertions
 * `check-pipelines` makes over the built program -- BSIC 59 for
 * `gsm_arfcn_69.bin`, 38 for `gsm_arfcn_113.bin`, and the System Information
 * behind them -- reachable in milliseconds because the orchestration is a
 * module now rather than a shape inside a view and a shape inside
 * `run_headless`.
 *
 * `check-pipelines` stays: it proves the session is wired in, which this
 * cannot. What this proves is that the decode is right, and it does it
 * without spawning anything.
 *
 * The captures are tuned **400 kHz below** their channel, which is what
 * `gsm_tune_selected()` does and what the sidecars record as
 * `carrier_offset_hz`. A decoder cannot recover that from the samples.
 */

#include "check.h"

#include "gsm_session.h"
#include "sdr_dsp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
#define RATE 2000000.0
#define CARRIER_OFFSET_HZ 400000.0

/* Everything on, which is what enter_gsm() sets and what a run without
   --gsm-features uses. */
#define ALL_REFINEMENTS (GSM_OPT_FILTER | GSM_OPT_FINECFO | GSM_OPT_TRELLIS)

static uint8_t raw[BLOCK_BYTES];
static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];

struct replay {
    int blocks;
    int sch_decoded;
    int broadcast_read;
    int frame_increases;
    int frame_decreases;
    struct gsm_session session;
};

/*
 * Every whole block of a capture, through one session, in order. A session is
 * stateful on purpose -- the continuity and the cell accumulate -- so the
 * order is part of what is being checked.
 */
static int replay_capture(const char *path, uint32_t options,
                          struct replay *out) {
    struct device_profile profile =
        device_profile_capture(path, SAMPLE_FORMAT_U8,
                               device_default_full_scale(SAMPLE_FORMAT_U8),
                               0.0, 2000000);
    FILE *f = fopen(path, "rb");
    int last_frame = 0;
    int have_last = 0;

    memset(out, 0, sizeof(*out));
    out->session.options = options;
    if (!f)
        return 0;

    for (;;) {
        struct gsm_session_event event;
        size_t got = fread(raw, 1, sizeof(raw), f);
        size_t pairs;

        if (got < sizeof(raw))
            break;  /* whole blocks only, so a run is repeatable */
        pairs = sdr_dsp_convert_iq(&profile, raw, got, I, Q, M, BLOCK_PAIRS);
        out->blocks++;

        if (gsm_session_feed(&out->session, I, Q, pairs, RATE,
                             CARRIER_OFFSET_HZ, (double)out->blocks, &event)) {
            out->sch_decoded++;
            if (have_last) {
                if (out->session.sch.frame_number > last_frame)
                    out->frame_increases++;
                else
                    out->frame_decreases++;
            }
            last_frame = out->session.sch.frame_number;
            have_last = 1;
        }
        if (event.broadcast_read)
            out->broadcast_read++;
    }
    fclose(f);
    return 1;
}

/*
 * ARFCN 69's cell, which CLAUDE.md pins: BSIC 59, and MCC 268 MNC 03 LAC 4010
 * Cell Identity 5131 out of the broadcast behind it.
 */
static void test_arfcn_69(void) {
    struct replay r;

    if (!replay_capture("testfiles/gsm_arfcn_69.bin", ALL_REFINEMENTS, &r)) {
        check_true("testfiles/gsm_arfcn_69.bin opens", 0);
        return;
    }
    check_int("31 whole blocks", r.blocks, 31);
    check_int("every one decodes an SCH", r.sch_decoded, 31);
    check_int("BSIC 59", r.session.sch.bsic, 59);
    check_int("which is NCC 7", r.session.sch.bsic >> 3, 7);
    check_int("and BCC 3", r.session.sch.bsic & 7, 3);

    /* The frame number tracks the burst timeline: it only goes forwards. */
    check_int("frame numbers never go backwards", r.frame_decreases, 0);
    check_true("and they advance", r.frame_increases > 25);

    check_true("System Information was read", r.broadcast_read > 0);
    check_int("seven messages", r.broadcast_read, 7);
    check_true("the cell has an identity", r.session.cell.have_lai != 0);
    check_int("MCC 268", r.session.cell.mcc, 268);
    check_int("MNC 3", r.session.cell.mnc, 3);
    check_int("two MNC digits, so it prints as 03",
              r.session.cell.mnc_digits, 2);
    check_int("LAC 4010", r.session.cell.lac, 4010);
    check_true("and a cell identity", r.session.cell.have_cell_id != 0);
    check_int("Cell Identity 5131", r.session.cell.cell_id, 5131);

    /* Neighbours come from a different message type than the identity, which
       is why the cell folds messages together rather than keeping the last. */
    check_true("it heard neighbours too", r.session.cell.neighbour_count > 0);
}

/*
 * ARFCN 113, whose BCC is different. That is the point of having three
 * captures: the BCC picks the training sequence every normal burst is found
 * by, so a decoder that hardcoded one would read 69 and fail 113.
 */
static void test_arfcn_113(void) {
    struct replay r;

    if (!replay_capture("testfiles/gsm_arfcn_113.bin", ALL_REFINEMENTS, &r)) {
        check_true("testfiles/gsm_arfcn_113.bin opens", 0);
        return;
    }
    check_true("it decodes", r.sch_decoded > 0);
    check_int("BSIC 38", r.session.sch.bsic, 38);
    check_int("which is NCC 4", r.session.sch.bsic >> 3, 4);
    check_int("and BCC 6 -- a different training sequence from 69's",
              r.session.sch.bsic & 7, 6);
    check_int("MNC 6", r.session.cell.mnc, 6);
    check_int("Cell Identity 16134", r.session.cell.cell_id, 16134);
    check_int("frame numbers never go backwards", r.frame_decreases, 0);
}

/* And 73, the third BCC. */
static void test_arfcn_73(void) {
    struct replay r;

    if (!replay_capture("testfiles/gsm_arfcn_73.bin", ALL_REFINEMENTS, &r)) {
        check_true("testfiles/gsm_arfcn_73.bin opens", 0);
        return;
    }
    check_true("it decodes", r.sch_decoded > 0);
    check_int("BSIC 56", r.session.sch.bsic, 56);
    check_int("NCC 7", r.session.sch.bsic >> 3, 7);
    check_int("BCC 0, the third of the three", r.session.sch.bsic & 7, 0);
}

/*
 * A session is stateful, and resetting it must forget the cell without
 * forgetting how the operator asked for the decode to be done. A different
 * channel is a different cell; the front-end options are a preference.
 */
static void test_reset_forgets_the_cell_and_keeps_the_options(void) {
    struct replay r;

    if (!replay_capture("testfiles/gsm_arfcn_69.bin", ALL_REFINEMENTS, &r))
        return;
    check_true("the cell is known", r.session.cell.have_lai != 0);

    r.session.options = GSM_OPT_FILTER | GSM_OPT_TRELLIS;
    gsm_session_reset(&r.session);
    check_int("the cell is forgotten", r.session.cell.have_lai, 0);
    check_int("and its identity", r.session.cell.cell_id, 0);
    check_int("and the message count", r.session.cell.blocks, 0);
    check_int("and nothing was last read", r.session.sch_valid, 0);
    check_int("but the options stay", (int)r.session.options,
              (int)(GSM_OPT_FILTER | GSM_OPT_TRELLIS));
}

/* The option bits, which were three ints in a view until they were one mask. */
static void test_the_option_bits(void) {
    struct gsm_session s;

    memset(&s, 0, sizeof(s));
    check_int("nothing set", gsm_session_option(&s, GSM_OPT_FILTER), 0);
    gsm_session_set_option(&s, GSM_OPT_FILTER, 1);
    check_int("set", gsm_session_option(&s, GSM_OPT_FILTER), 1);
    check_int("and only that one", gsm_session_option(&s, GSM_OPT_TRELLIS), 0);
    gsm_session_toggle_option(&s, GSM_OPT_FILTER);
    check_int("toggled off", gsm_session_option(&s, GSM_OPT_FILTER), 0);
    gsm_session_toggle_option(&s, GSM_OPT_FILTER);
    check_int("and back on", gsm_session_option(&s, GSM_OPT_FILTER), 1);
    gsm_session_set_option(&s, GSM_OPT_FILTER, 0);
    check_int("cleared", gsm_session_option(&s, GSM_OPT_FILTER), 0);
    check_int("a null session reads as unset",
              gsm_session_option(NULL, GSM_OPT_FILTER), 0);
}

/* Nothing to feed is not a decode, and must not be reported as one. */
static void test_an_empty_block_decodes_nothing(void) {
    struct gsm_session s;
    struct gsm_session_event event;
    static float zero[64];

    memset(&s, 0, sizeof(s));
    check_int("no samples", gsm_session_feed(&s, zero, zero, 0, RATE,
                                             CARRIER_OFFSET_HZ, 0.0, &event),
              0);
    check_int("and the event says so", event.sch_decoded, 0);
    check_int("a null session", gsm_session_feed(NULL, zero, zero, 4, RATE,
                                                 CARRIER_OFFSET_HZ, 0.0,
                                                 &event),
              0);
    check_int("null buffers", gsm_session_feed(&s, NULL, NULL, 4, RATE,
                                               CARRIER_OFFSET_HZ, 0.0, &event),
              0);
    check_int("nothing was latched", s.sch_valid, 0);
}

/*
 * The refinements earn their place, which `check-pipelines` asserts by running
 * the program twice with `--gsm-features`. Here it is one process and no
 * spawning: the same capture through two sessions differing only in their
 * option mask.
 */
static void test_the_refinements_decode_more(void) {
    struct replay bare, full;

    if (!replay_capture("testfiles/gsm_arfcn_73.bin", 0, &bare) ||
        !replay_capture("testfiles/gsm_arfcn_73.bin", ALL_REFINEMENTS, &full))
        return;
    check_int("the same blocks either way", bare.blocks, full.blocks);
    check_msg(full.sch_decoded > bare.sch_decoded,
              "refinements decoded %d bursts against %d without them\n",
              full.sch_decoded, bare.sch_decoded);
    check_int("and the identity does not depend on them",
              full.session.sch.bsic, 56);
}

int main(void) {
    test_arfcn_69();
    test_arfcn_113();
    test_arfcn_73();
    test_the_refinements_decode_more();
    test_reset_forgets_the_cell_and_keeps_the_options();
    test_the_option_bits();
    test_an_empty_block_decodes_nothing();
    return check_report("a GSM decode, block by block");
}
