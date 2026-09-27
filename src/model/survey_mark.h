#ifndef SURVEY_MARK_H
#define SURVEY_MARK_H

/*
 * Which of four marks a survey candidate wears, decided once.
 *
 * This lived in `sdrgui.h`, the components header -- so the Viewer link
 * included a GUI header, and raylib with it, to spell a candidate's mark for
 * the wire. ADR-0007 says a component takes plain data and decides nothing,
 * and this is a decision: it is the precedence between "on the receiver's
 * own comb" and "a closer look found nothing", and `CLAUDE.md` records why
 * empty wins -- "there is nothing here" is what a reader acts on.
 *
 * So it is here, in the model layer, and both readers take it: the chart
 * draws the mark it is handed and `survey_state` sends its name.
 * `.scratch/layer-boundaries/issues/03-*` is the why.
 */

enum survey_peak_mark {
    SURVEY_MARK_SIGNAL = 0,  /* a filled dot: nothing is known against it */
    SURVEY_MARK_RECEIVER,    /* a cross: on the receiver's own comb */
    SURVEY_MARK_EMPTY,       /* a hollow dot: a closer look found nothing */
    /*
     * A cross with a dot in it: receiver-like by frequency, and yet it reads
     * displaced by this receiver's own error, which a tone clocked here could
     * not. Something real is on a comb multiple.
     *
     * A **fourth shape** rather than resolving to one of the three, because
     * both of the obvious resolutions are wrong. A plain cross tells a reader
     * to stop looking at the one candidate they should look at; a plain dot
     * silently discards the comb mark, which
     * `.scratch/reading-origin/issues/01-*` decided against. The chart draws
     * one mark per peak, so "beside" has to mean a shape that carries both.
     */
    SURVEY_MARK_CONTESTED
};

#define SURVEY_MARK_FLAG_RECEIVER 0x1u   /* SURVEY_SUSPECT_REFERENCE */
#define SURVEY_MARK_FLAG_STEP 0x2u       /* SURVEY_SUSPECT_STEP_CENTRE */
#define SURVEY_MARK_FLAG_EMPTY 0x8u      /* SURVEY_SUSPECT_NO_CARRIER */
#define SURVEY_MARK_FLAG_DISPLACED 0x40u /* SURVEY_SUSPECT_DISPLACED */

static inline enum survey_peak_mark survey_mark_of(unsigned flags) {
    if (flags & SURVEY_MARK_FLAG_EMPTY)
        return SURVEY_MARK_EMPTY;
    if (flags & (SURVEY_MARK_FLAG_RECEIVER | SURVEY_MARK_FLAG_STEP)) {
        /* Receiver-like by frequency and contradicted by where it reads. The
           contradiction is the more useful half to a reader, so it shows --
           without discarding the cross that earned the suspicion. */
        if (flags & SURVEY_MARK_FLAG_DISPLACED)
            return SURVEY_MARK_CONTESTED;
        return SURVEY_MARK_RECEIVER;
    }
    return SURVEY_MARK_SIGNAL;
}


/*
 * The mark's name, for a reader that is not the raylib chart -- the Viewer
 * wire, today. It travels by name rather than as the enum's integer because
 * a second reader that re-declares the enum's order gets it wrong silently:
 * `web/views/survey.js` did exactly that and drew receiver-like and empty
 * candidates swapped, green throughout, until `web-visualization/15`. A name
 * cannot be mis-ordered, and one the browser does not know is visible rather
 * than becoming a different mark. Same shape as `survey_shape_name()`.
 */
static inline const char *survey_mark_name(enum survey_peak_mark m) {
    switch (m) {
    case SURVEY_MARK_SIGNAL:    return "signal";
    case SURVEY_MARK_RECEIVER:  return "receiver";
    case SURVEY_MARK_EMPTY:     return "empty";
    case SURVEY_MARK_CONTESTED: return "contested";
    }
    return "signal";
}

#endif
