#ifndef GSM_BCCH_H
#define GSM_BCCH_H

#include <stdint.h>

/*
 * The Broadcast Control Channel: four normal bursts in, one System
 * Information message out.
 *
 * This is where the GSM side crosses from the Probe context into the Decoder
 * context (CONTEXT-MAP.md). Everything before it -- the FCCH tone, the SCH
 * burst -- measures the channel and reads the cell's identity code and its
 * clock. This reads what the cell is *saying*: which network it belongs to,
 * which location area, which cell, and on which frequencies its neighbours
 * are. Those are messages, and the words for them are the Decoder context's.
 *
 * Nothing here touches raylib, librtlsdr or a receiver, and none of it needs
 * samples: it takes soft bits and returns a message, which is what lets the
 * whole chain be checked against a fixed vector (ADR-0012).
 *
 * Nothing feeds it yet. Getting 456 soft bits off the air needs a burst
 * equaliser this repo does not have: the four bursts are findable and their
 * training sequences come back without error, but the data bits between them
 * arrive 9 to 17 per cent wrong, which is inter-symbol interference and more
 * than a rate-1/2 code can repair. The measurements are in
 * .scratch/gsm-bcch/issues/01-burst-equaliser.md. Until that lands this layer
 * is complete and unused, which is the honest state to leave it in -- it is
 * the half that can be proved right without a receiver.
 *
 * The layers, from the air inwards (GSM 05.03 section 4.1):
 *
 *   4 normal bursts   4 x 114 bits, block-rectangular interleaved
 *   456 coded bits    rate 1/2, K = 5 convolutional, the same code the SCH uses
 *   228 bits          4 tail bits removed
 *   224 bits          a (224,184) Fire code, 40 parity bits
 *   184 bits          23 octets: a LAPDm frame carrying one RR message
 */

#define GSM_BCCH_BURSTS 4
#define GSM_BURST_DATA_BITS 114 /* per normal burst, 2 x 57 either side of
                                   the training sequence */
#define GSM_BCCH_CODED_BITS 456
#define GSM_BCCH_UNCODED_BITS 228
#define GSM_BCCH_TAIL_BITS 4
#define GSM_BCCH_PARITY_BITS 40
#define GSM_BCCH_INFO_BITS 184
#define GSM_BCCH_INFO_OCTETS 23

/* The eight training sequences a normal burst may carry (GSM 05.02 5.2.3).
   Which one is in use is the BCC, the low three bits of the BSIC the SCH
   already decodes -- so the SCH is what tells this layer where to correlate. */
#define GSM_TSC_COUNT 8
#define GSM_TSC_BITS 26
extern const uint8_t gsm_training_sequences[GSM_TSC_COUNT][GSM_TSC_BITS];

/* One decoded 184-bit block, and whether its Fire code checked out. A block
   whose parity fails is reported rather than dropped: the caller can see that
   a burst quadruple arrived and did not survive, which is the difference
   between a weak cell and no cell. */
struct gsm_bcch_block {
    uint8_t octets[GSM_BCCH_INFO_OCTETS];
    int parity_ok;
};

/*
 * Undo the block-rectangular interleaving of GSM 05.03 4.1.4: four bursts of
 * 114 soft bits become 456 coded soft bits.
 *
 * `bursts` is indexed [burst][bit]. The soft convention throughout is a
 * log-likelihood: positive means the bit is more likely 0, negative more
 * likely 1, and the magnitude is the confidence. Hard bits work too -- pass
 * +1 and -1.
 */
void gsm_bcch_deinterleave(const float bursts[GSM_BCCH_BURSTS]
                                             [GSM_BURST_DATA_BITS],
                           float coded[GSM_BCCH_CODED_BITS]);

/* And the forward direction, for a check to build a burst quadruple that the
   deinterleaver must take apart again. */
void gsm_bcch_interleave(const uint8_t coded[GSM_BCCH_CODED_BITS],
                         uint8_t bursts[GSM_BCCH_BURSTS][GSM_BURST_DATA_BITS]);

/* The (224,184) Fire code of GSM 05.03 4.1.2, generator
   g(D) = (D^23 + 1)(D^17 + D^3 + 1). */
void gsm_bcch_fire_parity(const uint8_t info[GSM_BCCH_INFO_BITS],
                          uint8_t parity[GSM_BCCH_PARITY_BITS]);
int gsm_bcch_fire_check(const uint8_t codeword[GSM_BCCH_INFO_BITS +
                                               GSM_BCCH_PARITY_BITS]);

/* Encode 184 information bits all the way to 456 coded bits, which is what a
   check needs to feed the decoder something real. */
void gsm_bcch_encode(const uint8_t info[GSM_BCCH_INFO_BITS],
                     uint8_t coded[GSM_BCCH_CODED_BITS]);

/*
 * Soft Viterbi over the rate-1/2 K = 5 code, then the Fire check. Returns 1
 * when the parity holds -- 40 parity bits, so a block that passes by accident
 * is a one-in-a-million-million event and the message can be believed.
 */
int gsm_bcch_decode_block(const float coded[GSM_BCCH_CODED_BITS],
                          struct gsm_bcch_block *out);

/* ---- what the message says --------------------------------------------- */

enum gsm_si_type {
    GSM_SI_UNKNOWN = 0,
    GSM_SI_TYPE_1,
    GSM_SI_TYPE_2,
    GSM_SI_TYPE_2BIS,
    GSM_SI_TYPE_2TER,
    GSM_SI_TYPE_3,
    GSM_SI_TYPE_4,
    GSM_SI_TYPE_13
};

#define GSM_SI_MAX_NEIGHBOURS 32

/*
 * What one System Information message told us. Every field has a `have_`
 * flag: a message carries some of these and not others, and a zero that
 * means "not said" must not read as a zero that means "zero".
 */
struct gsm_si {
    enum gsm_si_type type;

    int have_lai;
    int mcc;                /* 268 is Portugal */
    int mnc;
    int mnc_digits;         /* 2 or 3; 01 and 001 are different networks */
    int lac;                /* Location Area Code */

    int have_cell_id;
    int cell_id;

    /* From SI 1 and 2: the ARFCNs this cell says are in use, or that its
       neighbours broadcast on. A frequency list, not a measurement. */
    int neighbour_count;
    int neighbours[GSM_SI_MAX_NEIGHBOURS];
};

const char *gsm_si_type_name(enum gsm_si_type type);

/*
 * Parse one 23-octet block. Returns 1 when the block was a System Information
 * message this understands, 0 otherwise -- a BCCH also carries paging and
 * other things, and saying nothing about those is better than guessing.
 */
int gsm_si_parse(const uint8_t octets[GSM_BCCH_INFO_OCTETS],
                 struct gsm_si *out);

#endif
