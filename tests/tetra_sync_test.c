#include "check.h"

#include "tetra_sync.h"

#include <stdio.h>
#include <string.h>

/*
 * The synchronization block chain.
 *
 * Every constant here is transcribed from ETSI EN 300 392-2 V3.8.1, and a
 * round trip cannot check a transcription: an encoder and a decoder sharing a
 * mistake agree perfectly. This file proved that the hard way -- the whole
 * chain round-tripped on the first run while the scrambler's seed was one slot
 * out, and 202 synchronization bursts off the air produced not a single
 * passing parity.
 *
 * So the round trip below is here to catch *regressions*, not to establish
 * correctness, and the tests that establish anything are the ones that check a
 * piece against arithmetic outside it. What established the chain is recorded
 * in `.scratch/tetra-network-identity/issues/03`: 202 of 202 bursts decoding
 * with the parity checking, their frame counters advancing, and MCC 268 --
 * which gsm_bcch reads from a different technology on a different band.
 */

/* The encoder side, written here rather than in the module: nothing in the
   program transmits TETRA, so an encoder in src/ would be untested weight. */
static void mother_out(int state, int in, uint8_t o[4]) {
    int b1 = state & 1, b2 = (state >> 1) & 1;
    int b3 = (state >> 2) & 1, b4 = (state >> 3) & 1;

    o[0] = (uint8_t)(in ^ b1 ^ b4);
    o[1] = (uint8_t)(in ^ b2 ^ b3 ^ b4);
    o[2] = (uint8_t)(in ^ b1 ^ b2 ^ b4);
    o[3] = (uint8_t)(in ^ b1 ^ b3 ^ b4);
}

static void encode_any(const uint8_t *message, int message_bits,
                       int type2_bits, int scrambled_bits, int stride,
                       const uint8_t *colour, unsigned char *dibits) {
    uint8_t type2[TETRA_BNCH_TYPE2_BITS];
    uint8_t mother[4 * TETRA_BNCH_TYPE2_BITS];
    uint8_t type3[TETRA_BNCH_SCRAMBLED_BITS];
    uint8_t type4[TETRA_BNCH_SCRAMBLED_BITS];
    unsigned int crc;
    int k, state = 0;

    memcpy(type2, message, (size_t)message_bits);
    crc = tetra_sb_crc(message, message_bits);
    for (k = 0; k < TETRA_SB_CRC_BITS; k++)
        type2[message_bits + k] =
            (uint8_t)((crc >> (TETRA_SB_CRC_BITS - 1 - k)) & 1u);
    for (k = message_bits + TETRA_SB_CRC_BITS; k < type2_bits; k++)
        type2[k] = 0;
    for (k = 0; k < type2_bits; k++) {
        uint8_t o[4];
        mother_out(state, type2[k], o);
        memcpy(mother + 4 * k, o, 4);
        state = ((state << 1) | type2[k]) & (TETRA_RCPC_STATES - 1);
    }
    for (k = 1; k <= scrambled_bits; k++)
        type3[k - 1] = mother[tetra_rcpc_puncture_index(k)];
    for (k = 1; k <= scrambled_bits; k++)
        type4[(1 + ((stride * k) % scrambled_bits)) - 1] = type3[k - 1];
    tetra_descramble(type4, scrambled_bits, colour);   /* its own inverse */
    for (k = 0; k < scrambled_bits / 2; k++)
        dibits[k] = (unsigned char)((type4[2 * k] << 1) | type4[2 * k + 1]);
}

static void encode(const uint8_t message[TETRA_SB_MESSAGE_BITS],
                   unsigned char dibits[60]) {
    encode_any(message, TETRA_SB_MESSAGE_BITS, TETRA_SB_TYPE2_BITS,
               TETRA_SB_SCRAMBLED_BITS, TETRA_SB_INTERLEAVE_STRIDE, NULL,
               dibits);
}

/*
 * The parity, pinned by the one fact about it that does not come from this
 * file: a message carrying its own remainder leaves none.
 */
static void test_the_parity(void) {
    uint8_t bits[TETRA_SB_MESSAGE_BITS + TETRA_SB_CRC_BITS];
    unsigned int crc;
    int k, missed = 0, first = -1;

    for (k = 0; k < TETRA_SB_MESSAGE_BITS; k++)
        bits[k] = (uint8_t)((k * 7 + 3) & 1);
    crc = tetra_sb_crc(bits, TETRA_SB_MESSAGE_BITS);
    for (k = 0; k < TETRA_SB_CRC_BITS; k++)
        bits[TETRA_SB_MESSAGE_BITS + k] =
            (uint8_t)((crc >> (TETRA_SB_CRC_BITS - 1 - k)) & 1u);

    /* Every single-bit error is caught, which is the least a CRC must do and
       what a shift in the register would break. */
    for (k = 0; k < TETRA_SB_MESSAGE_BITS + TETRA_SB_CRC_BITS; k++) {
        unsigned int again;
        bits[k] ^= 1;
        again = tetra_sb_crc(bits, TETRA_SB_MESSAGE_BITS);
        if (k < TETRA_SB_MESSAGE_BITS) {
            if (again == crc) {
                missed++;
                if (first < 0)
                    first = k;
            }
        }
        bits[k] ^= 1;
    }
    check_msg(missed == 0,
              "%d single-bit errors left the parity unchanged, the first at "
              "%d\n", missed, first);
    check_true("and the polynomial is not the trivial one",
               tetra_sb_crc(bits, TETRA_SB_MESSAGE_BITS) != 0);
}

/* The puncturing: three of every eight mother bits, positions 1, 2 and 5 of
   each group, every index used at most once and none out of range. */
static void test_the_puncturing(void) {
    int used[4 * TETRA_SB_TYPE2_BITS];
    int j, twice = 0, out_of_range = 0;

    memset(used, 0, sizeof(used));
    for (j = 1; j <= TETRA_SB_SCRAMBLED_BITS; j++) {
        int at = tetra_rcpc_puncture_index(j);
        if (at < 0 || at >= 4 * TETRA_SB_TYPE2_BITS) {
            out_of_range++;
            continue;
        }
        if (used[at]++)
            twice++;
    }
    check_int("no punctured index is out of range", out_of_range, 0);
    check_int("and none is taken twice", twice, 0);
    /* Two input bits give eight mother bits and three survive: rate 2/3. */
    check_int("first of a group", tetra_rcpc_puncture_index(1), 0);
    check_int("second", tetra_rcpc_puncture_index(2), 1);
    check_int("fifth", tetra_rcpc_puncture_index(3), 4);
    check_int("and the next group starts eight later",
              tetra_rcpc_puncture_index(4), 8);
    check_int("out of range low", tetra_rcpc_puncture_index(0), -1);
    /* The pattern repeats every three output bits and does not depend on the
       block length, so it serves the 120-bit synchronization block and the
       216-bit broadcast channel alike; only past the longer one is it out of
       range. */
    check_true("the same pattern serves the longer block",
               tetra_rcpc_puncture_index(TETRA_SB_SCRAMBLED_BITS + 1) >= 0);
    check_int("out of range high",
              tetra_rcpc_puncture_index(TETRA_BNCH_SCRAMBLED_BITS + 1), -1);
}

/* The interleaver is a permutation, which is checkable without inverting it. */
static void test_the_interleaver(void) {
    uint8_t in[TETRA_SB_SCRAMBLED_BITS], out[TETRA_SB_SCRAMBLED_BITS];
    int seen[TETRA_SB_SCRAMBLED_BITS];
    int k, missing = 0;

    for (k = 0; k < TETRA_SB_SCRAMBLED_BITS; k++)
        in[k] = (uint8_t)(k & 1);
    tetra_sb_deinterleave(in, out);
    memset(seen, 0, sizeof(seen));
    for (k = 1; k <= TETRA_SB_SCRAMBLED_BITS; k++) {
        int at = 1 + ((TETRA_SB_INTERLEAVE_STRIDE * k) %
                      TETRA_SB_SCRAMBLED_BITS);
        seen[at - 1]++;
    }
    for (k = 0; k < TETRA_SB_SCRAMBLED_BITS; k++)
        if (seen[k] != 1)
            missing++;
    check_int("the stride visits every position exactly once", missing, 0);
    /* 11 and 120 are coprime, which is *why* it is a permutation; if that
       stopped being true the loop above would notice, but so would this. */
    {
        int a = TETRA_SB_INTERLEAVE_STRIDE, b = TETRA_SB_SCRAMBLED_BITS, t;
        while (b) { t = a % b; a = b; b = t; }
        check_int("the stride is coprime with the block", a, 1);
    }
}

/*
 * The scrambler, and the seed that was wrong.
 *
 * For this channel the extended colour code is all zeros, so the sequence is
 * fixed and the only thing that can be wrong is the seed -- which it was, by
 * one slot, and which nothing in a round trip could see. What is checkable
 * without the standard is that the sequence is not degenerate: a seed of all
 * zeros in a linear register produces all zeros for ever, and scrambling by
 * zeros is not scrambling.
 */
static void test_the_scrambler(void) {
    uint8_t bits[TETRA_SB_SCRAMBLED_BITS];
    uint8_t again[TETRA_SB_SCRAMBLED_BITS];
    int k, ones = 0;

    memset(bits, 0, sizeof(bits));
    tetra_sb_descramble(bits);
    for (k = 0; k < TETRA_SB_SCRAMBLED_BITS; k++)
        ones += bits[k];
    check_msg(ones > 30 && ones < 90,
              "the scrambling sequence has %d ones in %d bits, which is not a "
              "sequence -- a seed of all zeros gives none at all\n", ones,
              TETRA_SB_SCRAMBLED_BITS);
    /* Its own inverse, which is what makes one function serve both ways. */
    memcpy(again, bits, sizeof(again));
    tetra_sb_descramble(again);
    for (k = 0; k < TETRA_SB_SCRAMBLED_BITS; k++)
        check_msg(again[k] == 0,
                  "scrambling twice did not return bit %d to zero\n", k);
}

/*
 * The whole chain, forwards and back. A regression test rather than a proof:
 * this passed while the scrambler was wrong.
 */
static void test_the_chain_round_trips(void) {
    uint8_t message[TETRA_SB_MESSAGE_BITS], got[TETRA_SB_MESSAGE_BITS];
    unsigned char dibits[60];
    int trial, k;

    for (trial = 0; trial < 8; trial++) {
        unsigned int seed = 12345u + (unsigned int)trial * 7919u;
        for (k = 0; k < TETRA_SB_MESSAGE_BITS; k++) {
            seed = seed * 1103515245u + 12345u;
            message[k] = (uint8_t)((seed >> 20) & 1u);
        }
        encode(message, dibits);
        check_true("the block decodes", tetra_sync_block_decode(dibits, got));
        check_int("and comes back unchanged",
                  memcmp(got, message, TETRA_SB_MESSAGE_BITS), 0);
    }
}

/*
 * A block with an error in it is not reported. The GSM side sets this
 * standard: the Fire code either passes or the message is not handed back, and
 * a colour code with no check behind it is a number rather than a finding.
 */
static void test_a_damaged_block_is_refused(void) {
    uint8_t message[TETRA_SB_MESSAGE_BITS], got[TETRA_SB_MESSAGE_BITS];
    unsigned char dibits[60];
    int k, reported = 0;

    for (k = 0; k < TETRA_SB_MESSAGE_BITS; k++)
        message[k] = (uint8_t)((k * 5 + 1) & 1);
    encode(message, dibits);

    /* The code corrects a good deal, so damage has to be real: a quarter of
       the symbols replaced is well past what rate 2/3 can carry. */
    for (k = 0; k < 60; k += 4)
        dibits[k] = (unsigned char)((dibits[k] + 1) & 3);
    if (tetra_sync_block_decode(dibits, got))
        reported++;
    check_int("a heavily damaged block is not reported", reported, 0);

    /* And noise is never a message. */
    for (k = 0; k < 60; k++)
        dibits[k] = (unsigned char)((k * 13 + 7) & 3);
    check_int("nor is noise", tetra_sync_block_decode(dibits, got), 0);
}

/* Refusing nonsense rather than reading past it. */
static void test_it_refuses_nothing(void) {
    uint8_t got[TETRA_SB_MESSAGE_BITS];
    unsigned char dibits[60];

    memset(dibits, 0, sizeof(dibits));
    check_int("no message to write into", tetra_sync_block_decode(dibits, NULL),
              0);
    check_int("no dibits", tetra_sync_block_decode(NULL, got), 0);
}

/*
 * The broadcast channel: the same chain at different lengths, and scrambled
 * with the network's own colour code rather than with zeros.
 *
 * The ordering of MCC, MNC and colour code inside the 30 bits comes from
 * figure 23.5, which is an image and did not survive extraction from the PDF.
 * It was settled the way everything here is settled -- by the parity, on air:
 * MCC then MNC then colour code, most significant bit first, gives 190 of 202
 * blocks passing, and all three other orderings tried gave exactly zero.
 */
static void test_the_broadcast_channel(void) {
    uint8_t message[TETRA_BNCH_MESSAGE_BITS], got[TETRA_BNCH_MESSAGE_BITS];
    unsigned char dibits[TETRA_BNCH_SCRAMBLED_BITS / 2];
    uint8_t colour[TETRA_COLOUR_BITS], zeros[TETRA_COLOUR_BITS];
    int k, trial;

    /* The network this was read from: MCC 268, MNC 3, colour code 17. */
    tetra_extended_colour(268, 3, 17, colour);
    check_int("the extended colour code is thirty bits of MCC, MNC, colour",
              (int)sizeof(colour), TETRA_COLOUR_BITS);
    {
        int mcc = 0, mnc = 0, cc = 0;
        for (k = 0; k < 10; k++)
            mcc = (mcc << 1) | colour[k];
        for (k = 10; k < 24; k++)
            mnc = (mnc << 1) | colour[k];
        for (k = 24; k < 30; k++)
            cc = (cc << 1) | colour[k];
        check_int("MCC comes back", mcc, 268);
        check_int("MNC comes back", mnc, 3);
        check_int("colour code comes back", cc, 17);
    }

    for (trial = 0; trial < 4; trial++) {
        unsigned int seed = 777u + (unsigned int)trial * 4099u;
        for (k = 0; k < TETRA_BNCH_MESSAGE_BITS; k++) {
            seed = seed * 1103515245u + 12345u;
            message[k] = (uint8_t)((seed >> 19) & 1u);
        }
        encode_any(message, TETRA_BNCH_MESSAGE_BITS, TETRA_BNCH_TYPE2_BITS,
                   TETRA_BNCH_SCRAMBLED_BITS, TETRA_BNCH_INTERLEAVE_STRIDE,
                   colour, dibits);
        check_true("the broadcast block decodes",
                   tetra_bnch_decode(dibits, colour, got));
        check_int("and comes back unchanged",
                  memcmp(got, message, TETRA_BNCH_MESSAGE_BITS), 0);
    }

    /*
     * And the colour code is load-bearing: the same block read with the wrong
     * one must fail, or the scrambling is doing nothing and any network's
     * broadcast would decode as any other's.
     */
    memset(zeros, 0, sizeof(zeros));
    check_int("the wrong colour code does not decode",
              tetra_bnch_decode(dibits, zeros, got), 0);
    tetra_extended_colour(268, 3, 18, colour);
    check_int("nor a colour code one out",
              tetra_bnch_decode(dibits, colour, got), 0);

    /* The (216,101) stride is a permutation for the same reason (120,11) is. */
    {
        int a = TETRA_BNCH_INTERLEAVE_STRIDE, b = TETRA_BNCH_SCRAMBLED_BITS, t;
        while (b) { t = a % b; a = b; b = t; }
        check_int("101 is coprime with 216", a, 1);
    }
}

int main(void) {
    test_the_parity();
    test_the_puncturing();
    test_the_interleaver();
    test_the_scrambler();
    test_the_chain_round_trips();
    test_a_damaged_block_is_refused();
    test_the_broadcast_channel();
    test_it_refuses_nothing();

    return check_report("TETRA synchronization block");
}
