#define _POSIX_C_SOURCE 200809L

#include "frame_advance.h"
#include "view.h"

/*
 * See frame_advance.h. This reproduces sdrprobe.c's former run_gui sequence
 * unchanged -- same order, same conditions -- so the acceptance criterion is
 * that nothing observable moved, not that anything here got smarter.
 */
int frame_advance(struct app *app, struct slot_snapshot *snapshot, double now) {
    int have_new, spectrum_updated;

    decay_spectrum_peak(app, now);
    have_new = consume_latest(&app->acq, snapshot);
    spectrum_updated = have_new ? process_block(app, now) : 0;
    if (spectrum_updated) {
        advance_waterfall_row(app);
        update_scan(app);
        update_calibration_measurement(app);
    }

    /*
     * Every frame, and `spectrum_updated` says whether a block came with it.
     * The survey's machine has decisions on both clocks: a look is counted
     * only when a block arrives, while a step that has already heard
     * something is over on time alone. See sdrprobe.c's original comment at
     * this sequence for the reasoning; it is unchanged here.
     */
    update_startup(app, spectrum_updated);
    if (app->tab == TAB_SURVEY && !app->cal.open && !app->startup.open)
        update_survey(app, now, spectrum_updated);
    if (have_new && app->tab == TAB_DECODE &&
        app->decode == DECODE_ADSB && !app->cal.open)
        update_adsb(app, now);
    if (have_new && app->tab == TAB_DECODE &&
        app->decode == DECODE_GSM && !app->cal.open)
        update_gsm_sch(app, now);
    /* Every block, and only when one arrived: a TETRA downlink is continuous
       and each block carries several synchronization bursts, so there is
       nothing to carry between them. */
    if (have_new && app->tab == TAB_DECODE &&
        app->decode == DECODE_TETRA && !app->cal.open)
        update_tetra(app, now);
    if (have_new && app->tab == TAB_DECODE &&
        app->decode == DECODE_SRD && !app->cal.open)
        update_srd(app, now);
    if (app->tab == TAB_DECODE && app->decode == DECODE_LTE &&
        !app->cal.open) {
        /* The scan drives the tuning, so it runs every frame and not only
           when a block arrives: most of its time is spent waiting for the
           tuner to settle, and nothing arrives worth having then. */
        update_lte_scan(app, now, have_new);
        if (have_new && !app->lte.scan.running)
            update_lte(app, now);
    }
    if (app->tab == TAB_DECODE && app->decode == DECODE_FM) {
        /* Every block, and only when one arrived: the pilot loop is a
           continuous thing and a block skipped is a quarter second of its
           lock thrown away. While the scan is walking the band it owns the
           receiver and feeds the chain itself. */
        update_fm_scan(app, now, have_new);
        if (have_new && !app->fm.scan.running)
            update_fm(app, now);
        /* Every frame, not every block: the sound card asks on its own
           schedule and a block is several of its buffers. */
        update_fm_audio(app);
    }
    update_drift_check(app, spectrum_updated);
    advance_scatter_history(app, now,
                            have_new && app->tab == TAB_SCOPE &&
                                !app->cal.open &&
                                app->view == VIEW_SCATTER);

    return spectrum_updated;
}
