#include "band_plan.h"

#include <stdio.h>
#include <string.h>

/*
 * The band plan is a table, so most of what can go wrong with it is typing:
 * a transposed digit, an entry that swallows its neighbour, a range written
 * backwards. The structural checks below walk the whole table, so a bad edit
 * fails here rather than quietly shadowing an allocation.
 */

static int failures;

static void check_name(const char *what, double hz, const char *expected) {
    const struct band_plan_entry *entry = band_plan_lookup(hz);
    const char *got = entry ? entry->name : "(none)";
    if (strcmp(got, expected) != 0) {
        fprintf(stderr, "%s (%.3f MHz): got \"%s\", expected \"%s\"\n",
                what, hz / 1e6, got, expected);
        failures++;
    }
}

static void check_none(const char *what, double hz) {
    const struct band_plan_entry *entry = band_plan_lookup(hz);
    if (entry) {
        fprintf(stderr, "%s (%.3f MHz): got \"%s\", expected no entry\n",
                what, hz / 1e6, entry->name);
        failures++;
    }
}

static void test_known_frequencies(void) {
    check_name("FM broadcast", 100100000.0, "FM broadcast");
    check_name("airband", 121500000.0, "VHF airband");
    check_name("ADS-B", 1090000000.0, "Mode S / ADS-B");
    check_name("GSM downlink", 943200000.0, "GSM 900 downlink");
    check_name("GSM uplink", 890200000.0, "GSM 900 uplink");
    check_name("DAB", 227360000.0, "VHF band III");
    check_name("PMR446", 446100000.0, "PMR446");
}

/* A frequency the table says nothing about must come back empty rather than
   attach itself to the nearest neighbour. */
static void test_gaps(void) {
    check_none("between airband and 2 m", 140000000.0);
    check_none("between GSM up and down", 920000000.0);
    check_none("above the tuner", 2000000000.0);
    check_none("below everything", 50000.0);
}

/* The two entries a decode view can be pointed at, and only those. */
static void test_decoders(void) {
    const struct band_plan_entry *gsm = band_plan_lookup(943200000.0);
    const struct band_plan_entry *adsb = band_plan_lookup(1090000000.0);
    const struct band_plan_entry *fm = band_plan_lookup(100100000.0);

    if (!gsm || gsm->decoder != BAND_PLAN_GSM) {
        fprintf(stderr, "GSM 900 downlink does not offer the GSM decoder\n");
        failures++;
    }
    if (!adsb || adsb->decoder != BAND_PLAN_ADSB) {
        fprintf(stderr, "1090 MHz does not offer the ADS-B decoder\n");
        failures++;
    }
    if (!fm || fm->decoder != BAND_PLAN_NONE) {
        fprintf(stderr, "FM broadcast claims a decoder this program lacks\n");
        failures++;
    }
}

static void test_table_is_well_formed(void) {
    int count = band_plan_entry_count();
    const struct band_plan_entry *previous = NULL;

    if (count < 10) {
        fprintf(stderr, "band plan has only %d entries\n", count);
        failures++;
    }
    for (int i = 0; i < count; i++) {
        const struct band_plan_entry *entry = band_plan_entry_at(i);
        if (!entry->name || !entry->name[0]) {
            fprintf(stderr, "entry %d has no name\n", i);
            failures++;
            continue;
        }
        if (!(entry->lower_hz < entry->upper_hz)) {
            fprintf(stderr, "%s: range is not ascending\n", entry->name);
            failures++;
        }
        if (previous && entry->lower_hz < previous->upper_hz) {
            fprintf(stderr, "%s overlaps %s\n", entry->name, previous->name);
            failures++;
        }
        /* Every entry must be findable at its own midpoint: the check that
           catches an entry shadowed by a wider one written earlier. */
        double middle = (entry->lower_hz + entry->upper_hz) / 2.0;
        const struct band_plan_entry *found = band_plan_lookup(middle);
        if (found != entry) {
            fprintf(stderr, "%s is not reachable at its own midpoint\n",
                    entry->name);
            failures++;
        }
        previous = entry;
    }
    if (band_plan_entry_at(-1) || band_plan_entry_at(count)) {
        fprintf(stderr, "band_plan_entry_at accepted an out-of-range index\n");
        failures++;
    }
}

int main(void) {
    test_known_frequencies();
    test_gaps();
    test_decoders();
    test_table_is_well_formed();

    if (failures) {
        fprintf(stderr, "%d band_plan check(s) failed\n", failures);
        return 1;
    }
    puts("band_plan checks passed");
    return 0;
}
