#ifndef CALIBRATION_NAV_H
#define CALIBRATION_NAV_H

/*
 * Where "Back" goes, and where it does not.
 *
 * The calibration overlay is two screens wearing one title. First a channel is
 * chosen -- from the 4G cell list drawn in the overlay, or from the 2G channel
 * scan, which is an overlay of its own -- and then that channel is measured,
 * with the chart in the place the list had. Back used to close the whole thing
 * from either of them, so the only route from a measurement to the list it came
 * from was to leave calibration and start again.
 *
 * So Back is one step up, and leaving is its own button. Which step up depends
 * on where the channel came from, and that is a decision rather than a
 * drawing, so it lives here where a check can reach it (ADR-0012).
 *
 * `scanned` is whether the 2G scan has results to return to. It does not ask
 * whether the scan is still open: a scan whose results are on screen is not a
 * place Back is reachable from.
 */
enum calibration_back {
    CALIBRATION_BACK_NONE = 0,  /* already at the top; the button is dim */
    CALIBRATION_BACK_STOP,      /* stop measuring -- on 4G the list returns */
    CALIBRATION_BACK_SCAN       /* stop measuring and reopen the 2G scan */
};

static inline enum calibration_back calibration_back_target(int technology,
                                                            int running,
                                                            int scanned) {
    /* Not measuring means the list -- or the empty screen that offers to fill
       it -- is already what is on show, and there is nothing above it. */
    if (!running)
        return CALIBRATION_BACK_NONE;
    /*
     * 2G's list is the scan overlay, so going back to it means reopening it.
     * A typed ARFCN never opened one, and inventing a scan the operator did
     * not ask for would retune the receiver and take half a minute; stopping
     * is the honest step back from there.
     */
    if (technology == 0 && scanned)
        return CALIBRATION_BACK_SCAN;
    return CALIBRATION_BACK_STOP;
}

#endif
