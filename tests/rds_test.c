#include "check.h"

#include "fm_dsp.h"
#include "rds.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * RDS: blocks, groups, and what a station calls itself.
 *
 * Synchronisation here is a search -- RDS has no preamble anywhere, so a
 * receiver slides the block code along the bitstream until the syndrome comes
 * out equal to an offset word. The first thing checked below is therefore what
 * that search scores on bits that mean nothing, because a search whose floor
 * is unknown cannot be said to have found anything.
 *
 * And the synthetic tests below are a round trip: rds_encode_block feeding
 * rds_decode. They check the arithmetic and the state machine, and they would
 * pass just as happily if the whole file had a convention backwards. What
 * settles that is the real capture at the end, which reads a programme
 * identification and a name off the air and is asked to agree with three facts
 * that were not put there by this program.
 */

static unsigned long seed = 0x9E3779B9UL;
static unsigned rnd(void) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned)(seed >> 33);
}

static void test_the_block_code(void) {
    /* A block built with an offset has that offset as its syndrome. That is
       the whole trick: the code makes a valid block divisible, and adding the
       offset makes the remainder say which position it is. */
    int o;
    for (o = 0; o < RDS_OFFSET_COUNT; o++) {
        uint32_t block = rds_encode_block(0x1234, (enum rds_offset)o);
        check_msg(rds_syndrome(block) == rds_offset_word((enum rds_offset)o),
                  "offset %d: syndrome 0x%03X, expected 0x%03X\n", o,
                  rds_syndrome(block), rds_offset_word((enum rds_offset)o));
        check_msg(rds_offset_of(rds_syndrome(block)) == o,
                  "offset %d did not identify itself\n", o);
        /* And the data survives the round trip. */
        check_msg((block >> RDS_CHECK_BITS) == 0x1234,
                  "offset %d mangled the data\n", o);
    }
    check_int("a syndrome that is no offset says so", rds_offset_of(0x001), -1);

    /* Every offset is distinct, or two positions in a group would be
       indistinguishable and the sequence check below would be vacuous. */
    {
        int a, b;
        for (a = 0; a < RDS_OFFSET_COUNT; a++)
            for (b = a + 1; b < RDS_OFFSET_COUNT; b++)
                check_msg(rds_offset_word((enum rds_offset)a) !=
                          rds_offset_word((enum rds_offset)b),
                          "offsets %d and %d are the same word\n", a, b);
    }
    /* C and C' are both the third block; nothing else shares a position. */
    check_int("A is first", rds_offset_position(RDS_OFFSET_A), 0);
    check_int("B is second", rds_offset_position(RDS_OFFSET_B), 1);
    check_int("C is third", rds_offset_position(RDS_OFFSET_C), 2);
    check_int("and so is C prime", rds_offset_position(RDS_OFFSET_C_PRIME), 2);
    check_int("D is fourth", rds_offset_position(RDS_OFFSET_D), 3);

    /* A single bit flipped anywhere must break the syndrome, or the code is
       not doing the one job it has. */
    {
        int bit, broken = 0;
        uint32_t good = rds_encode_block(0xBEEF, RDS_OFFSET_A);
        for (bit = 0; bit < RDS_BLOCK_BITS; bit++)
            if (rds_syndrome(good ^ (1u << bit)) !=
                rds_offset_word(RDS_OFFSET_A))
                broken++;
        check_int("every single-bit error is caught", broken, RDS_BLOCK_BITS);
    }
}

/*
 * The floor. What the search scores on nothing.
 *
 * The ticket for this work asks for this before anything else, and it is the
 * right order: a ten-bit syndrome matching one of five offset words happens
 * about once in two hundred tries, so a lone block agreeing proves nothing at
 * all. Four in the right order is what makes it safe, and this measures both
 * the claim and the reality.
 */
static void test_the_search_has_a_floor(void) {
    static float noise_bits[40000];
    struct rds_station station;
    size_t groups, i;
    long hits = 0;

    check_close("one block agrees by chance about 4900 times in a million",
                rds_sync_odds_per_million(1), 4883.0, 1.0);
    check_true("four in a row, under one in a million",
               rds_sync_odds_per_million(4) < 1.0);
    check_true("and far under: less than a thousandth of one",
               rds_sync_odds_per_million(4) < 0.001);
    check_true("more blocks is always safer",
               rds_sync_odds_per_million(5) < rds_sync_odds_per_million(4));

    /* Now the reality, on bits that mean nothing. */
    for (i = 0; i < sizeof(noise_bits) / sizeof(noise_bits[0]); i++)
        noise_bits[i] = (rnd() & 1u) ? -1.0f : 1.0f;

    /* Single blocks do hit, at about the rate predicted -- which is the
       point: this is why one is not enough. */
    for (i = 0; i + RDS_BLOCK_BITS <= 40000; i++) {
        uint32_t block = 0;
        int k;
        for (k = 0; k < RDS_BLOCK_BITS; k++)
            block = (block << 1) | (noise_bits[i + (size_t)k] < 0.0f ? 1u : 0u);
        if (rds_offset_of(rds_syndrome(block)) >= 0)
            hits++;
    }
    check_msg(hits > 100 && hits < 400,
              "single blocks hit %ld times in 40000 positions of noise; "
              "about 195 was expected\n", hits);

    /* And the synchroniser, which wants four in order, finds nothing. */
    groups = rds_decode(noise_bits, 40000, &station, NULL, 0);
    check_msg(groups == 0, "the synchroniser found %zu groups in noise\n",
              groups);
    check_true("and no identification came out of it", !station.pi_valid);
    check_true("nor a name", !station.ps_valid);
    /* The funnel says where it stopped, which on noise is at the first
       rung: bits offered, nothing assembled from them. */
    check_int("the funnel counted the bits", (int)station.funnel.bits, 40000);
    check_int("and no groups", (int)station.funnel.groups, 0);
    check_int("and nothing identified", (int)station.funnel.identified, 0);
}

/* Build a bitstream of groups: `type` and `version` in block 2, and whatever
   the caller puts in blocks 3 and 4. */
static size_t build_groups(float *soft, size_t capacity, uint16_t pi,
                           const uint16_t *b2, const uint16_t *b3,
                           const uint16_t *b4, int count, size_t lead) {
    size_t at = 0;
    int g, i, k;

    /* A run of nothing first, so the synchroniser has to actually search
       rather than starting on a group boundary by luck. */
    for (i = 0; i < (int)lead && at < capacity; i++)
        soft[at++] = (rnd() & 1u) ? -1.0f : 1.0f;

    for (g = 0; g < count; g++) {
        uint32_t blocks[RDS_BLOCKS_PER_GROUP];
        int version_b = (b2[g] >> 11) & 1;

        blocks[0] = rds_encode_block(pi, RDS_OFFSET_A);
        blocks[1] = rds_encode_block(b2[g], RDS_OFFSET_B);
        blocks[2] = rds_encode_block(b3[g], version_b ? RDS_OFFSET_C_PRIME
                                                      : RDS_OFFSET_C);
        blocks[3] = rds_encode_block(b4[g], RDS_OFFSET_D);
        for (i = 0; i < RDS_BLOCKS_PER_GROUP; i++)
            for (k = RDS_BLOCK_BITS - 1; k >= 0 && at < capacity; k--)
                soft[at++] = ((blocks[i] >> k) & 1u) ? -1.0f : 1.0f;
    }
    return at;
}

/* Group 0A block 2: type 0, version A, TP, PTY, TA/MS/DI, segment. */
static uint16_t group0a_b2(int pty, int tp, int ta, int segment) {
    return (uint16_t)((0 << 12) | (0 << 11) | ((tp & 1) << 10) |
                      ((pty & 0x1F) << 5) | ((ta & 1) << 4) | (segment & 3));
}

static void test_a_name_arrives(void) {
    static float soft[8192];
    struct rds_station station;
    uint16_t b2[16], b3[16], b4[16];
    const char *name = "RADIO 1 ";
    size_t bits, groups;
    int g;

    /* Two full passes of the four segments, because a name is only shown once
       it has been seen whole twice. */
    for (g = 0; g < 8; g++) {
        int segment = g % 4;
        b2[g] = group0a_b2(10, 1, 0, segment);
        b3[g] = 0xE0E0;   /* alternative frequencies; unused here */
        b4[g] = (uint16_t)(((unsigned char)name[segment * 2] << 8) |
                           (unsigned char)name[segment * 2 + 1]);
    }
    bits = build_groups(soft, 8192, 0x1234, b2, b3, b4, 8, 37);
    groups = rds_decode(soft, bits, &station, NULL, 0);

    check_size("all eight groups were read", groups, 8);
    check_true("the identification came out", station.pi_valid);
    check_int("and is what was sent", (int)station.pi, 0x1234);
    check_true("agreed on by every group", station.pi_repeats >= 8);
    check_int("the programme type came out", station.pty, 10);
    check_str("with a name of its own", rds_pty_name(10), "pop music");
    check_int("the traffic flag came out", station.tp, 1);
    check_str("and the station's name", station.ps, name);
    check_true("which is complete", station.ps_segments == 0xF);
    check_true("and confirmed", station.ps_valid);

    /* The funnel, all the way down. */
    check_int("the funnel counted the groups", (int)station.funnel.groups, 8);
    check_int("every one of them identified",
              (int)station.funnel.identified, 8);
    check_int("all four blocks of each matched",
              (int)station.funnel.blocks_matched, 32);
    check_int("and the name was confirmed once", (int)station.funnel.named, 1);
}

/*
 * A half-filled name is a wrong name.
 *
 * Two characters at a time means "RADIO 1" passes through "RA", "RADI",
 * "RADIO ", and each of those is a station that does not exist. Nothing may
 * be shown until all four segments have arrived and the whole has repeated.
 */
static void test_a_half_name_is_not_shown(void) {
    static float soft[8192];
    struct rds_station station;
    uint16_t b2[4], b3[4], b4[4];
    const char *name = "RADIO 1 ";
    size_t bits, groups;
    int g;

    for (g = 0; g < 3; g++) {          /* three of the four segments */
        b2[g] = group0a_b2(10, 0, 0, g);
        b3[g] = 0;
        b4[g] = (uint16_t)(((unsigned char)name[g * 2] << 8) |
                           (unsigned char)name[g * 2 + 1]);
    }
    bits = build_groups(soft, 8192, 0x1234, b2, b3, b4, 3, 11);
    groups = rds_decode(soft, bits, &station, NULL, 0);

    check_size("the groups were read", groups, 3);
    check_true("but the name is not complete", station.ps_segments != 0xF);
    check_true("and is not offered", !station.ps_valid);

    /* One complete pass is still not enough: four segments can be four
       segments of two different names and look perfect. */
    for (g = 0; g < 4; g++) {
        b2[g] = group0a_b2(10, 0, 0, g);
        b3[g] = 0;
        b4[g] = (uint16_t)(((unsigned char)name[g * 2] << 8) |
                           (unsigned char)name[g * 2 + 1]);
    }
    bits = build_groups(soft, 8192, 0x1234, b2, b3, b4, 4, 11);
    rds_decode(soft, bits, &station, NULL, 0);
    check_true("one whole pass is complete", station.ps_segments == 0xF);
    check_true("and still not confirmed", !station.ps_valid);
}

static void test_a_changed_name_is_not_spliced(void) {
    static float soft[8192];
    struct rds_station station;
    uint16_t b2[12], b3[12], b4[12];
    const char *first = "OLDNAME ";
    const char *second = "NEWNAME ";
    size_t bits;
    int g;

    /* Two segments of one name, then all four of another, twice. A decoder
       that kept what it had would report OLDNAME's first half spliced onto
       NEWNAME's second. */
    for (g = 0; g < 12; g++) {
        const char *from = g < 2 ? first : second;
        int segment = g < 2 ? g : (g - 2) % 4;
        b2[g] = group0a_b2(3, 0, 0, segment);
        b3[g] = 0;
        b4[g] = (uint16_t)(((unsigned char)from[segment * 2] << 8) |
                           (unsigned char)from[segment * 2 + 1]);
    }
    bits = build_groups(soft, 8192, 0x1234, b2, b3, b4, 12, 5);
    rds_decode(soft, bits, &station, NULL, 0);
    check_str("the name that is actually being sent", station.ps, second);
    check_true("and it is confirmed", station.ps_valid);
}

static void test_version_b_repeats_the_identification(void) {
    static float soft[8192];
    struct rds_station station;
    uint16_t b2[4], b3[4], b4[4];
    size_t bits, groups;
    int g;

    for (g = 0; g < 4; g++) {
        /* Type 0, version B: the third block is the identification again. */
        b2[g] = (uint16_t)((0 << 12) | (1 << 11) | (1 << 10) | (7 << 5) | g);
        b3[g] = 0xABCD;   /* the repeat */
        b4[g] = 0x2020;
    }
    bits = build_groups(soft, 8192, 0xABCD, b2, b3, b4, 4, 3);
    groups = rds_decode(soft, bits, &station, NULL, 0);
    check_size("version B groups are read", groups, 4);
    check_int("and identified", (int)station.pi, 0xABCD);
    check_int("with the programme type from block two", station.pty, 7);
}

static void test_it_refuses_nonsense(void) {
    struct rds_station station;
    static float soft[200];

    check_size("no bits, no groups", rds_decode(NULL, 100, &station, NULL, 0),
               0);
    check_size("nor too few of them", rds_decode(soft, 10, &station, NULL, 0),
               0);
    check_true("an unknown programme type has no name", !rds_pty_name(99));
    check_true("nor a negative one", !rds_pty_name(-1));
    check_str("and a known one does", rds_pty_name(1), "news");
}

/*
 * The real capture, which is the only check here that can fail for the right
 * reason.
 *
 * Everything above is rds_encode_block feeding rds_decode. What this asks is
 * that bits off the air produce a station, and that three facts from three
 * different places in the signal agree about it:
 *
 *   - the programme service name, assembled from block 4 of eight group 0A
 *     transmissions, reads TSF;
 *   - the programme type, which lives in block 2 of *every* group and has
 *     nothing to do with the name, reads news -- and TSF Radio Noticias is a
 *     news station;
 *   - the identification's top nibble is the country, and the other station
 *     recorded at this site the same evening, at 87.7 MHz, carries the same
 *     nibble with an otherwise different code (0x8442 against 0x8343).
 *
 * No encoder of mine put any of that there.
 */
static void test_a_real_station(void) {
    const char *path = "testfiles/fm_rds_tsf.bin";
    static uint8_t raw[8192000];
    static float mpx[4096000];
    static float bb_i[64000], bb_q[64000];
    static float soft[8192];
    struct fm_rds_front front;
    struct rds_station station;
    FILE *f = fopen(path, "rb");
    size_t bytes, n, bb, bits, groups;
    int offset;
    double axis;

    if (!f) {
        check_msg(0, "cannot open %s -- run from the repository root\n", path);
        return;
    }
    bytes = fread(raw, 1, sizeof(raw), f);
    fclose(f);

    n = fm_discriminate(raw, bytes / 2, mpx,
                        sizeof(mpx) / sizeof(mpx[0]));
    check_int("the front end takes the capture", fm_rds_front_init(&front,
                                                                   2048000.0),
              0);
    bb = fm_rds_front_feed(&front, mpx, n, bb_i, bb_q, 64000);
    bits = fm_rds_soft_bits(bb_i, bb_q, bb, soft, 8192, &offset, &axis);
    check_true("soft bits came off the capture", bits > 1500);

    groups = rds_decode(soft, bits, &station, NULL, 0);
    /* 18 read of the 22 two seconds can carry. */
    check_msg(groups >= 14, "only %zu groups came out of the capture\n",
              groups);

    check_true("the station identified itself", station.pi_valid);
    check_msg(station.pi == 0x8343,
              "the identification read 0x%04X, not TSF's 0x8343\n",
              station.pi);
    check_msg(station.pi_repeats >= 12,
              "only %d groups agreed on the identification\n",
              station.pi_repeats);

    check_true("it named itself", station.ps_valid);
    check_str("and the name is TSF", station.ps, " TSF    ");

    /* The corroboration: a different field, from a different block. */
    check_int("the programme type is news", station.pty, 1);
    check_str("which is what a news station sends", rds_pty_name(station.pty),
              "news");
    check_int("and it carries traffic announcements", station.tp, 1);

    /* The country nibble, which the sidecar records for the second station
       recorded at this site. */
    check_int("the identification's country nibble", (station.pi >> 12) & 0xF,
              8);
}

int main(void) {
    test_the_block_code();
    test_the_search_has_a_floor();
    test_a_name_arrives();
    test_a_half_name_is_not_shown();
    test_a_changed_name_is_not_spliced();
    test_version_b_repeats_the_identification();
    test_it_refuses_nonsense();
    test_a_real_station();

    return check_report("RDS blocks, groups and a station's name");
}
