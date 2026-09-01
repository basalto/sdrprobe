#ifndef GSM_CONTINUITY_H
#define GSM_CONTINUITY_H

/*
 * Whether consecutive SCH decodes from one channel are consistent with each
 * other.
 *
 * This is what tells an operator a decode can be believed. It flags; it never
 * substitutes -- a wrong frame number is reported as the decoder read it, with
 * a warning beside it, because quietly correcting one would hide exactly the
 * fault worth seeing.
 *
 * Plain integers and a clock reading, so it can be checked without a receiver
 * (ADR-0012). The wrap is the case worth writing first: a decoder that never
 * wraps and one that wraps wrongly look identical for three and a half hours.
 */

/* T1 is the reduced frame number, 0..2047, advancing once per 51 x 26 = 1326
   TDMA frames. A frame is 60/13 ms, so T1 ticks every 6.12 s and the whole
   range takes 3 h 28 m to come round -- which is why a wrap that is handled
   wrongly can sit unnoticed for an afternoon. */
#define GSM_T1_MODULUS 2048
#define GSM_T1_FRAMES 1326
#define GSM_FRAME_SECONDS (60.0 / 13000.0)
#define GSM_T1_SECONDS ((double)GSM_T1_FRAMES * GSM_FRAME_SECONDS)

struct gsm_sch_continuity {
    int have_last;
    int last_t1;
    int last_bsic;
    double last_time;
    int implausible;
};

static inline void gsm_continuity_reset(struct gsm_sch_continuity *c) {
    c->have_last = 0;
    c->last_t1 = 0;
    c->last_bsic = -1;
    c->last_time = 0.0;
    c->implausible = 0;
}

/*
 * How far T1 advanced, counting forwards around the wrap. A decode after the
 * hyperframe rolls over reads T1 = 0 following T1 = 2047, which is an advance
 * of one, not a jump backwards of 2047.
 */
static inline int gsm_t1_advance(int previous, int current) {
    int advance = (current - previous) % GSM_T1_MODULUS;

    if (advance < 0)
        advance += GSM_T1_MODULUS;
    return advance;
}

/*
 * How far T1 could legitimately have moved in `seconds`, with a tick of slack
 * either side of the arithmetic: the two decodes are not synchronised to the
 * T1 boundary, so a gap of exactly one period can show up as one tick or two.
 */
static inline int gsm_t1_allowed_advance(double seconds) {
    int ticks;

    if (!(seconds > 0.0))
        return 1;
    ticks = (int)(seconds / GSM_T1_SECONDS);
    return ticks + 1;
}

/*
 * Fold one decode in, and say whether it is consistent with the last.
 *
 * Two ways it may not be. The frame number may have moved further than the
 * time between decodes allows -- that is a misread field, since T1 advances at
 * a fixed rate and cannot skip. Or the BSIC may have changed, which means a
 * different cell is being heard on the channel, or the last decode was wrong;
 * either way what is on screen is not what it claims. The BSIC comparison is
 * safe because tuning to another channel resets this.
 *
 * Comparing against the elapsed time, rather than requiring an advance of at
 * most one, is what lets a view be left and come back to without crying wolf:
 * a minute away is ten legitimate ticks.
 */
static inline int gsm_continuity_observe(struct gsm_sch_continuity *c, int t1,
                                         int bsic, double now) {
    int advance;

    if (!c->have_last) {
        c->have_last = 1;
        c->last_t1 = t1;
        c->last_bsic = bsic;
        c->last_time = now;
        c->implausible = 0;
        return 1;
    }
    advance = gsm_t1_advance(c->last_t1, t1);
    c->implausible = advance > gsm_t1_allowed_advance(now - c->last_time) ||
                     bsic != c->last_bsic;
    c->last_t1 = t1;
    c->last_bsic = bsic;
    c->last_time = now;
    return !c->implausible;
}

#endif
