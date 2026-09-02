#ifndef LTE_MIB_H
#define LTE_MIB_H

#include <stdint.h>

/*
 * The Master Information Block: the broadcast channel's soft bits in, a
 * message out.
 *
 * This is where the LTE side crosses from the Probe context into the Decoder
 * context (CONTEXT-MAP.md), exactly as gsm_bcch.c does for GSM. Everything
 * before it -- the primary and secondary synchronisation signals -- finds a
 * cell and measures it: an identity, a frame boundary, a frequency error.
 * This reads what the cell is *saying*: how wide it is, how it has arranged
 * its acknowledgement channel, and what time it thinks it is.
 *
 * Nothing here touches raylib, librtlsdr, a receiver or a sample. It takes
 * soft bits and returns a message, which is what lets the whole chain be
 * checked against a fixed vector (ADR-0012).
 *
 * The layers, from the air inwards (36.212 section 5.3.1):
 *
 *   480 soft bits     one 10 ms transmission, a quarter of the whole
 *   1920 bits         scrambled by a sequence the cell identity seeds
 *   120 coded bits    rate matched by interleaving and repetition
 *   40 bits           rate 1/3, K = 7, tail-biting convolutional
 *   24 bits           16 of parity, masked by the antenna-port count
 *
 * The 40 ms period is the awkward part and the reason the decode is a search.
 * One transmission carries 480 of the 1920 bits and says nothing about which
 * quarter of the period it is; four descrambling offsets are tried, and the
 * one whose parity checks out gives the two lowest bits of the frame number
 * as well as the message. The antenna-port count is found the same way, by
 * which of three parity masks fits -- which is why nothing above this layer
 * has to know it in advance.
 */

#define LTE_MIB_BITS 24
#define LTE_MIB_CRC_BITS 16
#define LTE_MIB_BLOCK_BITS (LTE_MIB_BITS + LTE_MIB_CRC_BITS)
#define LTE_MIB_CODED_BITS (3 * LTE_MIB_BLOCK_BITS)
#define LTE_MIB_RATE_MATCHED_BITS 1920
#define LTE_MIB_QUARTERS 4
#define LTE_MIB_QUARTER_BITS (LTE_MIB_RATE_MATCHED_BITS / LTE_MIB_QUARTERS)

/* Constraint length and the three generator polynomials, 133/171/165 octal
   (36.212 section 5.1.3.1). */
#define LTE_MIB_MEMORY 6
#define LTE_MIB_STATES (1 << LTE_MIB_MEMORY)

/*
 * What a Master Information Block says.
 *
 * `quarter` and `antenna_ports` are not fields of the message: they are what
 * decoding it discovered on the way -- which of the four scrambling offsets
 * fitted, and which parity mask. The frame number below already has the
 * quarter folded into its two lowest bits, which is the only place it can come
 * from.
 */
struct lte_mib {
    int bandwidth_prb;            /* 6, 15, 25, 50, 75 or 100 resource blocks */
    int phich_extended;           /* 0 normal duration, 1 extended */
    int phich_resource_sixths;    /* 1, 3, 6 or 12 -- a sixth, a half, 1, 2 */
    int system_frame_number;      /* 0 to 1023 */
    int antenna_ports;            /* 1, 2 or 4 */
    int quarter;                  /* which 10 ms of the 40 ms period */
};

/* The occupied bandwidth a resource-block count amounts to, in hertz: twelve
   subcarriers of 15 kHz each. The channel a carrier is licensed at is wider --
   a 50-block cell occupies 9 MHz of a 10 MHz channel -- and this reports what
   is transmitted rather than what is allocated. */
double lte_mib_occupied_hz(int bandwidth_prb);
/* "1/6", "1/2", "1" or "2", for the PHICH resource figure. NULL if it is not
   one of those. */
const char *lte_phich_resource_name(int sixths);

/*
 * The layers, each reversible and each worth checking on its own.
 *
 * The soft convention throughout is the one gsm_bcch.h uses: positive means
 * the bit is more likely 0, negative more likely 1, and the magnitude is the
 * confidence. Hard bits work too -- pass +1 and -1.
 */

/* Message <-> its 24 bits, most significant first. lte_mib_unpack returns 0
   when the bits name a bandwidth the standard does not define. */
void lte_mib_pack(const struct lte_mib *mib, uint8_t bits[LTE_MIB_BITS]);
int lte_mib_unpack(const uint8_t bits[LTE_MIB_BITS], struct lte_mib *mib);

/* The 16 parity bits, already masked for that many antenna ports. */
void lte_mib_parity(const uint8_t bits[LTE_MIB_BITS], int antenna_ports,
                    uint8_t parity[LTE_MIB_CRC_BITS]);
/* Which antenna-port count the parity of a decoded block fits, or 0 for none.
   The whole reason a receiver need not be told the port count in advance. */
int lte_mib_parity_ports(const uint8_t block[LTE_MIB_BLOCK_BITS]);

/* Rate 1/3, K = 7, tail-biting: the register starts holding the block's own
   last six bits rather than zeros, so there are no tail bits to spend and no
   known end state to lean on. The decoder pays for that by trying all 64
   starting states and keeping the best closed path. */
void lte_mib_convolutional_encode(const uint8_t block[LTE_MIB_BLOCK_BITS],
                                  uint8_t coded[LTE_MIB_CODED_BITS]);
void lte_mib_convolutional_decode(const float coded[LTE_MIB_CODED_BITS],
                                  uint8_t block[LTE_MIB_BLOCK_BITS]);

/* Sub-block interleaving and repetition, 120 bits out to 1920 and back. Each
   coded bit appears sixteen times in the full period, and exactly four times
   in any one 10 ms transmission -- which is what makes a single transmission
   decodable at all. */
void lte_mib_rate_match(const uint8_t coded[LTE_MIB_CODED_BITS],
                        uint8_t matched[LTE_MIB_RATE_MATCHED_BITS]);
void lte_mib_rate_dematch(const float matched[LTE_MIB_RATE_MATCHED_BITS],
                          float coded[LTE_MIB_CODED_BITS]);

/* The cell's scrambling sequence, applied in place to one quarter's soft bits.
   Its own inverse, as scrambling by a fixed sequence always is. */
void lte_mib_descramble(int pci, int quarter, float soft[LTE_MIB_QUARTER_BITS]);

/* One 10 ms transmission of a message, as +-1 soft bits: the whole chain
   forwards. */
void lte_mib_encode(const struct lte_mib *mib, int pci, int quarter,
                    float soft[LTE_MIB_QUARTER_BITS]);

/*
 * And the whole chain backwards. Returns 1 when one of the four quarters
 * decoded to a block whose parity fits one of the three port counts, in which
 * case *mib is filled in -- frame number, quarter and port count included.
 *
 * A message that fails is not reported: unlike the GSM broadcast block, whose
 * parity failure still tells the operator a burst quadruple arrived, there is
 * nothing here to hand back. The four quarters are indistinguishable until one
 * of them passes, so a failure means only that none did.
 */
int lte_mib_decode(const float soft[LTE_MIB_QUARTER_BITS], int pci,
                   struct lte_mib *mib);

#endif
