/*
 * check-clock-chain -- a clock family that goes up in octaves, not harmonics.
 *
 * `.scratch/device-model/issues/10-*`. Every case here is arithmetic over
 * three numbers and the frequencies are the ones measured on air on
 * 2026-09-11: no receiver, no window, no samples (ADR-0012).
 *
 * The property the whole file turns on is the **absence** of 3f. A harmonic
 * comb has it and a doubler or divider chain never does, and 225 MHz came
 * back empty at a 12 dB bar while 75, 150 and 300 stood at 11 to 17 dB. A
 * check that only asserted the three present members would pass against a
 * harmonic model too, which is the model the measurement refutes.
 */

#include "check.h"

#include "clock_chain.h"

#define F CLOCK_CHAIN_MEASURED_FUNDAMENTAL_HZ
/* One bin of the sweep that found them: 2 MHz over 2048 points. */
#define BIN_HZ 976.6

static void test_the_family_that_was_measured(void) {
    check_int("75 MHz is the fundamental",
              clock_chain_octave(75000000.0, F, BIN_HZ), 1);
    check_int("150 MHz is the second",
              clock_chain_octave(150000000.0, F, BIN_HZ), 2);
    check_int("300 MHz is the fourth",
              clock_chain_octave(300000000.0, F, BIN_HZ), 4);

    /* As they actually read: +488 Hz, exactly half a bin, at all three. */
    check_int("and they are recognised where they read",
              clock_chain_octave(75000488.0, F, BIN_HZ) +
                  clock_chain_octave(150000488.0, F, BIN_HZ) +
                  clock_chain_octave(300000488.0, F, BIN_HZ),
              1 + 2 + 4);
}

/*
 * The absences, which is what separates this model from a harmonic one.
 */
static void test_the_odd_multiple_is_not_on_it(void) {
    check_int("225 MHz is not on the chain",
              clock_chain_octave(225000000.0, F, BIN_HZ), 0);
    check_int("but it is a multiple of the fundamental",
              clock_chain_is_off_octave(225000000.0, F, BIN_HZ), 1);
    check_int("so is 375 MHz, five times",
              clock_chain_is_off_octave(375000000.0, F, BIN_HZ), 1);
    check_int("and 450, six times",
              clock_chain_is_off_octave(450000000.0, F, BIN_HZ), 1);
    /* An octave is never off-octave, which is what makes the two exclusive. */
    check_int("150 is not off-octave",
              clock_chain_is_off_octave(150000000.0, F, BIN_HZ), 0);
    /* And something that is no multiple at all is neither. */
    check_int("174.7 MHz is on no chain",
              clock_chain_octave(174718262.0, F, BIN_HZ), 0);
    check_int("and is not an odd multiple either",
              clock_chain_is_off_octave(174718262.0, F, BIN_HZ), 0);

    /* 37.5 is the fundamental halved, and was swept and found empty. The
       chain starts at the fundamental and does not go down. */
    check_int("37.5 MHz is below the chain",
              clock_chain_octave(37500000.0, F, BIN_HZ), 0);
}

static void test_the_nearest_member(void) {
    check_close("just above 150 is nearest 150",
                clock_chain_nearest_hz(150000488.0, F), 150000000.0, 1.0);
    check_close("175 is nearer 150 than 300",
                clock_chain_nearest_hz(175000000.0, F), 150000000.0, 1.0);
    check_close("225 is equidistant and takes the lower",
                clock_chain_nearest_hz(225000000.0, F), 150000000.0, 1.0);
    check_close("and something far below takes the fundamental",
                clock_chain_nearest_hz(1000000.0, F), 75000000.0, 1.0);
}

/*
 * No chain, no answer. This is the case every receiver but the one it was
 * measured on is in, and the case a capture is in -- a file has no crystal to
 * blame, which is why it gets no comb tests either.
 */
static void test_no_fundamental_means_no_chain(void) {
    check_int("no fundamental, no octave",
              clock_chain_octave(150000000.0, 0.0, BIN_HZ), 0);
    check_int("nor an off-octave",
              clock_chain_is_off_octave(225000000.0, 0.0, BIN_HZ), 0);
    check_close("and no nearest member",
                clock_chain_nearest_hz(150000000.0, 0.0), 0.0, 1e-9);
    check_int("a negative fundamental is refused rather than looped over",
              clock_chain_octave(150000000.0, -75000000.0, BIN_HZ), 0);
    check_int("and so is a zero frequency",
              clock_chain_octave(0.0, F, BIN_HZ), 0);
}

/*
 * The bound, so an absurd fundamental terminates. A one-hertz fundamental has
 * every frequency somewhere above its top octave, and the loop must stop
 * rather than run to the frequency asked about.
 */
static void test_the_search_is_bounded(void) {
    check_int("a tiny fundamental finds nothing up here",
              clock_chain_octave(150000000.0, 1.0, BIN_HZ), 0);
    check_close("and its nearest member is its top octave",
                clock_chain_nearest_hz(150000000.0, 1.0),
                (double)(1 << CLOCK_CHAIN_MAX_OCTAVES), 1e-6);
}

/* The tolerance decides, and a chain is sparse enough that it has to: 225 MHz
   is 75 MHz from its nearest member, so "nearest" is not evidence. */
static void test_the_tolerance_decides(void) {
    check_int("half a bin off is on it",
              clock_chain_octave(150000488.0, F, BIN_HZ), 2);
    check_int("ten kilohertz off is not",
              clock_chain_octave(150010000.0, F, BIN_HZ), 0);
    check_int("unless the tolerance says so",
              clock_chain_octave(150010000.0, F, 20000.0), 2);
}

int main(void) {
    test_the_family_that_was_measured();
    test_the_odd_multiple_is_not_on_it();
    test_the_nearest_member();
    test_no_fundamental_means_no_chain();
    test_the_search_is_bounded();
    test_the_tolerance_decides();
    return check_report("a clock family in octaves, not harmonics");
}
