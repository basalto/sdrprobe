#ifndef FM_SCAN_H
#define FM_SCAN_H

/*
 * Walking band II: which tunings cover it, which channels a step can see, and
 * how long the whole thing takes.
 *
 * Two passes, and the reason is arithmetic rather than taste. Band II holds
 * 205 channels on a 100 kHz raster, and deciding whether one carries RDS means
 * demodulating it -- a quarter of a second for the pilot loop alone, so
 * visiting all 205 is a minute of tuning to find the fifteen or so that exist.
 * But a receiver at 2 MS/s sees 1.6 MHz of the band at once, so thirteen
 * tunings and a couple of seconds say where the carriers *are*; only those get
 * the quarter second. Eleven seconds against sixty, for the same answer.
 *
 * That asymmetry is what makes FM the cheap band to scan, and it does not
 * hold for the cellular ones: a GSM channel is 200 kHz and an LTE carrier
 * needs the whole 1.92 MS/s grid to say anything, so neither can be triaged
 * from a spectrum the way a 200 kHz-wide FM carrier can.
 *
 * Plain doubles, no raylib and no receiver (ADR-0012).
 */

/* Band II, inset so a carrier is never measured on the shoulder of the
   tuner's response. */
#define FM_BAND_LOWER_HZ 87500000.0
#define FM_BAND_UPPER_HZ 108000000.0
#define FM_CHANNEL_SPACING_HZ 100000.0
#define FM_SCAN_EDGE_MARGIN_HZ 200000.0

/* How long a coarse step waits for the tuner and then measures. The
   measurement is a spectrum average, which needs a handful of blocks and no
   more -- nothing here has to lock to anything. */
#define FM_SCAN_STEP_SETTLE_SECONDS 0.12
#define FM_SCAN_STEP_MEASURE_SECONDS 0.10

/*
 * And how long a candidate gets. The pilot may not be believed for a quarter
 * second (FM_PILOT_SETTLE_SECONDS), and a station's name needs its four
 * segments to arrive and repeat, which is a second or so of groups. This is
 * the pilot plus enough for an identification, not enough for a name -- the
 * scan says which frequencies are worth stopping on, and stopping on one is
 * what reads the name.
 */
#define FM_SCAN_VISIT_SETTLE_SECONDS 0.10
#define FM_SCAN_VISIT_SECONDS 0.70
/*
 * How long to sit on a station that answered, waiting for it to say its name.
 *
 * A name is not one reading. It arrives in four two-character segments, and
 * rds.c shows it only once the four have been seen whole *twice* and agreed,
 * because one pass through four segments can be four segments of two
 * different names and look perfect. So this is bounded by how often a station
 * repeats group 0A, not by how strong it is.
 *
 * The listening stops the moment the name is confirmed, which is what keeps
 * the cost honest: a station that names itself in two seconds costs two, and
 * the cap is only paid by one that never manages it at all.
 *
 * Five seconds rather than more, because the cap is what bounds the worst
 * case and the worst case is what has to stay under the minute that visiting
 * all 205 channels would cost -- the arithmetic the two-pass arrangement
 * exists for. Measured here, six stations named themselves in about two
 * seconds each and none came close to it.
 */
#define FM_SCAN_NAME_SECONDS 5.0

#define FM_SCAN_SENTINEL_DBFS (-300.0f)

/*
 * How far above the local floor a channel has to stand to be worth visiting.
 * Generous on purpose: a visit costs under a second and the cost of missing a
 * station is that it is not in the list at all.
 */
#define FM_SCAN_CARRIER_MARGIN_DB 8.0

enum fm_scan_status {
    FM_SCAN_OK,
    FM_SCAN_RATE_TOO_LOW    /* the accept window would be under 400 kHz */
};

struct fm_scan_plan {
    double accept_half_hz;   /* half the window a step measures in */
    double step_hz;
    double first_center_hz;
    int step_count;
};

/* The channels: 87.5 MHz to 108.0 MHz inclusive, every 100 kHz. */
static inline int fm_scan_channel_count(void) {
    return (int)((FM_BAND_UPPER_HZ - FM_BAND_LOWER_HZ) /
                 FM_CHANNEL_SPACING_HZ) + 1;
}

static inline double fm_scan_channel_hz(int index) {
    if (index < 0 || index >= fm_scan_channel_count())
        return 0.0;
    return FM_BAND_LOWER_HZ + (double)index * FM_CHANNEL_SPACING_HZ;
}

/* The channel nearest a frequency, or -1 for one outside the band. Rounding
   rather than truncating: a carrier measured at 89.5993 MHz is channel 21,
   and a scan that filed it as 89.5 would report a station 100 kHz from where
   it is and tune to silence. */
static inline int fm_scan_channel_at(double hz) {
    double offset;
    int index;

    if (hz < FM_BAND_LOWER_HZ - FM_CHANNEL_SPACING_HZ / 2.0 ||
        hz > FM_BAND_UPPER_HZ + FM_CHANNEL_SPACING_HZ / 2.0)
        return -1;
    offset = (hz - FM_BAND_LOWER_HZ) / FM_CHANNEL_SPACING_HZ;
    index = (int)(offset + 0.5);
    if (index < 0)
        index = 0;
    if (index >= fm_scan_channel_count())
        index = fm_scan_channel_count() - 1;
    return index;
}

/*
 * How many tunings cover the band at this rate. The accept window is the span
 * minus a margin at each edge, and steps are one window apart, so the band is
 * covered once with no gap.
 */
static inline enum fm_scan_status fm_scan_plan_for(double sample_rate,
                                                   struct fm_scan_plan *plan) {
    double window, span;

    if (!plan)
        return FM_SCAN_RATE_TOO_LOW;
    window = sample_rate - 2.0 * FM_SCAN_EDGE_MARGIN_HZ;
    if (window < 400000.0)
        return FM_SCAN_RATE_TOO_LOW;

    plan->accept_half_hz = window / 2.0;
    plan->step_hz = window;
    /* Half a window inside the lower edge, so the first step's accept window
       starts exactly at the band edge rather than below it. */
    plan->first_center_hz = FM_BAND_LOWER_HZ + plan->accept_half_hz;
    span = FM_BAND_UPPER_HZ - FM_BAND_LOWER_HZ;
    plan->step_count = (int)(span / plan->step_hz);
    if ((double)plan->step_count * plan->step_hz < span)
        plan->step_count++;
    if (plan->step_count < 1)
        plan->step_count = 1;
    return FM_SCAN_OK;
}

/* Whether a channel's power says a carrier is there. */
static inline int fm_scan_is_carrier(double power_dbfs, double floor_dbfs) {
    if (power_dbfs <= FM_SCAN_SENTINEL_DBFS)
        return 0;
    return power_dbfs - floor_dbfs >= FM_SCAN_CARRIER_MARGIN_DB;
}

/* What the two passes cost, which is what the view quotes before committing
   an operator to it. */
static inline double fm_scan_sweep_seconds(const struct fm_scan_plan *plan) {
    if (!plan)
        return 0.0;
    return (double)plan->step_count * (FM_SCAN_STEP_SETTLE_SECONDS +
                                       FM_SCAN_STEP_MEASURE_SECONDS);
}

static inline double fm_scan_visit_seconds(int candidates) {
    if (candidates < 0)
        candidates = 0;
    return (double)candidates * (FM_SCAN_VISIT_SETTLE_SECONDS +
                                 FM_SCAN_VISIT_SECONDS);
}

/*
 * How much baseband to turn into bits, and when not to bother.
 *
 * The RDS decode works in fixed non-overlapping chunks: one timing search and
 * one axis per chunk, its bits appended, the baseband it came from discarded.
 * The fixed size is what lets *consecutive* chunks agree about which absolute
 * symbol an index means, which radio text needs because it arrives in sixteen
 * segments over twenty-five seconds.
 *
 * `flush` says there will be no next chunk -- the scan is leaving this
 * carrier -- so a short one is decoded rather than discarded unread. It is as
 * valid as a full chunk and simply carries fewer bits; there is nothing left
 * for it to have to agree with.
 *
 * This function exists because its absence was a bug with no symptom anybody
 * could see. A scan visit is under a second and a full chunk is seconds of
 * baseband, so every visit accumulated a fraction of a chunk, decoded none of
 * it, and the scan reported all of band II as carrying no RDS -- while the
 * panel below the list was decoding a station by name. Nothing was wrong with
 * the decoder, and nothing in a check could see the mismatch, because the
 * length was a comparison inside a draw-adjacent update rather than a
 * decision with a name (ADR-0012).
 */
#define FM_RDS_CHUNK_SAMPLES 32768
/*
 * The shortest run of baseband still worth a timing search, for when there
 * will not be a full chunk: about 250 symbols, which is ten blocks against
 * the four in the offset order that synchronisation needs.
 *
 * It exists because the band scan visits a carrier for less than a second and
 * a full chunk is several seconds of baseband. The scan therefore accumulated
 * a quarter of a chunk per station, decoded none of it, and reported every
 * station in band II as carrying no RDS -- including the one it was decoding
 * at the time.
 */
#define FM_RDS_MIN_CHUNK_SAMPLES 4096

static inline size_t fm_rds_chunk_length(size_t available, int flush) {
    if (available >= FM_RDS_CHUNK_SAMPLES)
        return FM_RDS_CHUNK_SAMPLES;
    if (!flush || available < FM_RDS_MIN_CHUNK_SAMPLES)
        return 0;
    return available;
}

/* Whether a visit of this many seconds can gather a chunk at all, at a given
   baseband rate. The scan's visit is deliberately shorter than a full chunk;
   this is what says so out loud. */
static inline int fm_scan_visit_fills_chunk(double visit_seconds,
                                            double baseband_rate) {
    return visit_seconds * baseband_rate >= (double)FM_RDS_CHUNK_SAMPLES;
}

/*
 * What a scan costs, in seconds: the coarse sweep, one visit each, and the
 * name pass over however many answered.
 *
 * The third pass is charged at its cap, so this is the worst case. It is
 * worth being able to state, because the argument for scanning band II in two
 * passes at all is arithmetic -- 205 channels at a quarter second each is a
 * minute, thirteen tunings plus the carriers that exist is a fraction of it --
 * and a third pass is only defensible while that argument still holds.
 */
static inline double fm_scan_seconds(int steps, int carriers, int with_rds) {
    return (double)steps *
               (FM_SCAN_STEP_SETTLE_SECONDS + FM_SCAN_STEP_MEASURE_SECONDS) +
           (double)carriers *
               (FM_SCAN_VISIT_SETTLE_SECONDS + FM_SCAN_VISIT_SECONDS) +
           (double)with_rds *
               (FM_SCAN_VISIT_SETTLE_SECONDS + FM_SCAN_NAME_SECONDS);
}

#endif
