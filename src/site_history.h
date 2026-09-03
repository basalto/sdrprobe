#ifndef SITE_HISTORY_H
#define SITE_HISTORY_H

#include <stddef.h>

/*
 * What this site has heard before.
 *
 * A survey answers "what is transmitting". The question worth asking is "what
 * changed", and that needs a memory: which signals this place has produced
 * before, how often, and when each was last heard. The saved JSON under
 * `surveys/` is the archive and is read by the reporting script; this is the
 * small derived summary the window itself needs, in a form C can read back in
 * a dozen lines rather than a JSON parser.
 *
 * One file per site, `surveys/history-<site>.txt`, line-oriented like the
 * configuration:
 *
 *     site home-desk
 *     sweeps 4
 *     seen 94492310 -7.7 35.9 4 4
 *     seen 97469360 -16.1 27.6 2 3
 *
 * A `seen` line is a frequency, the level and prominence it was last heard at,
 * how many sweeps it has appeared in, and which sweep it last appeared in.
 * That last number is what makes "missing" answerable: an entry whose last
 * sweep is not the current one was not heard this time.
 *
 * Everything that decides anything is pure and takes no path, so
 * tests/site_history_test.c reaches it without a directory to write into
 * (ADR-0012).
 */

#define SITE_HISTORY_MAX 512
/*
 * Presence kept per hour of the day, so "this comes and goes" can become
 * "this is on in the evenings".
 *
 * Counting sweeps alone cannot tell those apart: a signal heard in half a
 * site's sweeps looks the same whether it alternates minute to minute or runs
 * from six until midnight. Twenty-four counters per signal, against
 * twenty-four for the site, answers it -- and it is the difference between
 * a transmitter worth investigating and an office that closes.
 */
#define SITE_HOURS 24
#define SITE_NAME_MAX 96

/*
 * How near two sweeps' peaks must be to be the same signal.
 *
 * Not a constant, because a sweep of the whole tuner bins at 200 kHz and a
 * sweep of one band at 2 kHz: a fixed tolerance would either merge adjacent FM
 * stations in the narrow case or fail to match anything in the wide one. Twice
 * the bin is what the sweep could actually resolve, floored so that a very
 * fine sweep does not demand impossible precision of an older coarse one.
 */
#define SITE_MATCH_FLOOR_HZ 20000.0
double site_match_tolerance(double bin_hz);

/* The tolerance for comparing a sweep at `bin_hz` against an entry recorded at
   its own resolution: the wider of the two, because neither can be more
   precise than the coarser measurement that produced it. */
double site_match_tolerance_for(double bin_hz, double entry_bin_hz);

struct site_entry {
    double hz;
    float dbfs;
    float prominence_db;
    int sweeps;        /* how many sweeps it has appeared in */
    int last_sweep;    /* the sweep index it last appeared in */
    /* How many of this site's sweeps in each hour of the day heard it. */
    unsigned short heard_by_hour[SITE_HOURS];
    /*
     * The bin width of the sweep that last placed it.
     *
     * Kept because matching needs the *coarser* of the two resolutions, not
     * the current one. A sweep of the whole tuner locates a station to within
     * 200 kHz; a sweep of one band locates it to 2. Comparing the fine sweep
     * against the coarse memory with the fine tolerance calls the same
     * station both new and missing at once, which is precisely what it did.
     */
    double bin_hz;
};

struct site_history {
    char site[SITE_NAME_MAX];
    int sweeps;        /* how many sweeps this site has recorded */
    /* And how many of them fell in each hour, which is what makes a signal's
       own hour counts mean anything: three sightings in an hour swept three
       times is always, in an hour swept thirty it is hardly ever. */
    unsigned short sweeps_by_hour[SITE_HOURS];
    struct site_entry entries[SITE_HISTORY_MAX];
    int count;
};

/*
 * A signal heard in fewer than this fraction of a site's sweeps is
 * intermittent rather than steady. Two thirds, which separates "it is always
 * there" from "it comes and goes" without calling a signal unreliable for
 * missing one sweep in five.
 */
#define SITE_STEADY_NUMERATOR 2
#define SITE_STEADY_DENOMINATOR 3
/* And how many sweeps a site needs before any of that means anything. One
   sweep makes everything look perfectly steady. */
#define SITE_ENOUGH_SWEEPS 3

/*
 * What it takes to call something diurnal rather than merely intermittent.
 *
 * Deliberately demanding. Intermittent is the honest answer when the pattern
 * is unknown, and claiming a daily rhythm from two sweeps in one hour and one
 * in another would be reading tea leaves -- so it wants several hours each
 * swept enough times, and a difference between the busiest and quietest of
 * them too large to be luck.
 */
#define SITE_DIURNAL_MIN_HOURS 4      /* distinct hours with enough sweeps */
#define SITE_DIURNAL_MIN_SWEEPS 3     /* sweeps before an hour has a rate */
#define SITE_DIURNAL_SPREAD 60        /* percentage points, busiest to quietest */

/*
 * How a signal has behaved here, for the operator rather than for the code.
 *
 * `site_status` answers the question the marks need -- has this site heard it
 * before -- and nothing more. This is the fuller answer: heard every time,
 * heard sometimes, heard before but not now.
 */
enum site_seen {
    SITE_SEEN_UNKNOWN = 0,   /* too little history to say anything */
    SITE_SEEN_NEW,
    SITE_SEEN_STEADY,
    SITE_SEEN_INTERMITTENT,
    /* Intermittent, and the intermittency follows the clock. */
    SITE_SEEN_DIURNAL,
    SITE_SEEN_MISSING
};

/* `entry` may be NULL, which is what a signal this site has not heard looks
   like. `heard_now` says whether this sweep found it. */
enum site_seen site_history_seen(const struct site_history *history,
                                 const struct site_entry *entry,
                                 int heard_now);

/*
 * Whether an entry's presence follows the hour, and by how much.
 *
 * Returns the spread in percentage points between the hour it is most often
 * heard in and the hour it is least often heard in, counting only hours the
 * site has swept enough times to have an opinion about -- or -1 when there is
 * not enough of the day covered to say anything. `busiest` and `quietest`, if
 * given, receive those hours.
 */
/*
 * How many signals this site knows were heard in the previous sweep and not
 * in the one just folded in -- what has just gone quiet.
 *
 * Only the previous one: something absent for ten sweeps went quiet once, and
 * reporting it again every four minutes would make a watch useless. Call it
 * after merging.
 */
int site_history_lost_now(const struct site_history *history);

int site_history_daily_spread(const struct site_history *history,
                              const struct site_entry *entry, int *busiest,
                              int *quietest);
/* Short enough for a column: "new", "steady", "on/off", "gone". */
const char *site_seen_name(enum site_seen seen);

/* What the window says about one frequency in the sweep in front of it. */
enum site_status {
    SITE_STATUS_UNKNOWN = 0,  /* no history for this site yet */
    SITE_STATUS_NEW,          /* this site has never heard it before */
    SITE_STATUS_KNOWN         /* heard before, and heard now */
};

void site_history_init(struct site_history *history, const char *site);

/* Text in, history out. Returns the number of entries read. */
int site_history_parse(const char *text, struct site_history *history);
/* And back. Returns the length, or -1 if it did not fit. */
int site_history_format(const struct site_history *history, char *out,
                        size_t size);

/*
 * Fold a sweep's frequencies in, as sweep number `history->sweeps + 1`.
 * Entries already known have their level and counts updated; frequencies never
 * heard here are added. Returns how many were new.
 */
/*
 * `hour` is the hour of the day the sweep finished, 0 to 23, or -1 when it is
 * not known -- a capture replayed from disk, say. An unknown hour still counts
 * as a sweep; it simply teaches the clock nothing.
 */
int site_history_merge(struct site_history *history, const double *hz,
                       const float *dbfs, const float *prominence, int count,
                       double bin_hz, int hour);

/*
 * Record one signal as heard in the sweep already counted, without counting a
 * new sweep.
 *
 * This is what the confirmation pass needs and merge cannot give it: merge
 * takes a whole sweep and advances the sweep number, so folding targets in one
 * at a time through merge counts one sweep per target. Doing that and
 * decrementing afterwards leaves entries claiming to have been heard in more
 * sweeps than the site has recorded -- which it did, and which reads as
 * "heard in 4 of 3 sweeps".
 *
 * Returns 1 when the signal was not already known here.
 */
int site_history_record_one(struct site_history *history, double hz,
                            float dbfs, float prominence_db, double bin_hz,
                            int hour);

/* The entry nearest `hz` within the tolerance, or NULL. */
const struct site_entry *site_history_find(const struct site_history *history,
                                           double hz, double bin_hz);

/*
 * Whether this site has heard `hz` before *this* sweep. Call before merging;
 * afterwards everything in the sweep is known, which is the point of merging.
 */
enum site_status site_history_status(const struct site_history *history,
                                     double hz, double bin_hz);

/*
 * Entries the history holds that this sweep did not hear, within the range it
 * covered. Only that range: a sweep of one band says nothing about a signal
 * three hundred megahertz away, and reporting it as missing would be a lie
 * about what was looked at. Returns how many were filled in.
 */
int site_history_missing(const struct site_history *history, const double *hz,
                         int count, double lower_hz, double upper_hz,
                         double bin_hz, const struct site_entry **out,
                         int max);

/* Where a site's history lives: `surveys/history-<site>.txt`, with anything
   awkward in the name replaced so it is a filename. Returns 0, or -1. */
int site_history_path(const char *site, char *out, size_t size);

/* Read it. Returns 0 when a file was read, 1 when there is none yet (the
   history is initialised empty), -1 on an unreadable one. */
int site_history_load(const char *site, struct site_history *history);
/* Write it, creating `surveys/` if need be. Returns 0, or -1. */
int site_history_save(const struct site_history *history);

#endif
