/*
 * check-tetra-session -- a TETRA decode replayed block by block, with no
 * window, no receiver and no process launch.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`. `tetra_cc17.bin` and
 * `tetra_cc32.bin` are a pair and the pair is the point: colour code 17
 * against 32, location area 4375 against 4658. The broadcast channel is
 * scrambled with the network's **own** colour code, read out of the
 * synchronisation block first, so a decoder that hardcoded one would read one
 * capture and fail the other.
 *
 * That is the assertion `check-pipelines` makes over the built program. Here
 * it is milliseconds and no process.
 */

#include "check.h"

#include "tetra_session.h"
#include "sdr_dsp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
#define RATE 2000000.0

static uint8_t raw[BLOCK_BYTES];
static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
static struct tetra_session session;

struct replay {
    int blocks_fed;
    int demodulated;
    int identity_changes;
    int rate_refusals;
};

static int replay(const char *path, double rate, struct replay *out) {
    struct device_profile profile =
        device_profile_capture(path, SAMPLE_FORMAT_U8,
                               device_default_full_scale(SAMPLE_FORMAT_U8),
                               0.0, 2000000);
    FILE *f = fopen(path, "rb");

    memset(out, 0, sizeof(*out));
    tetra_session_reset(&session);
    if (!f)
        return 0;
    for (;;) {
        struct tetra_session_event event;
        size_t got = fread(raw, 1, sizeof(raw), f);
        size_t pairs;

        if (got < sizeof(raw))
            break;  /* whole blocks only, so a run is repeatable */
        pairs = sdr_dsp_convert_iq(&profile, raw, got, I, Q, M, BLOCK_PAIRS);
        out->blocks_fed++;
        tetra_session_feed(&session, I, Q, pairs, rate, &event);
        if (event.demodulated)
            out->demodulated++;
        if (event.identity_changed)
            out->identity_changes++;
        if (event.rate_unsupported)
            out->rate_refusals++;
    }
    fclose(f);
    return 1;
}

/* Colour code 17, location area 4375. */
static void test_cc17(void) {
    struct replay r;

    if (!replay("testfiles/tetra_cc17.bin", RATE, &r)) {
        check_true("testfiles/tetra_cc17.bin opens", 0);
        return;
    }
    check_true("it fed whole blocks", r.blocks_fed > 0);
    check_true("and demodulated them", r.demodulated > 0);
    check_true("the network is known", session.have_identity != 0);
    check_int("MCC 268, which is Portugal", session.mcc, 268);
    check_int("MNC 3", session.mnc, 3);
    check_int("colour code 17", session.colour, 17);
    check_int("location area 4375", session.la, 4375);

    /* The funnel: bursts found, blocks that passed parity, broadcasts read.
       Which stage stops is the diagnosis, so all three are counted. */
    check_true("bursts were found", session.bursts_total > 0);
    check_true("and blocks passed their parity", session.blocks_total > 0);
    check_true("and broadcasts were read", session.broadcast_total > 0);
    check_msg(session.blocks_total <= session.bursts_total,
              "blocks %llu cannot exceed bursts %llu\n",
              (unsigned long long)session.blocks_total,
              (unsigned long long)session.bursts_total);
    check_msg(session.broadcast_total <= session.blocks_total,
              "broadcasts %llu cannot exceed blocks %llu\n",
              (unsigned long long)session.broadcast_total,
              (unsigned long long)session.blocks_total);

    check_true("the carrier locked", session.lock > 0.5f);
    check_int("the identity settled rather than flapping",
              r.identity_changes, 1);
}

/*
 * Colour code 32, location area 4658 -- the other half of the pair.
 *
 * This capture is tuned to 392.8735 rather than a round number: recorded at
 * 392.84 the carrier was 33.5 kHz away, the coarse estimator pinned at the
 * edge of its range and half the blocks failed.
 */
static void test_cc32(void) {
    struct replay r;

    if (!replay("testfiles/tetra_cc32.bin", RATE, &r)) {
        check_true("testfiles/tetra_cc32.bin opens", 0);
        return;
    }
    check_true("it demodulated", r.demodulated > 0);
    check_int("MCC 268 again", session.mcc, 268);
    check_int("colour code 32, not 17", session.colour, 32);
    check_int("location area 4658, not 4375", session.la, 4658);
    check_true("broadcasts were read with this network's own colour code",
               session.broadcast_total > 0);
}

/*
 * A sample rate the channel filter cannot decimate to is its own answer, not a
 * silent nothing. Both adapters said so; now they say it from one flag.
 *
 * 2.048 MS/s is the rate to test with, and not an invented one: it is what
 * `fm_rds_tsf.bin` is recorded at, so it is a wrong answer a person could
 * actually give. `tetra_channel()` wants a whole multiple of
 * TETRA_WORK_RATE_HZ within a hertz -- it refuses rather than resampling --
 * and 2048000 is 48 kHz from the nearest. A first version of this check used
 * 1999999, which is exactly 1 Hz out and therefore **accepted**: the tolerance
 * is `> 1.0`.
 */
static void test_a_rate_it_cannot_use(void) {
    struct replay r;

    if (!replay("testfiles/tetra_cc17.bin", 2048000.0, &r))
        return;
    check_true("every block is refused", r.rate_refusals > 0);
    check_int("and none is demodulated", r.demodulated, 0);
    check_int("so nothing is claimed about the network",
              session.have_identity, 0);
}

/* Reset forgets the network. A different carrier is a different one. */
static void test_reset(void) {
    struct replay r;

    if (!replay("testfiles/tetra_cc17.bin", RATE, &r))
        return;
    check_true("the network is known", session.have_identity != 0);
    tetra_session_reset(&session);
    check_int("and then it is not", session.have_identity, 0);
    check_int("nor its colour code", session.colour, 0);
    check_msg(session.bursts_total == 0, "the funnel is cleared too\n");
}

/* Too few samples is not a decode. */
static void test_a_short_block(void) {
    struct tetra_session_event event;
    static float zero[64];

    tetra_session_reset(&session);
    check_int("under 64 pairs", tetra_session_feed(&session, zero, zero, 16,
                                                   RATE, &event),
              0);
    check_int("and it is not a rate complaint", event.rate_unsupported, 0);
    check_int("nor a demodulation", event.demodulated, 0);
    check_int("a null session", tetra_session_feed(NULL, zero, zero, 64, RATE,
                                                   &event),
              0);
    check_int("null buffers",
              tetra_session_feed(&session, NULL, NULL, 64, RATE, &event), 0);
}

int main(void) {
    test_cc17();
    test_cc32();
    test_a_rate_it_cannot_use();
    test_reset();
    test_a_short_block();
    return check_report("a TETRA decode, block by block");
}
