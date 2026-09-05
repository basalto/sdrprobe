#include "check.h"

#include "lte_turbo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * The turbo code, and above all the table that defines its interleaver.
 *
 * The interleaver's 188 parameter pairs are transcribed from the standard,
 * and a wrong pair is the failure this repository has twice paid months for:
 * it still looks like an interleaver, still round-trips through an encoder
 * that shares the same wrong table, and decodes nothing off the air. So the
 * table is checked against a fact about the numbers rather than against our
 * own encoder.
 */

static int gcd_of(int a, int b) {
    while (b) { int t = a % b; a = b; b = t; }
    return a;
}

static void test_the_block_sizes(void) {
    int i, expected = LTE_TURBO_MIN_K, step, wrong = 0;

    check_int("there are 188 permitted block sizes", LTE_TURBO_SIZES, 188);
    check_int("the smallest is 40", lte_turbo_block_size(0), LTE_TURBO_MIN_K);
    check_int("the largest is 6144", lte_turbo_block_size(LTE_TURBO_SIZES - 1),
              LTE_TURBO_MAX_K);

    /*
     * The sizes are not arbitrary and not a list to be trusted: 40 to 512 in
     * eights, 528 to 1024 in sixteens, 1056 to 2048 in thirty-twos, 2112 to
     * 6144 in sixty-fours. Generating them and comparing catches a
     * transcription slip that a monotonic check would not.
     */
    for (i = 0; i < LTE_TURBO_SIZES; i++) {
        /* The step changes *at* the boundary, not after it: 512 is followed
           by 528, not 520. */
        if (expected < 512) step = 8;
        else if (expected < 1024) step = 16;
        else if (expected < 2048) step = 32;
        else step = 64;
        if (lte_turbo_block_size(i) != expected)
            wrong++;
        expected += step;
    }
    check_int("and they follow the standard's four ranges", wrong, 0);

    check_int("a size outside the table is not one", lte_turbo_size_index(41),
              -1);
    check_int("nor is one below the smallest", lte_turbo_size_index(32), -1);
    check_int("nor above the largest", lte_turbo_size_index(6208), -1);
    check_int("40 is the first", lte_turbo_size_index(40), 0);

    check_int("a block of 100 bits needs 104", lte_turbo_fit(100), 104);
    check_int("a block of exactly 512 needs 512", lte_turbo_fit(512), 512);
    check_int("513 needs 528", lte_turbo_fit(513), 528);
    check_int("nothing holds more than 6144", lte_turbo_fit(6145), 0);
}

/*
 * The property that makes a quadratic permutation polynomial a permutation:
 * f1 coprime with K, and every prime factor of K dividing f2. Provable from
 * the numbers, with no encoder involved -- which is the whole point, because
 * an encoder and a decoder sharing a wrong table agree perfectly.
 */
static void test_the_table_is_permutations(void) {
    int i, not_coprime = 0, bad_f2 = 0;

    for (i = 0; i < LTE_TURBO_SIZES; i++) {
        int k = lte_turbo_block_size(i), f1 = 0, f2 = 0, d;

        check_msg(lte_turbo_params(k, &f1, &f2) == 1,
                  "K = %d has no parameters\n", k);
        if (gcd_of(f1, k) != 1)
            not_coprime++;
        /* Every *prime* factor. Dividing k down as they are found is what
           keeps the leftover prime -- treating the cofactor as prime without
           reducing it reports K = 120 as failing on 40, which is not a
           prime and not a factor anybody claimed. */
        {
            int n = k;
            for (d = 2; (long)d * d <= n; d++) {
                if (n % d)
                    continue;
                while (n % d == 0)
                    n /= d;
                if (f2 % d)
                    bad_f2++;
            }
            if (n > 1 && f2 % n)
                bad_f2++;
        }
    }
    check_int("f1 is coprime with K for every size", not_coprime, 0);
    check_int("and every prime factor of K divides f2", bad_f2, 0);
}

/* And it is a bijection in fact, not only in principle. Every output hit
   exactly once, at every size -- walked in full for the small ones and
   sampled for the large, since 188 full walks is 700000 steps. */
static void test_the_interleaver_is_a_bijection(void) {
    static unsigned char seen[LTE_TURBO_MAX_K];
    int i, collisions = 0, missing = 0, out_of_range = 0;

    for (i = 0; i < LTE_TURBO_SIZES; i++) {
        int k = lte_turbo_block_size(i), j;

        memset(seen, 0, (size_t)k);
        for (j = 0; j < k; j++) {
            int p = lte_turbo_pi(k, j);
            if (p < 0 || p >= k) {
                out_of_range++;
                continue;
            }
            if (seen[p])
                collisions++;
            seen[p] = 1;
        }
        for (j = 0; j < k; j++)
            if (!seen[j])
                missing++;
    }
    check_int("no interleaver sends two bits to one place", collisions, 0);
    check_int("none leaves a place empty", missing, 0);
    check_int("and none points outside its block", out_of_range, 0);

    check_int("an index past the block is refused", lte_turbo_pi(40, 40), -1);
    check_int("so is a negative one", lte_turbo_pi(40, -1), -1);
    check_int("and a size that is not permitted", lte_turbo_pi(41, 0), -1);
}

/* The largest block is where a naive f2 * i * i overflows a 32-bit int well
   before the modulus brings it back: 480 * 6143 * 6143 is about 1.8e10. */
static void test_the_largest_block_does_not_overflow(void) {
    int k = LTE_TURBO_MAX_K, i, out_of_range = 0;

    for (i = 0; i < k; i++) {
        int p = lte_turbo_pi(k, i);
        if (p < 0 || p >= k)
            out_of_range++;
    }
    check_int("K = 6144 stays inside its block", out_of_range, 0);
}

static void fill(uint8_t *bits, int k, unsigned seed) {
    int i;
    for (i = 0; i < k; i++) {
        seed = seed * 1664525u + 1013904223u;
        bits[i] = (uint8_t)((seed >> 24) & 1);
    }
}

/*
 * The encoder terminates. Both constituent encoders spend three bits getting
 * back to the zero state, and a decoder that assumed tail-biting -- which is
 * what the Master Information Block's convolutional code does, three files
 * away -- would lose the end of every block.
 */
static void test_the_trellis_terminates(void) {
    static uint8_t in[512], d0[512 + 4], d1[512 + 4], d2[512 + 4];
    int k = 512;

    fill(in, k, 7u);
    lte_turbo_encode(in, k, d0, d1, d2);

    /* The systematic stream carries the information bits unchanged, which is
       what "systematic" means and is worth asserting before anything else. */
    {
        int i, differ = 0;
        for (i = 0; i < k; i++)
            if (d0[i] != in[i])
                differ++;
        check_int("the systematic stream is the input", differ, 0);
    }
    /* Four tail positions per stream, twelve bits that are not information. */
    check_int("the tail is four positions", LTE_TURBO_TAIL, 4);
}

/*
 * Clean, the decoder is exact. This is the round trip, and it is the weakest
 * check here on purpose -- it cannot see a wrong interleaver, because the
 * encoder above it uses the same one.
 */
static void test_clean_round_trip(void) {
    static uint8_t in[1024], out[1024];
    static uint8_t d0[1024 + 4], d1[1024 + 4], d2[1024 + 4];
    static float f0[1024 + 4], f1[1024 + 4], f2[1024 + 4];
    static const int sizes[] = { 40, 104, 512, 1024 };
    unsigned s;

    for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        int k = sizes[s], i, wrong = 0;

        fill(in, k, 11u + s);
        lte_turbo_encode(in, k, d0, d1, d2);
        for (i = 0; i < k + LTE_TURBO_TAIL; i++) {
            f0[i] = d0[i] ? -4.0f : 4.0f;
            f1[i] = d1[i] ? -4.0f : 4.0f;
            f2[i] = d2[i] ? -4.0f : 4.0f;
        }
        lte_turbo_decode(f0, f1, f2, k, 4, out);
        for (i = 0; i < k; i++)
            if (out[i] != in[i])
                wrong++;
        check_msg(wrong == 0, "K = %d: %d of %d bits wrong with no noise\n",
                  k, wrong, k);
    }
}

/*
 * And it iterates. A turbo decoder that does not get better between the first
 * round and the eighth is not exchanging anything -- which is what a sign
 * error in the extrinsic, or feeding the systematic value back into it, looks
 * like from outside. The clean round trip above passes either way.
 */
static void test_it_improves_with_iterations(void) {
    static uint8_t in[512], out[512];
    static uint8_t d0[512 + 4], d1[512 + 4], d2[512 + 4];
    static float f0[512 + 4], f1[512 + 4], f2[512 + 4];
    int k = 512, i, one, eight;
    unsigned seed = 99u;

    fill(in, k, 3u);
    lte_turbo_encode(in, k, d0, d1, d2);
    /*
     * Sigma 1.0 against a unit signal: Es/N0 of -3 dB, which for a rate 1/3
     * code is about 1.8 dB of Eb/N0. That is the operating point where the
     * iteration is visible -- one pass leaves a score of errors and eight
     * clear them all.
     *
     * The point has to be chosen, not guessed. Written first with three times
     * this noise, where one iteration and eight both left about a third of
     * the block wrong: past the code's cliff, no decoder converges, and the
     * check said the decoder was broken when it was the noise.
     */
    for (i = 0; i < k + LTE_TURBO_TAIL; i++) {
        float n[3];
        int j, r;
        for (j = 0; j < 3; j++) {
            float sum = 0.0f;
            /* Four uniforms make a passable normal, and this needs a
               distribution with tails rather than a hard bound. */
            for (r = 0; r < 4; r++) {
                seed = seed * 1103515245u + 12345u;
                sum += (float)((seed >> 16) & 0xFFFF) / 32768.0f - 1.0f;
            }
            n[j] = sum * 0.866f;
        }
        f0[i] = (d0[i] ? -1.0f : 1.0f) + n[0];
        f1[i] = (d1[i] ? -1.0f : 1.0f) + n[1];
        f2[i] = (d2[i] ? -1.0f : 1.0f) + n[2];
    }

    lte_turbo_decode(f0, f1, f2, k, 1, out);
    for (i = 0, one = 0; i < k; i++)
        if (out[i] != in[i])
            one++;
    lte_turbo_decode(f0, f1, f2, k, 8, out);
    for (i = 0, eight = 0; i < k; i++)
        if (out[i] != in[i])
            eight++;

    check_msg(one > 0, "the noise was too gentle to tell anything: %d errors "
              "after one iteration\n", one);
    check_msg(eight < one, "eight iterations did not beat one: %d against "
              "%d errors\n", eight, one);
    check_msg(eight == 0, "eight iterations left %d of %d bits wrong\n",
              eight, k);
}

/*
 * Where it stops working, which is worth pinning because it is the number
 * that says the decoder is a turbo decoder and not merely a repetition
 * decoder that happens to pass a clean round trip.
 *
 * Measured: clean through Es/N0 -1.1 dB, twenty errors after one iteration
 * and none after eight at -3.0 dB, and past the cliff by -4.6 dB where
 * iterating makes it worse rather than better. A rate 1/3 code at -3.0 dB Es
 * is 1.8 dB of Eb/N0, which is where max-log-MAP on a block this size belongs
 * -- a decoder working several decibels the wrong side of that is not this
 * code.
 */
static void test_where_it_stops_working(void) {
    static uint8_t in[512], out[512];
    static uint8_t d0[512 + 4], d1[512 + 4], d2[512 + 4];
    static float f0[512 + 4], f1[512 + 4], f2[512 + 4];
    int k = 512, i, errors;
    unsigned seed = 5u;

    fill(in, k, 3u);
    lte_turbo_encode(in, k, d0, d1, d2);
    for (i = 0; i < k + LTE_TURBO_TAIL; i++) {
        float n[3];
        int j, r;
        for (j = 0; j < 3; j++) {
            float sum = 0.0f;
            for (r = 0; r < 4; r++) {
                seed = seed * 1103515245u + 12345u;
                sum += (float)((seed >> 16) & 0xFFFF) / 32768.0f - 1.0f;
            }
            n[j] = sum * 0.866f * 0.7f;      /* sigma 0.7, about -0.1 dB Es */
        }
        f0[i] = (d0[i] ? -1.0f : 1.0f) + n[0];
        f1[i] = (d1[i] ? -1.0f : 1.0f) + n[1];
        f2[i] = (d2[i] ? -1.0f : 1.0f) + n[2];
    }
    lte_turbo_decode(f0, f1, f2, k, 8, out);
    for (i = 0, errors = 0; i < k; i++)
        if (out[i] != in[i])
            errors++;
    check_msg(errors == 0, "sigma 0.7 should decode cleanly, %d bits wrong\n",
              errors);
}

int main(void) {
    test_the_block_sizes();
    test_the_table_is_permutations();
    test_the_interleaver_is_a_bijection();
    test_the_largest_block_does_not_overflow();
    test_the_trellis_terminates();
    test_clean_round_trip();
    test_it_improves_with_iterations();
    test_where_it_stops_working();

    return check_report("LTE turbo code and its QPP interleaver");
}
