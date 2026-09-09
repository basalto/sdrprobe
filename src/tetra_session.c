#include "tetra_session.h"

#include <math.h>
#include <string.h>

/*
 * A big-endian bit field out of a decoded message.
 *
 * One of the two copies of this chain had this helper and the other unrolled
 * the same loop four times. Every offset it is called with is a transcription
 * from ETSI EN 300 392-2, so having one of it matters more than the three
 * lines it saves: a wrong offset should be wrong once.
 */
static int field(const uint8_t *bits, int at, int count) {
    int v = 0, i;

    for (i = 0; i < count; i++)
        v = (v << 1) | (bits[at + i] & 1);
    return v;
}

void tetra_session_reset(struct tetra_session *s) {
    if (s)
        memset(s, 0, sizeof(*s));
}

int tetra_session_feed(struct tetra_session *s, const float *i_samples,
                       const float *q_samples, size_t pair_count,
                       double sample_rate, struct tetra_session_event *out) {
    /* Scratch, not state: it is overwritten every block and nothing reads it
       between calls. Static because 16384 floats twice is not a stack. */
    static float work_i[TETRA_SESSION_WORK], work_q[TETRA_SESSION_WORK];
    struct tetra_session_event event;
    const unsigned char *word = NULL;
    double coarse;
    size_t filtered;
    int at, k;
    int was_colour, was_la;

    memset(&event, 0, sizeof(event));
    if (out)
        *out = event;
    if (!s || !i_samples || !q_samples)
        return 0;

    s->bursts = s->blocks = s->broadcast = 0;
    s->symbols_valid = 0;
    s->sync_valid = 0;
    was_colour = s->have_identity ? s->colour : -1;
    was_la = s->have_identity ? s->la : -1;

    if (pair_count < 64)
        return 0;

    coarse = tetra_coarse_offset_hz(i_samples, q_samples, pair_count,
                                    sample_rate, 12500.0);
    filtered = tetra_channel(i_samples, q_samples, pair_count, sample_rate,
                             coarse, work_i, work_q, TETRA_SESSION_WORK);
    if (filtered == 0) {
        /* The filter decimates by a whole number or not at all. */
        event.rate_unsupported = 1;
        if (out)
            *out = event;
        return 0;
    }
    if (!tetra_demodulate(work_i, work_q, filtered, coarse, &s->symbols))
        return 0;

    s->symbols_valid = 1;
    s->lock = s->symbols.lock;
    s->offset_hz = coarse + s->symbols.fine_offset_hz;
    event.demodulated = 1;

    /*
     * How much of a timeslot repeats, which is what a burst structure looks
     * like from outside and needs nothing transcribed.
     *
     * 200 to 280 covers the 255-symbol slot and no more: the finder wants four
     * periods at the longest lag it is asked for, and a 65.5 ms block is about
     * 1180 symbols. Asking as far as 400 would need 1600 and would decline
     * every time -- which is what the empty chart looked like.
     */
    if (tetra_burst_find(s->symbols.dibit, s->symbols.count, 200, 280,
                         &s->sync) &&
        s->sync.period == TETRA_SLOT_SYMBOLS)
        s->sync_valid = 1;

    tetra_sync_dibits(&word);
    for (at = 60; at + TETRA_SYNC_SYMBOLS <= s->symbols.count; at++) {
        uint8_t block[TETRA_SB_MESSAGE_BITS];
        int hits = 0;

        for (k = 0; k < TETRA_SYNC_SYMBOLS; k++)
            if (s->symbols.dibit[at + k] == word[k])
                hits++;
        if (hits < 16)
            continue;
        s->bursts++;
        s->bursts_total++;
        if (!tetra_sync_block_decode(s->symbols.dibit + at - 60, block)) {
            s->blocks_failed++;
            continue;
        }
        s->blocks++;
        s->blocks_total++;
        s->mcc = field(block, 31, 10);
        s->mnc = field(block, 41, 14);
        s->colour = field(block, 4, 6);
        s->have_identity = 1;

        /* The broadcast channel rides in the same burst and is scrambled with
           the network's **own** colour code, so it cannot be read until the
           block above has given that up. */
        if (at + TETRA_BNCH_AT_SYMBOL + TETRA_BNCH_SCRAMBLED_BITS / 2 <=
            s->symbols.count) {
            uint8_t colour[TETRA_COLOUR_BITS];
            uint8_t sysinfo[TETRA_BNCH_MESSAGE_BITS];

            tetra_extended_colour(s->mcc, s->mnc, s->colour, colour);
            if (tetra_bnch_decode(s->symbols.dibit + at + TETRA_BNCH_AT_SYMBOL,
                                  colour, sysinfo)) {
                s->broadcast++;
                s->broadcast_total++;
                s->la = field(sysinfo, 82, 14);
            }
        }
    }

    if (s->blocks > 0 && (s->colour != was_colour || s->la != was_la))
        event.identity_changed = 1;
    if (out)
        *out = event;
    return s->blocks;
}
