#ifndef LTE_CONFIRM_H
#define LTE_CONFIRM_H

/*
 * Which cell identities on a carrier are real.
 *
 * A block's identity was never evidence here. What is new is the reason a
 * *count* of blocks is not evidence either: `lte_cell_search_all` can mistake
 * a sidelobe for a cell, and it makes the same mistake every block, so the
 * false identity repeats exactly as faithfully as the true one. Measured on
 * EARFCN 3625 over 352 blocks -- PCI 410 reported in twenty-nine of them,
 * never once decoding a broadcast channel, while PCI 402 and PCI 190 decode
 * routinely at the same reference power. Any threshold on hits would have
 * confirmed 410.
 *
 * So the verdict is not how often an identity was seen but whether its own
 * broadcast channel decoded. A Master Information Block is scrambled with the
 * cell identity and checked by a sixteen-bit CRC: it cannot fit unless the
 * identity is right, which no amount of repetition can establish.
 *
 * Two messages rather than one, and the arithmetic is in `view_lte.c`'s
 * `pending_mib` comment: four scrambling offsets against three masks for each
 * of three antenna-port hypotheses is thirty-six chances a block, so a long
 * run sees one pass by luck. Two passes agreeing on an identity is not luck.
 *
 * Pure arithmetic over counts -- no window, no receiver, no samples
 * (ADR-0012).
 */

/*
 * Sixteen, not eight. A noisy carrier produces more spurious identities than
 * it has cells: EARFCN 3625 filled an eight-entry table with 187, 410, 191,
 * 406, 318, 23 and 163 -- one real cell among them -- and PCI 190, confirmed
 * in other runs of the same channel, never got a slot.
 */
#define LTE_CONFIRM_MAX_CELLS 16
/* One parity pass is expected in a long run; two agreeing is not. */
#define LTE_CONFIRM_MIN_MESSAGES 2
/* Below this an identity is one block's opinion rather than a claim. */
#define LTE_CONFIRM_MIN_LOOKS 2

enum lte_cell_verdict {
    /* Not enough looks to say anything. */
    LTE_CELL_PENDING = 0,
    /* Its own broadcast channel decoded, more than once. */
    LTE_CELL_CONFIRMED,
    /* Seen repeatedly and never read: present, or a habit of the search.
       This is the honest answer and it is not the same as either other. */
    LTE_CELL_UNREAD,
    /* One block's opinion, not repeated. */
    LTE_CELL_SPURIOUS
};

struct lte_cell_sighting {
    int pci;
    int looks;      /* blocks whose search reported this identity */
    int messages;   /* blocks in which its broadcast channel decoded */
};

struct lte_cell_tally {
    struct lte_cell_sighting cell[LTE_CONFIRM_MAX_CELLS];
    int count;
    int blocks;     /* how many blocks were looked at in all */
};

/* Record one block's sighting. `message` is whether that block's broadcast
   channel decoded under this identity. Identities beyond the table are
   dropped rather than displacing one already there: a table that churns
   reports whatever arrived last. */
static inline void lte_confirm_saw(struct lte_cell_tally *tally, int pci,
                                   int message) {
    int i;

    if (!tally)
        return;
    for (i = 0; i < tally->count; i++) {
        if (tally->cell[i].pci != pci)
            continue;
        tally->cell[i].looks++;
        tally->cell[i].messages += message ? 1 : 0;
        return;
    }
    if (tally->count >= LTE_CONFIRM_MAX_CELLS) {
        /*
         * Full. Take the slot of an identity seen exactly once and never
         * read, if there is one -- it is worth no more than the newcomer,
         * which is also seen once, so exchanging them loses nothing.
         *
         * Nothing else is evicted. An entry that has been seen twice is
         * better established than a new arrival, and an entry with a message
         * is a cell; a table that evicted those would report whichever
         * artefacts arrived last. Sizing the table is what keeps this rare,
         * and the eviction is what stops a run of singletons burying a real
         * cell that appears late.
         */
        for (i = 0; i < tally->count; i++) {
            if (tally->cell[i].looks == 1 && tally->cell[i].messages == 0) {
                tally->cell[i].pci = pci;
                tally->cell[i].messages = message ? 1 : 0;
                return;
            }
        }
        return;
    }
    tally->cell[tally->count].pci = pci;
    tally->cell[tally->count].looks = 1;
    tally->cell[tally->count].messages = message ? 1 : 0;
    tally->count++;
}

/* Counted once per block, whatever it held. */
static inline void lte_confirm_block(struct lte_cell_tally *tally) {
    if (tally)
        tally->blocks++;
}

static inline enum lte_cell_verdict
lte_cell_verdict_for(const struct lte_cell_sighting *seen) {
    if (!seen || seen->looks <= 0)
        return LTE_CELL_PENDING;
    /* The message decides, and it decides on its own: an identity read twice
       is real whether it was seen twice or a thousand times. */
    if (seen->messages >= LTE_CONFIRM_MIN_MESSAGES)
        return LTE_CELL_CONFIRMED;
    if (seen->looks < LTE_CONFIRM_MIN_LOOKS)
        return LTE_CELL_SPURIOUS;
    return LTE_CELL_UNREAD;
}

static inline const char *lte_cell_verdict_name(enum lte_cell_verdict v) {
    switch (v) {
    case LTE_CELL_CONFIRMED: return "confirmed";
    case LTE_CELL_UNREAD:    return "unread";
    case LTE_CELL_SPURIOUS:  return "spurious";
    default:                 return "pending";
    }
}

#endif
