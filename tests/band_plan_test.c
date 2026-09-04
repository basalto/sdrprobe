#include "band_plan.h"
#include "band_plan_view.h"
#include "check.h"

#include <stdio.h>
#include <string.h>

/*
 * The band plan is a table, so most of what can go wrong with it is typing:
 * a transposed digit, an entry that swallows its neighbour, a range written
 * backwards. The structural checks below walk the whole table, so a bad edit
 * fails here rather than quietly shadowing an allocation.
 */

static void check_name(const char *what, double hz, const char *expected) {
    const struct band_plan_entry *entry = band_plan_lookup(hz);
    const char *got = entry ? entry->name : "(none)";
    check_msg(strcmp(got, expected) == 0,
              "%s (%.3f MHz): got \"%s\", expected \"%s\"\n", what, hz / 1e6,
              got, expected);
}

static void check_none(const char *what, double hz) {
    const struct band_plan_entry *entry = band_plan_lookup(hz);
    check_msg(!entry, "%s (%.3f MHz): got \"%s\", expected no entry\n", what,
              hz / 1e6, entry->name);
}

static void test_known_frequencies(void) {
    check_name("FM broadcast", 100100000.0, "FM broadcast");
    check_name("airband", 121500000.0, "VHF airband");
    check_name("ADS-B", 1090000000.0, "Mode S / ADS-B");
    check_name("GSM downlink", 943200000.0, "GSM 900 / LTE B8 downlink");
    check_name("GSM uplink", 890200000.0, "GSM 900 / LTE B8 uplink");
    check_name("DAB", 227360000.0, "VHF band III / DAB+");
    /* The 700 MHz band was cleared for mobile in Portugal in 2020, so UHF
       television stops at 694 rather than at the older 790. */
    check_name("UHF television", 650000000.0, "UHF television");
    check_name("cleared 700 MHz", 763000000.0, "LTE band 28 downlink");
    /* The guard between television and the 700 MHz band is named now rather
       than left blank. It is a real thing -- the APT700 arrangement leaves it
       deliberately empty -- and a survey saying "nothing is allocated here"
       is worth more than one saying "the table does not know". */
    check_name("the 694-703 guard", 700000000.0, "700 MHz guard band");
    check_name("the 700 MHz centre gap", 745000000.0, "700 MHz centre gap");
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

/*
 * The entries a decode view can be pointed at.
 *
 * FM used to be asserted as offering *no* decoder, because for a long time
 * there was none -- and that assertion went on being true for a while after
 * there was one, which is exactly the failure it was written to prevent in
 * the other direction.
 */
static void test_decoders(void) {
    const struct band_plan_entry *gsm = band_plan_lookup(943200000.0);
    const struct band_plan_entry *adsb = band_plan_lookup(1090000000.0);
    const struct band_plan_entry *fm = band_plan_lookup(100100000.0);

    check_msg(gsm && gsm->decoder == BAND_PLAN_GSM,
              "GSM 900 downlink does not offer the GSM decoder\n");
    check_msg(adsb && adsb->decoder == BAND_PLAN_ADSB,
              "1090 MHz does not offer the ADS-B decoder\n");
    check_msg(fm && fm->decoder == BAND_PLAN_FM,
              "FM broadcast does not offer the FM decoder\n");
}

static void test_table_is_well_formed(void) {
    int count = band_plan_entry_count();
    const struct band_plan_entry *previous = NULL;

    check_msg(count >= 10, "band plan has only %d entries\n", count);
    for (int i = 0; i < count; i++) {
        const struct band_plan_entry *entry = band_plan_entry_at(i);
        check_msg(entry->name && entry->name[0], "entry %d has no name\n", i);
        if (!entry->name || !entry->name[0])
            continue;
        check_msg(entry->lower_hz < entry->upper_hz,
                  "%s: range is not ascending\n", entry->name);
        check_msg(!previous || entry->lower_hz >= previous->upper_hz,
                  "%s overlaps %s\n", entry->name, previous->name);
        /* Every entry must be findable at its own midpoint: the check that
           catches an entry shadowed by a wider one written earlier. */
        double middle = (entry->lower_hz + entry->upper_hz) / 2.0;
        const struct band_plan_entry *found = band_plan_lookup(middle);
        check_msg(found == entry, "%s is not reachable at its own midpoint\n",
                  entry->name);
        previous = entry;
    }
    check_msg(!band_plan_entry_at(-1) && !band_plan_entry_at(count),
              "band_plan_entry_at accepted an out-of-range index\n");
}


/*
 * Every decoder an allocation names is one this program has.
 *
 * The table points a reader at somewhere to go next, and a row naming a view
 * that does not exist is worse than a row naming none -- it offers a button
 * that cannot work. It is also the other way round: FM broadcast sat here
 * with BAND_PLAN_NONE for as long as there was no FM view, and went on
 * saying so after there was, so the survey could find a station, name it, and
 * offer nothing.
 */
static void test_the_decoders_are_ones_we_have(void) {
    int i, count = band_plan_entry_count();
    int named[BAND_PLAN_DECODER_COUNT];
    int d;

    for (d = 0; d < BAND_PLAN_DECODER_COUNT; d++)
        named[d] = 0;
    for (i = 0; i < count; i++) {
        const struct band_plan_entry *entry = band_plan_entry_at(i);
        check_msg(entry->decoder >= 0 &&
                  entry->decoder < BAND_PLAN_DECODER_COUNT,
                  "'%s' names decoder %d, which is not one of them\n",
                  entry->name, (int)entry->decoder);
        if (entry->decoder >= 0 && entry->decoder < BAND_PLAN_DECODER_COUNT)
            named[entry->decoder]++;
    }
    /* And every decoder this program has is reachable from somewhere in the
       band plan, or the survey can never offer it. */
    check_true("some allocation points at GSM", named[BAND_PLAN_GSM] > 0);
    check_true("and at ADS-B", named[BAND_PLAN_ADSB] > 0);
    check_true("and at LTE", named[BAND_PLAN_LTE] > 0);
    check_true("and at FM", named[BAND_PLAN_FM] > 0);
}

/* The frequencies this program is actually pointed at land on the right one. */
static void test_where_the_decoders_are(void) {
    struct { double hz; enum band_plan_decoder want; const char *what; } cases[] = {
        {   94400000.0, BAND_PLAN_FM,   "a band II station" },
        {   89500000.0, BAND_PLAN_FM,   "another" },
        {  107900000.0, BAND_PLAN_FM,   "the top of the band" },
        {   87400000.0, BAND_PLAN_NONE, "just below it" },
        {  108100000.0, BAND_PLAN_NONE, "just above it" },
        {  948400000.0, BAND_PLAN_GSM,  "a GSM downlink" },
        { 1090000000.0, BAND_PLAN_ADSB, "Mode S" },
        {  806000000.0, BAND_PLAN_LTE,  "an LTE band 20 carrier" }
    };
    unsigned c;

    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        const struct band_plan_entry *entry = band_plan_lookup(cases[c].hz);
        enum band_plan_decoder got = entry ? entry->decoder : BAND_PLAN_NONE;
        check_msg(got == cases[c].want,
                  "%s at %.3f MHz maps to decoder %d, expected %d\n",
                  cases[c].what, cases[c].hz / 1e6, (int)got,
                  (int)cases[c].want);
    }
}


/*
 * Where Inspect sends a reader, and that it sends them somewhere for every
 * technology this program has.
 *
 * The click handler used to name GSM and ADS-B and fall off the end for the
 * other two, so an LTE carrier offered no button and an FM station offered
 * none -- and nothing could see a case was simply missing, because a missing
 * case is not a wrong value. Here it is a table, and a decoder with no label
 * is a decoder Inspect cannot reach.
 */
static void test_inspect_reaches_every_decoder(void) {
    check_str("GSM", band_plan_inspect_label(BAND_PLAN_GSM),
              "Inspect in Decode > GSM");
    check_str("ADS-B", band_plan_inspect_label(BAND_PLAN_ADSB),
              "Inspect in Decode > ADS-B");
    check_str("LTE", band_plan_inspect_label(BAND_PLAN_LTE),
              "Inspect in Decode > LTE");
    check_str("FM, which listens rather than inspects",
              band_plan_inspect_label(BAND_PLAN_FM), "Listen in Decode > FM");
    check_true("an allocation with no decoder offers nothing",
               band_plan_inspect_label(BAND_PLAN_NONE) == NULL);
    check_true("and so cannot be inspected",
               !band_plan_can_inspect(BAND_PLAN_NONE));

    /*
     * The property: every decoder the band plan can name is one Inspect can
     * reach. Adding a technology to the enum and forgetting this table is
     * exactly how the last two were missed.
     */
    {
        int d, unreachable = 0;
        for (d = 1; d < BAND_PLAN_DECODER_COUNT; d++)
            if (!band_plan_can_inspect((enum band_plan_decoder)d))
                unreachable++;
        check_int("every decoder has somewhere to go", unreachable, 0);
    }
}

int main(void) {
    test_known_frequencies();
    test_gaps();
    test_decoders();
    test_table_is_well_formed();

    test_the_decoders_are_ones_we_have();
    test_where_the_decoders_are();
    test_inspect_reaches_every_decoder();

    return check_report("band plan");
}
