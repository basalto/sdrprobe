#include "tetra_sync.h"

#include <string.h>

/*
 * The scrambling sequence, clause 8.2.5.2.
 *
 * c(x) = 1 + X + X^2 + X^4 + X^5 + X^7 + X^8 + X^10 + X^11 + X^12 + X^16 +
 *        X^22 + X^23 + X^26 + X^32, and p(k) = sum over i of c_i * p(k-i).
 *
 * Initialised p(k) = e(1-k) for k = -29..0 and p(k) = 1 for k = -31, -30 --
 * and for the synchronization channel every bit of the extended colour code
 * e() is zero. That is not an implementation shortcut but the point of the
 * channel: a terminal must be able to read this from a network whose colour
 * code it does not yet know, which is exactly the situation this program is
 * in.
 */
static const int scramble_taps[] = { 1, 2, 4, 5, 7, 8, 10, 11, 12, 16, 22, 23,
                                     26, 32 };

static void scramble_sequence(uint8_t *out, int count,
                              const uint8_t *colour) {
    /* history[i] holds p(k - 1 - i), so history[0] is the most recent. */
    uint8_t history[32];
    int k, t;

    memset(history, 0, sizeof(history));
    /*
     * history[i-1] holds p(k-i), so at the first step (k = 1) history[i-1] is
     * p(1-i): history[0] is p(0), history[29] is p(-29), history[30] is p(-30)
     * and history[31] is p(-31).
     *
     * Equation (8.42) sets p(-29..0) from the extended colour code -- all zero
     * for this channel -- and p(-31) and p(-30) to one. So the ones go in the
     * last two slots. Getting this off by one was worth 202 synchronization
     * bursts and not a single parity check, and a round trip could not see it:
     * a wrong scrambling sequence is its own inverse just as a right one is.
     */
    history[30] = 1;
    history[31] = 1;
    /* p(k) = e(1-k) for k = -29..0, so e(1) is history[0] and e(30) is
       history[29]. All zero for the synchronization block, and the network's
       own for everything else. */
    if (colour) {
        int i;
        for (i = 0; i < TETRA_COLOUR_BITS; i++)
            history[i] = (uint8_t)(colour[i] & 1);
    }
    for (k = 0; k < count; k++) {
        unsigned int bit = 0;
        for (t = 0; t < (int)(sizeof(scramble_taps) / sizeof(*scramble_taps));
             t++)
            bit ^= history[scramble_taps[t] - 1];
        out[k] = (uint8_t)(bit & 1u);
        memmove(history + 1, history, sizeof(history) - 1);
        history[0] = out[k];
    }
}

void tetra_descramble(uint8_t *bits, int count,
                      const uint8_t colour[TETRA_COLOUR_BITS]) {
    static uint8_t p[TETRA_BNCH_SCRAMBLED_BITS];
    int k;

    if (!bits || count <= 0 || count > TETRA_BNCH_SCRAMBLED_BITS)
        return;
    scramble_sequence(p, count, colour);
    for (k = 0; k < count; k++)
        bits[k] ^= p[k];
}

void tetra_sb_descramble(uint8_t bits[TETRA_SB_SCRAMBLED_BITS]) {
    tetra_descramble(bits, TETRA_SB_SCRAMBLED_BITS, NULL);
}

void tetra_extended_colour(int mcc, int mnc, int colour,
                           uint8_t out[TETRA_COLOUR_BITS]) {
    int i, at = 0;

    if (!out)
        return;
    for (i = 9; i >= 0; i--)
        out[at++] = (uint8_t)((mcc >> i) & 1);
    for (i = 13; i >= 0; i--)
        out[at++] = (uint8_t)((mnc >> i) & 1);
    for (i = 5; i >= 0; i--)
        out[at++] = (uint8_t)((colour >> i) & 1);
}

void tetra_deinterleave(const uint8_t *in, uint8_t *out, int count,
                        int stride) {
    int i;

    /* b4(k) = b3(i) with k = 1 + ((a * i) mod K), one-based throughout the
       standard and zero-based here, which is the usual place to go wrong. */
    for (i = 1; i <= count; i++)
        out[i - 1] = in[(1 + ((stride * i) % count)) - 1];
}

void tetra_sb_deinterleave(const uint8_t in[TETRA_SB_SCRAMBLED_BITS],
                           uint8_t out[TETRA_SB_SCRAMBLED_BITS]) {
    tetra_deinterleave(in, out, TETRA_SB_SCRAMBLED_BITS,
                       TETRA_SB_INTERLEAVE_STRIDE);
}

int tetra_rcpc_puncture_index(int j) {
    /* The pattern repeats every three output bits and does not depend on the
       block length, so the same function serves both chains. */
    /*
     * Clause 8.2.3.1.2 and 8.2.3.1.3: b3(j) = V(k) with
     * k = 8 * ((i-1) div t) + P(i - t * ((i-1) div t)), t = 3, i = j, and
     * P = {1, 2, 5}.
     *
     * Which is to say: out of every eight bits the mother code makes from two
     * input bits, keep the first, the second and the fifth. Two in, three out.
     */
    static const int p[3] = { 1, 2, 5 };
    int group, within;

    if (j < 1 || j > TETRA_BNCH_SCRAMBLED_BITS)
        return -1;
    group = (j - 1) / 3;
    within = j - 3 * group;         /* 1, 2 or 3 */
    return 8 * group + p[within - 1] - 1;   /* zero-based */
}

/*
 * The mother code, clause 8.2.3.1.1. Generators (8.3) to (8.6):
 *
 *   G1 = 1 + D + D^4
 *   G2 = 1 + D^2 + D^3 + D^4
 *   G3 = 1 + D + D^2 + D^4
 *   G4 = 1 + D + D^3 + D^4
 *
 * State is the four previous input bits, most recent in bit 0.
 */
static void mother_outputs(int state, int input, uint8_t out[4]) {
    int b1 = state & 1;             /* b2(k-1) */
    int b2 = (state >> 1) & 1;      /* b2(k-2) */
    int b3 = (state >> 2) & 1;      /* b2(k-3) */
    int b4 = (state >> 3) & 1;      /* b2(k-4) */

    out[0] = (uint8_t)(input ^ b1 ^ b4);
    out[1] = (uint8_t)(input ^ b2 ^ b3 ^ b4);
    out[2] = (uint8_t)(input ^ b1 ^ b2 ^ b4);
    out[3] = (uint8_t)(input ^ b1 ^ b3 ^ b4);
}

static int next_state(int state, int input) {
    return ((state << 1) | input) & (TETRA_RCPC_STATES - 1);
}

void tetra_rcpc_decode(const uint8_t *mother, const uint8_t *erased,
                       int type2_bits, uint8_t *out) {
    /*
     * Hard-decision Viterbi over a terminated code: the four tail bits are
     * zero, so the survivor to take at the end is the one ending in state 0
     * rather than the best of the sixteen. Taking the best would throw away
     * the only thing the tail is for.
     */
    static int cost[TETRA_BNCH_TYPE2_BITS + 1][TETRA_RCPC_STATES];
    static uint8_t from[TETRA_BNCH_TYPE2_BITS + 1][TETRA_RCPC_STATES];
    static uint8_t bit[TETRA_BNCH_TYPE2_BITS + 1][TETRA_RCPC_STATES];
    const int big = 1 << 24;
    int k, s, u, state;

    for (s = 0; s < TETRA_RCPC_STATES; s++)
        cost[0][s] = big;
    cost[0][0] = 0;
    for (k = 0; k < type2_bits; k++) {
        for (s = 0; s < TETRA_RCPC_STATES; s++)
            cost[k + 1][s] = big;
        for (s = 0; s < TETRA_RCPC_STATES; s++) {
            if (cost[k][s] >= big)
                continue;
            for (u = 0; u < 2; u++) {
                uint8_t expect[4];
                int n = next_state(s, u), d = cost[k][s], i;

                mother_outputs(s, u, expect);
                for (i = 0; i < 4; i++) {
                    int at = 4 * k + i;
                    if (erased[at])
                        continue;   /* punctured: no evidence either way */
                    if (mother[at] != expect[i])
                        d++;
                }
                if (d < cost[k + 1][n]) {
                    cost[k + 1][n] = d;
                    from[k + 1][n] = (uint8_t)s;
                    bit[k + 1][n] = (uint8_t)u;
                }
            }
        }
    }
    state = 0;                      /* terminated by the four zero tail bits */
    for (k = type2_bits; k > 0; k--) {
        out[k - 1] = bit[k][state];
        state = from[k][state];
    }
}

unsigned int tetra_sb_crc(const uint8_t *bits, int count) {
    /*
     * Clause 8.2.3.3, which is ITU-T X.25: G(X) = X^16 + X^12 + X^5 + 1, the
     * register preset to ones -- the X^K1 * sum(X^i) term of equation (8.15)
     * -- and the remainder complemented, which is the trailing sum(X^i).
     */
    unsigned int reg = 0xffffu;
    int i;

    for (i = 0; i < count; i++) {
        unsigned int feedback = ((reg >> 15) & 1u) ^ (bits[i] & 1u);
        reg = (reg << 1) & 0xffffu;
        if (feedback)
            reg ^= 0x1021u;
    }
    return (~reg) & 0xffffu;
}

/*
 * One chain, both channels. The differences are all lengths and one colour
 * code, so writing it twice would mean two places to get the scrambler seed
 * wrong instead of one -- and it was wrong once already.
 */
static int decode_block(const unsigned char *dibits, int scrambled_bits,
                        int type2_bits, int message_bits, int stride,
                        const uint8_t *colour, uint8_t *message) {
    static uint8_t bits[TETRA_BNCH_SCRAMBLED_BITS];
    static uint8_t type3[TETRA_BNCH_SCRAMBLED_BITS];
    static uint8_t mother[4 * TETRA_BNCH_TYPE2_BITS];
    static uint8_t erased[4 * TETRA_BNCH_TYPE2_BITS];
    static uint8_t type2[TETRA_BNCH_TYPE2_BITS];
    unsigned int expected, carried = 0;
    int k;

    if (!dibits || !message)
        return 0;
    /* B(2k-1) is the high bit of the dibit and B(2k) the low one, which is how
       table 5.1 reads them. */
    for (k = 0; k < scrambled_bits / 2; k++) {
        bits[2 * k] = (uint8_t)((dibits[k] >> 1) & 1);
        bits[2 * k + 1] = (uint8_t)(dibits[k] & 1);
    }
    tetra_descramble(bits, scrambled_bits, colour);
    {
        static uint8_t copy[TETRA_BNCH_SCRAMBLED_BITS];
        memcpy(copy, bits, (size_t)scrambled_bits);
        tetra_deinterleave(copy, type3, scrambled_bits, stride);
    }
    memset(mother, 0, (size_t)(4 * type2_bits));
    memset(erased, 1, (size_t)(4 * type2_bits));
    for (k = 1; k <= scrambled_bits; k++) {
        int at = tetra_rcpc_puncture_index(k);
        if (at < 0 || at >= 4 * type2_bits)
            continue;
        mother[at] = type3[k - 1];
        erased[at] = 0;
    }
    tetra_rcpc_decode(mother, erased, type2_bits, type2);

    expected = tetra_sb_crc(type2, message_bits);
    for (k = 0; k < TETRA_SB_CRC_BITS; k++)
        carried = (carried << 1) | (unsigned int)(type2[message_bits + k] & 1u);
    if (carried != expected)
        return 0;
    memcpy(message, type2, (size_t)message_bits);
    return 1;
}

int tetra_bnch_decode(const unsigned char *dibits,
                      const uint8_t colour[TETRA_COLOUR_BITS],
                      uint8_t message[TETRA_BNCH_MESSAGE_BITS]) {
    return decode_block(dibits, TETRA_BNCH_SCRAMBLED_BITS,
                        TETRA_BNCH_TYPE2_BITS, TETRA_BNCH_MESSAGE_BITS,
                        TETRA_BNCH_INTERLEAVE_STRIDE, colour, message);
}

int tetra_sync_block_decode(const unsigned char *dibits,
                            uint8_t message[TETRA_SB_MESSAGE_BITS]) {
    /* All zeros: the synchronization block is the one a terminal must read
       before it knows the network's colour code. */
    return decode_block(dibits, TETRA_SB_SCRAMBLED_BITS, TETRA_SB_TYPE2_BITS,
                        TETRA_SB_MESSAGE_BITS, TETRA_SB_INTERLEAVE_STRIDE,
                        NULL, message);
}
