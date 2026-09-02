#ifndef LTE_GOLD_H
#define LTE_GOLD_H

#include <stddef.h>
#include <stdint.h>

/*
 * The length-31 Gold sequence of 36.211 section 7.2, which is every
 * pseudo-random sequence LTE uses.
 *
 * It is a header rather than a unit because both sides of the LTE split need
 * it and neither should depend on the other: the physical layer (lte_dsp.c)
 * generates the cell-specific reference signals with it, and the decoder
 * (lte_mib.c) descrambles the broadcast channel with it. Copying twenty lines
 * into both would give the two sides a chance to disagree about a sequence the
 * standard fixes; this way check-lte-dsp and check-lte-mib each compile the
 * same source.
 *
 * Two maximum-length sequences of degree 31, the first with a fixed
 * initialisation and the second carrying the caller's, added modulo 2 after
 * both have been advanced 1600 steps.
 */

#define LTE_GOLD_NC 1600

/* Writes `length` bits (0 or 1) into `c`. `length` must be positive. */
static void lte_gold_sequence(uint32_t c_init, int length, uint8_t *c) {
    /* The two registers, as 31-bit words holding x(n) .. x(n+30) with x(n) in
       the low bit. Advancing shifts a new x(n+31) into bit 30. */
    uint32_t x1 = 1;
    uint32_t x2 = c_init & 0x7fffffffu;
    int n;

    for (n = 0; n < LTE_GOLD_NC; n++) {
        /* x1(n+31) = x1(n+3) + x1(n) */
        uint32_t b1 = ((x1 >> 3) ^ x1) & 1u;
        /* x2(n+31) = x2(n+3) + x2(n+2) + x2(n+1) + x2(n) */
        uint32_t b2 = ((x2 >> 3) ^ (x2 >> 2) ^ (x2 >> 1) ^ x2) & 1u;
        x1 = (x1 >> 1) | (b1 << 30);
        x2 = (x2 >> 1) | (b2 << 30);
    }
    for (n = 0; n < length; n++) {
        uint32_t b1 = ((x1 >> 3) ^ x1) & 1u;
        uint32_t b2 = ((x2 >> 3) ^ (x2 >> 2) ^ (x2 >> 1) ^ x2) & 1u;
        c[n] = (uint8_t)((x1 ^ x2) & 1u);
        x1 = (x1 >> 1) | (b1 << 30);
        x2 = (x2 >> 1) | (b2 << 30);
    }
}

#endif
