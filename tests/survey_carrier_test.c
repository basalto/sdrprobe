#include "check.h"

#include "survey_carrier.h"

#include <math.h>
#include <string.h>

/*
 * Turning local maxima into signals.
 *
 * The survey's peak finder returns maxima, and one transmission has several:
 * a 200 kHz FM carrier shows three or four. Reported raw, one station is five
 * candidates at five frequencies, each of them separately new, separately
 * confirmed, and separately surprising next time -- which is exactly what the
 * first live run of the site history produced.
 */

#define BINS 256
#define BIN_HZ 10000.0
#define FIRST_HZ 94000000.0
#define SENTINEL -200.0f

/* A carrier shaped like a real one: a flat top with skirts, so its interior
   holds several local maxima once noise is on it. */
static void put_carrier(float *power, int centre, int half, float peak) {
    int i;
    for (i = centre - half - 6; i <= centre + half + 6; i++) {
        float value;
        if (i < 0 || i >= BINS)
            continue;
        if (i >= centre - half && i <= centre + half)
            value = peak;
        else
            value = peak - 4.0f * (float)(abs(i - centre) - half);
        if (value > power[i])
            power[i] = value;
    }
}

static void flat_noise(float *power) {
    int i;
    for (i = 0; i < BINS; i++)
        power[i] = -80.0f;
}

static void test_one_carrier_many_maxima(void) {
    float power[BINS];
    struct sdr_peak peaks[8];
    struct survey_carrier carriers[8];
    int count;

    flat_noise(power);
    put_carrier(power, 100, 10, -20.0f);
    /* Three maxima inside one carrier, as ripple on its top produces. */
    power[93] = -19.0f;
    power[100] = -18.0f;
    power[107] = -19.5f;

    memset(peaks, 0, sizeof(peaks));
    peaks[0].index = 100; peaks[0].power_dbfs = -18.0f;
    peaks[0].floor_dbfs = -80.0f; peaks[0].prominence_db = 62.0f;
    peaks[0].lower_index = 90; peaks[0].upper_index = 110;
    peaks[1].index = 93; peaks[1].power_dbfs = -19.0f;
    peaks[1].floor_dbfs = -80.0f; peaks[1].prominence_db = 61.0f;
    peaks[1].lower_index = 88; peaks[1].upper_index = 98;
    peaks[2].index = 107; peaks[2].power_dbfs = -19.5f;
    peaks[2].floor_dbfs = -80.0f; peaks[2].prominence_db = 60.5f;
    peaks[2].lower_index = 102; peaks[2].upper_index = 112;

    count = survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                 20.0f, peaks, 3, carriers, 8);
    check_int("three overlapping maxima are one signal", count, 1);
    check_int("and it says how many it accounts for", carriers[0].peaks, 3);
    check_int("named by its tallest", carriers[0].strongest, 0);
    /* The extent is the union of what the finder measured, so the width is
       the carrier's, not one shoulder's. */
    /* The extent runs out to the trough on each side, so it covers the
       carrier's skirts and not just the part within 20 dB of the top. */
    check_close("its extent reaches the trough below it", carriers[0].lower_hz,
                FIRST_HZ + 84.0 * BIN_HZ, 1.0);
    check_close("and the one above", carriers[0].upper_hz,
                FIRST_HZ + 116.0 * BIN_HZ, 1.0);
    check_close("which is a width, not a bin", carriers[0].width_hz,
                33.0 * BIN_HZ, 1.0);
    /* The centre identifies the signal, so it is the middle of the extent --
       stable from sweep to sweep, where a power-weighted one would wander with
       whatever the transmitter happened to be carrying. */
    check_close("the centre is the middle of the extent",
                carriers[0].centre_hz, FIRST_HZ + 100.0 * BIN_HZ,
                2.0 * BIN_HZ);
    /* And the power centre is reported beside it, which for a symmetric
       carrier is the same place. */
    check_close("the power centre agrees on a symmetric carrier",
                carriers[0].power_centre_hz, carriers[0].centre_hz,
                3.0 * BIN_HZ);
}

static void test_two_carriers_stay_two(void) {
    float power[BINS];
    struct sdr_peak peaks[4];
    struct survey_carrier carriers[8];
    int count;

    flat_noise(power);
    put_carrier(power, 60, 8, -25.0f);
    put_carrier(power, 190, 8, -30.0f);

    memset(peaks, 0, sizeof(peaks));
    peaks[0].index = 60; peaks[0].power_dbfs = -25.0f;
    peaks[0].floor_dbfs = -80.0f; peaks[0].lower_index = 50;
    peaks[0].upper_index = 70;
    peaks[1].index = 190; peaks[1].power_dbfs = -30.0f;
    peaks[1].floor_dbfs = -80.0f; peaks[1].lower_index = 180;
    peaks[1].upper_index = 200;

    count = survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                 20.0f, peaks, 2, carriers, 8);
    check_int("signals that do not touch stay apart", count, 2);
    check_int("each with its single maximum", carriers[0].peaks, 1);
    check_int("and the other too", carriers[1].peaks, 1);
    /* Merging everything would be the easy failure, and it would report one
       carrier spanning the whole band. */
    check_true("and neither swallows the gap between them",
               carriers[0].upper_hz < carriers[1].lower_hz);
}

/*
 * What separates two signals is a trough, not a distance. Neighbours as close
 * as the band allows are still two if the power between them drops away, and
 * two maxima with nothing between them are one however far apart they look.
 */
static void test_a_trough_is_what_separates(void) {
    float power[BINS];
    struct sdr_peak peaks[4];
    struct survey_carrier carriers[8];
    int i, count;

    flat_noise(power);
    memset(peaks, 0, sizeof(peaks));
    peaks[0].index = 100; peaks[0].power_dbfs = -20.0f;
    peaks[0].floor_dbfs = -80.0f;
    peaks[1].index = 118; peaks[1].power_dbfs = -22.0f;
    peaks[1].floor_dbfs = -80.0f;
    /* Two narrow stations, close together, with the noise floor between. */
    put_carrier(power, 100, 3, -20.0f);
    put_carrier(power, 118, 3, -22.0f);
    count = survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                 20.0f, peaks, 2, carriers, 8);
    check_int("a trough between them makes them two", count, 2);

    /* Fill the gap so the power never drops between the maxima, and the same
       two peaks are one signal with a ripple on it. */
    for (i = 100; i <= 118; i++)
        if (power[i] < -23.0f)
            power[i] = -23.0f;
    count = survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                 20.0f, peaks, 2, carriers, 8);
    check_int("with the gap filled they are one", count, 1);
    check_int("holding both maxima", carriers[0].peaks, 2);
}

/*
 * A weak peak's extent has to mean something too. Marking where it falls 20 dB
 * below itself does not: one already near the floor never gets there, so its
 * extent runs to the end of the band and it appears to overlap everything --
 * which on air produced three "carriers" sharing one 2.4 MHz extent.
 */
static void test_a_weak_peak_is_still_bounded(void) {
    float power[BINS];
    struct sdr_peak peaks[2];
    struct survey_carrier carriers[8];
    int count;

    flat_noise(power);
    put_carrier(power, 40, 4, -20.0f);     /* something strong, far away */
    put_carrier(power, 150, 2, -74.0f);    /* and something barely there */
    memset(peaks, 0, sizeof(peaks));
    peaks[0].index = 40; peaks[0].power_dbfs = -20.0f;
    peaks[0].floor_dbfs = -80.0f;
    peaks[1].index = 150; peaks[1].power_dbfs = -74.0f;
    peaks[1].floor_dbfs = -80.0f;

    count = survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                 20.0f, peaks, 2, carriers, 8);
    check_int("both are found", count, 2);
    check_true("and the weak one does not span the band",
               carriers[1].width_hz < 40.0 * BIN_HZ);
    check_true("nor reach the strong one",
               carriers[1].lower_hz > carriers[0].upper_hz);
}

static void test_prominence_uses_the_quietest_floor(void) {
    float power[BINS];
    struct sdr_peak peaks[4];
    struct survey_carrier carriers[8];

    flat_noise(power);
    put_carrier(power, 100, 10, -20.0f);
    memset(peaks, 0, sizeof(peaks));
    peaks[0].index = 100; peaks[0].power_dbfs = -18.0f;
    peaks[0].floor_dbfs = -80.0f; peaks[0].lower_index = 90;
    peaks[0].upper_index = 110;
    /* A shoulder sits on the skirt of its own carrier, so the floor either
       side of it is raised by the carrier itself. Taking that as the
       carrier's floor would report it as far less prominent than it is. */
    peaks[1].index = 106; peaks[1].power_dbfs = -21.0f;
    peaks[1].floor_dbfs = -30.0f; peaks[1].lower_index = 102;
    peaks[1].upper_index = 110;

    survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ, 20.0f, peaks, 2,
                         carriers, 8);
    check_close("the floor is the quietest of them", carriers[0].floor_dbfs,
                -80.0, 0.01);
    check_close("so the prominence is the carrier's",
                carriers[0].prominence_db, 62.0, 0.01);
}

static void test_refuses_nonsense(void) {
    float power[BINS];
    struct sdr_peak peaks[2];
    struct survey_carrier carriers[4];

    flat_noise(power);
    memset(peaks, 0, sizeof(peaks));
    check_int("no power, no carriers",
              survey_carriers_from(NULL, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                   20.0f, peaks, 1, carriers, 4), 0);
    check_int("no peaks, no carriers",
              survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                   20.0f, peaks, 0, carriers, 4), 0);
    check_int("no room, no carriers",
              survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ,
                                   20.0f, peaks, 1, carriers, 0), 0);
}

/*
 * A lopsided carrier: the power sits away from the middle, and the two centres
 * must part company. This is what a loaded LTE downlink looks like, and the
 * reason the identity is the extent's middle rather than the power's.
 */
static void test_a_lopsided_carrier(void) {
    float power[BINS];
    struct sdr_peak peaks[2];
    struct survey_carrier carriers[4];
    int i;

    flat_noise(power);
    put_carrier(power, 120, 20, -40.0f);
    for (i = 125; i <= 140; i++)          /* one side carrying much more */
        power[i] = -20.0f;
    memset(peaks, 0, sizeof(peaks));
    peaks[0].index = 132; peaks[0].power_dbfs = -20.0f;
    peaks[0].floor_dbfs = -80.0f;

    survey_carriers_from(power, BINS, SENTINEL, FIRST_HZ, BIN_HZ, 20.0f,
                         peaks, 1, carriers, 4);
    check_true("the power centre sits where the power is",
               carriers[0].power_centre_hz > carriers[0].centre_hz);
    check_true("but the identity stays in the middle of the extent",
               fabs(carriers[0].centre_hz -
                    (carriers[0].lower_hz + carriers[0].upper_hz) / 2.0) < 1.0);
}

/*
 * The shape a width implies. A description of the measurement, never an
 * identification -- the band plan does that job and is a lookup besides. What
 * this buys the reader is the pairing: medium inside the FM allocation is a
 * station, medium inside a gap in the table is worth a closer look.
 */
static void test_shape_from_width(void) {
    check_int("a bare carrier", (int)survey_carrier_shape(2000.0),
              (int)SURVEY_SHAPE_TONE);
    check_int("voice or telemetry", (int)survey_carrier_shape(16000.0),
              (int)SURVEY_SHAPE_NARROW);
    check_int("a broadcast FM station",
              (int)survey_carrier_shape(200000.0), (int)SURVEY_SHAPE_MEDIUM);
    check_int("a GSM carrier is the same shape",
              (int)survey_carrier_shape(200000.0), (int)SURVEY_SHAPE_MEDIUM);
    check_int("something wider", (int)survey_carrier_shape(1500000.0),
              (int)SURVEY_SHAPE_WIDE);
    /* The two the survey actually measured on air: a 9 MHz LTE downlink came
       out at 8.3 MHz, and a television multiplex is 8. */
    check_int("an LTE downlink", (int)survey_carrier_shape(8300000.0),
              (int)SURVEY_SHAPE_VERY_WIDE);
    check_str("and each has a word short enough for a column",
              survey_shape_name(SURVEY_SHAPE_VERY_WIDE), "very wide");
    check_str("as does the narrowest",
              survey_shape_name(SURVEY_SHAPE_TONE), "tone");
}

/*
 * The bug that shipped: the notch was a fixed two bins, so how much smoothing
 * a dip had to survive depended on how finely the sweep happened to be binned.
 * A single tuning bins at about 977 Hz, where two bins is four kilohertz --
 * less than a GSM carrier's own ripple -- and one 200 kHz carrier came back as
 * three signals with troughs between them.
 */
static void test_a_carrier_at_fine_resolution(void) {
    enum { FINE_BINS = 1638 };
    static float power[FINE_BINS];
    const double fine_bin = 976.8;        /* what one tuning of 1.6 MHz gives */
    const double first = 948000000.0;
    struct sdr_peak peaks[6];
    struct survey_carrier carriers[8];
    int i, count, centre = 800, half = 102;   /* ~200 kHz of carrier */
    unsigned seed = 12345u;

    for (i = 0; i < FINE_BINS; i++)
        power[i] = -80.0f;
    /* A carrier with the several decibels of bin-to-bin swing that any
       modulated signal has. Deterministic, so the check is too. */
    for (i = centre - half; i <= centre + half; i++) {
        seed = seed * 1103515245u + 12345u;
        power[i] = -32.0f - (float)((seed >> 16) % 900u) / 100.0f;
    }
    memset(peaks, 0, sizeof(peaks));
    for (i = 0; i < 6; i++) {
        peaks[i].index = centre - 90 + i * 36;
        peaks[i].power_dbfs = power[peaks[i].index];
        peaks[i].floor_dbfs = -80.0f;
    }

    count = survey_carriers_from(power, FINE_BINS, SENTINEL, first, fine_bin,
                                 20.0f, peaks, 6, carriers, 8);
    check_int("one carrier, however finely it is binned", count, 1);
    check_int("holding every maximum in it", carriers[0].peaks, 6);
    /* And the smoothing scales, rather than being a bin count that means
       four kilohertz here and most of a band on a full sweep. */
    check_int("the notch is ten kilohertz of bins here",
              survey_carrier_notch_bins(fine_bin), 10);
    check_int("and one bin when a bin is already wider than that",
              survey_carrier_notch_bins(213000.0), 1);
}

/*
 * Which carrier a frequency belongs to, which is how a candidate inherits the
 * verdict of the signal it is a maximum of.
 */
static void test_which_carrier_holds_a_frequency(void) {
    struct survey_carrier carriers[3];

    memset(carriers, 0, sizeof(carriers));
    carriers[0].centre_hz = 94.4e6;
    carriers[0].lower_hz = 94.3e6;
    carriers[0].upper_hz = 94.5e6;
    carriers[0].width_hz = 200e3;
    carriers[1].centre_hz = 95.7e6;
    carriers[1].lower_hz = 95.6e6;
    carriers[1].upper_hz = 95.8e6;
    carriers[1].width_hz = 200e3;
    /* A narrow one sharing the first's upper edge: a tone sitting on the
       shoulder of a broadcast station. */
    carriers[2].centre_hz = 94.49e6;
    carriers[2].lower_hz = 94.48e6;
    carriers[2].upper_hz = 94.50e6;
    carriers[2].width_hz = 20e3;

    check_int("a maximum inside the first",
              survey_carrier_holding(carriers, 3, 94.35e6), 0);
    check_int("its own centre too",
              survey_carrier_holding(carriers, 3, 94.4e6), 0);
    check_int("a maximum inside the second",
              survey_carrier_holding(carriers, 3, 95.75e6), 1);
    /* Both the wide one and the narrow one reach here; the narrow one is what
       this is a maximum of. */
    check_int("where two overlap, the narrower one owns it",
              survey_carrier_holding(carriers, 3, 94.49e6), 2);
    check_int("the gap between them belongs to neither",
              survey_carrier_holding(carriers, 3, 95.0e6), -1);
    check_int("and an empty list holds nothing",
              survey_carrier_holding(carriers, 0, 94.4e6), -1);
}

int main(void) {
    test_one_carrier_many_maxima();
    test_two_carriers_stay_two();
    test_a_trough_is_what_separates();
    test_a_weak_peak_is_still_bounded();
    test_a_lopsided_carrier();
    test_a_carrier_at_fine_resolution();
    test_prominence_uses_the_quietest_floor();
    test_refuses_nonsense();

    test_shape_from_width();

    test_which_carrier_holds_a_frequency();

    return check_report("peaks to carriers");
}
