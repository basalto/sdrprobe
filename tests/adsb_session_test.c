/*
 * check-adsb-session -- a Mode S decode replayed block by block, with no
 * window, no receiver and no process launch.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`. ADS-B was already shared
 * -- `run_headless` called `update_adsb()` -- so this was an extraction rather
 * than a de-duplication, and the standard is the same: if it changed an answer
 * the extraction is wrong.
 *
 * `adsb_cpr_pair.bin` must keep decoding 6 frames with 1 global position
 * resolved, and it is the only capture that exercises the **even/odd pairing
 * cache** end to end. That cache lives in `struct adsb_decoder` and therefore
 * in the session: a global position needs two frames of opposite parity from
 * the same aircraft, so a session that forgot between blocks would resolve
 * nothing.
 */

#include "check.h"

#include "adsb_session.h"
#include "sdr_dsp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
/*
 * A block is 65.5 ms of signal at 2 MS/s, and `now` must be in **seconds**
 * rather than a block index -- unlike the other sessions, ADS-B's timestamp
 * does real work. The even/odd pairing cache expires a held frame after a few
 * seconds, so feeding block numbers stretches the gap between the two frames
 * of a pair by about fifteen times and no position ever resolves. That is
 * exactly what a first version of this check did, and it read as a broken
 * pairing cache.
 */
#define BLOCK_SECONDS (BLOCK_PAIRS / 2000000.0)

static uint8_t raw[BLOCK_BYTES];
static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
static struct adsb_session session;

struct replay {
    int blocks_fed;
    int frames;
    int positions;
};

static int replay(const char *path, struct replay *out) {
    struct device_profile profile =
        device_profile_capture(path, SAMPLE_FORMAT_U8,
                               device_default_full_scale(SAMPLE_FORMAT_U8),
                               0.0, 2000000);
    FILE *f = fopen(path, "rb");

    memset(out, 0, sizeof(*out));
    adsb_session_reset(&session);
    if (!f)
        return 0;
    for (;;) {
        size_t got = fread(raw, 1, sizeof(raw), f);
        size_t pairs;
        int count, i;

        if (got < sizeof(raw))
            break;  /* whole blocks only, so a run is repeatable */
        pairs = sdr_dsp_convert_iq(&profile, raw, got, I, Q, M, BLOCK_PAIRS);
        out->blocks_fed++;
        count = adsb_session_feed(&session, M, pairs,
                                  out->blocks_fed * BLOCK_SECONDS);
        out->frames += count;
        for (i = 0; i < count; i++)
            if (session.messages[i].has_position)
                out->positions++;
    }
    fclose(f);
    return 1;
}

/* The capture the app recorded itself, and the pinned answer. */
static void test_cpr_pair(void) {
    struct replay r;

    if (!replay("testfiles/adsb_cpr_pair.bin", &r)) {
        check_true("testfiles/adsb_cpr_pair.bin opens", 0);
        return;
    }
    check_true("blocks were fed", r.blocks_fed > 0);
    check_int("six frames", r.frames, 6);
    check_int("and one global position resolved", r.positions, 1);
    check_int("the counters agree", (int)session.frames_total, r.frames);
    check_int("as do the position ones", (int)session.positions_total,
              r.positions);

    /*
     * The funnel, which is what makes "nothing decoded" a diagnosis rather
     * than a shrug: preambles found, frames whose parity passed, and what
     * never became a message.
     */
    check_true("preambles were found", session.totals.preambles > 0);
    check_msg(session.totals.decoded == (uint64_t)r.frames,
              "the funnel decoded %llu against %d frames emitted\n",
              (unsigned long long)session.totals.decoded, r.frames);
    /* Every stage is a subset of the one before it. The module has its own
       predicate for that, because counters that break it describe an
       impossible run and a reader would take them as evidence about an
       antenna. */
    check_true("the funnel is consistent",
               adsb_funnel_is_consistent(&session.totals));
}

/*
 * The pairing cache is the point of this capture, so this is the check that
 * would notice if it stopped surviving between blocks: the position arrives
 * later than the frames that make it, and a session reset in the middle loses
 * it.
 */
static void test_the_pairing_cache_spans_blocks(void) {
    struct replay whole;

    if (!replay("testfiles/adsb_cpr_pair.bin", &whole))
        return;
    check_int("one position across the run", whole.positions, 1);

    /* Now the same capture with the decoder forgotten before every block. A
       global position needs an even and an odd frame together; if they landed
       in one block this would still find it, and it does not. */
    {
        struct device_profile profile = device_profile_capture(
            "x", SAMPLE_FORMAT_U8, device_default_full_scale(SAMPLE_FORMAT_U8),
            0.0, 2000000);
        FILE *f = fopen("testfiles/adsb_cpr_pair.bin", "rb");
        int positions = 0, frames = 0, blocks = 0;

        if (!f)
            return;
        for (;;) {
            size_t got = fread(raw, 1, sizeof(raw), f);
            size_t pairs;
            int count, i;

            if (got < sizeof(raw))
                break;
            pairs = sdr_dsp_convert_iq(&profile, raw, got, I, Q, M,
                                       BLOCK_PAIRS);
            adsb_session_reset(&session);   /* forget between blocks */
            blocks++;
            count = adsb_session_feed(&session, M, pairs,
                                      blocks * BLOCK_SECONDS);
            frames += count;
            for (i = 0; i < count; i++)
                if (session.messages[i].has_position)
                    positions++;
        }
        fclose(f);
        check_int("forgetting between blocks still finds the frames", frames,
                  whole.frames);
        check_int("but resolves no position", positions, 0);
    }
}

/* Nothing to feed is not a decode. */
static void test_an_empty_block(void) {
    static float zero[64];

    adsb_session_reset(&session);
    check_int("no samples", adsb_session_feed(&session, zero, 0, 0.0), 0);
    check_int("a null session", adsb_session_feed(NULL, zero, 64, 0.0), 0);
    check_int("null magnitudes",
              adsb_session_feed(&session, NULL, 64, 0.0), 0);
    check_int("and nothing is claimed", session.message_count, 0);
}

int main(void) {
    test_cpr_pair();
    test_the_pairing_cache_spans_blocks();
    test_an_empty_block();
    return check_report("a Mode S decode, block by block");
}
