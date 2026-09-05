#ifndef LTE_TRANSPORT_H
#define LTE_TRANSPORT_H

#include <stdint.h>

#include "lte_turbo.h"

/*
 * What happens to a transport block between the message and the air: the
 * 24-bit parity, the filler bits, and the rate matching that fits a turbo
 * codeword to however many resource elements it was given (36.212 sections
 * 5.1.1, 5.1.2 and 5.1.4.1).
 *
 * The Master Information Block's rate matching is next door in lte_mib.c and
 * is a different problem wearing the same name. It repeats a fixed 120 bits
 * into a fixed 1920, and the receiver knows both numbers before it starts.
 * Here the output length is whatever the downlink control message allocated,
 * so the dematcher has to be *told* how many bits it is unpacking, and the
 * same codeword is punctured differently every time.
 *
 * Three things make this worth its own file rather than a function in the
 * decoder above it.
 *
 * **The filler bits.** When a transport block is shorter than the nearest
 * permitted turbo size, the standard prepends F bits that are not
 * transmitted: they are <NULL> at the encoder's input, and the first F bits of
 * streams 0 and 1 are <NULL> on the way out. Stream 2 is untouched, because
 * the interleaver has scattered those positions. A dematcher that forgets any
 * of that shifts every stream against the decoder and produces noise that
 * looks like a decode failure.
 *
 * **Two 24-bit polynomials in one document.** CRC-24A guards the transport
 * block; CRC-24B guards each code block when one is segmented. They differ,
 * they are the same width, and using the wrong one fails silently -- the
 * parity simply never checks, and nothing says why.
 *
 * **The circular buffer runs both ways.** Rate matching is a permutation with
 * puncturing and repetition, and the receiver has to invert it: soft values
 * for a position transmitted twice must be *added*, which is where the
 * repetition gain comes from. Forwards and backwards therefore have to agree
 * about every position, so one function computes the permutation and both
 * directions read it.
 *
 * Nothing here touches raylib, librtlsdr, a receiver or a sample (ADR-0012).
 */

/* The sub-block interleaver is 32 columns wide, whatever the block size. */
#define LTE_RM_COLUMNS 32

/*
 * g(D) = D^24 + D^23 + D^18 + D^17 + D^14 + D^11 + D^10 + D^7 + D^6 + D^5 +
 * D^4 + D^3 + D + 1, and D^24 + D^23 + D^6 + D^5 + D + 1. Written without the
 * leading D^24, which the shift register carries implicitly.
 */
#define LTE_CRC24A_POLY 0x864cfbu
#define LTE_CRC24B_POLY 0x800063u
#define LTE_CRC24_BITS 24

/*
 * The remainder of `count` bits, one per byte, most significant first.
 *
 * Two functions rather than one with a polynomial argument, because the
 * argument is exactly the thing that gets passed wrongly: a call site that
 * says `lte_crc24a` cannot be reading the wrong table, and one that says
 * `crc24(bits, n, poly)` can.
 */
unsigned int lte_crc24a(const uint8_t *bits, int count);
unsigned int lte_crc24b(const uint8_t *bits, int count);

/*
 * How a codeword is laid out in the circular buffer, given the block size and
 * how many of its leading bits are filler.
 *
 * `rows` is the sub-block interleaver's row count, `padding` the dummy bits it
 * prepends to fill the rectangle, `stream` the length of one interleaved
 * stream and `buffer` the whole thing. The dummies and the fillers are
 * different things that behave the same way -- both are skipped when bits are
 * selected -- and confusing them is why they are named apart here.
 */
struct lte_rm_plan {
    int k;          /* the turbo block size */
    int fillers;    /* leading bits that are not transmitted */
    int coded;      /* K + LTE_TURBO_TAIL, one encoded stream */
    int rows;
    int padding;    /* rows * 32 - coded */
    int stream;     /* rows * 32 */
    int buffer;     /* 3 * stream */
};

/* Returns 0, or -1 when `k` is not a permitted turbo size or `fillers` does
   not fit inside it. */
int lte_rm_plan_make(struct lte_rm_plan *plan, int k, int fillers);

/*
 * Where buffer position `at` came from: a flat index into the three encoded
 * streams, `stream * LTE_TURBO_TAIL`-free and laid out as
 * `s * plan->coded + i`, or **-1 when nothing was ever written there**.
 *
 * The -1 is the whole reason this exists. Two different kinds of hole land in
 * the buffer -- the interleaver's padding and the transport block's fillers --
 * and bit selection walks past both. Anything that walks the buffer without
 * asking transmits dummy bits and shortens the codeword by however many it hit.
 */
int lte_rm_origin(const struct lte_rm_plan *plan, int at);

/*
 * Where bit selection starts, for redundancy version 0 to 3.
 *
 * The four versions are four windows into the same buffer, spaced so that a
 * retransmission carries mostly bits the first one punctured. A receiver that
 * assumes version 0 decodes the first transmission and nothing else.
 */
int lte_rm_start(const struct lte_rm_plan *plan, int rv);

/*
 * Forwards: three encoded streams of `plan->coded` bits each into `e`
 * transmitted bits. Returns how many were written, which is `e` unless the
 * buffer holds nothing but holes.
 */
int lte_rate_match(const struct lte_rm_plan *plan, int rv, const uint8_t *d0,
                   const uint8_t *d1, const uint8_t *d2, uint8_t *out, int e);

/*
 * Backwards: `e` soft bits into three soft streams, which are **zeroed first
 * and then accumulated into**.
 *
 * Accumulated because a codeword shorter than the allocation wraps the
 * circular buffer and sends some positions more than once; adding the soft
 * values is what turns that repetition into confidence. A dematcher that
 * assigns instead of adding throws away every repeat but the last, and on a
 * heavily repeated allocation that is most of the signal.
 *
 * Positions that were punctured are left at zero, which is the soft value for
 * "no information" -- exactly what the decoder should be told about them.
 *
 * The filler positions are the exception, and they are the reason this knows
 * `fillers` at all. They were never transmitted, so nothing accumulates there,
 * but they are not unknown: the standard defines them as zero. Left at an
 * erasure the decoder has to work them out from the parity, which it can
 * mostly do and need not; told they are zero, it starts from the truth. So the
 * first `fillers` bits of stream 0 come back as a certainty rather than a
 * measurement. Stream 1's filler positions stay erased -- those are parity,
 * and parity over known bits is still whatever the encoder's state made it.
 */
#define LTE_RM_KNOWN_LLR 10.0f
void lte_rate_dematch(const struct lte_rm_plan *plan, int rv, const float *in,
                      int e, float *d0, float *d1, float *d2);

#endif
