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
                FIRST_HZ + 83.0 * BIN_HZ, 1.0);
    check_close("and the one above", carriers[0].upper_hz,
                FIRST_HZ + 117.0 * BIN_HZ, 1.0);
    check_close("which is a width, not a bin", carriers[0].width_hz,
                35.0 * BIN_HZ, 1.0);
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

int main(void) {
    test_one_carrier_many_maxima();
    test_two_carriers_stay_two();
    test_a_trough_is_what_separates();
    test_a_weak_peak_is_still_bounded();
    test_a_lopsided_carrier();
    test_prominence_uses_the_quietest_floor();
    test_refuses_nonsense();

    return check_report("peaks to carriers");
}
