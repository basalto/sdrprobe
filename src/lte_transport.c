#include "lte_transport.h"

#include <string.h>

/*
 * The inter-column permutation the sub-block interleaver uses, 36.212 table
 * 5.1.4-1. Thirty-two entries, and it is a bit-reversal of the five-bit column
 * index: column 1 is 16, column 2 is 8, column 3 is 24.
 *
 * Worth knowing, because it is what the table can be checked against. A
 * transcribed permutation that is wrong in one place still permutes, still
 * round-trips through a matcher that shares the mistake, and fails only
 * against real air -- the failure this repository has now met twice, in a
 * conjugated primary sequence and a scattered SCH field layout.
 */
static const int column_order[LTE_RM_COLUMNS] = {
    0, 16,  8, 24,  4, 20, 12, 28,  2, 18, 10, 26,  6, 22, 14, 30,
    1, 17,  9, 25,  5, 21, 13, 29,  3, 19, 11, 27,  7, 23, 15, 31
};

static unsigned int crc24(const uint8_t *bits, int count, unsigned int poly) {
    unsigned int reg = 0;
    int i;

    if (!bits)
        return 0;
    for (i = 0; i < count; i++) {
        unsigned int feedback = ((reg >> 23) & 1u) ^ (bits[i] & 1u);
        reg = (reg << 1) & 0xffffffu;
        if (feedback)
            reg ^= poly;
    }
    return reg;
}

unsigned int lte_crc24a(const uint8_t *bits, int count) {
    return crc24(bits, count, LTE_CRC24A_POLY);
}

unsigned int lte_crc24b(const uint8_t *bits, int count) {
    return crc24(bits, count, LTE_CRC24B_POLY);
}

int lte_rm_plan_make(struct lte_rm_plan *plan, int k, int fillers) {
    int coded, rows;

    if (!plan || lte_turbo_size_index(k) < 0 || fillers < 0 || fillers >= k)
        return -1;
    coded = k + LTE_TURBO_TAIL;
    rows = (coded + LTE_RM_COLUMNS - 1) / LTE_RM_COLUMNS;
    plan->k = k;
    plan->fillers = fillers;
    plan->coded = coded;
    plan->rows = rows;
    plan->stream = rows * LTE_RM_COLUMNS;
    plan->padding = plan->stream - coded;
    plan->buffer = 3 * plan->stream;
    return 0;
}

/*
 * Streams 0 and 1: written into the rectangle row by row, the columns shuffled,
 * read out column by column. Position `at` in the output is column at/rows,
 * row at%rows -- so it came from the row-major cell (row, order[column]).
 */
static int interleaved_source(const struct lte_rm_plan *plan, int at) {
    int column = at / plan->rows;
    int row = at % plan->rows;

    return row * LTE_RM_COLUMNS + column_order[column];
}

/*
 * Stream 2 is not the same interleaver. It reads the same rectangle through a
 * permutation of its own, offset by one -- which is what keeps the two parity
 * streams from being punctured in the same places.
 */
static int interleaved_source_two(const struct lte_rm_plan *plan, int at) {
    int column = at / plan->rows;
    int row = at % plan->rows;

    return (column_order[column] + LTE_RM_COLUMNS * row + 1) % plan->stream;
}

int lte_rm_origin(const struct lte_rm_plan *plan, int at) {
    int stream, index, cell;

    if (!plan || at < 0 || at >= plan->buffer)
        return -1;
    if (at < plan->stream) {
        stream = 0;
        cell = interleaved_source(plan, at);
    } else if (((at - plan->stream) & 1) == 0) {
        stream = 1;
        cell = interleaved_source(plan, (at - plan->stream) / 2);
    } else {
        stream = 2;
        cell = interleaved_source_two(plan, (at - plan->stream) / 2);
    }
    /* The rectangle is filled from the right: its first `padding` cells were
       never written, and reading one transmits a dummy. */
    if (cell < plan->padding)
        return -1;
    index = cell - plan->padding;
    if (index >= plan->coded)
        return -1;
    /*
     * And the transport block's own fillers, which are a different hole in the
     * same buffer: the first F bits of streams 0 and 1 only. Stream 2 carries
     * no fillers because the turbo interleaver has already scattered those
     * positions through the block, so there is nothing contiguous to skip.
     */
    if (stream != 2 && index < plan->fillers)
        return -1;
    return stream * plan->coded + index;
}

int lte_rm_start(const struct lte_rm_plan *plan, int rv) {
    int per_window;

    if (!plan || rv < 0 || rv > 3)
        return -1;
    per_window = (plan->buffer + 8 * plan->rows - 1) / (8 * plan->rows);
    return (plan->rows * (2 * per_window * rv + 2)) % plan->buffer;
}

/*
 * Walk the circular buffer from the start position, skipping holes, and hand
 * back each position that carries a bit. Returns how many were found, which is
 * `want` unless the buffer has nothing in it at all.
 *
 * One walk, used forwards and backwards. Two walks that agreed today would
 * stop agreeing the first time either was changed, and the failure is a
 * decoder that reads noise with nothing to say why.
 */
static int selection(const struct lte_rm_plan *plan, int rv, int *positions,
                     int want) {
    int start = lte_rm_start(plan, rv);
    int found = 0;
    int step;

    if (start < 0)
        return 0;
    /* Bounded by a whole trip round the buffer per bit wanted: a buffer that
       is entirely holes would otherwise spin here for ever. */
    for (step = 0; step < plan->buffer && found < want; step++) {
        int at = (start + step) % plan->buffer;

        if (lte_rm_origin(plan, at) >= 0)
            positions[found++] = at;
    }
    if (found == 0)
        return 0;
    /*
     * Anything still wanted repeats the same positions in the same order,
     * which is what the circular buffer means.
     *
     * Modulo how many positions the walk *found*, not how many steps it took:
     * the two differ by exactly the holes, and using the step count makes the
     * first repeat read the slot it is about to write. That is uninitialised
     * memory, and it shows up as a couple of bits of a doubly-repeated
     * allocation arriving once instead of twice -- which is a 3 dB loss on
     * those bits and nothing a clean round trip would ever notice.
     */
    {
        int cycle = found;
        while (found < want) {
            positions[found] = positions[found % cycle];
            found++;
        }
    }
    return found;
}

int lte_rate_match(const struct lte_rm_plan *plan, int rv, const uint8_t *d0,
                   const uint8_t *d1, const uint8_t *d2, uint8_t *out, int e) {
    const uint8_t *streams[3];
    int i, taken;
    static int positions[3 * (LTE_TURBO_MAX_K + LTE_TURBO_TAIL) + 32];

    if (!plan || !d0 || !d1 || !d2 || !out || e <= 0)
        return 0;
    if (e > (int)(sizeof(positions) / sizeof(positions[0])))
        return 0;
    streams[0] = d0;
    streams[1] = d1;
    streams[2] = d2;
    taken = selection(plan, rv, positions, e);
    for (i = 0; i < taken; i++) {
        int origin = lte_rm_origin(plan, positions[i]);
        out[i] = streams[origin / plan->coded][origin % plan->coded];
    }
    return taken;
}

void lte_rate_dematch(const struct lte_rm_plan *plan, int rv, const float *in,
                      int e, float *d0, float *d1, float *d2) {
    float *streams[3];
    int i, taken;
    static int positions[3 * (LTE_TURBO_MAX_K + LTE_TURBO_TAIL) + 32];

    if (!plan || !in || !d0 || !d1 || !d2)
        return;
    memset(d0, 0, (size_t)plan->coded * sizeof(*d0));
    memset(d1, 0, (size_t)plan->coded * sizeof(*d1));
    memset(d2, 0, (size_t)plan->coded * sizeof(*d2));
    if (e <= 0 || e > (int)(sizeof(positions) / sizeof(positions[0])))
        return;
    streams[0] = d0;
    streams[1] = d1;
    streams[2] = d2;
    taken = selection(plan, rv, positions, e);
    for (i = 0; i < taken; i++) {
        int origin = lte_rm_origin(plan, positions[i]);
        streams[origin / plan->coded][origin % plan->coded] += in[i];
    }
    /* The fillers are known, not missing: see the note in the header. */
    for (i = 0; i < plan->fillers; i++)
        d0[i] = LTE_RM_KNOWN_LLR;
}
