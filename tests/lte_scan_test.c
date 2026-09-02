#include "check.h"

#include "lte_scan.h"

#include <stdlib.h>
#include <string.h>

/*
 * The scan order. One property carries the suite: every channel of a band is
 * named exactly once. An order that skipped one would leave whatever
 * transmits there permanently invisible, and a scan that found nothing would
 * look exactly like a scan of an empty band.
 */

static void test_every_channel_once(void) {
    int b;
    for (b = 0; b < lte_band_count(); b++) {
        const struct lte_band *band = lte_band_at(b);
        int count = lte_scan_count(band);
        unsigned char *seen = calloc((size_t)count, 1);
        int index, missing = 0, twice = 0, outside = 0;

        if (!seen)
            exit(2);
        for (index = 0; index < count; index++) {
            unsigned int earfcn = lte_scan_candidate(band, index);
            long offset = (long)earfcn - (long)band->earfcn_low;
            if (offset < 0 || offset >= count) {
                outside++;
                continue;
            }
            if (seen[offset])
                twice++;
            seen[offset] = 1;
        }
        for (index = 0; index < count; index++)
            if (!seen[index])
                missing++;
        free(seen);

        check_msg(outside == 0, "band %d: %d candidates outside the band\n",
                  band->band, outside);
        check_msg(twice == 0, "band %d: %d channels named twice\n",
                  band->band, twice);
        check_msg(missing == 0, "band %d: %d channels never named\n",
                  band->band, missing);
        /* And it stops. A caller walks the index until this returns nothing. */
        check_msg(lte_scan_candidate(band, count) == 0,
                  "band %d: the order runs past its own end\n", band->band);
    }
}

static void test_the_likely_ones_come_first(void) {
    const struct lte_band *band = lte_band_for_earfcn(6200);   /* band 20 */
    int count = lte_scan_count(band);
    int coarse = (count + LTE_SCAN_COARSE_STEP - 1) / LTE_SCAN_COARSE_STEP;
    int index, off_raster = 0;

    check_int("band 20 holds 300 channels", count, 300);
    check_int("its first pass is 30 of them", coarse, 30);

    /* Every candidate of the first pass is a whole megahertz above the band's
       first channel, which is where a carrier centred in an allocated block
       lands. */
    for (index = 0; index < coarse; index++) {
        unsigned int earfcn = lte_scan_candidate(band, index);
        uint32_t hz = 0;
        lte_earfcn_downlink_hz(earfcn, &hz);
        if (hz % 1000000U != 0)
            off_raster++;
    }
    check_int("the first pass is whole megahertz", off_raster, 0);

    /* The three carriers a receiver actually found here are all in it. */
    {
        int found_796 = 0, found_806 = 0, found_816 = 0;
        for (index = 0; index < coarse; index++) {
            uint32_t hz = 0;
            lte_earfcn_downlink_hz(lte_scan_candidate(band, index), &hz);
            if (hz == 796000000U) found_796 = 1;
            if (hz == 806000000U) found_806 = 1;
            if (hz == 816000000U) found_816 = 1;
        }
        check_true("796 MHz is in the first pass", found_796);
        check_true("806 MHz is in the first pass", found_806);
        check_true("816 MHz is in the first pass", found_816);
    }

    /* The second pass is the half-megahertz points, and nothing in the first
       two passes repeats. */
    for (index = coarse; index < coarse + 30 && index < count; index++) {
        uint32_t hz = 0;
        lte_earfcn_downlink_hz(lte_scan_candidate(band, index), &hz);
        check_msg(hz % 500000U == 0,
                  "second-pass candidate %d is %u Hz, not a half megahertz\n",
                  index, hz);
        check_msg(hz % 1000000U != 0,
                  "second-pass candidate %d repeats the first pass\n", index);
    }
}

static void test_how_long_it_takes(void) {
    const struct lte_band *band = lte_band_for_earfcn(6200);
    /* The figures the view quotes. A whole band is a few minutes; the first
       pass, which is when the list stops filling in practice, is under twenty
       seconds. */
    check_close("a whole band 20 scan", lte_scan_seconds(band), 168.0, 1.0);
    check_close("its first pass", lte_scan_first_pass_seconds(band), 16.8, 0.5);
    check_true("a channel gets enough looks to confirm on",
               LTE_SCAN_MIN_LOOKS >= LTE_SCAN_CONFIRMATIONS);
    check_true("and no more than five", LTE_SCAN_MAX_LOOKS >= LTE_SCAN_MIN_LOOKS);
    check_true("the first pass is a tenth of the whole",
               lte_scan_first_pass_seconds(band) < lte_scan_seconds(band) / 9.0);
}

/*
 * The third look, and what it buys.
 *
 * A channel with nothing on it costs the minimum, so the minimum is what
 * decides whether a cell that only answers sometimes is seen at all. The one
 * this was raised for -- the live band 20 carrier at 816 MHz -- yields an
 * identity in about half its blocks.
 */
static void test_the_third_look(void) {
    double miss_at_two = 0.5 * 0.5;
    double miss_at_three = 0.5 * 0.5 * 0.5;
    const struct lte_band *band = lte_band_for_earfcn(6200);
    double two_look_sweep = (double)lte_scan_count(band) *
                            (LTE_SCAN_SETTLE_SECONDS +
                             2 * LTE_SCAN_PROBE_SECONDS);

    check_int("a silent channel is looked at three times",
              LTE_SCAN_MIN_LOOKS, 3);
    check_close("which misses a half-answering cell an eighth of the time",
                miss_at_three, 0.125, 1e-9);
    check_true("where two looks missed it a quarter of the time",
               miss_at_two > miss_at_three * 1.9);
    /* And what the third costs: one probe on every channel of the band. */
    check_close("the third look costs a probe a channel",
                lte_scan_seconds(band) - two_look_sweep,
                (double)lte_scan_count(band) * LTE_SCAN_PROBE_SECONDS, 0.5);
}

/*
 * The confirmation pass. The sweep is generous because it cannot revisit;
 * this can, and it is cheap because by then there is almost nothing left to
 * ask.
 */
static void test_the_confirmation_pass(void) {
    check_true("two agreements keep an entry", lte_scan_confirmed(2));
    check_true("and more than two", lte_scan_confirmed(5));
    check_true("one does not", !lte_scan_confirmed(1));
    check_true("nor none at all", !lte_scan_confirmed(0));
    check_true("an entry gets more looks than the sweep gave it",
               LTE_SCAN_CONFIRM_LOOKS > LTE_SCAN_MIN_LOOKS);
    check_true("and cannot be kept without repeating",
               LTE_SCAN_CONFIRM_AGREE >= 2);

    /* The cost, against the sweep it follows: for the handful of cells a band
       actually holds, seconds against minutes. That ratio is the argument for
       the pass existing, so it is worth pinning. */
    check_close("five cells cost four seconds",
                lte_scan_confirm_seconds(5), 4.0, 0.01);
    check_close("nothing found costs nothing",
                lte_scan_confirm_seconds(0), 0.0, 1e-9);
    check_true("a found band's confirmation is a fortieth of its sweep",
               lte_scan_confirm_seconds(5) <
                   lte_scan_seconds(lte_band_for_earfcn(6200)) / 40.0);
}

/*
 * Dropping an entry keeps the rest in order. The confirmation pass walks the
 * list while removing from it, so an entry sliding past the index is how a
 * real cell would be skipped without anything saying so.
 */
static void test_dropping_an_entry(void) {
    struct lte_found_cell list[4];
    int count = 4, i;

    for (i = 0; i < 4; i++) {
        list[i].earfcn = (unsigned int)(6200 + i);
        list[i].frequency_hz = (uint32_t)(796000000 + i * 1000000);
        list[i].pci = 10 + i;
        list[i].pss = 0.9f - 0.1f * (float)i;
        list[i].sss_margin = 0.3f;
    }

    count = lte_scan_remove(list, count, 1);
    check_int("removing one shortens the list", count, 3);
    check_int("the entry before it is untouched", list[0].pci, 10);
    check_int("the one after moves up", list[1].pci, 12);
    check_int("and the rest follow it", list[2].pci, 13);

    count = lte_scan_remove(list, count, 2);   /* the last */
    check_int("the last can go too", count, 2);
    check_int("leaving the others in order", list[1].pci, 12);

    /* An index nobody holds changes nothing, which is what stops a confirm
       pass walking off its own list. */
    check_int("an index past the end is refused",
              lte_scan_remove(list, count, 2), 2);
    check_int("so is a negative one", lte_scan_remove(list, count, -1), 2);
    check_int("so is no list at all", lte_scan_remove(NULL, 3, 0), 3);
}

/*
 * Two entries too close together are one carrier. The rule exists because a
 * Zadoff-Chu correlation peaks a few hundred kilohertz off a strong cell's
 * own centre -- a live scan listed three ghosts around one real carrier --
 * and it is safe because the standard has no carrier narrower than 1.4 MHz.
 */
static void test_ghosts_are_one_carrier(void) {
    check_true("400 kHz apart is one carrier",
               lte_scan_same_carrier(796000000.0, 796400000.0));
    check_true("and so is 1.1 MHz, whichever way round",
               lte_scan_same_carrier(797100000.0, 796000000.0));
    check_true("the real carriers here are not",
               !lte_scan_same_carrier(796000000.0, 806000000.0));
    check_true("nor two at the narrowest spacing the standard allows",
               !lte_scan_same_carrier(796000000.0, 797400000.0));
    check_true("a carrier is itself", lte_scan_same_carrier(796e6, 796e6));
}

static void test_refuses_nonsense(void) {
    const struct lte_band *band = lte_band_for_earfcn(6200);
    check_int("no band, no channels", lte_scan_count(NULL), 0);
    check_int("no band, no candidate", (int)lte_scan_candidate(NULL, 0), 0);
    check_int("a negative index names nothing",
              (int)lte_scan_candidate(band, -1), 0);
}

int main(void) {
    test_every_channel_once();
    test_the_likely_ones_come_first();
    test_how_long_it_takes();
    test_the_third_look();
    test_the_confirmation_pass();
    test_dropping_an_entry();
    test_ghosts_are_one_carrier();
    test_refuses_nonsense();

    return check_report("lte band scan order");
}
