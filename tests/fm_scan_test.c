#include "check.h"

#include "fm_scan.h"

#include <math.h>

/*
 * The band II scan's arithmetic: the raster, the coverage, and what the two
 * passes cost.
 *
 * The coverage property is the one that matters. A step plan with a gap in it
 * does not look like an error -- it looks like a quiet stretch of the band,
 * and nothing else in the program would say otherwise. That is the same
 * reason scan_plan_test.c exists for the GSM scan.
 */

static void test_the_raster(void) {
    check_int("band II holds 206 channels at 100 kHz",
              fm_scan_channel_count(), 206);
    check_close("the first is 87.5 MHz", fm_scan_channel_hz(0), 87.5e6, 1.0);
    check_close("the last is 108.0 MHz",
                fm_scan_channel_hz(fm_scan_channel_count() - 1), 108.0e6, 1.0);
    check_close("and they are 100 kHz apart",
                fm_scan_channel_hz(1) - fm_scan_channel_hz(0), 100000.0, 1.0);
    check_close("an index in the middle", fm_scan_channel_hz(21), 89.6e6, 1.0);

    /* Out of range names nothing rather than an edge channel. */
    check_close("nothing outside the band", fm_scan_channel_hz(-1), 0.0, 1e-9);
    check_close("nor past its end",
                fm_scan_channel_hz(fm_scan_channel_count()), 0.0, 1e-9);
}

/*
 * Snapping a measured frequency back to the raster.
 *
 * Rounding, not truncating. A carrier the spectrum puts at 89.5993 MHz is
 * channel 21 and a scan that filed it as 89.5 would list a station 100 kHz
 * from where it is -- and tuning there finds silence, which reads as the scan
 * having been wrong about the station rather than about the frequency.
 */
static void test_snapping_to_the_raster(void) {
    check_int("dead on", fm_scan_channel_at(89.6e6), 21);
    check_int("a little low", fm_scan_channel_at(89.5993e6), 21);
    check_int("a little high", fm_scan_channel_at(89.6007e6), 21);
    check_int("nearly halfway down", fm_scan_channel_at(89.5501e6), 21);
    check_int("and nearly halfway up", fm_scan_channel_at(89.6499e6), 21);
    check_int("past halfway is the next one", fm_scan_channel_at(89.6501e6),
              22);
    check_int("the first channel", fm_scan_channel_at(87.5e6), 0);
    check_int("the last", fm_scan_channel_at(108.0e6),
              fm_scan_channel_count() - 1);

    /* Outside the band, including the cellular frequencies this program
       spends most of its time on, is not a channel. */
    check_int("well below the band", fm_scan_channel_at(80.0e6), -1);
    check_int("well above it", fm_scan_channel_at(120.0e6), -1);
    check_int("nor 1090 MHz", fm_scan_channel_at(1090.0e6), -1);

    /* Every channel snaps back to itself, which is the round trip that keeps
       the two functions honest about each other. */
    {
        int i, wrong = 0;
        for (i = 0; i < fm_scan_channel_count(); i++)
            if (fm_scan_channel_at(fm_scan_channel_hz(i)) != i)
                wrong++;
        check_int("every channel is its own nearest", wrong, 0);
    }
}

static void test_the_coarse_sweep_covers_the_band(void) {
    struct fm_scan_plan plan;

    check_int("the house rate plans a sweep",
              fm_scan_plan_for(2000000.0, &plan), FM_SCAN_OK);
    check_close("with a 1.6 MHz window", 2.0 * plan.accept_half_hz, 1600000.0,
                1.0);
    check_int("in thirteen steps", plan.step_count, 13);

    /*
     * The property: every channel in the band falls inside some step's accept
     * window. A gap here is a stretch of band that reports no stations and
     * looks exactly like a stretch of band with no stations in it.
     */
    {
        int channel, uncovered = 0;
        for (channel = 0; channel < fm_scan_channel_count(); channel++) {
            double hz = fm_scan_channel_hz(channel);
            int step, seen = 0;

            for (step = 0; step < plan.step_count; step++) {
                double centre = plan.first_center_hz +
                                (double)step * plan.step_hz;
                if (hz >= centre - plan.accept_half_hz &&
                    hz <= centre + plan.accept_half_hz)
                    seen = 1;
            }
            if (!seen)
                uncovered++;
        }
        check_int("every channel is inside some step", uncovered, 0);
    }

    /* A rate too low to see a useful slice is refused rather than planning a
       sweep of two hundred tunings. */
    check_int("a rate that sees nothing is refused",
              fm_scan_plan_for(500000.0, &plan), FM_SCAN_RATE_TOO_LOW);
    check_int("and no plan at all", fm_scan_plan_for(2000000.0, NULL),
              FM_SCAN_RATE_TOO_LOW);

    /* A wider receiver takes fewer steps, which is the only reason the plan
       is computed rather than written down. */
    {
        struct fm_scan_plan wide;
        fm_scan_plan_for(2400000.0, &wide);
        check_true("a wider rate takes fewer steps",
                   wide.step_count < plan.step_count);
    }
}

static void test_what_counts_as_a_carrier(void) {
    check_true("eight decibels over the floor is a carrier",
               fm_scan_is_carrier(-40.0, -48.0));
    check_true("and more so", fm_scan_is_carrier(-20.0, -60.0));
    check_true("seven is not", !fm_scan_is_carrier(-41.0, -48.0));
    check_true("a channel never measured is not",
               !fm_scan_is_carrier(FM_SCAN_SENTINEL_DBFS, -100.0));
}

/*
 * The cost, which is the argument for doing it in two passes at all. The
 * numbers the view quotes come from here, so they cannot drift from what it
 * actually does.
 */
static void test_what_it_costs(void) {
    struct fm_scan_plan plan;

    fm_scan_plan_for(2000000.0, &plan);
    check_close("the coarse sweep is under three seconds",
                fm_scan_sweep_seconds(&plan), 2.86, 0.01);
    check_close("and twenty candidates cost sixteen more",
                fm_scan_visit_seconds(20), 16.0, 0.01);
    check_close("nothing found costs nothing", fm_scan_visit_seconds(0), 0.0,
                1e-9);

    /*
     * And the comparison that justifies the whole arrangement: visiting every
     * channel in the band would cost more than a minute, against about twenty
     * seconds for a sweep and the twenty or so carriers it finds.
     */
    {
        double every = fm_scan_visit_seconds(fm_scan_channel_count());
        double two_pass = fm_scan_sweep_seconds(&plan) +
                          fm_scan_visit_seconds(20);
        check_true("visiting every channel takes over a minute", every > 60.0);
        check_true("and two passes take under a quarter of that",
                   two_pass < every / 4.0);
    }
}

/*
 * How much baseband becomes bits.
 *
 * The bug this is for: a scan visit is under a second, a full chunk is
 * seconds of baseband, so every visit gathered a fraction of a chunk and
 * decoded none of it. The scan reported all of band II as carrying no RDS
 * while the panel below the list was reading a station by name. Nothing was
 * wrong with the decoder; the length was a comparison buried in an update
 * rather than a decision anything could ask about.
 */
static void test_chunk_length(void) {
    /* A full chunk is taken whole, flush or not: a chunk that shortened
       itself whenever asked would stop consecutive chunks agreeing about
       which absolute symbol an index means, which is what radio text needs. */
    check_size("a full chunk is a full chunk",
               fm_rds_chunk_length(FM_RDS_CHUNK_SAMPLES, 0),
               FM_RDS_CHUNK_SAMPLES);
    check_size("and stays one when flushing",
               fm_rds_chunk_length(FM_RDS_CHUNK_SAMPLES, 1),
               FM_RDS_CHUNK_SAMPLES);
    check_size("more than one is still one",
               fm_rds_chunk_length(FM_RDS_CHUNK_SAMPLES * 3, 0),
               FM_RDS_CHUNK_SAMPLES);

    /* Short of one, nothing is decoded until somebody says there will be no
       more -- which is the whole difference between the two callers. */
    check_size("a partial chunk waits",
               fm_rds_chunk_length(FM_RDS_CHUNK_SAMPLES - 1, 0), 0);
    check_size("and is taken when the tuning is ending",
               fm_rds_chunk_length(FM_RDS_CHUNK_SAMPLES - 1, 1),
               FM_RDS_CHUNK_SAMPLES - 1);

    /* Below the floor it is not worth a timing search even when flushing. */
    check_size("too little to search",
               fm_rds_chunk_length(FM_RDS_MIN_CHUNK_SAMPLES - 1, 1), 0);
    check_size("just enough",
               fm_rds_chunk_length(FM_RDS_MIN_CHUNK_SAMPLES, 1),
               FM_RDS_MIN_CHUNK_SAMPLES);
    check_size("nothing at all", fm_rds_chunk_length(0, 1), 0);

    /* The floor is worth more than the four blocks synchronisation needs.
       RDS runs at 1187.5 bits a second against a baseband of about 19 kHz,
       so roughly sixteen samples a bit and twenty-six bits a block. */
    {
        double bits = (double)FM_RDS_MIN_CHUNK_SAMPLES / 16.0;
        check_true("the floor holds more than four blocks",
                   bits / 26.0 > 4.0);
    }

    /*
     * And the fact that made the bug: a visit cannot fill a chunk. This is
     * asserted rather than assumed, because if the visit ever grows past a
     * chunk the flush stops mattering and somebody should find that out here
     * rather than by deleting it.
     */
    check_true("a scan visit is shorter than a chunk",
               !fm_scan_visit_fills_chunk(FM_SCAN_VISIT_SECONDS, 19000.0));
    check_true("a station parked on for ten seconds is not",
               fm_scan_visit_fills_chunk(10.0, 19000.0));
}

int main(void) {
    test_the_raster();
    test_snapping_to_the_raster();
    test_the_coarse_sweep_covers_the_band();
    test_what_counts_as_a_carrier();
    test_what_it_costs();

    test_chunk_length();

    return check_report("band II scan: raster, coverage and cost");
}
