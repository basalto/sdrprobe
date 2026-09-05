#include "check.h"

#include "lte_transport.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * The layer between a turbo codeword and the air.
 *
 * Two things here are transcribed from a document and everything else is
 * arithmetic over them: the two 24-bit polynomials, and the 32-entry column
 * permutation. Both are checked against properties they must have rather than
 * against their own use, because a wrong constant that both sides share
 * round-trips perfectly and fails only on real air -- which is how a
 * conjugated primary sequence and a scattered SCH field layout each stayed
 * green here for months.
 */

/* A repeatable bit pattern; the values do not matter, only that both
   directions see the same ones. */
static uint32_t seed = 0x12345678u;
static int next_bit(void) {
    seed = seed * 1103515245u + 12345u;
    return (int)((seed >> 16) & 1u);
}

/*
 * The polynomials, pinned by the one fact about them that does not come from
 * this file.
 *
 * A CRC register shifting in a message computes M(D)*D^24 mod g(D). Feed it
 * g(D) itself -- the twenty-five bits of the polynomial, leading one included
 * -- and the remainder is g(D)*D^24 mod g(D), which is zero for the right
 * polynomial and nothing in particular for any other. So a mistyped constant
 * cannot pass, and neither can the two being swapped.
 */
static void polynomial_bits(unsigned int poly, uint8_t out[25]) {
    int i;

    out[0] = 1;                       /* the implicit D^24 */
    for (i = 0; i < 24; i++)
        out[1 + i] = (uint8_t)((poly >> (23 - i)) & 1u);
}

static void test_the_two_polynomials(void) {
    uint8_t a[25], b[25];

    polynomial_bits(LTE_CRC24A_POLY, a);
    polynomial_bits(LTE_CRC24B_POLY, b);

    check_int("CRC-24A divides its own polynomial", (int)lte_crc24a(a, 25), 0);
    check_int("CRC-24B divides its own polynomial", (int)lte_crc24b(b, 25), 0);
    /*
     * And they are different polynomials, which is the whole risk: two 24-bit
     * CRCs in one document, one for the transport block and one for the code
     * blocks inside it. Using the wrong one fails silently -- the parity never
     * checks and nothing says why.
     */
    check_true("A does not divide B's polynomial", lte_crc24a(b, 25) != 0);
    check_true("B does not divide A's polynomial", lte_crc24b(a, 25) != 0);
    check_true("and the constants themselves differ",
               LTE_CRC24A_POLY != LTE_CRC24B_POLY);
}

static void test_what_a_crc_is_for(void) {
    uint8_t message[64 + LTE_CRC24_BITS];
    unsigned int remainder;
    int i;

    for (i = 0; i < 64; i++)
        message[i] = (uint8_t)next_bit();
    remainder = lte_crc24a(message, 64);
    for (i = 0; i < LTE_CRC24_BITS; i++)
        message[64 + i] = (uint8_t)((remainder >> (LTE_CRC24_BITS - 1 - i)) & 1u);

    /* The property a receiver actually uses: parity appended, remainder zero. */
    check_int("a message carrying its own parity checks out",
              (int)lte_crc24a(message, 64 + LTE_CRC24_BITS), 0);

    /* Every single-bit error is caught -- the least a CRC must do, and the
       thing a shift by one in the register would break. */
    {
        int missed = 0, first = -1;
        for (i = 0; i < 64 + LTE_CRC24_BITS; i++) {
            message[i] ^= 1;
            if (lte_crc24a(message, 64 + LTE_CRC24_BITS) == 0) {
                missed++;
                if (first < 0)
                    first = i;
            }
            message[i] ^= 1;
        }
        check_msg(missed == 0,
                  "%d single-bit errors went undetected, the first at bit "
                  "%d\n", missed, first);
    }

    /* Nothing at all has no remainder, whatever the polynomial. */
    check_int("an empty message", (int)lte_crc24a(message, 0), 0);
}

/* The permutation is a bit reversal of the five-bit column index, which is a
   property of the table rather than a copy of it. */
static void test_the_column_permutation(void) {
    static const int order[LTE_RM_COLUMNS] = {
        0, 16,  8, 24,  4, 20, 12, 28,  2, 18, 10, 26,  6, 22, 14, 30,
        1, 17,  9, 25,  5, 21, 13, 29,  3, 19, 11, 27,  7, 23, 15, 31
    };
    int seen[LTE_RM_COLUMNS];
    int i, b;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < LTE_RM_COLUMNS; i++) {
        int reversed = 0;
        for (b = 0; b < 5; b++)
            if (i & (1 << b))
                reversed |= 1 << (4 - b);
        check_msg(order[i] == reversed,
                  "column %d maps to %d, not to its bit reversal %d\n", i,
                  order[i], reversed);
        check_msg(order[i] >= 0 && order[i] < LTE_RM_COLUMNS,
                  "column %d maps outside the rectangle\n", i);
        seen[order[i]]++;
    }
    for (i = 0; i < LTE_RM_COLUMNS; i++)
        check_msg(seen[i] == 1, "column %d is used %d times\n", i, seen[i]);
}

/*
 * The buffer, and the two kinds of hole in it.
 *
 * Every bit of every stream must appear exactly once, and nothing else may.
 * A dematcher that walks the buffer without asking transmits the interleaver's
 * padding as though it were data, and shortens the codeword by however many it
 * hits -- silently, because the length still comes out right.
 */
static void test_every_bit_once_and_nothing_else(void) {
    const int sizes[] = { 40, 128, 512, 1024, 6144 };
    size_t s;

    for (s = 0; s < sizeof(sizes) / sizeof(*sizes); s++) {
        struct lte_rm_plan plan;
        static int seen[3 * (LTE_TURBO_MAX_K + LTE_TURBO_TAIL)];
        int at, i, holes = 0, expected;

        check_int("a permitted size plans",
                  lte_rm_plan_make(&plan, sizes[s], 0), 0);
        check_msg(plan.stream == plan.rows * LTE_RM_COLUMNS,
                  "K=%d: the rectangle is not rows by columns\n", sizes[s]);
        check_msg(plan.padding >= 0 && plan.padding < LTE_RM_COLUMNS,
                  "K=%d: %d padding bits, which is not less than a row\n",
                  sizes[s], plan.padding);

        memset(seen, 0, sizeof(int) * (size_t)(3 * plan.coded));
        {
            int out_of_range = 0;
            for (at = 0; at < plan.buffer; at++) {
                int origin = lte_rm_origin(&plan, at);
                if (origin < 0) {
                    holes++;
                    continue;
                }
                if (origin >= 3 * plan.coded) {
                    out_of_range++;
                    continue;
                }
                seen[origin]++;
            }
            check_msg(out_of_range == 0,
                      "K=%d: %d buffer positions claim an origin outside the "
                      "three streams\n", sizes[s], out_of_range);
        }
        {
            int missing = 0, twice = 0, first = -1;
            for (i = 0; i < 3 * plan.coded; i++) {
                if (seen[i] == 1)
                    continue;
                if (seen[i] == 0)
                    missing++;
                else
                    twice++;
                if (first < 0)
                    first = i;
            }
            check_msg(missing == 0 && twice == 0,
                      "K=%d: %d encoded bits never reach the buffer and %d "
                      "reach it more than once, the first at %d\n", sizes[s],
                      missing, twice, first);
        }
        /* Three streams of padding, and the padding is the only hole when
           there are no fillers. */
        expected = 3 * plan.padding;
        check_msg(holes == expected,
                  "K=%d: %d holes, expected %d\n", sizes[s], holes, expected);
    }
}

/*
 * The fillers, which are the trap this file exists for.
 *
 * They are a second kind of hole, in streams 0 and 1 only -- stream 2 has none,
 * because the turbo interleaver has already scattered those positions through
 * the block and there is nothing contiguous left to skip. Nulling all three
 * would drop parity that was really transmitted.
 */
static void test_the_fillers(void) {
    struct lte_rm_plan plan;
    int at, filler_holes = 0, stream2 = 0;

    check_int("a block with fillers plans",
              lte_rm_plan_make(&plan, 128, 12), 0);
    for (at = 0; at < plan.buffer; at++) {
        int origin = lte_rm_origin(&plan, at);
        if (origin >= 0 && origin / plan.coded == 2)
            stream2++;
    }
    check_int("stream 2 is complete, fillers or not", stream2, plan.coded);

    /* And the fillers really are missing from the other two. */
    for (at = 0; at < plan.buffer; at++)
        if (lte_rm_origin(&plan, at) < 0)
            filler_holes++;
    check_int("holes are the padding plus the fillers of two streams",
              filler_holes, 3 * plan.padding + 2 * 12);

    check_int("a size that is not permitted", lte_rm_plan_make(&plan, 100, 0),
              -1);
    check_int("more fillers than block", lte_rm_plan_make(&plan, 128, 128),
              -1);
    check_int("negative fillers", lte_rm_plan_make(&plan, 128, -1), -1);
}

/* The four redundancy versions are four windows into one buffer, and a
   receiver that assumes version 0 reads a retransmission as noise. */
static void test_the_redundancy_versions(void) {
    struct lte_rm_plan plan;
    int rv, previous = -1;

    lte_rm_plan_make(&plan, 512, 0);
    for (rv = 0; rv < 4; rv++) {
        int start = lte_rm_start(&plan, rv);
        check_msg(start >= 0 && start < plan.buffer,
                  "version %d starts at %d, outside a buffer of %d\n", rv,
                  start, plan.buffer);
        check_msg(start != previous,
                  "version %d starts where version %d did\n", rv, rv - 1);
        previous = start;
    }
    check_int("there is no version 4", lte_rm_start(&plan, 4), -1);
    check_int("nor a version -1", lte_rm_start(&plan, -1), -1);
}

/*
 * Matching then dematching, at several lengths and with fillers, through the
 * real turbo encoder and decoder.
 *
 * The block is what actually matters: a rate matcher that is subtly wrong
 * still produces the right *number* of bits, and only the decoder notices.
 */
static void round_trip(int k, int fillers, int e, int rv) {
    struct lte_rm_plan plan;
    static uint8_t in[LTE_TURBO_MAX_K];
    static uint8_t d0[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static uint8_t d1[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static uint8_t d2[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static uint8_t matched[3 * (LTE_TURBO_MAX_K + LTE_TURBO_TAIL) + 32];
    static float soft[3 * (LTE_TURBO_MAX_K + LTE_TURBO_TAIL) + 32];
    static float s0[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static float s1[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static float s2[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static uint8_t out[LTE_TURBO_MAX_K];
    char name[96];
    int i, taken, wrong = 0;

    check_int("the plan", lte_rm_plan_make(&plan, k, fillers), 0);
    for (i = 0; i < k; i++)
        in[i] = (uint8_t)(i < fillers ? 0 : next_bit());
    lte_turbo_encode(in, k, d0, d1, d2);

    taken = lte_rate_match(&plan, rv, d0, d1, d2, matched, e);
    snprintf(name, sizeof(name), "K=%d F=%d E=%d rv=%d: every bit asked for",
             k, fillers, e, rv);
    check_int(name, taken, e);

    /* Clean, so what is being measured is the permutation and not the
       decoder's noise performance -- that is lte_turbo_test's job. */
    for (i = 0; i < e; i++)
        soft[i] = matched[i] ? -1.0f : 1.0f;
    lte_rate_dematch(&plan, rv, soft, e, s0, s1, s2);
    lte_turbo_decode(s0, s1, s2, k, 6, out);

    for (i = 0; i < k; i++)
        if (out[i] != in[i])
            wrong++;
    snprintf(name, sizeof(name), "K=%d F=%d E=%d rv=%d: the block comes back",
             k, fillers, e, rv);
    check_int(name, wrong, 0);
}

static void test_round_trips(void) {
    /* The whole codeword, no puncturing and no repetition. */
    round_trip(40, 0, 3 * (40 + LTE_TURBO_TAIL), 0);
    round_trip(512, 0, 3 * (512 + LTE_TURBO_TAIL), 0);
    /* Fillers, which is the case the ticket was written for. A 100-bit
       transport block plus its 24-bit parity is 124, and the smallest
       permitted size holding it is 128 -- so four bits are never sent. */
    round_trip(128, 4, 3 * (128 + LTE_TURBO_TAIL), 0);
    round_trip(128, 12, 3 * (128 + LTE_TURBO_TAIL), 0);
    /* Repetition: fewer resource elements than the codeword has bits means
       going round the buffer twice, and the soft values must add. */
    round_trip(256, 8, 2 * 3 * (256 + LTE_TURBO_TAIL), 0);
    /* And a version other than the first. */
    round_trip(256, 0, 3 * (256 + LTE_TURBO_TAIL), 1);
}

/*
 * Repetition has to accumulate, not overwrite.
 *
 * A codeword shorter than its allocation goes round the buffer more than once,
 * and adding the soft values is where the repetition gain comes from. Assigning
 * instead throws away every repeat but the last, which costs 3 dB on a doubly
 * repeated allocation and looks like nothing at all in a clean round trip.
 */
static void test_repetition_adds(void) {
    struct lte_rm_plan plan;
    static float soft[8192];
    static float s0[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static float s1[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    static float s2[LTE_TURBO_MAX_K + LTE_TURBO_TAIL];
    int i, e, doubled = 0;

    lte_rm_plan_make(&plan, 40, 0);
    /* Twice round: every real position is sent exactly twice. */
    e = 2 * (plan.buffer - 3 * plan.padding);
    check_true("the test sends each position twice", e <= 8192);
    for (i = 0; i < e; i++)
        soft[i] = 1.0f;
    lte_rate_dematch(&plan, 0, soft, e, s0, s1, s2);
    {
        int thin = 0, first = -1;
        for (i = 0; i < plan.coded; i++) {
            if (fabsf(s0[i] - 2.0f) < 1e-6f)
                doubled++;
            if (s0[i] <= 1.5f) {
                thin++;
                if (first < 0)
                    first = i;
            }
        }
        check_msg(thin == 0,
                  "%d bits of stream 0 came back under 1.5, the first at %d, "
                  "so a repeat was overwritten rather than added\n", thin,
                  first);
    }
    check_int("every bit of stream 0 arrived twice", doubled, plan.coded);
}

int main(void) {
    test_the_two_polynomials();
    test_what_a_crc_is_for();
    test_the_column_permutation();
    test_every_bit_once_and_nothing_else();
    test_the_fillers();
    test_the_redundancy_versions();
    test_round_trips();
    test_repetition_adds();

    return check_report("LTE transport block: parity, fillers, rate matching");
}
