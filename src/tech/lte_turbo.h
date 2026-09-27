#ifndef LTE_TURBO_H
#define LTE_TURBO_H

#include <stdint.h>

/*
 * The LTE turbo code: rate 1/3, two eight-state recursive encoders, and the
 * quadratic permutation polynomial between them (36.212 section 5.1.3.2).
 *
 * Everything decoded in this repository so far has been Viterbi -- one pass, a
 * best path, done. This is the other kind: two decoders each producing soft
 * output, passing what they learned to each other, converging over several
 * rounds. It is what the broadcast channel above the Master Information Block
 * uses, so it is the gate to reading what an LTE cell actually says.
 *
 * Nothing here touches raylib, librtlsdr, a receiver or a sample. Soft bits
 * in, a block out (ADR-0012).
 *
 * The interleaver is the part worth being careful about. The two constituent
 * encoders are ordinary; what makes this code work is the permutation between
 * them, and that permutation is a table of 188 parameter pairs. A single
 * wrong pair still produces something that looks like an interleaver, still
 * round-trips through an encoder that shares the same wrong table, and
 * decodes nothing off the air -- which is exactly how a conjugated primary
 * sequence and a scattered SCH field layout both stayed green for months.
 *
 * So the table is checked against a property of the numbers rather than
 * against its own encoder: pi(i) = (f1*i + f2*i^2) mod K is a permutation if
 * and only if f1 is coprime with K and every prime factor of K divides f2.
 * That holds or it does not, with no encoder involved.
 */

/* 40 to 512 in steps of 8, 528 to 1024 in 16, 1056 to 2048 in 32, 2112 to
   6144 in 64. */
#define LTE_TURBO_SIZES 188
#define LTE_TURBO_MIN_K 40
#define LTE_TURBO_MAX_K 6144
/* Three tail positions per stream, four bits each, terminating both encoders
   rather than tail-biting: 12 bits that are not information. */
#define LTE_TURBO_TAIL 4
#define LTE_TURBO_STATES 8

/* The k'th permitted block size, or 0. Sizes are in increasing order. */
int lte_turbo_block_size(int index);
/* Which index a block size is, or -1 when it is not one of the 188. */
int lte_turbo_size_index(int k);
/* The smallest permitted size that holds `bits`, or 0 when none does. */
int lte_turbo_fit(int bits);
/* The interleaver's parameters for a block size, 0 when it is not permitted. */
int lte_turbo_params(int k, int *f1, int *f2);
/* pi(i) for a block size: where information bit i goes in the second
   encoder's input. Returns -1 on a size that is not permitted. */
int lte_turbo_pi(int k, int i);

/*
 * The encoder. `in` is K bits, one per byte, 0 or 1. Each output stream is
 * K + LTE_TURBO_TAIL bits.
 *
 * d0 is the information bits followed by tail; d1 and d2 are the two parity
 * streams. Present so the decoder can be checked against something other than
 * itself, and so a synthetic block can be built at a known noise level.
 */
void lte_turbo_encode(const uint8_t *in, int k, uint8_t *d0, uint8_t *d1,
                      uint8_t *d2);

/*
 * The decoder. Soft convention as everywhere else here: positive means the bit
 * is more likely 0, negative more likely 1, magnitude is confidence.
 *
 * Max-log-MAP, which is the log-domain BCJR with the sum replaced by its
 * largest term. It costs a few tenths of a decibel against the full version
 * and needs no lookup table, no scaling of the input, and no knowledge of the
 * noise variance -- the last of which matters most here, since nothing
 * upstream measures it.
 *
 * `iterations` is how many times the two decoders exchange. Beyond eight the
 * return is negligible; below three it has not converged.
 */
void lte_turbo_decode(const float *d0, const float *d1, const float *d2,
                      int k, int iterations, uint8_t *out);

#endif
