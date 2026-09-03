#ifndef RDS_H
#define RDS_H

#include <stddef.h>
#include <stdint.h>

/*
 * RDS: soft bits in, a station's own account of itself out.
 *
 * Decoder side of the context map, beside gsm_bcch.c and lte_mib.c. No
 * samples reach here and no receiver: the front end in fm_dsp.c stops at soft
 * symbols, and this starts where bits become a message. It links libm and
 * nothing else.
 *
 * The layers, from the bitstream inwards (IEC 62106):
 *
 *   26 bits    one block: 16 of data, 10 of check
 *   10 bits    a shortened cyclic code, plus one of five offset words
 *   4 blocks   one group, 104 bits, offsets in a fixed order
 *   16 bits    block 1 is always the programme identification
 *
 * There is no preamble anywhere in RDS. A receiver finds the block boundary
 * by sliding the code along the bitstream and seeing where the syndrome comes
 * out equal to an offset word -- which is a search, and a search needs a floor
 * before its answer means anything. A 10-bit syndrome matches one of five
 * offsets by chance about once in two hundred tries, so a single block proves
 * nothing at all; what makes a lock safe is four of them in the right order at
 * the right spacing, and then the programme identification saying the same
 * thing twice. rds_sync_odds_per_million() states the first of those and
 * check-rds asserts it.
 */

#define RDS_BLOCK_BITS 26
#define RDS_DATA_BITS 16
#define RDS_CHECK_BITS 10
#define RDS_BLOCKS_PER_GROUP 4
#define RDS_GROUP_BITS (RDS_BLOCK_BITS * RDS_BLOCKS_PER_GROUP)

/* x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1 */
#define RDS_GENERATOR 0x5B9u

/*
 * The five offset words, and which position in a group each marks.
 *
 * C and C' both sit third. Which one appears says whether the group is
 * version A or version B, and in a version B group the third block repeats
 * the programme identification instead of carrying data -- so this is not a
 * detail of synchronisation, it changes what block three means.
 */
enum rds_offset {
    RDS_OFFSET_A = 0,
    RDS_OFFSET_B,
    RDS_OFFSET_C,
    RDS_OFFSET_C_PRIME,
    RDS_OFFSET_D,
    RDS_OFFSET_COUNT
};

/* The syndrome of a 26-bit block: the remainder modulo the generator. For a
   correctly received block this equals the offset word that was added. */
unsigned rds_syndrome(uint32_t block);
/* Which offset word a syndrome is, or -1 for none. */
int rds_offset_of(unsigned syndrome);
/* The word itself, for building a block. */
unsigned rds_offset_word(enum rds_offset offset);
/* Where in a group an offset sits: 0 to 3, or -1. C and C' both give 2. */
int rds_offset_position(enum rds_offset offset);
/* A 26-bit block from 16 data bits and an offset: the encoder, which exists
   so the decoder can be checked against something. */
uint32_t rds_encode_block(uint16_t data, enum rds_offset offset);

/*
 * How often a run of `blocks` consecutive blocks all match the offsets a group
 * demands, purely by chance, per million bit positions tried.
 *
 * Not decoration. The whole synchroniser is a search, and this is the number
 * that says how much of what it finds is nothing. One block is about 4900 per
 * million, which is to say a fifth of a percent and useless on its own; four
 * in a row is under a millionth of one.
 */
double rds_sync_odds_per_million(int blocks);

/*
 * A group, once found.
 *
 * `data` holds the sixteen information bits of each block, `present` says
 * which of the four actually passed their syndrome -- a group is reported
 * with a bad block rather than dropped, because block one carrying the
 * programme identification is worth having even when block four is noise.
 */
struct rds_group {
    uint16_t data[RDS_BLOCKS_PER_GROUP];
    int present[RDS_BLOCKS_PER_GROUP];
    int version_b;              /* the third block was C', not C */
    size_t bit_offset;          /* where in the stream it started */
};

/*
 * What a station says about itself.
 *
 * `ps` is only a name once every one of its four segments has arrived and the
 * whole thing has been seen twice: a half-filled programme service name is a
 * wrong name rather than a partial one -- "BBC R4" delivered two characters at
 * a time reads as "BB", then "BBC ", and each of those is a station that does
 * not exist. `ps_valid` is what says it may be shown.
 */
/*
 * Where a decode stopped, which is the only thing that tells two empty panels
 * apart.
 *
 * Nothing transmitting and every block failing its syndrome look identical on
 * screen -- the LTE view had to learn the same lesson -- and the difference is
 * the whole diagnosis. No blocks is tuning, or no RDS on this station. Blocks
 * without groups is the offset sequence not lining up, which is a
 * synchroniser problem. Groups without a name is a station that has one and
 * has not finished spelling it yet.
 */
struct rds_funnel {
    long bits;              /* soft bits offered */
    long blocks_matched;    /* blocks whose syndrome named their position */
    long groups;            /* groups with at least one good block */
    long identified;        /* groups that carried a programme identification */
    long named;             /* times a complete name was confirmed */
};

struct rds_station {
    struct rds_funnel funnel;
    int pi_valid;
    uint16_t pi;                /* programme identification */
    int pi_repeats;             /* how many groups agreed on it */

    int pty_valid;
    int pty;                    /* programme type, 0 to 31 */
    int tp;                     /* this programme carries traffic reports */
    int ta;                     /* a traffic announcement is on now */

    char ps[9];                 /* eight characters and a terminator */
    int ps_segments;            /* bitmask of the four that have arrived */
    int ps_valid;               /* complete, and confirmed by a repeat */
    char ps_pending[9];         /* the one being assembled */

    char rt[65];                /* radio text, up to 64 characters */
    int rt_valid;
    char rt_pending[65];
    uint32_t rt_segments;       /* which of the sixteen have arrived */
    int rt_ab;                  /* the A/B flag; it flipping clears the text */

    int groups_used;
    int groups_by_type[32];     /* type << 1 | version, for diagnosis */
};

void rds_station_init(struct rds_station *station);
/* Fold one group into what is known. Returns 1 when it changed anything. */
int rds_station_apply(struct rds_station *station, const struct rds_group *g);
/* The programme type's name, or NULL. The European table (IEC 62106), which
   is not the North American one -- the same number means something else
   there, and nothing in the signal says which table is in use. */
const char *rds_pty_name(int pty);

/*
 * Find where the groups are and read them.
 *
 * `soft` is the front end's output: positive means the bit is more likely
 * zero, the convention gsm_bcch.h and lte_mib.h share. Returns how many
 * groups were read, and fills `station` from all of them.
 *
 * Synchronisation is established once RDS_SYNC_BLOCKS consecutive blocks
 * agree with the offset sequence, and given up after RDS_SYNC_LOSS
 * consecutive groups arrive with no good block at all -- at which point it
 * searches again rather than reading noise at a stale alignment.
 */
#define RDS_SYNC_BLOCKS 4
#define RDS_SYNC_LOSS 8

size_t rds_decode(const float *soft, size_t count, struct rds_station *station,
                  struct rds_group *groups, size_t capacity);

#endif
