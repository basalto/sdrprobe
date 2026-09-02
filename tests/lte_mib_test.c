#include "check.h"

#include "lte_mib.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * The Master Information Block, from 480 soft bits to a message and back.
 *
 * No samples and no receiver: every layer here is a permutation, a
 * polynomial, or a trellis, and each is pushed on in both directions. The
 * round trip alone would pass against any self-consistent spelling of the
 * standard, so the layers are also checked against properties that only the
 * real construction has -- the interleaver is proved to be a permutation, the
 * parity to catch every single-bit error, the code to correct damage a
 * repetition could not, and the four scrambling offsets to be distinguishable
 * from one another.
 */

static uint32_t rng_state;

static void rng_seed(uint32_t seed) { rng_state = seed ? seed : 1u; }

static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static double rng_uniform(void) {
    return ((double)(rng_next() >> 8) + 0.5) / 16777216.0;
}

static double rng_normal(void) {
    double u = rng_uniform(), v = rng_uniform();
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

static const int all_bandwidths[6] = { 6, 15, 25, 50, 75, 100 };
static const int all_resources[4] = { 1, 3, 6, 12 };


static void test_message_round_trip(void) {
    int bandwidth, resource, extended, mismatches = 0;

    for (bandwidth = 0; bandwidth < 6; bandwidth++)
        for (resource = 0; resource < 4; resource++)
            for (extended = 0; extended < 2; extended++) {
                struct lte_mib out;
                struct lte_mib in;
                uint8_t bits[LTE_MIB_BITS];
                /* Frame numbers whose low two bits are not carried in the
                   message: packing must drop them, not round them. */
                int sfn = (bandwidth * 163 + resource * 41 + extended) % 1024;
                memset(&in, 0, sizeof(in));
                in.bandwidth_prb = all_bandwidths[bandwidth];
                in.phich_resource_sixths = all_resources[resource];
                in.phich_extended = extended;
                in.system_frame_number = sfn;

                lte_mib_pack(&in, bits);
                if (!lte_mib_unpack(bits, &out)) {
                    mismatches++;
                    continue;
                }
                if (out.bandwidth_prb != in.bandwidth_prb ||
                    out.phich_resource_sixths != in.phich_resource_sixths ||
                    out.phich_extended != in.phich_extended ||
                    out.system_frame_number != (sfn & ~3))
                    mismatches++;
            }
    check_int("every message packs and unpacks", mismatches, 0);

    /* Two of the eight bandwidth codes name nothing. A block that decodes to
       one of them is not a message, and saying so is the point. */
    {
        struct lte_mib out;
        uint8_t bits[LTE_MIB_BITS];
        memset(bits, 0, sizeof(bits));
        bits[0] = 1; bits[1] = 1; bits[2] = 0;      /* code 6 */
        check_int("bandwidth code 6 is refused", lte_mib_unpack(bits, &out), 0);
        bits[2] = 1;                                 /* code 7 */
        check_int("bandwidth code 7 is refused", lte_mib_unpack(bits, &out), 0);
    }

    check_str("a sixth of the blocks", lte_phich_resource_name(1), "1/6");
    check_str("two blocks' worth", lte_phich_resource_name(12), "2");
    check_true("and nothing else", lte_phich_resource_name(5) == NULL);
    check_close("a 50-block cell occupies 9 MHz", lte_mib_occupied_hz(50),
                9000000.0, 1.0);
    check_close("a 6-block cell occupies 1.08 MHz", lte_mib_occupied_hz(6),
                1080000.0, 1.0);
}

static void test_parity(void) {
    uint8_t bits[LTE_MIB_BITS];
    uint8_t one[LTE_MIB_CRC_BITS], two[LTE_MIB_CRC_BITS], four[LTE_MIB_CRC_BITS];
    int n, trial, missed = 0, confused = 0;

    rng_seed(4242u);
    for (n = 0; n < LTE_MIB_BITS; n++)
        bits[n] = (uint8_t)(rng_next() & 1u);

    lte_mib_parity(bits, 1, one);
    lte_mib_parity(bits, 2, two);
    lte_mib_parity(bits, 4, four);
    check_true("one port and two ports differ",
               memcmp(one, two, sizeof(one)) != 0);
    check_true("two ports and four ports differ",
               memcmp(two, four, sizeof(two)) != 0);
    check_true("one port and four ports differ",
               memcmp(one, four, sizeof(one)) != 0);
    /* Two ports is the all-ones mask, so its parity is the one-port parity
       inverted bit for bit. */
    for (n = 0; n < LTE_MIB_CRC_BITS; n++)
        check_msg(two[n] == (one[n] ^ 1u), "two-port mask inverts bit %d\n", n);

    /* Every single-bit error in the whole 40, caught, for every port count. */
    for (trial = 0; trial < 3; trial++) {
        int ports = (trial == 0) ? 1 : (trial == 1) ? 2 : 4;
        uint8_t block[LTE_MIB_BLOCK_BITS];
        memcpy(block, bits, LTE_MIB_BITS);
        lte_mib_parity(bits, ports, block + LTE_MIB_BITS);
        if (lte_mib_parity_ports(block) != ports)
            confused++;
        for (n = 0; n < LTE_MIB_BLOCK_BITS; n++) {
            block[n] ^= 1u;
            if (lte_mib_parity_ports(block) != 0)
                missed++;
            block[n] ^= 1u;
        }
    }
    check_int("a clean block names its own port count", confused, 0);
    check_int("every single-bit error is caught", missed, 0);

    /* And random damage almost always is. Sixteen bits of parity against
       three masks leaves about one chance in twenty thousand. */
    {
        int accepted = 0;
        for (trial = 0; trial < 4000; trial++) {
            uint8_t block[LTE_MIB_BLOCK_BITS];
            for (n = 0; n < LTE_MIB_BLOCK_BITS; n++)
                block[n] = (uint8_t)(rng_next() & 1u);
            if (lte_mib_parity_ports(block) != 0)
                accepted++;
        }
        check_msg(accepted <= 3,
                  "random blocks are rejected, %d of 4000 slipped through\n",
                  accepted);
    }
}

static void test_convolutional_code(void) {
    int trial, wrong = 0, not_closed = 0;

    rng_seed(77u);
    for (trial = 0; trial < 200; trial++) {
        uint8_t block[LTE_MIB_BLOCK_BITS], back[LTE_MIB_BLOCK_BITS];
        uint8_t coded[LTE_MIB_CODED_BITS];
        float soft[LTE_MIB_CODED_BITS];
        int n;

        for (n = 0; n < LTE_MIB_BLOCK_BITS; n++)
            block[n] = (uint8_t)(rng_next() & 1u);
        lte_mib_convolutional_encode(block, coded);
        for (n = 0; n < LTE_MIB_CODED_BITS; n++)
            soft[n] = coded[n] ? -1.0f : 1.0f;
        lte_mib_convolutional_decode(soft, back);
        if (memcmp(block, back, sizeof(block)) != 0)
            wrong++;
    }
    check_int("the code decodes what it encodes", wrong, 0);

    /*
     * Tail-biting means the register finishes where it started. Re-encoding a
     * block from a zero register and comparing the two proves it: the outputs
     * agree only from the seventh bit onwards unless the block really does
     * close on itself.
     */
    for (trial = 0; trial < 50; trial++) {
        uint8_t block[LTE_MIB_BLOCK_BITS];
        uint8_t coded[LTE_MIB_CODED_BITS];
        uint8_t rotated[LTE_MIB_BLOCK_BITS];
        uint8_t rotated_coded[LTE_MIB_CODED_BITS];
        int n, stream, k;

        for (n = 0; n < LTE_MIB_BLOCK_BITS; n++)
            block[n] = (uint8_t)(rng_next() & 1u);
        lte_mib_convolutional_encode(block, coded);
        /* Rotating the block by one must rotate every coded stream by one:
           true of a tail-biting code and of nothing else, because the state
           at the seam is the same state. */
        for (n = 0; n < LTE_MIB_BLOCK_BITS; n++)
            rotated[n] = block[(n + 1) % LTE_MIB_BLOCK_BITS];
        lte_mib_convolutional_encode(rotated, rotated_coded);
        for (stream = 0; stream < 3; stream++)
            for (k = 0; k < LTE_MIB_BLOCK_BITS; k++) {
                int from = stream * LTE_MIB_BLOCK_BITS +
                           (k + 1) % LTE_MIB_BLOCK_BITS;
                int to = stream * LTE_MIB_BLOCK_BITS + k;
                if (coded[from] != rotated_coded[to])
                    not_closed++;
            }
    }
    check_int("the trellis closes on itself", not_closed, 0);

    /* And it corrects. A rate 1/3 code with six bits of memory should shrug
       off a handful of flipped bits in 120. */
    {
        int damaged_wrong = 0;
        for (trial = 0; trial < 200; trial++) {
            uint8_t block[LTE_MIB_BLOCK_BITS], back[LTE_MIB_BLOCK_BITS];
            uint8_t coded[LTE_MIB_CODED_BITS];
            float soft[LTE_MIB_CODED_BITS];
            int n, flip;

            for (n = 0; n < LTE_MIB_BLOCK_BITS; n++)
                block[n] = (uint8_t)(rng_next() & 1u);
            lte_mib_convolutional_encode(block, coded);
            for (n = 0; n < LTE_MIB_CODED_BITS; n++)
                soft[n] = coded[n] ? -1.0f : 1.0f;
            for (flip = 0; flip < 4; flip++)
                soft[rng_next() % LTE_MIB_CODED_BITS] *= -1.0f;
            lte_mib_convolutional_decode(soft, back);
            if (memcmp(block, back, sizeof(block)) != 0)
                damaged_wrong++;
        }
        check_msg(damaged_wrong <= 20,
                  "four flipped bits in 120 usually survive, %d of 200 lost\n",
                  damaged_wrong);
    }
}

static void test_rate_matching(void) {
    uint8_t coded[LTE_MIB_CODED_BITS];
    uint8_t matched[LTE_MIB_RATE_MATCHED_BITS];
    float received[LTE_MIB_RATE_MATCHED_BITS];
    float back[LTE_MIB_CODED_BITS];
    int counts[LTE_MIB_CODED_BITS];
    int n, quarter, uneven = 0;

    /* Send each coded position in turn as the only one set, and count where
       it lands. Sixteen copies over the period, four in every quarter -- the
       second is what makes one 10 ms transmission self-sufficient. */
    for (n = 0; n < LTE_MIB_CODED_BITS; n++)
        counts[n] = 0;
    memset(coded, 0, sizeof(coded));
    for (n = 0; n < LTE_MIB_CODED_BITS; n++) {
        int m, seen = 0;
        coded[n] = 1;
        lte_mib_rate_match(coded, matched);
        for (m = 0; m < LTE_MIB_RATE_MATCHED_BITS; m++)
            if (matched[m])
                seen++;
        counts[n] = seen;
        for (quarter = 0; quarter < LTE_MIB_QUARTERS; quarter++) {
            int in_quarter = 0;
            for (m = 0; m < LTE_MIB_QUARTER_BITS; m++)
                if (matched[quarter * LTE_MIB_QUARTER_BITS + m])
                    in_quarter++;
            if (in_quarter != 4)
                uneven++;
        }
        coded[n] = 0;
    }
    for (n = 0; n < LTE_MIB_CODED_BITS; n++)
        check_msg(counts[n] == 16, "coded bit %d appears %d times, expected 16\n",
                  n, counts[n]);
    check_int("and four times in each quarter", uneven, 0);

    /* Which also says the map is onto: 120 bits x 16 copies fills all 1920,
       so nothing in the period is left unaccounted for. */
    {
        int total = 0;
        for (n = 0; n < LTE_MIB_CODED_BITS; n++)
            total += counts[n];
        check_int("the period is exactly filled", total,
                  LTE_MIB_RATE_MATCHED_BITS);
    }

    /* Dematching adds the copies up, so a clean +-1 comes back as +-16. */
    rng_seed(909u);
    for (n = 0; n < LTE_MIB_CODED_BITS; n++)
        coded[n] = (uint8_t)(rng_next() & 1u);
    lte_mib_rate_match(coded, matched);
    for (n = 0; n < LTE_MIB_RATE_MATCHED_BITS; n++)
        received[n] = matched[n] ? -1.0f : 1.0f;
    lte_mib_rate_dematch(received, back);
    for (n = 0; n < LTE_MIB_CODED_BITS; n++)
        check_msg(fabs(back[n] - (coded[n] ? -16.0 : 16.0)) < 1e-3,
                  "dematched bit %d is %.1f\n", n, back[n]);
}

static void test_scrambling(void) {
    float soft[LTE_MIB_QUARTER_BITS], copy[LTE_MIB_QUARTER_BITS];
    int n, quarter, differences;

    for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
        soft[n] = 1.0f;

    /* Its own inverse. */
    memcpy(copy, soft, sizeof(soft));
    lte_mib_descramble(227, 0, copy);
    lte_mib_descramble(227, 0, copy);
    check_int("scrambling twice is not scrambling",
              memcmp(copy, soft, sizeof(soft)), 0);

    /* The four quarters use four different stretches of the sequence, which
       is the only thing that tells them apart. */
    for (quarter = 1; quarter < LTE_MIB_QUARTERS; quarter++) {
        float first[LTE_MIB_QUARTER_BITS], later[LTE_MIB_QUARTER_BITS];
        memcpy(first, soft, sizeof(soft));
        memcpy(later, soft, sizeof(soft));
        lte_mib_descramble(227, 0, first);
        lte_mib_descramble(227, quarter, later);
        differences = 0;
        for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
            if (first[n] != later[n])
                differences++;
        check_msg(differences > 180 && differences < 300,
                  "quarter %d differs from quarter 0 in %d of 480\n", quarter,
                  differences);
    }

    /* And two cells differ, which is why a neighbour's broadcast does not
       decode as this one's. */
    {
        float mine[LTE_MIB_QUARTER_BITS], theirs[LTE_MIB_QUARTER_BITS];
        memcpy(mine, soft, sizeof(soft));
        memcpy(theirs, soft, sizeof(soft));
        lte_mib_descramble(227, 0, mine);
        lte_mib_descramble(228, 0, theirs);
        differences = 0;
        for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
            if (mine[n] != theirs[n])
                differences++;
        check_msg(differences > 180 && differences < 300,
                  "a neighbouring cell scrambles differently, %d of 480\n",
                  differences);
    }
}

static struct lte_mib sample_message(int index) {
    struct lte_mib mib;
    memset(&mib, 0, sizeof(mib));
    mib.bandwidth_prb = all_bandwidths[index % 6];
    mib.phich_resource_sixths = all_resources[index % 4];
    mib.phich_extended = index % 2;
    mib.system_frame_number = (index * 37) % 1024;
    mib.antenna_ports = (index % 3 == 0) ? 1 : (index % 3 == 1) ? 2 : 4;
    return mib;
}

static void test_whole_chain(void) {
    int index, quarter, failures = 0, mismatches = 0;

    for (index = 0; index < 24; index++) {
        struct lte_mib sent = sample_message(index);
        int pci = (index * 71) % 504;
        for (quarter = 0; quarter < LTE_MIB_QUARTERS; quarter++) {
            float soft[LTE_MIB_QUARTER_BITS];
            struct lte_mib got;
            /* The frame number and the quarter are not free of each other:
               the two lowest bits of one are the other. */
            struct lte_mib transmitted = sent;
            transmitted.system_frame_number =
                (sent.system_frame_number & ~3) | quarter;

            lte_mib_encode(&transmitted, pci, quarter, soft);
            if (!lte_mib_decode(soft, pci, &got)) {
                failures++;
                continue;
            }
            if (got.bandwidth_prb != transmitted.bandwidth_prb ||
                got.phich_extended != transmitted.phich_extended ||
                got.phich_resource_sixths != transmitted.phich_resource_sixths ||
                got.antenna_ports != transmitted.antenna_ports ||
                got.quarter != quarter ||
                got.system_frame_number != transmitted.system_frame_number)
                mismatches++;
        }
    }
    check_int("every message survives the whole chain", failures, 0);
    check_int("and comes back saying the same thing", mismatches, 0);
}

static void test_the_wrong_cell_does_not_decode(void) {
    struct lte_mib sent = sample_message(5);
    struct lte_mib got;
    float soft[LTE_MIB_QUARTER_BITS];
    int decoded = 0, pci;

    sent.system_frame_number = (sent.system_frame_number & ~3) | 2;
    lte_mib_encode(&sent, 300, 2, soft);
    check_int("the cell it was sent from reads it",
              lte_mib_decode(soft, 300, &got), 1);

    for (pci = 0; pci < 40; pci++)
        if (pci != 300 && lte_mib_decode(soft, pci, &got))
            decoded++;
    check_int("and forty other cells do not", decoded, 0);
}

static void test_noise_alone_is_not_a_message(void) {
    int trial, decoded = 0;

    rng_seed(31337u);
    for (trial = 0; trial < 600; trial++) {
        float soft[LTE_MIB_QUARTER_BITS];
        struct lte_mib got;
        int n;
        for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
            soft[n] = (float)rng_normal();
        if (lte_mib_decode(soft, 227, &got))
            decoded++;
    }
    /* Four quarters against three masks is twelve chances of one in 65536,
       so about one run in ninety should see a single false message. */
    check_msg(decoded <= 2, "noise decoded as a message %d times in 600\n",
              decoded);
}

/*
 * The parity is not enough on its own, and this is what backs that up.
 *
 * Four scrambling offsets against three masks is twelve attempts a call, and
 * the LTE view makes three calls a block -- once per antenna-port guess. At
 * about one in seven thousand a call, a session that finds a cell nine
 * thousand times expects one pass by chance. That is not a rare accident to
 * tolerate; it is the number, so believing the first pass means reporting
 * noise as a message.
 *
 * What a chance pass cannot do is agree with the next one. A message that
 * passed by luck carries whatever the trellis happened to produce, spread over
 * the 144 configurations a cell could be in -- six bandwidths, two
 * acknowledgement durations, four resource figures, three antenna counts --
 * while a real cell says the same thing every frame. Measuring that is cheap
 * and exact, and does not depend on waiting for a rare event to happen twice.
 */
static void test_a_message_has_to_repeat(void) {
    struct lte_mib a = sample_message(3), b = sample_message(3);
    int trial, agreed = 0;
    const int draws = 20000;

    b.system_frame_number = (a.system_frame_number + 44) % 1024;
    b.quarter = (a.quarter + 1) % 4;
    check_true("the same cell at a later frame is the same cell",
               lte_mib_same_cell(&a, &b));
    b.bandwidth_prb = (a.bandwidth_prb == 50) ? 25 : 50;
    check_true("a different bandwidth is not", !lte_mib_same_cell(&a, &b));
    b = a;
    b.antenna_ports = (a.antenna_ports == 2) ? 4 : 2;
    check_true("nor a different antenna count", !lte_mib_same_cell(&a, &b));
    b = a;
    b.phich_extended = !a.phich_extended;
    check_true("nor a different acknowledgement duration",
               !lte_mib_same_cell(&a, &b));
    b = a;
    b.phich_resource_sixths = (a.phich_resource_sixths == 6) ? 1 : 6;
    check_true("nor a different acknowledgement resource",
               !lte_mib_same_cell(&a, &b));
    check_true("and nothing is the same as nothing",
               !lte_mib_same_cell(NULL, &a) && !lte_mib_same_cell(&a, NULL));

    /*
     * Two chance passes in a row, twenty thousand times. A pass carries a
     * configuration drawn from those 144, so agreement should happen about
     * seven times in a thousand -- which is the factor the repeat buys.
     */
    rng_seed(8191u);
    for (trial = 0; trial < draws; trial++) {
        struct lte_mib first, second;
        memset(&first, 0, sizeof(first));
        memset(&second, 0, sizeof(second));
        first.bandwidth_prb = all_bandwidths[rng_next() % 6];
        first.phich_extended = (int)(rng_next() & 1u);
        first.phich_resource_sixths = all_resources[rng_next() % 4];
        first.antenna_ports = (int)(1u << (rng_next() % 3));
        second.bandwidth_prb = all_bandwidths[rng_next() % 6];
        second.phich_extended = (int)(rng_next() & 1u);
        second.phich_resource_sixths = all_resources[rng_next() % 4];
        second.antenna_ports = (int)(1u << (rng_next() % 3));
        if (lte_mib_same_cell(&first, &second))
            agreed++;
    }
    check_msg(agreed * 40 < draws,
              "two chance passes agreed %d times in %d; the repeat is meant "
              "to cut them by two orders of magnitude\n", agreed, draws);
    check_msg(agreed > 0,
              "no two chance passes agreed at all in %d, which means the "
              "draw is not random and this measures nothing\n", draws);
}

static void test_how_much_damage_it_takes(void) {
    /*
     * What the coding is worth, measured rather than asserted. Soft bits of
     * unit magnitude with Gaussian noise added: at a noise level equal to the
     * signal the message still comes through, because each coded bit arrives
     * four times and the trellis has six bits of memory behind it.
     */
    double levels[4] = { 0.4, 0.7, 1.0, 1.6 };
    int level;

    for (level = 0; level < 4; level++) {
        int trial, decoded = 0;
        rng_seed(500u + (uint32_t)level);
        for (trial = 0; trial < 100; trial++) {
            struct lte_mib sent = sample_message(trial);
            struct lte_mib got;
            float soft[LTE_MIB_QUARTER_BITS];
            int quarter = trial % LTE_MIB_QUARTERS, n;
            sent.system_frame_number =
                (sent.system_frame_number & ~3) | quarter;
            lte_mib_encode(&sent, 227, quarter, soft);
            for (n = 0; n < LTE_MIB_QUARTER_BITS; n++)
                soft[n] += (float)(levels[level] * rng_normal());
            if (lte_mib_decode(soft, 227, &got) &&
                got.bandwidth_prb == sent.bandwidth_prb &&
                got.system_frame_number == sent.system_frame_number)
                decoded++;
        }
        if (level == 0)
            check_msg(decoded == 100,
                      "at a quarter the signal's amplitude, %d of 100\n",
                      decoded);
        else if (level == 2)
            check_msg(decoded >= 90,
                      "at noise equal to the signal, %d of 100 decode\n",
                      decoded);
        else
            check_msg(decoded >= 0, "noise level %.1f decoded %d of 100\n",
                      levels[level], decoded);
    }
}

int main(void) {
    test_message_round_trip();
    test_parity();
    test_convolutional_code();
    test_rate_matching();
    test_scrambling();
    test_whole_chain();
    test_the_wrong_cell_does_not_decode();
    test_noise_alone_is_not_a_message();
    test_a_message_has_to_repeat();
    test_how_much_damage_it_takes();

    return check_report("lte master information block");
}
