#include "rds.h"

#include <math.h>
#include <string.h>

/*
 * The block code. See rds.h for the layers; this file is the arithmetic.
 */

static const unsigned OFFSET_WORDS[RDS_OFFSET_COUNT] = {
    0x0FCu,   /* A  */
    0x198u,   /* B  */
    0x168u,   /* C  */
    0x350u,   /* C' */
    0x1B4u    /* D  */
};

/* Where each sits in a group. C and C' are both the third block. */
static const int OFFSET_POSITION[RDS_OFFSET_COUNT] = { 0, 1, 2, 2, 3 };

unsigned rds_syndrome(uint32_t block) {
    uint32_t reg = 0;
    int i;

    for (i = RDS_BLOCK_BITS - 1; i >= 0; i--) {
        reg = (reg << 1) | ((block >> i) & 1u);
        if (reg & 0x400u)
            reg ^= (0x400u | RDS_GENERATOR);
    }
    return (unsigned)(reg & 0x3FFu);
}

int rds_offset_of(unsigned syndrome) {
    int i;

    for (i = 0; i < RDS_OFFSET_COUNT; i++)
        if (OFFSET_WORDS[i] == syndrome)
            return i;
    return -1;
}

unsigned rds_offset_word(enum rds_offset offset) {
    if (offset < 0 || offset >= RDS_OFFSET_COUNT)
        return 0;
    return OFFSET_WORDS[offset];
}

int rds_offset_position(enum rds_offset offset) {
    if (offset < 0 || offset >= RDS_OFFSET_COUNT)
        return -1;
    return OFFSET_POSITION[offset];
}

uint32_t rds_encode_block(uint16_t data, enum rds_offset offset) {
    uint32_t block = (uint32_t)data << RDS_CHECK_BITS;
    unsigned check;

    /* The check bits are the remainder of the data shifted up, which is what
       makes the whole block divisible; the offset is then added on top, which
       is what makes the remainder identify the position. */
    check = rds_syndrome(block);
    return block | ((check ^ rds_offset_word(offset)) & 0x3FFu);
}

double rds_sync_odds_per_million(int blocks) {
    /*
     * One block at a known position in a group has one offset it may be --
     * except the third, which has two. So a run of four expects, in the worst
     * case for a false lock, five candidate words at the first block and one
     * or two after: taking 1/1024 per block after the first and 5/1024 for
     * the first is the honest bound, since the searcher does not know which
     * position it is starting at.
     */
    double p;
    int i;

    if (blocks <= 0)
        return 1e6;
    p = 5.0 / 1024.0;
    for (i = 1; i < blocks; i++)
        p *= 2.0 / 1024.0;   /* generous: the third block allows C or C' */
    return p * 1e6;
}

void rds_station_init(struct rds_station *station) {
    if (!station)
        return;
    memset(station, 0, sizeof(*station));
    station->pty = -1;
    station->rt_ab = -1;
}

/*
 * The European programme types. The North American table puts different
 * meanings on the same numbers and nothing in the signal says which is in
 * use, so this is a choice about where the receiver is, not a decode.
 */
static const char *PTY_NAMES[32] = {
    "none", "news", "current affairs", "information", "sport", "education",
    "drama", "culture", "science", "varied", "pop music", "rock music",
    "easy listening", "light classical", "serious classical", "other music",
    "weather", "finance", "children", "social affairs", "religion",
    "phone in", "travel", "leisure", "jazz", "country", "national music",
    "oldies", "folk music", "documentary", "alarm test", "alarm"
};

const char *rds_pty_name(int pty) {
    if (pty < 0 || pty > 31)
        return NULL;
    return PTY_NAMES[pty];
}

/*
 * RDS carries a character set of its own; everything printable in the basic
 * table agrees with ASCII, and what does not is replaced rather than passed
 * through to a terminal that would make a mess of it.
 *
 * With one exception, and it was a bug before it was an exception: a carriage
 * return is how a station ends a radio text shorter than sixty-four
 * characters, and this function was turning it into a space before the code
 * that looks for it ever saw one. So a short message waited for all sixteen
 * segments and, on a station that only ever sends three, waited for ever.
 */
static char rds_character(unsigned code) {
    if (code >= 0x20 && code < 0x7F)
        return (char)code;
    if (code == 0x0D)
        return '\r';
    return ' ';
}

int rds_station_apply(struct rds_station *station, const struct rds_group *g) {
    int changed = 0;
    int type, version;

    if (!station || !g)
        return 0;

    /* Block one is the programme identification and nothing else, in every
       group there is. Requiring it to repeat is what separates a decode from
       a syndrome that passed by luck -- the rule lte_mib_same_cell() exists
       for, applied to the one field every group carries. */
    if (g->present[0]) {
        if (station->pi_valid && station->pi == g->data[0]) {
            station->pi_repeats++;
        } else if (station->pi_valid) {
            /* Disagreement: trust the new one but start counting again. */
            station->pi = g->data[0];
            station->pi_repeats = 1;
            changed = 1;
        } else {
            station->pi = g->data[0];
            station->pi_valid = 1;
            station->pi_repeats = 1;
            changed = 1;
        }
    }

    if (!g->present[1])
        return changed;

    type = (g->data[1] >> 12) & 0xF;
    version = (g->data[1] >> 11) & 0x1;
    station->groups_used++;
    station->groups_by_type[(type << 1) | version]++;

    station->tp = (g->data[1] >> 10) & 0x1;
    station->pty = (g->data[1] >> 5) & 0x1F;
    station->pty_valid = 1;

    /* In a version B group the third block repeats the identification, which
       is a second chance at it when block one was noise. */
    if (version && g->present[2] && !g->present[0]) {
        if (!station->pi_valid) {
            station->pi = g->data[2];
            station->pi_valid = 1;
            station->pi_repeats = 1;
            changed = 1;
        } else if (station->pi == g->data[2]) {
            station->pi_repeats++;
        }
    }

    if (type == 0) {
        /* Programme service name: two characters per group, at a segment
           address the group carries. */
        int segment = g->data[1] & 0x3;
        station->ta = (g->data[1] >> 4) & 0x1;
        if (g->present[3]) {
            char a = rds_character((unsigned)(g->data[3] >> 8) & 0xFF);
            char b = rds_character((unsigned)g->data[3] & 0xFF);
            if (station->ps_pending[segment * 2] != a ||
                station->ps_pending[segment * 2 + 1] != b) {
                /*
                 * A segment that arrives different from last time means the
                 * name being assembled is not the one now being sent, so what
                 * has been collected so far is not a prefix of anything.
                 * Keeping it would build a name out of two.
                 */
                if (station->ps_segments & (1 << segment)) {
                    station->ps_segments = 0;
                    memset(station->ps_pending, 0,
                           sizeof(station->ps_pending));
                }
                station->ps_pending[segment * 2] = a;
                station->ps_pending[segment * 2 + 1] = b;
            }
            station->ps_segments |= 1 << segment;
            station->ps_pending[8] = '\0';

            if (station->ps_segments == 0xF) {
                /* Complete. Shown only once it has been seen whole twice:
                   one pass through four segments can be four segments of two
                   different names and look perfect. */
                if (station->ps_valid &&
                    strcmp(station->ps, station->ps_pending) == 0) {
                    /* already agreed */
                } else if (!station->ps_valid &&
                           strcmp(station->ps, station->ps_pending) == 0) {
                    station->ps_valid = 1;
                    changed = 1;
                } else {
                    memcpy(station->ps, station->ps_pending, 9);
                    station->ps_valid = 0;
                    changed = 1;
                }
            }
        }
    } else if (type == 2) {
        /* Radio text. Version A carries four characters in blocks three and
           four; version B carries two, in block four. */
        int address = g->data[1] & 0xF;
        int ab = (g->data[1] >> 4) & 0x1;
        int base = version ? address * 2 : address * 4;
        char chars[4];
        int count = 0;

        if (station->rt_ab != ab) {
            /* The flag flipping is the station saying the text has changed;
               anything already collected belongs to the old one. */
            station->rt_ab = ab;
            station->rt_segments = 0;
            memset(station->rt_pending, 0, sizeof(station->rt_pending));
        }
        if (!version && g->present[2]) {
            chars[count++] = rds_character((unsigned)(g->data[2] >> 8) & 0xFF);
            chars[count++] = rds_character((unsigned)g->data[2] & 0xFF);
        }
        if (g->present[3]) {
            if (!version && count != 2)
                count = 2;   /* block three was missing; leave a hole */
            chars[count++] = rds_character((unsigned)(g->data[3] >> 8) & 0xFF);
            chars[count++] = rds_character((unsigned)g->data[3] & 0xFF);
        }
        {
            int k;
            for (k = 0; k < count; k++) {
                int at = base + k;
                if (at >= 0 && at < 64)
                    station->rt_pending[at] = chars[k];
            }
            if (count > 0)
                station->rt_segments |= 1u << address;
        }
        /* A carriage return ends the text early, which is how a station sends
           something shorter than sixty-four characters. */
        {
            int k, end = 64;
            for (k = 0; k < 64; k++)
                if (station->rt_pending[k] == '\r') {
                    end = k;
                    break;
                }
            if (end < 64 || station->rt_segments == 0xFFFFu) {
                memcpy(station->rt, station->rt_pending, 64);
                station->rt[end < 64 ? end : 64] = '\0';
                /*
                 * The padding is not information. A station with less than
                 * sixty-four characters to say fills the rest with spaces --
                 * ANTENA 2 sends "Cultura em antena2.rtp.pt" and thirty-nine
                 * blanks -- and passing those on makes every reader trim
                 * them, or forget to.
                 */
                {
                    int last = (int)strlen(station->rt);
                    while (last > 0 && station->rt[last - 1] == ' ')
                        station->rt[--last] = '\0';
                }
                station->rt_valid = 1;
                changed = 1;
            }
        }
    }
    return changed;
}

/* Twenty-six soft bits into a block, hard-decided. Positive means zero. */
static uint32_t block_at(const float *soft, size_t at) {
    uint32_t block = 0;
    int k;

    for (k = 0; k < RDS_BLOCK_BITS; k++)
        block = (block << 1) | (soft[at + (size_t)k] < 0.0f ? 1u : 0u);
    return block;
}

/*
 * Does a run of blocks starting here follow the offset order a group demands?
 * Returns how many consecutive blocks agreed, and where in the group the run
 * started.
 */
static int run_length(const float *soft, size_t count, size_t at,
                      int *start_position) {
    int matched = 0;
    int expected = -1;
    size_t p = at;

    while (p + RDS_BLOCK_BITS <= count) {
        int offset = rds_offset_of(rds_syndrome(block_at(soft, p)));
        int position;

        if (offset < 0)
            break;
        position = rds_offset_position((enum rds_offset)offset);
        if (expected < 0) {
            if (start_position)
                *start_position = position;
        } else if (position != expected) {
            break;
        }
        expected = (position + 1) % RDS_BLOCKS_PER_GROUP;
        matched++;
        p += RDS_BLOCK_BITS;
    }
    return matched;
}

size_t rds_decode(const float *soft, size_t count, struct rds_station *station,
                  struct rds_group *groups, size_t capacity) {
    size_t made = 0;
    size_t at;
    int position = 0;
    int synced = 0;
    int barren = 0;

    if (!soft || !station)
        return 0;
    rds_station_init(station);
    station->funnel.bits = (long)count;
    if (count < RDS_GROUP_BITS)
        return 0;

    at = 0;
    while (at + RDS_BLOCK_BITS <= count) {
        if (!synced) {
            /*
             * The search. Every bit position is tried, and a position is only
             * believed once RDS_SYNC_BLOCKS blocks in a row agree with the
             * offset order -- see rds_sync_odds_per_million for why one is
             * not nearly enough.
             */
            int start = 0;
            int run = run_length(soft, count, at, &start);

            if (run >= RDS_SYNC_BLOCKS) {
                synced = 1;
                barren = 0;
                position = start;
                continue;   /* read from here, without advancing */
            }
            at++;
            continue;
        }

        /* Synced: take four blocks as a group, from whichever position the
           run began at. Walk forward to the start of a group first. */
        if (position != 0) {
            at += RDS_BLOCK_BITS * (size_t)(RDS_BLOCKS_PER_GROUP - position);
            position = 0;
            continue;
        }
        if (at + RDS_GROUP_BITS > count)
            break;
        {
            struct rds_group g;
            int i, good = 0;

            memset(&g, 0, sizeof(g));
            g.bit_offset = at;
            for (i = 0; i < RDS_BLOCKS_PER_GROUP; i++) {
                size_t p = at + (size_t)i * RDS_BLOCK_BITS;
                uint32_t raw = block_at(soft, p);
                int offset = rds_offset_of(rds_syndrome(raw));

                if (offset >= 0 &&
                    rds_offset_position((enum rds_offset)offset) == i) {
                    g.data[i] = (uint16_t)(raw >> RDS_CHECK_BITS);
                    g.present[i] = 1;
                    good++;
                    if (i == 2 && offset == RDS_OFFSET_C_PRIME)
                        g.version_b = 1;
                }
            }
            station->funnel.blocks_matched += good;
            if (good == 0) {
                /* Nothing here at all. A few of these is fading; a run of
                   them means the alignment is stale and searching again beats
                   reading noise at it. */
                if (++barren >= RDS_SYNC_LOSS) {
                    synced = 0;
                    at++;
                    continue;
                }
            } else {
                barren = 0;
                station->funnel.groups++;
                if (g.present[0] || (g.present[2] && g.present[1] &&
                                     ((g.data[1] >> 11) & 1)))
                    station->funnel.identified++;
                {
                    int was_named = station->ps_valid;
                    rds_station_apply(station, &g);
                    if (!was_named && station->ps_valid)
                        station->funnel.named++;
                }
                if (groups && made < capacity)
                    groups[made] = g;
                made++;
            }
            at += RDS_GROUP_BITS;
        }
    }
    return made;
}

const char *rds_traffic_name(int tp, int ta) {
    if (tp && ta)
        return "traffic announcement now";
    if (tp)
        return "carries traffic announcements";
    if (ta)
        return "points to traffic on another station";
    return "no traffic information";
}
