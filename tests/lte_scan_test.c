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
    /* The figures the view quotes. A whole band is a minute and a bit; the
       first pass, which is when the list stops filling in practice, is under
       ten seconds. */
    check_close("a whole band 20 scan", lte_scan_seconds(band), 132.0, 1.0);
    check_close("its first pass", lte_scan_first_pass_seconds(band), 13.2, 0.5);
    /* Confirming costs nothing on an empty channel and everything on a
       promising one, which is the only place it decides anything. */
    check_true("a channel gets at least two looks",
               LTE_SCAN_MIN_LOOKS >= LTE_SCAN_CONFIRMATIONS);
    check_true("and no more than five", LTE_SCAN_MAX_LOOKS >= LTE_SCAN_MIN_LOOKS);
    check_true("the first pass is a tenth of the whole",
               lte_scan_first_pass_seconds(band) < lte_scan_seconds(band) / 9.0);
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
    test_refuses_nonsense();

    return check_report("lte band scan order");
}
