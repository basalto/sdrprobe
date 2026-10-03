#ifndef FM_VIEW_MODEL_H
#define FM_VIEW_MODEL_H

#include "tech/fm_dsp.h"
#include "tech/rds.h"

/* The FM view's own state (`app.h`), which is all this reads: forward
   declared, so nothing that takes a model has to take `struct app` with
   it. */
struct fm_view;

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

/* The audio waveform, decimated for the chart: a line over a few hundred
   pixels does not need the window's full ~3277 samples, and a quarter of them
   is the same shape at a quarter the wire cost. */
#define FM_VIEW_MODEL_AUDIO_POINTS 1024

/* The RDS constellation, capped: a symbol cloud reads the same at 256 points
   as at the window's 512, and the chart is about shape, not count. */
#define FM_VIEW_MODEL_SCATTER_POINTS 256

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

/*
 * One carrier a band walk found, as plain fields -- the mirror of
 * `struct fm_found_station` (`app.h`), restated here because this header is
 * below the runtime layer and cannot reach it (`check-layers`). The builder,
 * which sees both, asserts the two stay the same shape.
 */
struct fm_model_station {
    double frequency_hz;
    float power_dbfs;
    int stereo;             /* a pilot: the station broadcasts in stereo */
    int rds;                /* groups arrived */
    int pi_valid;
    unsigned pi;            /* programme identification */
    char ps[9];             /* a whole repeated name, else empty */
};

/*
 * `FM_SCAN_MAX_FOUND` (app.h) mirrored, for the same reason
 * `FM_VIEW_MODEL_MAX_BINS` mirrors the spectrum length: a wire writer sizes
 * its buffer from this header without reaching up into the runtime layer. The
 * builder has both in hand and asserts they are equal, so this cannot quietly
 * become a cap that drops carriers (the `input_route.h` idiom).
 */
#define FM_VIEW_MODEL_MAX_STATIONS 48

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

    /* -- The analysis arrangement's other charts, the five beside the
          multiplex the window draws behind "Show charts". Each carries what
          the window's own chart reads; the browser draws the same picture.
          Decimated where a chart does not need the window's full resolution
          (a waveform and a symbol cloud are the same shape at a quarter the
          points), which is also what keeps the wire cost of these to a few
          tens of KB/s. All are zero/empty until a decode has produced them. -- */

    /* The sound, as a waveform -- `fm_view.audio_trace`, decimated to a count
       a line chart renders cleanly. */
    int audio_points;
    float audio_wave[FM_VIEW_MODEL_AUDIO_POINTS];

    /* The sound's own spectrum, de-emphasised, 0 to about 16 kHz
       (`fm_view.audio_spectrum`). Its own bin width, like the multiplex. */
    int audio_spectrum_bins;
    double audio_spectrum_bin_hz;
    float audio_spectrum[FM_VIEW_MODEL_MAX_BINS];

    /* The RDS symbols as a constellation -- `fm_rds_symbols()` over the
       session's baseband, which the window computes in the draw call and this
       computes in the builder so a check can reach it. Interleaved would save
       a field; two arrays match how the Scope's spectrum already travels. */
    int scatter_points;
    float scatter_i[FM_VIEW_MODEL_SCATTER_POINTS];
    float scatter_q[FM_VIEW_MODEL_SCATTER_POINTS];

    /* The timing search's sixteen offsets, and which won (the offset is
       `timing_offset` above). A small bar chart: a clear peak is a symbol
       clock nobody need think about. */
    float timing_energy[FM_RDS_SAMPLES_PER_SYMBOL];

    /* How many of each RDS group type arrived -- type 0 carries the name,
       type 2 the radio text. The window sums the A and B versions of each;
       this carries the sixteen sums. */
    int groups_by_type[16];

    /* -- Band II: what a band walk found, one row per carrier. The window
          draws this as a scrolling table beside the waterfall and pills each
          carrier over the waterfall; a Viewer reads the same list, since in
          `web` mode the browser is the only frontend and starts the scan
          itself (the `scan` command). `scan_status` is the one-line summary
          the panel heads with -- "24 carriers, 18 in stereo, 7 carrying RDS,
          5 named" -- decided by the scan, never re-counted by a reader.
          `station_count` is zero until a scan has run, which is a fact, not a
          placeholder. -- */
    int scanning;              /* a sweep is under way */
    char scan_status[160];
    int station_count;
    struct fm_model_station stations[FM_VIEW_MODEL_MAX_STATIONS];
};

/*
 * Fills `out` from the FM view's own state.

 * It took a `const struct app *` and reached for `app->fm`, which made a
 * contract depend on everything `struct app` depends on -- and made a check
 * of it build a nine-megabyte struct to fill one field
 * (`.scratch/layer-boundaries/issues/03-*`). It takes what it reads. Not a snapshot to keep past this frame: every
 * field is read fresh, the same as the drawing it serves.
 *
 * The band scan's list of found stations **is** carried (the fields above).
 * It once was not, on the argument that "nothing reads a scan it cannot
 * start" -- but a Viewer in `web` mode is the only frontend, and now starts
 * the scan itself through the `scan` command (`viewer_command.h`), so the
 * list has a reader that can fill it. The window reads the same fields, so
 * the two cannot disagree about a carrier.
 *
 * What this deliberately does not carry, said here rather than left as a gap
 * for a reader to discover:
 *
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
void fm_view_model_build(const struct fm_view *fm,
                         struct fm_view_model *out);

#endif
