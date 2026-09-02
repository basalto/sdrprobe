#include "lte_mib.h"

#include "lte_gold.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* The message itself.                                                 */
/* ------------------------------------------------------------------ */

/* 36.331: dl-Bandwidth is an enumeration of six widths, not a number. */
static const int bandwidths[6] = { 6, 15, 25, 50, 75, 100 };

/* And phich-Resource is one of four fractions of the downlink blocks. Held in
   sixths so the whole set is integers. */
static const int phich_resources[4] = { 1, 3, 6, 12 };

double lte_mib_occupied_hz(int bandwidth_prb) {
    return (double)bandwidth_prb * 12.0 * 15000.0;
}

const char *lte_phich_resource_name(int sixths) {
    switch (sixths) {
    case 1: return "1/6";
    case 3: return "1/2";
    case 6: return "1";
    case 12: return "2";
    default: return NULL;
    }
}

static void put_bits(uint8_t *bits, int from, int count, unsigned int value) {
    int i;
    for (i = 0; i < count; i++)
        bits[from + i] = (uint8_t)((value >> (count - 1 - i)) & 1u);
}

static unsigned int take_bits(const uint8_t *bits, int from, int count) {
    unsigned int value = 0;
    int i;
    for (i = 0; i < count; i++)
        value = (value << 1) | (bits[from + i] & 1u);
    return value;
}

void lte_mib_pack(const struct lte_mib *mib, uint8_t bits[LTE_MIB_BITS]) {
    int bandwidth_code = 0, resource_code = 0, i;

    memset(bits, 0, LTE_MIB_BITS);
    for (i = 0; i < 6; i++)
        if (bandwidths[i] == mib->bandwidth_prb)
            bandwidth_code = i;
    for (i = 0; i < 4; i++)
        if (phich_resources[i] == mib->phich_resource_sixths)
            resource_code = i;

    put_bits(bits, 0, 3, (unsigned int)bandwidth_code);
    put_bits(bits, 3, 1, mib->phich_extended ? 1u : 0u);
    put_bits(bits, 4, 2, (unsigned int)resource_code);
    /* Only the eight most significant bits of the frame number travel in the
       message; the two below them are the quarter of the 40 ms period the
       transmission occupied, and are recovered from the scrambling. */
    put_bits(bits, 6, 8, (unsigned int)((mib->system_frame_number >> 2) & 0xff));
    /* Bits 14 to 23 are spare and sent as zeros. */
}

int lte_mib_unpack(const uint8_t bits[LTE_MIB_BITS], struct lte_mib *mib) {
    unsigned int bandwidth_code = take_bits(bits, 0, 3);

    if (bandwidth_code >= 6)
        return 0;
    memset(mib, 0, sizeof(*mib));
    mib->bandwidth_prb = bandwidths[bandwidth_code];
    mib->phich_extended = (int)take_bits(bits, 3, 1);
    mib->phich_resource_sixths = phich_resources[take_bits(bits, 4, 2)];
    mib->system_frame_number = (int)(take_bits(bits, 6, 8) << 2);
    return 1;
}


/* ------------------------------------------------------------------ */
/* Parity.                                                             */
/* ------------------------------------------------------------------ */

/*
 * The mask the parity is exclusive-ored with says how many antenna ports the
 * cell transmits on (36.212 table 5.3.1.1-1). It is not spare capacity being
 * borrowed: the count has to reach a receiver before anything else can be
 * demodulated, and there is nowhere earlier to put it.
 */
static int port_mask_bit(int antenna_ports, int index) {
    if (antenna_ports == 1)
        return 0;
    if (antenna_ports == 2)
        return 1;
    return index & 1;      /* four ports: alternating, starting at zero */
}

/* g(D) = D^16 + D^12 + D^5 + 1, the message shifted in most significant bit
   first. */
static unsigned int crc16(const uint8_t *bits, int count) {
    unsigned int reg = 0;
    int i;
    for (i = 0; i < count; i++) {
        unsigned int feedback = ((reg >> 15) & 1u) ^ (bits[i] & 1u);
        reg = (reg << 1) & 0xffffu;
        if (feedback)
            reg ^= 0x1021u;
    }
    return reg;
}

void lte_mib_parity(const uint8_t bits[LTE_MIB_BITS], int antenna_ports,
                    uint8_t parity[LTE_MIB_CRC_BITS]) {
    unsigned int remainder = crc16(bits, LTE_MIB_BITS);
    int i;
    for (i = 0; i < LTE_MIB_CRC_BITS; i++) {
        int bit = (int)((remainder >> (LTE_MIB_CRC_BITS - 1 - i)) & 1u);
        parity[i] = (uint8_t)(bit ^ port_mask_bit(antenna_ports, i));
    }
}

int lte_mib_parity_ports(const uint8_t block[LTE_MIB_BLOCK_BITS]) {
    static const int candidates[3] = { 1, 2, 4 };
    int c;
    for (c = 0; c < 3; c++) {
        uint8_t parity[LTE_MIB_CRC_BITS];
        lte_mib_parity(block, candidates[c], parity);
        if (memcmp(parity, block + LTE_MIB_BITS, LTE_MIB_CRC_BITS) == 0)
            return candidates[c];
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* The convolutional code.                                             */
/* ------------------------------------------------------------------ */

/*
 * One step of the encoder. The register holds the six previous input bits,
 * the most recent in the lowest bit, and the three generators are 133, 171
 * and 165 octal read as taps on the input and those six.
 */
static void encoder_step(int state, int input, int *next, int output[3]) {
    int s1 = state & 1;
    int s2 = (state >> 1) & 1;
    int s3 = (state >> 2) & 1;
    int s4 = (state >> 3) & 1;
    int s5 = (state >> 4) & 1;
    int s6 = (state >> 5) & 1;

    output[0] = input ^ s2 ^ s3 ^ s5 ^ s6;
    output[1] = input ^ s1 ^ s2 ^ s3 ^ s6;
    output[2] = input ^ s1 ^ s2 ^ s4 ^ s6;
    *next = ((state << 1) | input) & (LTE_MIB_STATES - 1);
}

/* The state a tail-biting block starts and ends in: its own last six bits,
   most recent first. */
static int tail_biting_state(const uint8_t block[LTE_MIB_BLOCK_BITS]) {
    int state = 0, i;
    for (i = 0; i < LTE_MIB_MEMORY; i++)
        state |= (block[LTE_MIB_BLOCK_BITS - 1 - i] & 1) << i;
    return state;
}

void lte_mib_convolutional_encode(const uint8_t block[LTE_MIB_BLOCK_BITS],
                                  uint8_t coded[LTE_MIB_CODED_BITS]) {
    int state = tail_biting_state(block);
    int k;
    for (k = 0; k < LTE_MIB_BLOCK_BITS; k++) {
        int output[3], next, stream;
        encoder_step(state, block[k] & 1, &next, output);
        for (stream = 0; stream < 3; stream++)
            coded[stream * LTE_MIB_BLOCK_BITS + k] = (uint8_t)output[stream];
        state = next;
    }
}

/*
 * Viterbi over every closed path.
 *
 * A tail-biting code gives the decoder no free end: the register starts where
 * it finishes, and which state that is depends on the answer. The usual
 * shortcuts guess it and iterate. This tries all 64, keeping the best path
 * that begins and ends in the same state, which is the maximum-likelihood
 * answer rather than an approximation of it -- 40 stages of 64 states is small
 * enough that exactness costs nothing worth saving.
 */
void lte_mib_convolutional_decode(const float coded[LTE_MIB_CODED_BITS],
                                  uint8_t block[LTE_MIB_BLOCK_BITS]) {
    /* The trellis is fixed, so its branches are worth building once. */
    int next_state[LTE_MIB_STATES][2];
    float branch[LTE_MIB_STATES][2][3];
    float best_overall = 0.0f;
    int have_best = 0;
    uint8_t survivor[LTE_MIB_BLOCK_BITS][LTE_MIB_STATES];
    int state, input, k, start;

    for (state = 0; state < LTE_MIB_STATES; state++)
        for (input = 0; input < 2; input++) {
            int output[3], next, stream;
            encoder_step(state, input, &next, output);
            next_state[state][input] = next;
            for (stream = 0; stream < 3; stream++)
                branch[state][input][stream] = output[stream] ? -1.0f : 1.0f;
        }

    for (start = 0; start < LTE_MIB_STATES; start++) {
        float metric[LTE_MIB_STATES], updated[LTE_MIB_STATES];
        float closed;

        for (state = 0; state < LTE_MIB_STATES; state++)
            metric[state] = -1e30f;
        metric[start] = 0.0f;

        for (k = 0; k < LTE_MIB_BLOCK_BITS; k++) {
            float s0 = coded[k];
            float s1 = coded[LTE_MIB_BLOCK_BITS + k];
            float s2 = coded[2 * LTE_MIB_BLOCK_BITS + k];
            for (state = 0; state < LTE_MIB_STATES; state++)
                updated[state] = -1e30f;
            for (state = 0; state < LTE_MIB_STATES; state++) {
                if (metric[state] < -1e29f)
                    continue;
                for (input = 0; input < 2; input++) {
                    int to = next_state[state][input];
                    float score = metric[state] +
                                  branch[state][input][0] * s0 +
                                  branch[state][input][1] * s1 +
                                  branch[state][input][2] * s2;
                    if (score > updated[to]) {
                        updated[to] = score;
                        survivor[k][to] = (uint8_t)state;
                    }
                }
            }
            memcpy(metric, updated, sizeof(metric));
            /* The survivor of every state at this stage is now recorded, but
               only the paths that close will be traced back. */
        }

        closed = metric[start];
        if (closed < -1e29f)
            continue;
        if (!have_best || closed > best_overall) {
            int here = start;
            /* Trace back through the survivors. The bit that entered a state
               is its lowest bit, whatever it came from. */
            for (k = LTE_MIB_BLOCK_BITS - 1; k >= 0; k--) {
                block[k] = (uint8_t)(here & 1);
                here = survivor[k][here];
            }
            best_overall = closed;
            have_best = 1;
        }
    }
}


/* ------------------------------------------------------------------ */
/* Rate matching.                                                      */
/* ------------------------------------------------------------------ */

/*
 * Each of the three coded streams is written row-wise into a 2 x 32 grid,
 * padded at the front so it fills, and read out column by column in this
 * order (36.212 table 5.1.4.2.1-1). The padding is not transmitted; skipping
 * it is what turns 192 grid positions back into 120 bits.
 */
static const int column_order[32] = {
     1, 17,  9, 25,  5, 21, 13, 29,  3, 19, 11, 27,  7, 23, 15, 31,
     0, 16,  8, 24,  4, 20, 12, 28,  2, 18, 10, 26,  6, 22, 14, 30
};

#define SUBBLOCK_COLUMNS 32
#define SUBBLOCK_ROWS 2
#define SUBBLOCK_SIZE (SUBBLOCK_ROWS * SUBBLOCK_COLUMNS)
#define SUBBLOCK_PADDING (SUBBLOCK_SIZE - LTE_MIB_BLOCK_BITS)

/* Position n of the repeating cycle -> which coded bit goes there. */
static void cycle_map(int map[LTE_MIB_CODED_BITS]) {
    int stream, m, written = 0;
    for (stream = 0; stream < 3; stream++)
        for (m = 0; m < SUBBLOCK_SIZE; m++) {
            int column = column_order[m / SUBBLOCK_ROWS];
            int row = m % SUBBLOCK_ROWS;
            int source = row * SUBBLOCK_COLUMNS + column;
            if (source < SUBBLOCK_PADDING)
                continue;      /* padding, never transmitted */
            map[written++] = stream * LTE_MIB_BLOCK_BITS +
                             (source - SUBBLOCK_PADDING);
        }
}

void lte_mib_rate_match(const uint8_t coded[LTE_MIB_CODED_BITS],
                        uint8_t matched[LTE_MIB_RATE_MATCHED_BITS]) {
    int map[LTE_MIB_CODED_BITS];
    int n;
    cycle_map(map);
    for (n = 0; n < LTE_MIB_RATE_MATCHED_BITS; n++)
        matched[n] = coded[map[n % LTE_MIB_CODED_BITS]];
}

void lte_mib_rate_dematch(const float matched[LTE_MIB_RATE_MATCHED_BITS],
                          float coded[LTE_MIB_CODED_BITS]) {
    int map[LTE_MIB_CODED_BITS];
    int n;
    cycle_map(map);
    for (n = 0; n < LTE_MIB_CODED_BITS; n++)
        coded[n] = 0.0f;
    /* Every repetition of a bit is evidence about the same bit, so they add.
       Positions that were never received are zero and add nothing. */
    for (n = 0; n < LTE_MIB_RATE_MATCHED_BITS; n++)
        coded[map[n % LTE_MIB_CODED_BITS]] += matched[n];
}


/* ------------------------------------------------------------------ */
/* Scrambling, and the two whole directions.                           */
/* ------------------------------------------------------------------ */

void lte_mib_descramble(int pci, int quarter,
                        float soft[LTE_MIB_QUARTER_BITS]) {
    uint8_t sequence[LTE_MIB_RATE_MATCHED_BITS];
    int n, from = quarter * LTE_MIB_QUARTER_BITS;

    lte_gold_sequence((uint32_t)pci, LTE_MIB_RATE_MATCHED_BITS, sequence);
    for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
        if (sequence[from + n])
            soft[n] = -soft[n];
}

void lte_mib_encode(const struct lte_mib *mib, int pci, int quarter,
                    float soft[LTE_MIB_QUARTER_BITS]) {
    uint8_t block[LTE_MIB_BLOCK_BITS];
    uint8_t coded[LTE_MIB_CODED_BITS];
    uint8_t matched[LTE_MIB_RATE_MATCHED_BITS];
    int n, from = quarter * LTE_MIB_QUARTER_BITS;

    lte_mib_pack(mib, block);
    lte_mib_parity(block, mib->antenna_ports, block + LTE_MIB_BITS);
    lte_mib_convolutional_encode(block, coded);
    lte_mib_rate_match(coded, matched);
    for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
        soft[n] = matched[from + n] ? -1.0f : 1.0f;
    lte_mib_descramble(pci, quarter, soft);
}

int lte_mib_same_cell(const struct lte_mib *a, const struct lte_mib *b) {
    if (!a || !b)
        return 0;
    return a->bandwidth_prb == b->bandwidth_prb &&
           a->phich_extended == b->phich_extended &&
           a->phich_resource_sixths == b->phich_resource_sixths &&
           a->antenna_ports == b->antenna_ports;
}

int lte_mib_decode(const float soft[LTE_MIB_QUARTER_BITS], int pci,
                   struct lte_mib *mib) {
    int quarter;

    for (quarter = 0; quarter < LTE_MIB_QUARTERS; quarter++) {
        float attempt[LTE_MIB_QUARTER_BITS];
        float whole[LTE_MIB_RATE_MATCHED_BITS];
        float coded[LTE_MIB_CODED_BITS];
        uint8_t block[LTE_MIB_BLOCK_BITS];
        int ports, n, from = quarter * LTE_MIB_QUARTER_BITS;

        memcpy(attempt, soft, sizeof(attempt));
        lte_mib_descramble(pci, quarter, attempt);

        /* Lay this transmission where the hypothesis says it belongs in the
           40 ms period and leave the rest unknown. Every coded bit still
           arrives four times, which is what makes one transmission enough. */
        for (n = 0; n < LTE_MIB_RATE_MATCHED_BITS; n++)
            whole[n] = 0.0f;
        for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
            whole[from + n] = attempt[n];

        lte_mib_rate_dematch(whole, coded);
        lte_mib_convolutional_decode(coded, block);
        ports = lte_mib_parity_ports(block);
        if (!ports)
            continue;
        if (!lte_mib_unpack(block, mib))
            continue;
        mib->antenna_ports = ports;
        mib->quarter = quarter;
        mib->system_frame_number |= quarter;
        return 1;
    }
    return 0;
}
