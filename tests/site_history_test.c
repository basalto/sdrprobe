#include "check.h"

#include "site_history.h"

#include <stdio.h>
#include <string.h>

/*
 * What a site remembers, and how a sweep is judged against it.
 *
 * Text in, text out, plus arithmetic over arrays. No receiver, no window and
 * no directory, which is what lets the window's "this is new here" claim be
 * checked at all -- on screen it is a coloured tick nobody can argue with.
 */

static void test_a_new_site_knows_nothing(void) {
    struct site_history h;
    site_history_init(&h, "home-desk");
    check_int("no sweeps", h.sweeps, 0);
    check_int("no entries", h.count, 0);
    /* Not NEW: with no history, "new" would be a claim about a place we have
       never listened to. Everything is unknown until something is known. */
    check_int("and nothing is called new yet",
              (int)site_history_status(&h, 94e6, 2000.0),
              (int)SITE_STATUS_UNKNOWN);
}

static void test_first_sweep_then_second(void) {
    struct site_history h;
    double first[] = { 94.5e6, 97.5e6 };
    double second[] = { 94.5e6, 100.1e6 };
    float level[] = { -10.0f, -20.0f };
    int added;

    site_history_init(&h, "home-desk");
    added = site_history_merge(&h, first, level, level, 2, 2000.0);
    check_int("a first sweep is all new", added, 2);
    check_int("one sweep recorded", h.sweeps, 1);

    check_int("something it heard is known",
              (int)site_history_status(&h, 94.5e6, 2000.0),
              (int)SITE_STATUS_KNOWN);
    check_int("something it did not is new",
              (int)site_history_status(&h, 100.1e6, 2000.0),
              (int)SITE_STATUS_NEW);

    added = site_history_merge(&h, second, level, level, 2, 2000.0);
    check_int("the second sweep adds only what is new", added, 1);
    check_int("two sweeps now", h.sweeps, 2);
    check_int("three signals known between them", h.count, 3);

    {
        const struct site_entry *e = site_history_find(&h, 94.5e6, 2000.0);
        check_true("the one heard twice", e != NULL);
        check_int("counts twice", e->sweeps, 2);
        check_int("and was heard in the latest", e->last_sweep, 2);
        e = site_history_find(&h, 97.5e6, 2000.0);
        check_int("the one heard once counts once", e->sweeps, 1);
        check_int("and stopped at the first sweep", e->last_sweep, 1);
    }
}

static void test_missing_only_where_we_looked(void) {
    struct site_history h;
    double first[] = { 94.5e6, 300.0e6 };
    double narrow[] = { 94.5e6 };
    const struct site_entry *gone[8];
    int count;

    site_history_init(&h, "home-desk");
    site_history_merge(&h, first, NULL, NULL, 2, 2000.0);
    site_history_merge(&h, narrow, NULL, NULL, 1, 2000.0);

    /* A sweep of 88-108 MHz says nothing whatever about 300 MHz, and calling
       it missing would be a claim about spectrum nobody looked at. */
    count = site_history_missing(&h, narrow, 1, 88e6, 108e6, 2000.0, gone, 8);
    check_int("nothing is missing inside the range swept", count, 0);

    /* Widen the range and the one that really did go quiet shows up. */
    count = site_history_missing(&h, narrow, 1, 24e6, 1766e6, 2000.0, gone, 8);
    check_int("but it is missing across a range that covered it", count, 1);
    check_close("and it is the right one", gone[0]->hz, 300.0e6, 1.0);
}

static void test_a_coarse_memory_still_matches_a_fine_sweep(void) {
    struct site_history h;
    /* The whole tuner bins at 212 kHz, so it places a station only that well.
       A later sweep of one band bins at 2 kHz and places the same station
       97 kHz away -- the same signal, measured better. Matching at the fine
       sweep's tolerance calls it new *and* the old entry missing, which is
       what the first live run did. */
    double coarse[] = { 94.492e6 };
    double fine[] = { 94.395e6 };
    const struct site_entry *gone[4];

    site_history_init(&h, "home-desk");
    site_history_merge(&h, coarse, NULL, NULL, 1, 212646.5);
    check_int("the coarse sweep records where it thinks it heard it",
              h.count, 1);

    check_int("a finer sweep still recognises it",
              (int)site_history_status(&h, fine[0], 2441.4),
              (int)SITE_STATUS_KNOWN);
    check_int("and does not call the old entry missing",
              site_history_missing(&h, fine, 1, 88e6, 108e6, 2441.4, gone, 4),
              0);

    site_history_merge(&h, fine, NULL, NULL, 1, 2441.4);
    check_int("merging keeps one entry, not two", h.count, 1);
    check_close("and takes the better placement",
                site_history_find(&h, fine[0], 2441.4)->hz, 94.395e6, 1.0);

    /* Which then holds the fine sweep to a fine tolerance, as it should. */
    check_int("a genuinely different station nearby is still new",
              (int)site_history_status(&h, 94.700e6, 2441.4),
              (int)SITE_STATUS_NEW);
}

static void test_tolerance_scales_with_the_sweep(void) {
    /* A sweep of the whole tuner bins at 200 kHz and one band at 2 kHz. A
       fixed tolerance would merge adjacent FM stations in the narrow case or
       match nothing in the wide one. */
    check_close("a coarse sweep matches loosely",
                site_match_tolerance(212646.5), 425293.0, 1.0);
    check_close("a fine one matches tightly, down to the floor",
                site_match_tolerance(2441.4), SITE_MATCH_FLOOR_HZ, 1.0);
    check_close("and two resolutions match at the coarser",
                site_match_tolerance_for(2441.4, 212646.5),
                site_match_tolerance(212646.5), 1.0);
    check_true("and never demands impossible precision",
               site_match_tolerance(1.0) >= SITE_MATCH_FLOOR_HZ);
}

static void test_one_wide_carrier_counts_once(void) {
    struct site_history h;
    /* Two peaks inside one carrier, both inside the tolerance. Counting the
       entry twice for one sweep would make it look more reliable than it is. */
    double pair[] = { 100.00e6, 100.01e6 };
    const struct site_entry *e;

    site_history_init(&h, "home-desk");
    site_history_merge(&h, pair, NULL, NULL, 2, 2000.0);
    check_int("two peaks of one carrier make one entry", h.count, 1);
    e = site_history_find(&h, 100.0e6, 2000.0);
    check_int("counted once for the sweep", e->sweeps, 1);
}

static void test_round_trip(void) {
    struct site_history written, read;
    double hz[] = { 94.5e6, 97.5e6 };
    float level[] = { -7.7f, -16.1f };
    char text[4096];

    site_history_init(&written, "home-desk");
    site_history_merge(&written, hz, level, level, 2, 2000.0);
    check_true("formats", site_history_format(&written, text,
                                              sizeof(text)) > 0);
    check_int("reads back the same entries",
              site_history_parse(text, &read), 2);
    check_int("and the same sweep count", read.sweeps, written.sweeps);
    check_close("with levels intact",
                (double)site_history_find(&read, 94.5e6, 2000.0)->dbfs,
                -7.7, 0.05);
    check_str("and the site's name", read.site, "home-desk");
}

static void test_the_filename_is_a_filename(void) {
    char path[256];
    check_int("a plain name", site_history_path("home-desk", path,
                                                sizeof(path)), 0);
    check_str("becomes a path", path, "surveys/history-home-desk.txt");
    /* A person types what they like into that field, and a slash in it would
       write outside surveys/ or fail. */
    site_history_path("lisbon/office 2", path, sizeof(path));
    check_str("anything awkward is replaced", path,
              "surveys/history-lisbon-office-2.txt");
    check_int("and no site is no path",
              site_history_path("", path, sizeof(path)), -1);
}

int main(void) {
    test_a_new_site_knows_nothing();
    test_first_sweep_then_second();
    test_missing_only_where_we_looked();
    test_a_coarse_memory_still_matches_a_fine_sweep();
    test_tolerance_scales_with_the_sweep();
    test_one_wide_carrier_counts_once();
    test_round_trip();
    test_the_filename_is_a_filename();

    return check_report("what a site remembers");
}
