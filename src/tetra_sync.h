#ifndef TETRA_SYNC_H
#define TETRA_SYNC_H

#include <stdint.h>

/*
 * The TETRA synchronization block: 120 bits off the air to a 60-bit message
 * whose parity checks, or nothing.
 *
 * Everything here is transcribed from ETSI EN 300 392-2 V3.8.1 (2016-08), and
 * transcription is the whole risk. A puncturing pattern, an interleaver stride
 * or a polynomial that is wrong still round-trips perfectly against an encoder
 * that shares the mistake, and reads nothing off the air. That has happened
 * three times in this repository -- a conjugated LTE primary sequence, a
 * scattered GSM SCH field layout, and the pi/4-DQPSK phase table next door,
 * which was wrong for a day while every synthetic test passed.
 *
 * So the check that matters is not in tests/: it is the 16-bit parity passing
 * on real symbols. Nothing above this layer should believe a block that failed
 * it, and this returns nothing when it does.
 *
 * The chain, clause 8.3.1.2, read backwards:
 *
 *   120 bits   sb(1..120), scrambled, off the burst
 *   descramble clause 8.2.5, and for BSCH the extended colour code is all
 *              zeros -- which is what lets a terminal read this from a network
 *              it has never seen
 *   (120,11)   block de-interleaving, clause 8.2.4.1
 *   RCPC 2/3   16-state, punctured from a rate 1/4 mother code, clause 8.2.3.1
 *   80 bits    of which the last four are the tail
 *   (76,60)    CRC-CCITT over the first 60, clause 8.2.3.3
 *   60 bits    the SYNC PDU
 *
 * No raylib, no librtlsdr, no receiver (ADR-0012).
 */

#define TETRA_SB_SCRAMBLED_BITS 120
#define TETRA_SB_TYPE2_BITS 80
#define TETRA_SB_TAIL_BITS 4
#define TETRA_SB_CRC_BITS 16
#define TETRA_SB_MESSAGE_BITS 60

/* The mother code is rate 1/4 with memory 4, so sixteen states and four output
   bits per input bit. */
#define TETRA_RCPC_STATES 16
#define TETRA_RCPC_MEMORY 4
#define TETRA_RCPC_OUTPUTS 4

/* (120,11): b4(k) = b3(i) with k = 1 + ((11 * i) mod 120). */
#define TETRA_SB_INTERLEAVE_STRIDE 11

/*
 * Turn 60 dibits -- the 120 scrambled bits sb(1..120), which sit at bits 95 to
 * 214 of the synchronization burst, immediately before the training sequence
 * -- into the 60-bit message.
 *
 * Returns 1 when the parity checks and `message` is filled in, 0 otherwise.
 * There is deliberately no way to ask for the bits of a block that failed: a
 * colour code with no check behind it is a number, not a finding, and the GSM
 * side sets that standard.
 */
int tetra_sync_block_decode(const unsigned char *dibits,
                            uint8_t message[TETRA_SB_MESSAGE_BITS]);

/*
 * The broadcast network channel, which rides in the same burst: 216 scrambled
 * bits at bits 283 to 498, which is symbols 142 to 249 -- 34 symbols after the
 * synchronization training sequence starts.
 *
 * Two things differ from the synchronization block and both matter. It is a
 * (140,124) code over 144 type-2 bits with a (216,101) interleaver rather than
 * (76,60) over 80 with (120,11). And **it is scrambled with the network's real
 * extended colour code**, not with zeros -- 30 bits of MCC, MNC and colour
 * code, every one of which has to be read out of the synchronization block
 * first. That ordering is the whole architecture of the air interface: a
 * terminal finds the network before it can hear what the network says about
 * itself.
 */
#define TETRA_BNCH_SCRAMBLED_BITS 216
#define TETRA_BNCH_TYPE2_BITS 144
#define TETRA_BNCH_MESSAGE_BITS 124
#define TETRA_BNCH_INTERLEAVE_STRIDE 101
#define TETRA_BNCH_AT_SYMBOL 34      /* after the sync word's first symbol */
#define TETRA_COLOUR_BITS 30

/* MCC, MNC and colour code into the 30-bit extended colour code (clause 23.2.1,
   figure 23.5): the 24-bit network identity then the 6-bit colour code. */
void tetra_extended_colour(int mcc, int mnc, int colour,
                           uint8_t out[TETRA_COLOUR_BITS]);

/*
 * Returns 1 when the parity checks. `colour` is the extended colour code, and
 * for the synchronization block it is all zeros -- which is what
 * tetra_sync_block_decode passes.
 */
int tetra_bnch_decode(const unsigned char *dibits,
                      const uint8_t colour[TETRA_COLOUR_BITS],
                      uint8_t message[TETRA_BNCH_MESSAGE_BITS]);

/*
 * The pieces, exposed so each can be checked on its own rather than only
 * through the whole chain -- a chain that fails tells you nothing about which
 * link did it.
 */
void tetra_descramble(uint8_t *bits, int count,
                      const uint8_t colour[TETRA_COLOUR_BITS]);
void tetra_deinterleave(const uint8_t *in, uint8_t *out, int count,
                        int stride);
/* Kept as the zero-colour, 120-bit case the synchronization block uses. */
void tetra_sb_descramble(uint8_t bits[TETRA_SB_SCRAMBLED_BITS]);
void tetra_sb_deinterleave(const uint8_t in[TETRA_SB_SCRAMBLED_BITS],
                           uint8_t out[TETRA_SB_SCRAMBLED_BITS]);
/* The rate-2/3 puncturing: which mother-code bit each of the 120 carries, as a
   0-based index into the 320 the mother code produces. */
int tetra_rcpc_puncture_index(int j);
/* Hard-decision Viterbi over the terminated mother code. `erased` marks the
   punctured positions, which cost nothing rather than counting as errors. */
void tetra_rcpc_decode(const uint8_t *mother, const uint8_t *erased,
                       int type2_bits, uint8_t *out);
/* CRC-CCITT as clause 8.2.3.3 defines it: X^16 + X^12 + X^5 + 1, the register
   preset to ones and the remainder complemented (ITU-T X.25). */
unsigned int tetra_sb_crc(const uint8_t *bits, int count);

#endif
