/*
 * check-fm-session -- an RDS decode replayed block by block, with no window,
 * no receiver and no process launch.
 *
 * `.scratch/deepening/issues/02-decode-sessions.md`, last of five.
 *
 * `fm_rds_tsf.bin` is at **2.048 MS/s**, tuned to 89.5 where TSF is, and three
 * seconds long. It must keep reading identification 0x8343 and the name
 * `TSF` -- **and the programme type**, because the name alone would pass with
 * the differential sense backwards and the type lives in a different block of
 * every group.
 *
 * Three seconds rather than two is margin, not generosity: a name is four
 * segments seen whole twice and agreeing, one second of this capture names
 * nothing, and two names it only depending on where the segment cycle falls.
 */

#include "check.h"

#include "fm_session.h"
#include "sdr_dsp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
#define RATE 2048000.0
#define BLOCK_SECONDS (BLOCK_PAIRS / RATE)

static uint8_t raw[BLOCK_BYTES];
static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
static float multiplex[BLOCK_PAIRS];
static struct fm_session session;

struct replay {
    int blocks_fed;
    int chunks;
    int group_advances;
};

/*
 * The caller does the discriminator, because a real one wants the multiplex
 * anyway -- for the sound and for the charts -- and doing it twice would be
 * the same work for the same answer.
 */
static int replay(const char *path, int flush_last, struct replay *out) {
    struct device_profile profile =
        device_profile_capture(path, SAMPLE_FORMAT_U8,
                               device_default_full_scale(SAMPLE_FORMAT_U8),
                               0.0, 2048000);
    FILE *f = fopen(path, "rb");
    long size, whole;

    memset(out, 0, sizeof(*out));
    fm_session_reset(&session);
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    whole = size / BLOCK_BYTES;

    for (;;) {
        struct fm_session_event event;
        size_t got = fread(raw, 1, sizeof(raw), f);
        size_t pairs, n;

        if (got < sizeof(raw))
            break;  /* whole blocks only, so a run is repeatable */
        pairs = sdr_dsp_convert_iq(&profile, raw, got, I, Q, M, BLOCK_PAIRS);
        out->blocks_fed++;
        n = fm_discriminate_f(I, Q, pairs, multiplex, BLOCK_PAIRS);
        fm_session_feed(&session, multiplex, n, RATE,
                        out->blocks_fed * BLOCK_SECONDS,
                        flush_last && out->blocks_fed == whole, &event);
        if (event.chunk_decoded)
            out->chunks++;
        if (event.groups_advanced)
            out->group_advances++;
    }
    fclose(f);
    return 1;
}

/* The station, and all three of the things that identify it. */
static void test_tsf(void) {
    struct replay r;

    if (!replay("testfiles/fm_rds_tsf.bin", 0, &r)) {
        check_true("testfiles/fm_rds_tsf.bin opens", 0);
        return;
    }
    check_true("blocks were fed", r.blocks_fed > 0);
    check_true("and chunks decoded", r.chunks > 0);

    check_true("the identification was read", session.station.pi_valid != 0);
    check_int("identification 0x8343", session.station.pi, 0x8343);
    check_true("the name was read", session.station.ps_valid != 0);
    check_str("and it is TSF", session.station.ps, " TSF    ");
    /* The programme type lives in a different block of every group, so the
       name alone would pass with the differential sense backwards. */
    check_true("the programme type was read", session.station.pty_valid != 0);
    check_int("news", session.station.pty, 1);

    check_true("groups came out", session.groups_total > 0);
    check_true("and the run recorded when", session.last_group_at > 0.0);
}

/*
 * The funnel behind that, which is what says whether a run that read nothing
 * was off the air or off the axis. RDS has no preamble, so synchronisation is
 * a search and a syndrome matches by chance about once in two hundred tries.
 */
static void test_the_funnel_is_ordered(void) {
    struct replay r;

    if (!replay("testfiles/fm_rds_tsf.bin", 0, &r))
        return;
    check_true("blocks were found", session.station.funnel.blocks_matched > 0);
    check_true("and groups assembled from them",
               session.station.funnel.groups > 0);
    check_msg(session.station.funnel.groups <=
                  session.station.funnel.blocks_matched,
              "a group needs at least one matched block: %ld groups from "
              "%ld blocks\n", session.station.funnel.groups,
              session.station.funnel.blocks_matched);
    check_msg(session.station.funnel.identified <=
                  session.station.funnel.groups,
              "only a group can carry an identification: %ld of %ld\n",
              session.station.funnel.identified,
              session.station.funnel.groups);
    check_msg(session.station.funnel.named <=
                  session.station.funnel.identified,
              "a name is confirmed inside an identified group: %ld of %ld\n",
              session.station.funnel.named,
              session.station.funnel.identified);
    check_int("the session's tally agrees with the station's",
              (int)session.groups_total, (int)session.station.funnel.groups);
}

/*
 * Bits accumulate across chunks and are **not re-counted**.
 *
 * The window slides by whole symbols, so symbol k of this decode is symbol
 * k + dropped of the last one and only the newest few are unseen. Re-appending
 * the whole window every block would count each group many times over.
 *
 * The numbers are pinned because nothing softer catches it. Measured on this
 * capture: appending only what is new gives **2057 bits and 19 groups**, and
 * re-appending the whole window gives **4092 and 31**. A first version of this
 * check compared `bit_count` against `groups * 104 * 4` and passed under both,
 * so it named a fault it could not see -- and the station reads 0x8343 `TSF`
 * either way, so the identity is no help either.
 */
static void test_bits_accumulate_without_double_counting(void) {
    struct replay r;

    if (!replay("testfiles/fm_rds_tsf.bin", 0, &r))
        return;
    check_msg(session.bit_count <= FM_SESSION_BIT_MEMORY,
              "bit memory overran: %zu of %d\n", session.bit_count,
              FM_SESSION_BIT_MEMORY);
    check_size("2057 bits, not the 4092 double counting gives",
               session.bit_count, 2057);
    check_msg(session.station.funnel.groups == 19,
              "19 groups, not the 31 double counting gives: got %ld\n",
              session.station.funnel.groups);
}

/*
 * `flush` decodes a short chunk rather than throwing away what has arrived,
 * which is what a band scan needs when it is about to move on. It is as valid
 * as a full chunk and simply carries fewer bits.
 */
static void test_flush_decodes_the_remainder(void) {
    struct replay plain, flushed;

    if (!replay("testfiles/fm_rds_tsf.bin", 0, &plain) ||
        !replay("testfiles/fm_rds_tsf.bin", 1, &flushed))
        return;
    check_int("the same blocks either way", plain.blocks_fed,
              flushed.blocks_fed);
    check_msg(flushed.chunks >= plain.chunks,
              "flushing decoded %d chunks against %d without it\n",
              flushed.chunks, plain.chunks);
    check_int("and the station is the same either way", session.station.pi,
              0x8343);
}

/* Nothing to feed is not a decode, and a rate change rebuilds the front end. */
static void test_edges(void) {
    struct fm_session_event event;
    static float zero[64];

    fm_session_reset(&session);
    check_int("no samples",
              fm_session_feed(&session, zero, 0, RATE, 0.0, 0, &event), 0);
    check_int("a null session",
              fm_session_feed(NULL, zero, 64, RATE, 0.0, 0, &event), 0);
    check_int("null multiplex",
              fm_session_feed(&session, NULL, 64, RATE, 0.0, 0, &event), 0);

    /* The first block at a rate builds the front end for it. */
    fm_session_feed(&session, zero, 64, RATE, 0.0, 0, &event);
    check_int("the front end was built for this rate", event.front_rebuilt, 1);
    check_int("and not again at the same rate",
              (fm_session_feed(&session, zero, 64, RATE, 0.0, 0, &event),
               event.front_rebuilt),
              0);
    fm_session_feed(&session, zero, 64, 2000000.0, 0.0, 0, &event);
    check_int("but again at a different one", event.front_rebuilt, 1);
}

int main(void) {
    test_tsf();
    test_the_funnel_is_ordered();
    test_bits_accumulate_without_double_counting();
    test_flush_decodes_the_remainder();
    test_edges();
    return check_report("an RDS decode, block by block");
}
