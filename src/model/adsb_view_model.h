#ifndef ADSB_VIEW_MODEL_H
#define ADSB_VIEW_MODEL_H

#include <stdint.h>

#include "tech/adsb_analysis.h"
#include "tech/adsb_dsp.h"

/*
 * What the ADS-B screen says, as plain data -- no raylib type anywhere
 * (`web-visualization/07`).
 *
 * The screen is a message log and a funnel, and the funnel is the point: a
 * sky with no aircraft in it and a receiver on the wrong frequency both
 * produce an empty table, and which stage stopped is what tells them apart.
 * `adsb_receiver_ready()` decides the second of those, and the window used
 * to ask it while drawing; it is a field here, so a browser does not decide
 * it again.
 *
 * Takes the structs it reads rather than a `const struct app *`, so
 * `check-adsb-view-model` links `-lm` alone.
 */

/*
 * How much of the log travels.
 *
 * The window keeps `ADSB_LOG_CAPACITY` (256). All of it, every block, is
 * about 570 KB/s -- more than ADR-0027 budgets for *all* derived state --
 * so the newest 48 go instead, which is about three screens and roughly
 * 108 KB/s.
 *
 * Whole rather than incremental, and that is the interesting half: the
 * Viewer link **drops** messages under load, by design, so an increment
 * that went missing would lose those decoded rows permanently with nothing
 * to say they had existed. Re-sending the newest 48 every time means a
 * dropped message costs nothing -- the next one is complete. It is the
 * opposite choice to the waterfall, which *is* incremental, because there a
 * gap is one missing row of a picture and here it is a lost aircraft.
 */
#define ADSB_VIEW_MODEL_LOG 48

/*
 * Whether Mode S could be here at all, and if not, whose problem it is.
 *
 * Two answers rather than one because only one of them is actionable: a
 * receiver pointed elsewhere can be retuned from this screen, and a capture
 * holds the one tuning it was taken at. The window drew two things -- a
 * Retune button or a sentence -- and the browser drew one sentence for both.
 */
enum adsb_readiness {
    ADSB_READY = 0,
    ADSB_NOT_READY_RECEIVER,
    ADSB_NOT_READY_CAPTURE
};

static inline const char *adsb_readiness_name(enum adsb_readiness r) {
    switch (r) {
    case ADSB_READY:              return "ready";
    case ADSB_NOT_READY_RECEIVER: return "receiver-elsewhere";
    case ADSB_NOT_READY_CAPTURE:  return "capture-elsewhere";
    }
    return "ready";
}

struct adsb_view_model {
    /* Whether Mode S could be here at all: on 1090 MHz, at 2 MS/s or more.
       Off frequency every chart is empty and that is not a quiet sky. */
    int ready;
    enum adsb_readiness readiness;

    /* The run's totals, as the header prints them. */
    unsigned long long frames_total;
    unsigned long long positions_total;

    /*
     * The funnel, for the run and for the latest block. "Nothing decoded"
     * has several causes and which stage stopped is the diagnosis: no
     * preambles at all is tuning or antenna, preambles with failing parity
     * is marginal bits.
     */
    struct adsb_demod_stats totals;
    struct adsb_demod_stats block;
    /*
     * And the one reading of that funnel a reader acts on: frames are
     * arriving and **none** of them decode, which is a different fault from
     * a quiet band and the state an empty message log cannot express. The
     * window coloured its funnel line for this and the browser did not.
     */
    int funnel_warn;

    /*
     * Whether any samples have arrived at all. An empty log means "listening"
     * once they have and "waiting" before -- two sentences, and the
     * difference is whether the receiver is running.
     */
    int have_samples;

    /* The log, newest first -- the same entries the window's table draws. */
    int log_count;
    struct adsb_log_entry log[ADSB_VIEW_MODEL_LOG];
};

struct adsb_view;

/*
 * Fills `out`. Reads plain fields only -- no I/O, no raylib call.
 *
 * The tuning is passed rather than read: whether the receiver is where Mode
 * S is belongs to the receiver, and this view only reports it.
 */
void adsb_view_model_build(const struct adsb_view *adsb,
                           uint32_t frequency_hz, uint32_t sample_rate_hz,
                           int receiver_mode, int have_samples,
                           struct adsb_view_model *out);

#endif
