#include "gsm_bcch.h"

#include <string.h>

/*
 * Four bursts to a System Information message. See gsm_bcch.h for the layers;
 * this file is each of them in turn, in that order.
 */

/* ---- interleaving (GSM 05.03 4.1.4) ------------------------------------ */

/*
 * Where coded bit k lands: burst k mod 4, bit 2((49k) mod 57) + ((k mod 8)/4)
 * of it. Spreading a block across four bursts this way is what lets a whole
 * burst be lost to a fade and the block still decode -- the convolutional code
 * sees the damage as scattered single errors rather than one long burst of
 * them, which is the shape it can repair.
 */
static void interleave_position(int k, int *burst, int *bit) {
    *burst = k % GSM_BCCH_BURSTS;
    *bit = 2 * ((49 * k) % 57) + ((k % 8) / 4);
}

void gsm_bcch_deinterleave(const float bursts[GSM_BCCH_BURSTS]
                                             [GSM_BURST_DATA_BITS],
                           float coded[GSM_BCCH_CODED_BITS]) {
    for (int k = 0; k < GSM_BCCH_CODED_BITS; k++) {
        int burst;
        int bit;

        interleave_position(k, &burst, &bit);
        coded[k] = bursts[burst][bit];
    }
}

void gsm_bcch_interleave(const uint8_t coded[GSM_BCCH_CODED_BITS],
                         uint8_t bursts[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS]) {
    for (int k = 0; k < GSM_BCCH_CODED_BITS; k++) {
        int burst;
        int bit;

        interleave_position(k, &burst, &bit);
        bursts[burst][bit] = coded[k] & 1u;
    }
}

/* ---- the Fire code (GSM 05.03 4.1.2) ----------------------------------- */

/*
 * g(D) = (D^23 + 1)(D^17 + D^3 + 1) = D^40 + D^26 + D^23 + D^17 + D^3 + 1.
 *
 * Held as the taps below D^40, applied whenever the bit shifted out of the
 * register is a one. Forty parity bits is a great deal for 184 of payload,
 * and it is what makes a decoded System Information message believable
 * without a second opinion: the chance of noise producing a block that passes
 * is 2^-40.
 */
#define FIRE_TAPS 0x0000000004820009ull /* D^26 + D^23 + D^17 + D^3 + 1 */

/*
 * And the parity is inverted before it is sent, so a good codeword leaves the
 * register full of ones rather than empty -- the same trick GSM plays on the
 * SCH's parity, and the reason gsm_dsp.c's sch_parity() ends with an XOR too.
 *
 * This was not read out of the specification. A capture decoded the same 23
 * octets three times over, four hundred frames apart, and every block in it
 * left this remainder: forty ones, every time. Bits that repeat exactly are
 * not noise, so the bits were right and the check was wrong.
 */
#define FIRE_SYNDROME 0xFFFFFFFFFFull

static uint64_t fire_remainder(const uint8_t *bits, int count) {
    uint64_t reg = 0;

    for (int i = 0; i < count; i++) {
        uint64_t out = (reg >> 39) & 1ull;
        reg = (reg << 1) & 0xFFFFFFFFFFull; /* 40 bits */
        reg ^= (uint64_t)(bits[i] & 1u);
        if (out)
            reg ^= FIRE_TAPS;
    }
    return reg;
}

void gsm_bcch_fire_parity(const uint8_t info[GSM_BCCH_INFO_BITS],
                          uint8_t parity[GSM_BCCH_PARITY_BITS]) {
    uint8_t padded[GSM_BCCH_INFO_BITS + GSM_BCCH_PARITY_BITS];
    uint64_t reg;

    memcpy(padded, info, GSM_BCCH_INFO_BITS);
    memset(padded + GSM_BCCH_INFO_BITS, 0, GSM_BCCH_PARITY_BITS);
    reg = fire_remainder(padded, GSM_BCCH_INFO_BITS + GSM_BCCH_PARITY_BITS);
    for (int j = 0; j < GSM_BCCH_PARITY_BITS; j++)
        parity[j] = (uint8_t)(((reg >> (GSM_BCCH_PARITY_BITS - 1 - j)) & 1ull) ^
                              1ull);
}

int gsm_bcch_fire_check(const uint8_t codeword[GSM_BCCH_INFO_BITS +
                                               GSM_BCCH_PARITY_BITS]) {
    return fire_remainder(codeword,
                          GSM_BCCH_INFO_BITS + GSM_BCCH_PARITY_BITS) ==
           FIRE_SYNDROME;
}

/* ---- the convolutional code (the same one the SCH uses) ---------------- */

/* G0 = 1 + D^3 + D^4, G1 = 1 + D + D^3 + D^4, rate 1/2, constraint length 5. */
static void conv_outputs(unsigned int state, unsigned int in, unsigned int *g0,
                         unsigned int *g1) {
    *g0 = in ^ ((state >> 2) & 1u) ^ ((state >> 3) & 1u);
    *g1 = in ^ (state & 1u) ^ ((state >> 2) & 1u) ^ ((state >> 3) & 1u);
}

void gsm_bcch_encode(const uint8_t info[GSM_BCCH_INFO_BITS],
                     uint8_t coded[GSM_BCCH_CODED_BITS]) {
    uint8_t u[GSM_BCCH_UNCODED_BITS];
    uint8_t parity[GSM_BCCH_PARITY_BITS];
    unsigned int state = 0;

    gsm_bcch_fire_parity(info, parity);
    memcpy(u, info, GSM_BCCH_INFO_BITS);
    memcpy(u + GSM_BCCH_INFO_BITS, parity, GSM_BCCH_PARITY_BITS);
    memset(u + GSM_BCCH_INFO_BITS + GSM_BCCH_PARITY_BITS, 0,
           GSM_BCCH_TAIL_BITS);

    for (int k = 0; k < GSM_BCCH_UNCODED_BITS; k++) {
        unsigned int in = u[k] & 1u;
        unsigned int g0;
        unsigned int g1;

        conv_outputs(state, in, &g0, &g1);
        coded[2 * k] = (uint8_t)g0;
        coded[2 * k + 1] = (uint8_t)g1;
        state = ((state << 1) | in) & 0xFu;
    }
}

/*
 * Soft Viterbi. Both ends of the trellis are known -- the encoder starts at
 * zero and the four tail bits walk it back there -- so the traceback needs no
 * guesswork about where to begin.
 *
 * The metric is the correlation between the soft value and the bit the branch
 * would have produced, accumulated and maximised. Working in soft values
 * rather than hard decisions is worth roughly 2 dB, which on a BCCH at the
 * edge of a cell is the difference between a message and nothing.
 */
static void soft_viterbi(const float coded[GSM_BCCH_CODED_BITS],
                         uint8_t u[GSM_BCCH_UNCODED_BITS]) {
    static uint8_t back[GSM_BCCH_UNCODED_BITS][16];
    float metric[16];
    float next[16];
    const float unreachable = -1e30f;

    for (int s = 0; s < 16; s++)
        metric[s] = (s == 0) ? 0.0f : unreachable;

    for (int k = 0; k < GSM_BCCH_UNCODED_BITS; k++) {
        float r0 = coded[2 * k];
        float r1 = coded[2 * k + 1];

        for (int s = 0; s < 16; s++)
            next[s] = unreachable;
        for (int s = 0; s < 16; s++) {
            if (metric[s] <= unreachable)
                continue;
            for (unsigned int in = 0; in < 2; in++) {
                unsigned int g0;
                unsigned int g1;
                float score;
                int ns;

                conv_outputs((unsigned int)s, in, &g0, &g1);
                /* A soft value is positive for a 0 bit, so the expected sign
                   is +1 for a 0 branch and -1 for a 1 branch. */
                score = metric[s] + (g0 ? -r0 : r0) + (g1 ? -r1 : r1);
                ns = (int)((((unsigned int)s << 1) | in) & 0xFu);
                if (score > next[ns]) {
                    next[ns] = score;
                    back[k][ns] = (uint8_t)s;
                }
            }
        }
        memcpy(metric, next, sizeof(metric));
    }

    int s = 0; /* the tail bits leave the encoder here */
    for (int k = GSM_BCCH_UNCODED_BITS - 1; k >= 0; k--) {
        int prev = back[k][s];

        u[k] = (uint8_t)(s & 1);
        s = prev;
    }
}

int gsm_bcch_decode_block(const float coded[GSM_BCCH_CODED_BITS],
                          struct gsm_bcch_block *out) {
    uint8_t u[GSM_BCCH_UNCODED_BITS];

    if (!coded || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    soft_viterbi(coded, u);
    out->parity_ok = gsm_bcch_fire_check(u);
    /*
     * Least significant bit first. GSM 04.06 numbers an octet's bits 1 to 8
     * with bit 1 sent first, so the first bit off the air is the octet's low
     * bit -- and the fill octet is the proof: packed this way the padding of a
     * real block reads 0x2B, and the other way round it reads 0xD4.
     */
    for (int i = 0; i < GSM_BCCH_INFO_BITS; i++)
        if (u[i])
            out->octets[i / 8] |= (uint8_t)(1u << (i % 8));
    return out->parity_ok;
}

/* ---- what the message says --------------------------------------------- */

const char *gsm_si_type_name(enum gsm_si_type type) {
    switch (type) {
    case GSM_SI_TYPE_1:    return "System Information 1";
    case GSM_SI_TYPE_2:    return "System Information 2";
    case GSM_SI_TYPE_2BIS: return "System Information 2bis";
    case GSM_SI_TYPE_2TER: return "System Information 2ter";
    case GSM_SI_TYPE_3:    return "System Information 3";
    case GSM_SI_TYPE_4:    return "System Information 4";
    case GSM_SI_TYPE_13:   return "System Information 13";
    default:               return "unknown";
    }
}

static enum gsm_si_type si_type_of(uint8_t message_type) {
    switch (message_type) {
    case 0x19: return GSM_SI_TYPE_1;
    case 0x1A: return GSM_SI_TYPE_2;
    case 0x02: return GSM_SI_TYPE_2BIS;
    case 0x03: return GSM_SI_TYPE_2TER;
    case 0x1B: return GSM_SI_TYPE_3;
    case 0x1C: return GSM_SI_TYPE_4;
    case 0x00: return GSM_SI_TYPE_13;
    default:   return GSM_SI_UNKNOWN;
    }
}

/*
 * The Location Area Identification, five octets (3GPP TS 24.008 10.5.1.3):
 * MCC and MNC packed as BCC nibbles, then a 16-bit LAC.
 *
 * The third MNC digit sits in the high nibble of the second octet and is 0xF
 * when the network has a two-digit MNC. Getting that wrong turns operator 01
 * into 001, which is a different network on the other side of the world.
 */
static void parse_lai(const uint8_t *p, struct gsm_si *out) {
    int mcc1 = p[0] & 0x0F;
    int mcc2 = (p[0] >> 4) & 0x0F;
    int mcc3 = p[1] & 0x0F;
    int mnc3 = (p[1] >> 4) & 0x0F;
    int mnc1 = p[2] & 0x0F;
    int mnc2 = (p[2] >> 4) & 0x0F;

    if (mcc1 > 9 || mcc2 > 9 || mcc3 > 9 || mnc1 > 9 || mnc2 > 9)
        return;
    out->mcc = mcc1 * 100 + mcc2 * 10 + mcc3;
    if (mnc3 == 0x0F) {
        out->mnc = mnc1 * 10 + mnc2;
        out->mnc_digits = 2;
    } else if (mnc3 <= 9) {
        out->mnc = mnc1 * 100 + mnc2 * 10 + mnc3;
        out->mnc_digits = 3;
    } else {
        return;
    }
    out->lac = (p[3] << 8) | p[4];
    out->have_lai = 1;
}

/*
 * The Cell Channel Description and Neighbour Cell Description both carry a
 * 16-octet frequency list whose format depends on its leading bits. Only the
 * bitmap-of-0 format is read here -- the range formats are a different
 * encoding each, and reporting a wrong ARFCN is worse than reporting none.
 */
static void parse_frequency_bitmap(const uint8_t *p, struct gsm_si *out) {
    if ((p[0] & 0xC0) != 0x00)
        return; /* a range or variable-bitmap format, not read */
    /*
     * Sixteen octets, 128 bits. The first four are the format and spare, and
     * the remaining 124 are ARFCN 124 down to 1 -- so ARFCN 124 is bit 3 of
     * the first octet and ARFCN 1 is bit 0 of the last. Counting from the
     * wrong end here puts every neighbour on a channel that mirrors the right
     * one, which looks entirely plausible on screen.
     */
    for (int arfcn = 1; arfcn <= 124; arfcn++) {
        int offset = 4 + (124 - arfcn);
        int octet = offset / 8;
        int shift = 7 - (offset % 8);

        if (!((p[octet] >> shift) & 1))
            continue;
        if (out->neighbour_count < GSM_SI_MAX_NEIGHBOURS)
            out->neighbours[out->neighbour_count++] = arfcn;
    }
}

int gsm_si_parse(const uint8_t octets[GSM_BCCH_INFO_OCTETS],
                 struct gsm_si *out) {
    if (!octets || !out)
        return 0;
    memset(out, 0, sizeof(*out));

    /*
     * A BCCH block is a LAPDm frame in format Bbis, which is the short one:
     * a length indicator and then the message, with no address or control
     * octet. Broadcast has nobody to address and nothing to acknowledge, so
     * those fields would say nothing.
     *
     * Octet 1 is the protocol discriminator (6 = Radio Resources) and octet 2
     * the message type. The length indicator is not enforced -- a block whose
     * Fire code passed has already proved it is not noise, and refusing a
     * message over a length field would lose one for no gain.
     */
    if ((octets[1] & 0x0F) != 0x06)
        return 0;
    out->type = si_type_of(octets[2]);
    if (out->type == GSM_SI_UNKNOWN)
        return 0;

    switch (out->type) {
    case GSM_SI_TYPE_1:
        /* Cell Channel Description, 16 octets from octet 3. */
        parse_frequency_bitmap(&octets[3], out);
        break;
    case GSM_SI_TYPE_2:
        /* Neighbour Cell Description, 16 octets from octet 3. */
        parse_frequency_bitmap(&octets[3], out);
        break;
    case GSM_SI_TYPE_3:
        /* Cell Identity, then the Location Area Identification. */
        out->cell_id = (octets[3] << 8) | octets[4];
        out->have_cell_id = 1;
        parse_lai(&octets[5], out);
        break;
    case GSM_SI_TYPE_4:
        /* Location Area Identification, with no Cell Identity before it. */
        parse_lai(&octets[3], out);
        break;
    default:
        break;
    }
    return 1;
}
