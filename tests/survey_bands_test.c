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

#define TUNER_LOW 24000000.0
#define TUNER_HIGH 1766000000.0

static void test_only_what_the_tuner_reaches(void) {
    int count = survey_band_count(TUNER_LOW, TUNER_HIGH);
    int i, outside = 0;

    check_true("the list is not empty", count > 10);
    check_true("and is shorter than the whole table",
               count < band_plan_entry_count());

    for (i = 0; i < count; i++) {
        const struct band_plan_entry *entry =
            survey_band_at(i, TUNER_LOW, TUNER_HIGH);
        check_msg(entry != NULL, "entry %d of %d is missing\n", i, count);
        if (!entry)
            continue;
        if (entry->upper_hz <= TUNER_LOW || entry->lower_hz >= TUNER_HIGH)
            outside++;
    }
    check_int("nothing on it is out of the tuner's reach", outside, 0);

    /* Past the end names nothing rather than the first one again. */
    check_true("an index past the end is empty",
               survey_band_at(count, TUNER_LOW, TUNER_HIGH) == NULL);
    check_true("and a negative one",
               survey_band_at(-1, TUNER_LOW, TUNER_HIGH) == NULL);

    /*
     * The one that matters: nothing the receiver *can* reach is left off. A
     * list that quietly dropped band II would read as this receiver not
     * covering FM.
     */
    {
        int j, missing = 0;
        for (j = 0; j < band_plan_entry_count(); j++) {
            const struct band_plan_entry *entry = band_plan_entry_at(j);
            int found = 0;

            if (!survey_band_reachable(entry, TUNER_LOW, TUNER_HIGH))
                continue;
            for (i = 0; i < count; i++)
                if (survey_band_at(i, TUNER_LOW, TUNER_HIGH) == entry)
                    found = 1;
            if (!found)
                missing++;
        }
        check_int("every reachable allocation is offered", missing, 0);
    }

    /* A narrower receiver offers fewer, which is the only reason the tuner's
       limits are arguments rather than constants. */
    check_true("a narrower tuner offers fewer bands",
               survey_band_count(80000000.0, 120000000.0) < count);
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
