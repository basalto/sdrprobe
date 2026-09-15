/*
 * Deterministic, hardware-free checks for the SRD decode session.
 *
 *   make check-srd-session
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "srd_dsp.h"
#include "srd_frame.h"
#include "srd_session.h"

#define FS 2000000.0
#define BLOCK_PAIRS 131072

static float b_i[BLOCK_PAIRS];
static float b_q[BLOCK_PAIRS];

/*
 * A second, longer block for the carry-rule check below. A block is a
 * parameter of srd_session_feed(), not a constant of the session, and the
 * arithmetic there needs one: a delimited frame at the remote control's 500 us chips
 * runs to 28.5 ms and the rule under test wants 50 ms of silence after it,
 * which does not fit in the 65.5 ms of a house block.
 */
#define BIG_BLOCK_PAIRS 200000
static float g_i[BIG_BLOCK_PAIRS];
static float g_q[BIG_BLOCK_PAIRS];

static void unpack_bytes_to_bits(const uint8_t *bytes, size_t byte_count,
                                 uint8_t *bits_out) {
    for (size_t i = 0; i < byte_count; i++) {
        for (int b = 7; b >= 0; b--) {
            *bits_out++ = (bytes[i] >> b) & 1;
        }
    }
}

static size_t encode_manchester(const uint8_t *bits, size_t bit_count,
                                enum srd_manchester_polarity polarity,
                                uint8_t *chips_out) {
    size_t chips = 0;
    for (size_t i = 0; i < bit_count; i++) {
        uint8_t b = bits[i];
        if (polarity == SRD_MANCHESTER_THOMAS) {
            chips_out[chips++] = b ? 0 : 1;
            chips_out[chips++] = b ? 1 : 0;
        } else {
            chips_out[chips++] = b ? 1 : 0;
            chips_out[chips++] = b ? 0 : 1;
        }
    }
    return chips;
}

static void test_split_across_blocks_and_deduplication(void) {
    struct srd_session s;
    srd_session_reset(&s);

    /* Build a full 10-byte OOK frame: 6 delimiter runs of 750us + 160 chips of 500us */
    const uint8_t full_bytes[10] = { 0x3F, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0xD4 };
    uint8_t bits[80];
    uint8_t chips[160];
    unpack_bytes_to_bits(full_bytes, 10, bits);
    encode_manchester(bits, 80, SRD_MANCHESTER_THOMAS, chips);

    /* Block 1: Preamble + Delimiter + first 40 chips (40*1000 = 40000 samples) */
    memset(b_i, 0, sizeof(b_i));
    memset(b_q, 0, sizeof(b_q));
    size_t start_pair = BLOCK_PAIRS - 50000;
    for (int d = 0; d < 6; d++) {
        float amp = (d % 2 == 0) ? 80.0f : 0.0f;
        for (size_t k = 0; k < 1500; k++) {
            size_t idx = start_pair + (size_t)d * 1500 + k;
            double t = (double)idx / FS;
            b_i[idx] = (float)(amp * cos(2.0 * M_PI * 50000.0 * t));
            b_q[idx] = (float)(amp * sin(2.0 * M_PI * 50000.0 * t));
        }
    }
    for (size_t c = 0; c < 40; c++) {
        float amp = chips[c] ? 80.0f : 0.0f;
        for (size_t k = 0; k < 1000; k++) {
            size_t idx = start_pair + 9000 + c * 1000 + k;
            if (idx >= BLOCK_PAIRS) break;
            double t = (double)idx / FS;
            b_i[idx] = (float)(amp * cos(2.0 * M_PI * 50000.0 * t));
            b_q[idx] = (float)(amp * sin(2.0 * M_PI * 50000.0 * t));
        }
    }

    struct srd_session_event ev1;
    srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                     SRD_MANCHESTER_THOMAS, 1.0, &ev1);
    check_int("block 1 alone emits no frame", ev1.frame_count, 0);
    check_true("block 1 accumulated runs in stream", s.stream_run_count > 0);

    /* Block 2: Remainder of chips (chips 40 to 160 = 120 chips) at start of block */
    memset(b_i, 0, sizeof(b_i));
    memset(b_q, 0, sizeof(b_q));
    for (size_t c = 40; c < 160; c++) {
        float amp = chips[c] ? 80.0f : 0.0f;
        for (size_t k = 0; k < 1000; k++) {
            size_t idx = (c - 40) * 1000 + k;
            if (idx >= BLOCK_PAIRS) break;
            double t = (double)idx / FS;
            b_i[idx] = (float)(amp * cos(2.0 * M_PI * 50000.0 * t));
            b_q[idx] = (float)(amp * sin(2.0 * M_PI * 50000.0 * t));
        }
    }

    struct srd_session_event ev2;
    srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                     SRD_MANCHESTER_THOMAS, 1.065, &ev2);
    check_int("block 2 completes the split frame", ev2.frame_count, 1);
    if (ev2.frame_count >= 1) {
        check_int("frame kind is FULL", ev2.frames[0].frame.kind, SRD_FRAME_FULL);
        check_size("frame has 10 bytes", ev2.frames[0].frame.byte_count, 10);
        check_int("frame matches payload",
                  memcmp(ev2.frames[0].frame.bytes, full_bytes, 10), 0);
    }

    /* Duplicate check: feed block 2 again, must not re-emit the same frame */
    struct srd_session_event ev3;
    srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                     SRD_MANCHESTER_THOMAS, 1.130, &ev3);
    check_int("same burst in block 3 does not duplicate frame", ev3.frame_count, 0);

    /* Quiet blocks check: 4 empty blocks reset the stream runs */
    memset(b_i, 0, sizeof(b_i));
    memset(b_q, 0, sizeof(b_q));
    struct srd_session_event qev;
    for (int q = 0; q < 3; q++) {
        srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                         SRD_MANCHESTER_THOMAS, 1.2 + q * 0.065, &qev);
        check_int("quiet block does not reset yet", qev.quiet_reset, 0);
    }
    srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                     SRD_MANCHESTER_THOMAS, 1.45, &qev);
    check_int("4th quiet block triggers quiet_reset", qev.quiet_reset, 1);
    check_size("stream runs cleared after quiet reset", s.stream_run_count, 0);
}

/*
 * A transmission that finished inside its block is not carried into the next.
 *
 * The session joins runs across a block boundary because one transmission can
 * straddle it. It used to join them unconditionally, so a burst that ended in
 * the middle of a block was still glued to whatever spoke next -- and
 * srd_extract_frames() reads a run stream as one continuous Manchester
 * signal, so the junction is a violation that truncates the frame in
 * progress. On testfiles/srd_remote_control_fsk.bin, whose presses are
 * about 100 ms apart, that cost the assembled program most of its decodes:
 * 15 frames against 4 with the gluing left in, over the same capture.
 *
 * The gap that decides it is the one srd_find_transmissions() already groups
 * by. A burst ending more than SRD_GAP_SECONDS_DEFAULT before the block ends
 * would have been a separate transmission had the samples kept coming, so it
 * is treated as one.
 *
 * The fixture is the remote control's own geometry -- 500 us chips under a 750 us
 * delimiter, which srd_frame.h defines as one and a half of them -- in a
 * block long enough to hold it and the silence after it. An earlier version
 * shrank the chips to 64 us to fit a house block instead, and that fixture
 * was not a signal any transmitter could send: the delimiter stayed at
 * 750 us and so became 11.7 chips, which srd_chip_coverage() correctly
 * refused to believe in.
 */
static void test_a_finished_transmission_is_not_carried(void) {
    struct srd_session s;
    const size_t chip_pairs = 1000; /* 500 us at 2 MS/s */
    const uint8_t repeat_a[3] = { 0x1F, 0x9E, 0xD4 };
    const uint8_t repeat_b[3] = { 0x1F, 0x21, 0xD4 };
    const uint8_t *payloads[2] = { repeat_a, repeat_b };
    struct srd_session_event ev;
    int block;

    srd_session_reset(&s);

    for (block = 0; block < 2; block++) {
        uint8_t bits[24];
        uint8_t chips[48];
        size_t at = 2000;
        int d;

        unpack_bytes_to_bits(payloads[block], 3, bits);
        encode_manchester(bits, 24, SRD_MANCHESTER_THOMAS, chips);

        memset(g_i, 0, sizeof(g_i));
        memset(g_q, 0, sizeof(g_q));

        /* Six alternating runs of 750 us: the delimiter. */
        for (d = 0; d < 6; d++) {
            float amp = (d % 2 == 0) ? 80.0f : 0.0f;
            for (size_t k = 0; k < 1500; k++, at++) {
                double tt = (double)at / FS;
                g_i[at] = (float)(amp * cos(2.0 * M_PI * 50000.0 * tt));
                g_q[at] = (float)(amp * sin(2.0 * M_PI * 50000.0 * tt));
            }
        }
        for (size_t c = 0; c < 48; c++) {
            float amp = chips[c] ? 80.0f : 0.0f;
            for (size_t k = 0; k < chip_pairs; k++, at++) {
                double tt = (double)at / FS;
                g_i[at] = (float)(amp * cos(2.0 * M_PI * 50000.0 * tt));
                g_q[at] = (float)(amp * sin(2.0 * M_PI * 50000.0 * tt));
            }
        }

        /*
         * Where it ends matters more than anything else here: the burst has
         * to finish far enough inside the block that a continuation is not a
         * possible reading of it.
         */
        check_true("the burst ends a clear gap before the block does",
                   (double)(BIG_BLOCK_PAIRS - at) / FS > SRD_GAP_SECONDS_DEFAULT);

        srd_session_feed(&s, g_i, g_q, BIG_BLOCK_PAIRS, FS, 127.5f,
                         SRD_MANCHESTER_THOMAS, 3.0 + block, &ev);

        check_int("the burst decodes to one frame", ev.frame_count, 1);
        if (ev.frame_count >= 1) {
            check_int("it is a repeat frame", ev.frames[0].frame.kind,
                      SRD_FRAME_REPEAT);
            check_int("it carries this block's own payload",
                      memcmp(ev.frames[0].frame.bytes, payloads[block], 3), 0);
        }
        check_size("nothing is carried into the next block",
                   s.stream_run_count, 0);
    }
}

static void test_undecoded_fsk_event(void) {
    struct srd_session s;
    srd_session_reset(&s);

    /* Generate a 2-FSK unmodulated carrier / wakeup burst (constant tone at +50 kHz) */
    memset(b_i, 0, sizeof(b_i));
    memset(b_q, 0, sizeof(b_q));
    double fc = 50000.0;
    size_t burst_len = 50000; /* 25 ms */
    for (size_t n = 0; n < burst_len; n++) {
        double t = (double)n / FS;
        /* Deviating FSK tone */
        double tone = fc + (n % 256 < 128 ? 16000.0 : -16000.0);
        double p = 2.0 * M_PI * tone * t;
        b_i[n] = (float)(80.0 * cos(p));
        b_q[n] = (float)(80.0 * sin(p));
    }

    struct srd_session_event ev;
    srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                     SRD_MANCHESTER_THOMAS, 2.0, &ev);
    check_int("wakeup burst emits undecoded event", ev.undecoded_count, 1);
    check_int("wakeup burst emits 0 frames", ev.frame_count, 0);
    if (ev.undecoded_count >= 1) {
        check_int("undecoded event modulation is 2FSK",
                  ev.undecoded[0].modulation, SRD_MOD_FSK2);
        check_close("carrier frequency near 50 kHz",
                    ev.undecoded[0].carrier_hz, 50000.0, 20000.0);

        /*
         * The verdict is the session's, and it is checked here because it
         * used to be reached twice: the window applied this chip threshold
         * and said UNDECODED while the headless report called every 2-FSK
         * burst a WAKEUP and printed "preamble 0 chips" for one that had no
         * chip period at all. Whichever this event is, both adapters now
         * read it off the event.
         */
        check_true("a burst with chips to show is a wakeup, one without is not",
                   ev.undecoded[0].kind ==
                   (ev.undecoded[0].chip_count >= SRD_WAKEUP_MIN_CHIPS
                        ? SRD_FRAME_FSK_DETECTED : SRD_FRAME_UNDECODED));
        check_int("this burst yielded chips, so it is a wakeup",
                  ev.undecoded[0].kind, SRD_FRAME_FSK_DETECTED);
        check_true("and it says how many chips it saw",
                   ev.undecoded[0].chip_count >= SRD_WAKEUP_MIN_CHIPS);
    }
}

/*
 * A burst whose chip period was refused is undecoded, not a wakeup.
 *
 * srd_chip_period() returns 0.0 for a signal with no chip structure, and
 * srd_session_feed() then has no chips to show -- so there is no preamble to
 * report and the burst is exactly what it looks like: detected, and not read.
 * The headless report used to call it a wakeup anyway and print "preamble 0
 * chips" beside it.
 */
/*
 * A carrier that is plainly there and says nothing: a tone with enough phase
 * dither that the frequency discriminator crosses its slicing threshold every
 * sample or two, so the run lengths carry no chip structure at all.
 *
 * It has to be a *carrier* and not white noise. An earlier version of this
 * fixture drew a uniformly random phase per sample, which spreads the energy
 * across the whole band -- srd_find_transmissions() wants a bin standing 25 dB
 * over the median and there was none, so no transmission was found and the
 * checks beneath it never ran. That is the shape of a check that passes by
 * testing nothing, so both callers assert that a transmission was found
 * before asserting anything about it.
 */
static void fill_structureless_carrier(size_t pairs, uint32_t *seed) {
    double phase = 0.0;
    size_t n;

    for (n = 0; n < pairs && n < BLOCK_PAIRS; n++) {
        double dither;

        *seed = *seed * 1664525u + 1013904223u;
        dither = ((double)(*seed >> 8) / (double)(1u << 24) - 0.5) * 1.2;
        phase += 2.0 * M_PI * 50000.0 / FS + dither;
        b_i[n] = (float)(80.0 * cos(phase));
        b_q[n] = (float)(80.0 * sin(phase));
    }
    for (; n < BLOCK_PAIRS; n++) {
        b_i[n] = 0.0f;
        b_q[n] = 0.0f;
    }
}

static void test_a_refused_chip_period_is_not_a_wakeup(void) {
    struct srd_session s;
    struct srd_session_event ev;
    uint32_t seed = 12345u;

    srd_session_reset(&s);
    fill_structureless_carrier(60000, &seed);

    srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                     SRD_MANCHESTER_THOMAS, 5.0, &ev);

    check_int("the structureless carrier is found as a transmission",
              ev.undecoded_count >= 1, 1);
    check_int("a burst with no chip structure decodes to no frame",
              ev.frame_count, 0);
    if (ev.undecoded_count >= 1) {
        check_int("it is reported undecoded, not as a wakeup",
                  ev.undecoded[0].kind, SRD_FRAME_UNDECODED);
        check_size("and it claims no chips at all",
                   ev.undecoded[0].chip_count, 0);
    }
}

static void test_one_transmission_is_reported_once(void) {
    struct srd_session s;
    struct srd_session_event ev;
    uint32_t seed = 999u;
    int block, reports = 0, found = 0;

    srd_session_reset(&s);

    /* Four blocks of one carrier, edge to edge: a transmission that outlives
       the buffer, which is what the live signal was. */
    for (block = 0; block < 4; block++) {
        fill_structureless_carrier(BLOCK_PAIRS, &seed);
        srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                         SRD_MANCHESTER_THOMAS, 6.0 + block * 0.065, &ev);
        reports += ev.undecoded_count;
        found += (ev.undecoded_count > 0 || s.stream_run_count > 0);
    }

    check_true("the carrier was seen in every block", found == 4);
    check_true("but it is reported at least once", reports >= 1);
    check_true("and four blocks of it do not make four reports", reports <= 2);

    /*
     * A different carrier is a different transmission, however the blocks
     * fall. Half a megahertz away is what the 2-FSK remote's presses do, and
     * it is what no timing tolerance could see.
     */
    {
        size_t n;
        double phase = 0.0;

        for (n = 0; n < BLOCK_PAIRS; n++) {
            double dither;

            seed = seed * 1664525u + 1013904223u;
            dither = ((double)(seed >> 8) / (double)(1u << 24) - 0.5) * 1.2;
            phase += 2.0 * M_PI * 570000.0 / FS + dither;
            b_i[n] = (float)(80.0 * cos(phase));
            b_q[n] = (float)(80.0 * sin(phase));
        }
        srd_session_feed(&s, b_i, b_q, BLOCK_PAIRS, FS, 127.5f,
                         SRD_MANCHESTER_THOMAS, 6.5, &ev);
        check_true("a carrier half a megahertz away is reported as its own",
                   ev.undecoded_count >= 1);
    }
}

int main(void) {
    test_one_transmission_is_reported_once();
    test_a_refused_chip_period_is_not_a_wakeup();
    test_split_across_blocks_and_deduplication();
    test_a_finished_transmission_is_not_carried();
    test_undecoded_fsk_event();
    return check_report("SRD decode session");
}
