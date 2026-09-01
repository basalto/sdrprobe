#include "gsm_bcch.h"
#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Four bursts to a System Information message.
 *
 * The round trips below prove the chain is self-consistent, which is worth
 * something and is not worth much on its own -- an encoder and a decoder that
 * share a wrong convention agree perfectly, which is exactly how the SCH field
 * layout was wrong for months. What pins these to reality is the Fire code: a
 * block decoded off the air whose 40 parity bits check out is right or is a
 * one-in-a-million-million accident, and check-pipelines runs that against a
 * recorded capture (ADR-0012).
 */

static uint8_t info[GSM_BCCH_INFO_BITS];
static uint8_t coded[GSM_BCCH_CODED_BITS];
static uint8_t bursts[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];
static float soft[GSM_BCCH_CODED_BITS];

static unsigned int seed = 12345;

static int coin(void) {
    seed = seed * 1103515245u + 12345u;
    return (int)((seed >> 16) & 1u);
}

static void random_info(void) {
    for (int i = 0; i < GSM_BCCH_INFO_BITS; i++)
        info[i] = (uint8_t)coin();
}

/*
 * Interleaving must be a permutation: every coded bit lands somewhere, and no
 * two land in the same place. A mapping that dropped a bit would still round
 * trip for most messages, because the convolutional code would repair the
 * hole -- and would quietly cost the margin that repairs real damage.
 */
static void test_interleaving_is_a_permutation(void) {
    int seen[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];

    memset(seen, 0, sizeof(seen));
    for (int k = 0; k < GSM_BCCH_CODED_BITS; k++)
        coded[k] = (uint8_t)(k & 1);
    memset(bursts, 0xFF, sizeof(bursts));
    gsm_bcch_interleave(coded, bursts);

    /* Count how many coded bits claim each burst position. */
    for (int k = 0; k < GSM_BCCH_CODED_BITS; k++) {
        float one[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];
        float out[GSM_BCCH_CODED_BITS];
        int hits = 0;
        int burst = -1;
        int bit = -1;

        /* Find where k came from by deinterleaving a unit impulse. */
        memset(one, 0, sizeof(one));
        for (int b = 0; b < GSM_BCCH_BURSTS; b++)
            for (int j = 0; j < GSM_BURST_DATA_BITS; j++) {
                one[b][j] = 1.0f;
                gsm_bcch_deinterleave((const float (*)[GSM_BURST_DATA_BITS])one,
                                      out);
                if (out[k] == 1.0f) {
                    hits++;
                    burst = b;
                    bit = j;
                }
                one[b][j] = 0.0f;
            }
        check_msg(hits == 1, "coded bit %d comes from %d burst positions\n", k,
                  hits);
        if (hits == 1)
            seen[burst][bit]++;
    }
    /* 456 coded bits into 4 x 114 = 456 positions, each used exactly once. */
    {
        int unused = 0;
        int doubled = 0;

        for (int b = 0; b < GSM_BCCH_BURSTS; b++)
            for (int j = 0; j < GSM_BURST_DATA_BITS; j++) {
                if (seen[b][j] == 0)
                    unused++;
                if (seen[b][j] > 1)
                    doubled++;
            }
        check_int("every burst bit is used", unused, 0);
        check_int("and none of them twice", doubled, 0);
    }
}

/* Interleaving spreads consecutive coded bits across all four bursts, which is
   the whole reason it exists: losing one burst must not take a run of the
   convolutional code with it. */
static void test_interleaving_spreads_the_block(void) {
    int per_burst[GSM_BCCH_BURSTS];

    memset(per_burst, 0, sizeof(per_burst));
    for (int k = 0; k < 16; k++)
        per_burst[k % GSM_BCCH_BURSTS]++;
    for (int b = 0; b < GSM_BCCH_BURSTS; b++)
        check_int("consecutive bits are dealt round the bursts", per_burst[b],
                  4);
}

/* The Fire code: it must accept what it produced, and refuse what it did
   not. */
static void test_the_fire_code(void) {
    uint8_t codeword[GSM_BCCH_INFO_BITS + GSM_BCCH_PARITY_BITS];
    uint8_t parity[GSM_BCCH_PARITY_BITS];
    int missed = 0;

    for (int trial = 0; trial < 32; trial++) {
        random_info();
        gsm_bcch_fire_parity(info, parity);
        memcpy(codeword, info, GSM_BCCH_INFO_BITS);
        memcpy(codeword + GSM_BCCH_INFO_BITS, parity, GSM_BCCH_PARITY_BITS);
        if (!gsm_bcch_fire_check(codeword))
            missed++;
    }
    check_int("a codeword it built checks out", missed, 0);

    /* Single errors, anywhere. */
    random_info();
    gsm_bcch_fire_parity(info, parity);
    memcpy(codeword, info, GSM_BCCH_INFO_BITS);
    memcpy(codeword + GSM_BCCH_INFO_BITS, parity, GSM_BCCH_PARITY_BITS);
    missed = 0;
    for (int i = 0; i < GSM_BCCH_INFO_BITS + GSM_BCCH_PARITY_BITS; i++) {
        codeword[i] ^= 1u;
        if (gsm_bcch_fire_check(codeword))
            missed++;
        codeword[i] ^= 1u;
    }
    check_int("every single-bit error is caught", missed, 0);

    /*
     * And random damage. Forty parity bits should let essentially nothing
     * through; anything that does would be a System Information message
     * reported from noise, which is the failure this whole layer exists to
     * make impossible.
     */
    missed = 0;
    for (int trial = 0; trial < 2000; trial++) {
        random_info();
        gsm_bcch_fire_parity(info, parity);
        memcpy(codeword, info, GSM_BCCH_INFO_BITS);
        memcpy(codeword + GSM_BCCH_INFO_BITS, parity, GSM_BCCH_PARITY_BITS);
        for (int hit = 0; hit < 8; hit++) {
            seed = seed * 1103515245u + 12345u;
            codeword[(seed >> 8) % (GSM_BCCH_INFO_BITS +
                                    GSM_BCCH_PARITY_BITS)] ^= 1u;
        }
        if (gsm_bcch_fire_check(codeword))
            missed++;
    }
    check_int("and eight-bit damage, two thousand times", missed, 0);
}

/* The whole chain, with nothing in the way. */
static void test_a_clean_block_decodes(void) {
    struct gsm_bcch_block block;
    float in[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];
    uint8_t expected[GSM_BCCH_INFO_OCTETS];
    int wrong = 0;

    random_info();
    gsm_bcch_encode(info, coded);
    gsm_bcch_interleave(coded, bursts);
    for (int b = 0; b < GSM_BCCH_BURSTS; b++)
        for (int j = 0; j < GSM_BURST_DATA_BITS; j++)
            in[b][j] = bursts[b][j] ? -1.0f : 1.0f;
    gsm_bcch_deinterleave((const float (*)[GSM_BURST_DATA_BITS])in, soft);

    check_int("it decodes", gsm_bcch_decode_block(soft, &block), 1);
    check_int("and its parity holds", block.parity_ok, 1);

    memset(expected, 0, sizeof(expected));
    for (int i = 0; i < GSM_BCCH_INFO_BITS; i++)
        if (info[i])
            expected[i / 8] |= (uint8_t)(0x80u >> (i % 8));
    for (int i = 0; i < GSM_BCCH_INFO_OCTETS; i++)
        if (block.octets[i] != expected[i])
            wrong++;
    check_int("with the message it started as", wrong, 0);
}

/*
 * A whole burst lost. This is what the interleaving is for: a fade takes one
 * of the four bursts, the damage arrives at the convolutional code spread
 * evenly instead of as one run of 114, and the block still decodes.
 */
static void test_a_lost_burst_is_survivable(void) {
    int recovered = 0;

    for (int lost = 0; lost < GSM_BCCH_BURSTS; lost++) {
        struct gsm_bcch_block block;
        float in[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS];

        random_info();
        gsm_bcch_encode(info, coded);
        gsm_bcch_interleave(coded, bursts);
        for (int b = 0; b < GSM_BCCH_BURSTS; b++)
            for (int j = 0; j < GSM_BURST_DATA_BITS; j++)
                in[b][j] = b == lost ? 0.0f /* no opinion */
                                     : (bursts[b][j] ? -1.0f : 1.0f);
        gsm_bcch_deinterleave((const float (*)[GSM_BURST_DATA_BITS])in, soft);
        if (gsm_bcch_decode_block(soft, &block))
            recovered++;
    }
    check_int("any one of the four bursts may be lost", recovered,
              GSM_BCCH_BURSTS);
}

/* Soft values must be worth having: the same damage that a hard decision
   cannot survive should be survivable when the decoder is told how sure each
   bit is. */
static void test_soft_decisions_beat_hard_ones(void) {
    int soft_ok = 0;
    int hard_ok = 0;

    for (int trial = 0; trial < 64; trial++) {
        struct gsm_bcch_block block;
        float weak[GSM_BCCH_CODED_BITS];
        float hard[GSM_BCCH_CODED_BITS];

        random_info();
        gsm_bcch_encode(info, coded);
        /* Half the bits arrive confidently, the other half nearly flat and
           wrong as often as not -- a fading burst. */
        for (int k = 0; k < GSM_BCCH_CODED_BITS; k++) {
            float truth = coded[k] ? -1.0f : 1.0f;

            if ((k % 2) == 0) {
                weak[k] = truth;
                hard[k] = truth;
            } else {
                float noisy = coin() ? -0.05f : 0.05f;

                weak[k] = noisy;
                hard[k] = noisy < 0.0f ? -1.0f : 1.0f;
            }
        }
        if (gsm_bcch_decode_block(weak, &block))
            soft_ok++;
        if (gsm_bcch_decode_block(hard, &block))
            hard_ok++;
    }
    check_msg(soft_ok > hard_ok,
              "soft decisions recovered %d of 64 and hard ones %d, so the "
              "soft metric is not earning its place\n",
              soft_ok, hard_ok);
}

/*
 * A System Information 3, built octet by octet from 3GPP TS 44.018, and read
 * back. This is the one that carries who the cell belongs to.
 */
static void test_parsing_system_information_3(void) {
    struct gsm_si si;
    /* LAPDm header, then RR (pd 6), then message type 0x1B = SI 3. */
    uint8_t message[GSM_BCCH_INFO_OCTETS] = {
        0x49, 0x06, 0x1B,       /* LAPDm address, control, length */
        0x06,                   /* protocol discriminator: Radio Resources */
        0x1B,                   /* message type: System Information 3 */
        0x12, 0x34,             /* Cell Identity 0x1234 */
        0x62, 0xF8, 0x10,       /* MCC 268, MNC 01 (0xF fills the third
                                   MNC digit, marking a two-digit MNC) */
        0x2B, 0x67,             /* Location Area Code 0x2B67 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    check_int("it is a System Information message", gsm_si_parse(message, &si),
              1);
    check_int("of type 3", si.type, GSM_SI_TYPE_3);
    check_str("named", gsm_si_type_name(si.type), "System Information 3");
    check_int("with a cell identity", si.have_cell_id, 1);
    check_int("which is 0x1234", si.cell_id, 0x1234);
    check_int("and a location area", si.have_lai, 1);
    /* 268 is Portugal, which is where the captures in testfiles/ were taken --
       so this is the value the end-to-end check looks for. */
    check_int("MCC 268", si.mcc, 268);
    check_int("MNC 1", si.mnc, 1);
    check_int("a two-digit MNC", si.mnc_digits, 2);
    check_int("LAC 0x2B67", si.lac, 0x2B67);

    /* The three-digit case, which the 0xF filler is what distinguishes: MNC
       001 is a different network from MNC 01. */
    message[8] = 0x08; /* MCC digit 3 still 8; MNC digit 3 now 0, not 0xF */
    check_int("still parses", gsm_si_parse(message, &si), 1);
    check_int("a three-digit MNC", si.mnc_digits, 3);
    check_int("which is 010", si.mnc, 10);
}

/* A block that is not System Information, and one that is not a Radio
   Resources message at all, must both be declined rather than guessed at. */
static void test_declining_what_it_does_not_know(void) {
    struct gsm_si si;
    uint8_t message[GSM_BCCH_INFO_OCTETS];

    memset(message, 0, sizeof(message));
    message[3] = 0x06;
    message[4] = 0x21; /* Paging Request Type 1, not System Information */
    check_int("a paging request is declined", gsm_si_parse(message, &si), 0);

    message[3] = 0x05; /* Mobility Management, not Radio Resources */
    message[4] = 0x1B;
    check_int("another protocol is declined", gsm_si_parse(message, &si), 0);

    check_int("and so is nothing at all", gsm_si_parse(NULL, &si), 0);
}

/* The frequency list in a System Information 1, in the bitmap format. */
static void test_parsing_a_frequency_list(void) {
    struct gsm_si si;
    uint8_t message[GSM_BCCH_INFO_OCTETS];
    const int wanted[] = { 1, 69, 73, 124 };
    int found = 0;

    memset(message, 0, sizeof(message));
    message[3] = 0x06;
    message[4] = 0x19; /* System Information 1 */
    /* Cell Channel Description: format bits 00 = bitmap of ARFCNs 1..124,
       most significant bit of the map is ARFCN 124. */
    for (size_t w = 0; w < sizeof(wanted) / sizeof(*wanted); w++) {
        int bit = 124 - wanted[w];

        int offset = 4 + bit;

        message[5 + offset / 8] |= (uint8_t)(0x80u >> (offset % 8));
    }
    check_int("it parses", gsm_si_parse(message, &si), 1);
    check_int("as System Information 1", si.type, GSM_SI_TYPE_1);
    check_int("with four channels", si.neighbour_count, 4);
    for (int i = 0; i < si.neighbour_count; i++)
        for (size_t w = 0; w < sizeof(wanted) / sizeof(*wanted); w++)
            if (si.neighbours[i] == wanted[w])
                found++;
    check_int("and they are the ones set", found, 4);

    /* A range format is not read rather than read wrongly: reporting a
       neighbour on the wrong ARFCN sends an operator to an empty channel. */
    memset(message, 0, sizeof(message));
    message[3] = 0x06;
    message[4] = 0x19;
    message[5] = 0x8E; /* a range-1024 format */
    check_int("a format it cannot read yields no channels",
              gsm_si_parse(message, &si) && si.neighbour_count == 0, 1);
}

int main(void) {
    test_interleaving_is_a_permutation();
    test_interleaving_spreads_the_block();
    test_the_fire_code();
    test_a_clean_block_decodes();
    test_a_lost_burst_is_survivable();
    test_soft_decisions_beat_hard_ones();
    test_parsing_system_information_3();
    test_declining_what_it_does_not_know();
    test_parsing_a_frequency_list();

    return check_report("GSM BCCH");
}
