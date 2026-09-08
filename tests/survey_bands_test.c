#include "check.h"

#include "survey_bands.h"

#include <math.h>
#include <string.h>

/*
 * Which allocations the survey offers to sweep, and what each one means.
 *
 * The point of the list is that a reader should not have to know a band's
 * edges to sweep it. The point of the checks is that every entry on the list
 * has to be a sweep the receiver can actually perform: offering one it cannot
 * tune is offering a button that does nothing, which is the same objection
 * the Inspect button's table answers.
 */

#include "device_profile.h"

/*
 * Two receivers, and the reason this is a property rather than a fact about
 * one device (ticket 05 in .scratch/device-model/).
 *
 * An RTL-SDR reaches 24 MHz to 1766. A B210-class part reaches 70 MHz to
 * 6 GHz, and **it is not a superset**: it opens 2.4 GHz ISM, n78 and the
 * 5 GHz RLAN bands, and it loses everything below about 70 MHz -- short wave,
 * CB, the 6 m and 10 m amateur bands, band I television. A check written
 * against one device's numbers would pass while the list offered half of one
 * and missed half of the other.
 */
#define TUNER_LOW 24000000.0
#define TUNER_HIGH 1766000000.0
#define WIDE_LOW 70000000.0
#define WIDE_HIGH 6000000000.0

/*
 * Both directions, for whatever reach it is handed: nothing offered is out of
 * range, and nothing in range is left off. The second half is the one that
 * matters -- a list that quietly dropped band II would read as the receiver
 * not covering FM.
 */
static void check_both_directions(const char *who, double low, double high,
                                  int least) {
    int count = survey_band_count(low, high);
    int i, j, outside = 0, missing = 0;
    char name[96];

    /* `least` is per-reach and not a constant: a receiver that sees only
       80-120 MHz has four allocations in front of it, and asserting ten of
       everything would be a claim about the table rather than the filter. */
    snprintf(name, sizeof(name), "%s: the list is not empty", who);
    check_true(name, count >= least);
    snprintf(name, sizeof(name), "%s: and shorter than the whole table", who);
    check_true(name, count < band_plan_entry_count());

    for (i = 0; i < count; i++) {
        const struct band_plan_entry *entry = survey_band_at(i, low, high);
        check_msg(entry != NULL, "%s: entry %d of %d is missing\n", who, i,
                  count);
        if (!entry)
            continue;
        if (entry->upper_hz <= low || entry->lower_hz >= high)
            outside++;
    }
    snprintf(name, sizeof(name), "%s: nothing offered is out of reach", who);
    check_int(name, outside, 0);

    for (j = 0; j < band_plan_entry_count(); j++) {
        const struct band_plan_entry *entry = band_plan_entry_at(j);
        int found = 0;

        if (!survey_band_reachable(entry, low, high))
            continue;
        for (i = 0; i < count; i++)
            if (survey_band_at(i, low, high) == entry)
                found = 1;
        if (!found)
            missing++;
    }
    snprintf(name, sizeof(name), "%s: every reachable allocation is offered",
             who);
    check_int(name, missing, 0);

    /* Past the end names nothing rather than the first one again. */
    snprintf(name, sizeof(name), "%s: an index past the end is empty", who);
    check_true(name, survey_band_at(count, low, high) == NULL);
    snprintf(name, sizeof(name), "%s: and a negative one", who);
    check_true(name, survey_band_at(-1, low, high) == NULL);
}

static void test_only_what_the_tuner_reaches(void) {
    check_both_directions("RTL-SDR", TUNER_LOW, TUNER_HIGH, 10);
    check_both_directions("wideband", WIDE_LOW, WIDE_HIGH, 10);
    check_both_directions("FM only", 80000000.0, 120000000.0, 2);

    /* And the reach comes from a profile, not from constants here. */
    struct device_profile rtl = device_profile_rtlsdr(NULL, NULL, 0);
    check_close("the RTL profile's lower reach", rtl.tune_lower_hz, TUNER_LOW,
                0.5);
    check_close("and its upper", rtl.tune_upper_hz, TUNER_HIGH, 0.5);
    check_int("which is the list the profile gives",
              survey_band_count(rtl.tune_lower_hz, rtl.tune_upper_hz),
              survey_band_count(TUNER_LOW, TUNER_HIGH));

    /*
     * The asymmetry, asserted rather than assumed. This is the whole reason
     * ticket 05 exists: a wider device is not a bigger version of this one.
     */
    int narrow = survey_band_count(TUNER_LOW, TUNER_HIGH);
    int wide = survey_band_count(WIDE_LOW, WIDE_HIGH);
    check_true("a narrower tuner offers fewer bands",
               survey_band_count(80000000.0, 120000000.0) < narrow);

    check_true("the wideband part reaches 2.4 GHz ISM, which the RTL cannot",
               survey_band_reachable(band_plan_lookup(2437000000.0),
                                     WIDE_LOW, WIDE_HIGH) &&
               !survey_band_reachable(band_plan_lookup(2437000000.0),
                                      TUNER_LOW, TUNER_HIGH));
    check_true("and n78",
               survey_band_reachable(band_plan_lookup(3600000000.0),
                                     WIDE_LOW, WIDE_HIGH) &&
               !survey_band_reachable(band_plan_lookup(3600000000.0),
                                      TUNER_LOW, TUNER_HIGH));
    check_true("and it loses CB at 27 MHz, which the RTL reaches",
               !survey_band_reachable(band_plan_lookup(27185000.0),
                                      WIDE_LOW, WIDE_HIGH) &&
               survey_band_reachable(band_plan_lookup(27185000.0),
                                     TUNER_LOW, TUNER_HIGH));
    check_true("so neither list contains the other",
               wide != narrow);

    /* Both reach the 75 MHz clock harmonic carrier_75000_bare.bin holds --
       the fundamental at 25 MHz is RTL-only, its third harmonic is not. */
    check_true("both reach the ILS marker band at 75 MHz",
               survey_band_reachable(band_plan_lookup(75000500.0),
                                     WIDE_LOW, WIDE_HIGH) &&
               survey_band_reachable(band_plan_lookup(75000500.0),
                                     TUNER_LOW, TUNER_HIGH));
}

/* The bands this program spends its time on are all on the list. */
static void test_the_ones_that_matter_are_there(void) {
    struct { double hz; const char *what; } wanted[] = {
        {   94400000.0, "FM broadcast" },
        {  948400000.0, "the GSM 900 downlink" },
        { 1090000000.0, "Mode S" },
        {  806000000.0, "LTE band 20" }
    };
    int count = survey_band_count(TUNER_LOW, TUNER_HIGH);
    unsigned w;

    for (w = 0; w < sizeof(wanted) / sizeof(wanted[0]); w++) {
        const struct band_plan_entry *want = band_plan_lookup(wanted[w].hz);
        int i, found = 0;

        check_msg(want != NULL, "%s is not in the band plan\n",
                  wanted[w].what);
        for (i = 0; i < count && want; i++)
            if (survey_band_at(i, TUNER_LOW, TUNER_HIGH) == want)
                found = 1;
        check_msg(found, "%s cannot be chosen from the list\n",
                  wanted[w].what);
    }
}

static void test_the_range_a_band_means(void) {
    const struct band_plan_entry *fm = band_plan_lookup(94400000.0);
    double from = 0.0, to = 0.0;

    check_int("FM broadcast has a range",
              survey_band_range(fm, TUNER_LOW, TUNER_HIGH, &from, &to), 0);
    /* Its own edges with a little air, so a carrier at the very bottom of the
       band is measured rather than sitting on the shoulder of the sweep. */
    check_close("starting just below the band", from,
                87500000.0 - SURVEY_BAND_MARGIN_HZ, 1.0);
    check_close("and ending just above it", to,
                108000000.0 + SURVEY_BAND_MARGIN_HZ, 1.0);

    /*
     * Clipped to the tuner rather than refused. Plenty of allocations run off
     * the end of what the receiver reaches, and sweeping the part it can is
     * the right answer.
     */
    {
        const struct band_plan_entry *entry;
        int i, clipped = 0;
        int count = survey_band_count(TUNER_LOW, TUNER_HIGH);

        for (i = 0; i < count; i++) {
            entry = survey_band_at(i, TUNER_LOW, TUNER_HIGH);
            if (survey_band_range(entry, TUNER_LOW, TUNER_HIGH, &from,
                                  &to) != 0)
                continue;
            check_msg(from >= TUNER_LOW - 0.5 && to <= TUNER_HIGH + 0.5,
                      "'%s' sweeps %.3f to %.3f MHz, outside the tuner\n",
                      entry->name, from / 1e6, to / 1e6);
            check_msg(to > from, "'%s' sweeps backwards\n", entry->name);
            if (from <= TUNER_LOW + 0.5 || to >= TUNER_HIGH - 0.5)
                clipped++;
        }
        check_true("and some of them are clipped", clipped > 0);
    }

    check_int("no entry, no range",
              survey_band_range(NULL, TUNER_LOW, TUNER_HIGH, &from, &to), -1);
    check_int("nor nowhere to put it",
              survey_band_range(fm, TUNER_LOW, TUNER_HIGH, NULL, &to), -1);
    /* An allocation entirely outside the tuner has no range to sweep. */
    check_int("nor a band the receiver cannot reach",
              survey_band_range(fm, 200000000.0, 300000000.0, &from, &to), -1);
}

/*
 * The dwell that comes with a band.
 *
 * One dwell does not suit a band of half a megahertz and one of twenty: the
 * tenth of a second that makes a whole-tuner sweep bearable wastes a narrow
 * band, and the second that suits a narrow band makes a wide one a quarter of
 * an hour.
 */
static void test_the_dwell_follows_the_width(void) {
    double narrow = survey_band_dwell(144000000.0, 146000000.0, 2000000.0);
    double medium = survey_band_dwell(87500000.0, 108000000.0, 2000000.0);
    double broad = survey_band_dwell(470000000.0, 694000000.0, 2000000.0);
    double huge = survey_band_dwell(24000000.0, 1766000000.0, 2000000.0);

    /*
     * Everything up to about fifty megahertz gets the same generous dwell and
     * still finishes inside the budget -- half a second is eight blocks and
     * another half buys very little. That flat region is deliberate, and
     * comparing two bands inside it was this check's own first mistake: 2 MHz
     * and 20 MHz both get the maximum, so "narrower means longer" was false
     * for a reason that was not a fault.
     */
    check_close("a narrow band gets the full dwell", narrow,
                SURVEY_BAND_MAX_DWELL, 1e-9);
    check_close("and so does a twenty-megahertz one", medium,
                SURVEY_BAND_MAX_DWELL, 1e-9);
    check_true("a band of two hundred megahertz gets less", broad < medium);
    /*
     * And there is a floor as well as a ceiling, which the two widest cases
     * both sit on: at a tenth of a second a step is already mostly the
     * tuner settling, and going below it would buy time by measuring less
     * than the default whole-tuner sweep does.
     */
    check_close("a very wide band sits on the floor", broad,
                SURVEY_DWELL_DEFAULT, 1e-9);
    check_close("and so does the whole tuner", huge, SURVEY_DWELL_DEFAULT,
                1e-9);
    check_true("which is never gone under", huge >= SURVEY_DWELL_DEFAULT);
    check_true("and never over the maximum",
               narrow <= SURVEY_BAND_MAX_DWELL);

    /*
     * The property: whatever band is chosen, the sweep it implies is worth
     * waiting for. Under the target where the dwell had room to shrink, and
     * never more than a few minutes where it did not.
     */
    {
        int i, count = survey_band_count(TUNER_LOW, TUNER_HIGH);
        for (i = 0; i < count; i++) {
            const struct band_plan_entry *entry =
                survey_band_at(i, TUNER_LOW, TUNER_HIGH);
            double from, to, dwell, steps, seconds;

            if (survey_band_range(entry, TUNER_LOW, TUNER_HIGH, &from,
                                  &to) != 0)
                continue;
            dwell = survey_band_dwell(from, to, 2000000.0);
            steps = ceil((to - from) / (2000000.0 * SURVEY_USABLE_SPAN));
            seconds = steps * (SURVEY_SETTLE_SECONDS + dwell);
            check_msg(seconds < 180.0,
                      "'%s' would sweep for %.0f s\n", entry->name, seconds);
        }
    }

    check_close("nonsense gives the default",
                survey_band_dwell(100.0, 100.0, 2000000.0),
                SURVEY_DWELL_DEFAULT, 1e-9);
}

int main(void) {
    test_only_what_the_tuner_reaches();
    test_the_ones_that_matter_are_there();
    test_the_range_a_band_means();
    test_the_dwell_follows_the_width();

    return check_report("the survey's band list");
}
