#ifndef FM_VIEW_MODEL_H
#define FM_VIEW_MODEL_H

#include "fm_dsp.h"
#include "rds.h"

struct app;

/*
 * What the FM view knows, as plain fields -- ticket 07's next view after the
 * survey, and ticket 14's Phase 4.
 *
 * The three panels `view_fm.c` draws (Signal, Station, "Where the decode
 * stopped") each read `app->fm` directly, which is fine for one reader and is
 * exactly the arrangement ticket 03 replaced on the Scope the moment there
 * were two: a browser reading the same screen has to arrive at the same
 * numbers, and two readers of `struct app` are two chances to disagree about
 * what a field means. `spec.md`'s own words for the goal are "alternative
 * readers of one object rather than two presentations kept in step".
 *
 * Reads only plain fields and the `fm_dsp`/`rds` accessors over them -- no
 * raylib call, no audio device, no I/O -- so `check-fm-view-model` links
 * `-lm` alone against it, the same as `scope_view_model.h` and
 * `survey_view_model.h`.
 */

/* `FM_MPX_SPECTRUM_BINS` (1024) under a name a reader sizing a wire buffer
   can take from this header without also including `fm_dsp.h`, the same
   reason `SURVEY_VIEW_MODEL_MAX_BINS` exists. Nothing is ever truncated:
   this is the array's own length, not a cap chosen under it. */
#define FM_VIEW_MODEL_MAX_BINS FM_MPX_SPECTRUM_BINS

/*
 * How the funnel's closing sentence reads, decided once here rather than by
 * whoever draws it.
 *
 * The window paints that sentence green, amber or grey, and those three are a
 * verdict rather than a colour scheme: "a pilot but no blocks" is a station
 * working correctly and carrying no RDS, while "blocks but no groups" is
 * reception failing. A second reader deriving its own emphasis from the five
 * counts is the second presentation this whole seam exists to avoid -- the
 * same argument `survey_view_model.h` makes for carrying a candidate's mark
 * instead of its flag word.
 */
enum fm_reading_tone {
    FM_READING_NEUTRAL = 0,  /* in progress; nothing is wrong yet */
    FM_READING_GOOD,         /* the decode is doing what it should */
    FM_READING_WEAK          /* it stopped, and the sentence says where */
};

/* The name that crosses the wire, for the reason `site_seen_name()` gives. */
static inline const char *fm_reading_tone_name(enum fm_reading_tone tone) {
    switch (tone) {
    case FM_READING_NEUTRAL: return "neutral";
    case FM_READING_GOOD:    return "good";
    case FM_READING_WEAK:    return "weak";
    }
    return "neutral";
}

struct fm_view_model {
    /* -- The Signal panel: whether anything is being received, then how
          well, in that order (the panel's own ordering rule). -- */
    int pilot_locked;
    double pilot_hz;        /* valid only when pilot_locked */
    /* The *transmitter's* pilot offset, not this receiver's clock: five
       stations here read between +2 and -57 ppm on one receiver. Valid only
       when pilot_locked, the same as pilot_hz. */
    double pilot_ppm;
    double pilot_coherence; /* always measured, lock or no lock */
    /* Whether the station transmits stereo -- a fact about it rather than
       about the sound card, so it is carried whether or not anything is
       playing. */
    int broadcast_stereo;

    /* The sound, which is this machine's business and not the station's: a
       remote reader cannot hear it, and says so rather than showing a
       control it cannot work. `audio_error` is empty when there is none. */
    int playing;
    double audio_rate_hz;
    char audio_error[80];

    /* How the symbol clock and the subcarrier axis came out -- both valid
       only when pilot_locked, since neither is searched for without one. */
    int timing_offset;
    int timing_samples_per_symbol;
    double axis_radians;

    /* -- The Station panel: the identity first, then how sure. -- */
    int pi_valid;
    unsigned pi;            /* programme identification */
    int pi_repeats;         /* how many groups agreed on it */

    /* The name, shown only when whole and repeated: a programme service name
       arrives two characters at a time, and a half-arrived one puts a station
       that does not exist on the screen. `ps_segments` is how many of the
       four have arrived -- the count the panel shows, not the bitmask it
       counts, which is `struct rds_station`'s own business. */
    int ps_valid;
    char ps[9];             /* eight characters and a terminator */
    int ps_segments;

    int pty_valid;
    int pty;                /* 0 to 31 */
    char pty_name[32];      /* rds_pty_name(), spelled once */
    int tp;                 /* this programme carries traffic reports */
    int ta;                 /* a traffic announcement is on now */
    char traffic[48];       /* rds_traffic_name(tp, ta), likewise */

    /* Radio text, unwrapped. Where the line breaks go is the reader's, and
       the two readers wrap to different widths -- `text_wrap.h` for the
       window, a browser's own layout for the other.

       `ps` and `rt` are `struct rds_station`'s own two sizes, restated
       rather than derived: this is a plain view model a wire writer reads
       without knowing the decoder's types. `check-fm-view-model` asserts the
       two stay equal, so the restatement cannot quietly become a truncation. */
    int rt_valid;
    char rt[65];            /* up to 64 characters, and a terminator */

    /* -- Where the decode stopped. Two empty panels look the same whether
          nothing is transmitting or every block is failing its syndrome, and
          that difference is the whole diagnosis. -- */
    long bits;              /* soft bits offered */
    long blocks_matched;    /* blocks whose syndrome named their position */
    long groups;            /* groups with at least one good block */
    long identified;        /* groups that carried a programme identification */
    long named;             /* times a complete name was confirmed */
    char reading[80];       /* the funnel in words */
    enum fm_reading_tone reading_tone;

    /* -- The multiplex, as a spectrum: the chart that answers the question
          none of the panels can, whether this station carries RDS at all.
          Three humps -- the pilot at 19 kHz, the stereo subcarrier at 38, the
          RDS band at 57 -- and a station with the first two and not the third
          is an ordinary station simply not sending any. Zero bins before the
          first refresh, which is a fact rather than a placeholder. -- */
    int spectrum_bins;
    double spectrum_bin_hz;
    float spectrum[FM_VIEW_MODEL_MAX_BINS];
};

/*
 * Fills `out` from `app->fm`. Not a snapshot to keep past this frame: every
 * field is read fresh, the same as the drawing it serves.
 *
 * What this deliberately does not carry, said here rather than left as a gap
 * for a reader to discover:
 *
 * - **The band scan's list of found stations.** A scan is started by a button
 *   and walks the receiver across band II; nothing reads a scan it cannot
 *   start, and a modelled list that stays empty for its only remote reader is
 *   the "half a screen modelled" fault `CLAUDE.md` names about layout
 *   headers, moved into a view model instead. `fm_scan.h` already owns it.
 * - **The audio ring and the `AudioStream`.** A raylib handle and a ring
 *   between two rates on *this* machine; `playing` and `audio_error` above
 *   are what a second reader can truthfully say about them.
 * - **The analysis arrangement's other five charts** (constellation, timing,
 *   groups, audio wave, audio spectrum). The multiplex is the one that
 *   decides whether there is anything to read at all; the rest are detail
 *   about a decode that is already happening, and each can be added the day
 *   something asks for it rather than the day nothing does.
 * - **The tuned frequency.** `scope_view_model.h` carries it already, and a
 *   second copy travelling beside this one is two answers to one question.
 */
void fm_view_model_build(const struct app *app, struct fm_view_model *out);

#endif
