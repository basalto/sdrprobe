#ifndef SRD_LOG_H
#define SRD_LOG_H

#include <stdint.h>

/*
 * What an SRD log row means, and what clicking one asks for.
 *
 * Two decisions used to live inside `view_srd.c`'s drawing, and both are the
 * shape `.scratch/testability/issues/09-*` is about: a function that draws may
 * not also decide, and a decision taken inside a draw is reachable by no check
 * that has no window.
 *
 * Plain numbers in, plain answers out. No raylib, no `struct app`, links -lm.
 */

/*
 * Where a burst actually was, fixed when the entry is written.
 *
 * The session reports a carrier as an *offset* from whatever the receiver was
 * tuned to when it heard it, and an offset only means something beside that
 * tuning. The waterfall used to place its marker at `applied.frequency_hz +
 * carrier_hz` every frame, with the *current* tuning -- so once the SRD
 * header's arrows could walk the receiver across a ten-megahertz allocation,
 * retuning dragged every historical label along with it and a burst heard at
 * 434.42 MHz was drawn at 435.42 after one arrow press.
 *
 * It is one addition, and that is the point: it is right by construction only
 * as long as the tuning it is handed is the one that heard the samples, and
 * nothing about a function taking `struct app` said which tuning that was.
 */
static inline double srd_log_absolute_hz(uint32_t heard_at_hz,
                                         double carrier_hz) {
    return (double)heard_at_hz + carrier_hz;
}

/* What clicking a row asks the input phase to do. */
enum srd_row_action {
    SRD_ROW_NONE = 0,       /* the click was not on a row */
    SRD_ROW_SELECT,         /* select it, and go no further */
    SRD_ROW_SELECT_AND_TUNE /* select it and put the receiver where it was */
};

struct srd_row_intent {
    enum srd_row_action action;
    int row;
    uint32_t tune_hz;       /* meaningful only for SELECT_AND_TUNE */
};

/*
 * What a click on log row `row` asks for.
 *
 * Selecting a row also tunes to it, because the frequency in that row is the
 * one useful thing a reader can act on -- it is where the signal was, and
 * getting the receiver there is otherwise a typed number or a run of arrow
 * presses. Three cases stop short of the retune, and each is a real screen
 * rather than defensive padding:
 *
 *   - a row outside the log, which is what an empty log's geometry returns;
 *   - file playback, where the tuning is baked into the capture and a retune
 *     is not something the receiver can be asked for -- the row still selects,
 *     because selection is how a reader marks their place;
 *   - an entry with no frequency, which is what a log restored from before
 *     `absolute_hz` existed holds, and what a zeroed entry holds.
 *
 * `heard_at_hz` is the entry's own fixed frequency, never the current tuning.
 */
static inline struct srd_row_intent srd_log_row_intent(int row, int log_count,
                                                       double absolute_hz,
                                                       int receiver_mode) {
    struct srd_row_intent intent;

    intent.action = SRD_ROW_NONE;
    intent.row = -1;
    intent.tune_hz = 0;

    if (row < 0 || row >= log_count)
        return intent;

    intent.row = row;
    intent.action = SRD_ROW_SELECT;

    if (!receiver_mode || !(absolute_hz > 0.0))
        return intent;

    intent.action = SRD_ROW_SELECT_AND_TUNE;
    /*
     * Rounded, not truncated: the receiver takes whole hertz and the entry
     * carries a measured offset, so truncation would put the tuning
     * systematically low -- by under a hertz, which is nothing on air and is
     * still the wrong arithmetic to write down.
     */
    intent.tune_hz = (uint32_t)(absolute_hz + 0.5);
    return intent;
}

#endif /* SRD_LOG_H */
