#include "check.h"

#include "config.h"

#include <string.h>

/*
 * The persisted installation: an antenna and a site.
 *
 * All of it is text in, text out. The one part that touches the environment
 * is config_path, and that is a snprintf over getenv -- everything that
 * decides anything is reachable here without a home directory to write into
 * (ADR-0012).
 */

static void test_defaults(void) {
    struct config config;
    config_defaults(&config);
    check_str("an antenna nobody has named", config.antenna,
              CONFIG_ANTENNA_DEFAULT);
    check_str("which is the whip a dongle ships with", config.antenna,
              "telescopic");
    /* And it is already in the list, so the picker is never empty. */
    check_int("the default is a known antenna", config.antenna_count, 1);
    check_str("namely itself", config.antennas[0], CONFIG_ANTENNA_DEFAULT);
    /* And no site, deliberately. A default would be a guess, and two sweeps
       sharing a guessed label would compare as the same place -- which is the
       one mistake the site exists to prevent. */
    check_int("but no site", (int)strlen(config.site), 0);
}

static void test_reads_a_file(void) {
    struct config config;
    int found = config_parse("# a comment\n"
                             "antenna discone, roof\n"
                             "\n"
                             "site lisbon-office\n", &config);
    check_int("two settings recognised", found, 2);
    check_str("the antenna", config.antenna, "discone, roof");
    check_str("the site", config.site, "lisbon-office");
    /* The value is the rest of the line, so a name may contain spaces and
       commas -- which every real antenna description does. */
}

static void test_absent_keys_keep_their_defaults(void) {
    struct config config;
    config_parse("site out-back\n", &config);
    check_str("an unmentioned antenna is the default", config.antenna,
              CONFIG_ANTENNA_DEFAULT);
    check_str("the mentioned one is read", config.site, "out-back");
}

static void test_unknown_keys_survive(void) {
    struct config config;
    char text[1024];
    config_parse("antenna whip\nfuture_setting 42\n", &config);
    check_int("the unknown one is kept", config.unknown_count, 1);
    check_true("and written back",
               config_format(&config, text, sizeof(text)) > 0 &&
               strstr(text, "future_setting 42") != NULL);
    /* An older binary must not silently discard a newer one's settings. */
}

static void test_round_trip(void) {
    struct config written, read;
    char text[1024];
    config_defaults(&written);
    snprintf(written.antenna, sizeof(written.antenna), "%s", "log-periodic");
    snprintf(written.site, sizeof(written.site), "%s", "roof-mast");
    check_true("formats", config_format(&written, text, sizeof(text)) > 0);
    config_parse(text, &read);
    check_str("antenna survives", read.antenna, written.antenna);
    check_str("site survives", read.site, written.site);
}

static void test_an_unset_site_is_not_written(void) {
    struct config config;
    char text[1024];
    config_defaults(&config);
    check_true("formats", config_format(&config, text, sizeof(text)) > 0);
    check_true("and says nothing about a site nobody set",
               strstr(text, "site ") == NULL);
}

static void test_refuses_what_will_not_fit(void) {
    struct config config;
    char tiny[8];
    config_defaults(&config);
    check_int("a buffer too small is refused, not overrun",
              config_format(&config, tiny, sizeof(tiny)), -1);
    check_int("and so is no buffer at all",
              config_format(&config, NULL, 100), -1);
    check_int("parsing nothing recognises nothing",
              config_parse(NULL, &config), 0);
}

static void test_a_long_value_is_cut_not_overrun(void) {
    struct config config;
    char line[CONFIG_VALUE_MAX * 3];
    size_t i;
    memcpy(line, "antenna ", 8);
    for (i = 8; i < sizeof(line) - 1; i++)
        line[i] = 'x';
    line[sizeof(line) - 1] = '\0';
    config_parse(line, &config);
    check_true("the value is truncated inside its buffer",
               strlen(config.antenna) < CONFIG_VALUE_MAX);
}

/*
 * The list of places this receiver has been, which the survey window offers
 * instead of asking for the spelling again. Spelling one place two ways makes
 * it two places and nothing downstream can tell, so the list is the guard.
 */
static void test_remembered_sites(void) {
    struct config config;
    config_defaults(&config);

    check_int("a new one is new", config_remember_site(&config, "home"), 1);
    check_int("and another", config_remember_site(&config, "office"), 1);
    check_int("both are held", config.site_count, 2);
    check_str("most recent first", config.sites[0].label, "office");

    check_int("one already known is not new",
              config_remember_site(&config, "home"), 0);
    check_int("and does not grow the list", config.site_count, 2);
    check_str("but moves to the front, where it will be looked for",
              config.sites[0].label, "home");
    check_str("pushing the other back", config.sites[1].label, "office");

    check_int("nothing is not a site", config_remember_site(&config, ""), 0);
    check_int("nor is no site at all",
              config_remember_site(&config, NULL), 0);
    check_int("and the list is unchanged", config.site_count, 2);
}

static void test_the_list_survives_the_file(void) {
    struct config written, read;
    char text[2048];

    config_defaults(&written);
    config_remember_site(&written, "home");
    config_remember_site(&written, "office");
    snprintf(written.site, sizeof(written.site), "%s", "office");
    check_true("formats", config_format(&written, text, sizeof(text)) > 0);

    config_parse(text, &read);
    check_int("both sites come back", read.site_count, 2);
    check_str("in the order they were in", read.sites[0].label, "office");
    check_str("and the second", read.sites[1].label, "home");
    check_str("with the current one still current", read.site, "office");
}

static void test_the_list_has_an_end(void) {
    struct config config;
    int i;
    config_defaults(&config);
    for (i = 0; i < CONFIG_SITES_MAX + 5; i++) {
        char name[32];
        snprintf(name, sizeof(name), "site-%d", i);
        config_remember_site(&config, name);
    }
    check_int("it stops at its limit", config.site_count, CONFIG_SITES_MAX);
    /* The least recently used falls off the end, not the most recent. */
    check_str("keeping the newest", config.sites[0].label, "site-16");
}

/*
 * The tuning correction, kept per site.
 *
 * It belongs to the receiver, not the room -- but it is measured against
 * whatever reference the room offers and it drifts, so calibrating at one
 * place and carrying the dongle to another arrives with a number that was
 * true somewhere else.
 */
static void test_a_correction_per_site(void) {
    struct config config;
    config_defaults(&config);

    check_int("a site nobody has calibrated is uncorrected",
              config_site_ppm(&config, "home"), 0);
    check_int("recording one is a change",
              config_set_site_ppm(&config, "home", -36), 1);
    check_int("and it comes back", config_site_ppm(&config, "home"), -36);
    check_int("recording the same value again is not a change",
              config_set_site_ppm(&config, "home", -36), 0);

    check_int("another site keeps its own",
              config_set_site_ppm(&config, "roof-mast", 12), 1);
    check_int("the first is untouched", config_site_ppm(&config, "home"), -36);
    check_int("and the second is its own", config_site_ppm(&config, "roof-mast"), 12);

    /* Recording against a site nobody has named yet names it, so calibrating
       somewhere new does not silently discard the result. */
    check_int("a correction for an unknown site is a change",
              config_set_site_ppm(&config, "field", 4), 1);
    check_int("and the site now exists", config.site_count, 3);
    check_str("at the front, as the most recent", config.sites[0].label,
              "field");

    check_int("an unknown site is still uncorrected rather than wrong",
              config_site_ppm(&config, "nowhere"), 0);
}

static void test_the_correction_survives_the_file(void) {
    struct config written, read;
    char text[2048];

    config_defaults(&written);
    config_set_site_ppm(&written, "home", -36);
    config_set_site_ppm(&written, "roof-mast", 12);
    check_true("formats", config_format(&written, text, sizeof(text)) > 0);
    config_parse(text, &read);
    check_int("both corrections come back",
              config_site_ppm(&read, "home"), -36);
    check_int("each with its own site",
              config_site_ppm(&read, "roof-mast"), 12);

    /* And moving a site to the front carries its correction with it, rather
       than shuffling labels past values. */
    config_remember_site(&read, "home");
    check_str("the moved site leads", read.sites[0].label, "home");
    check_int("still with its own number", config_site_ppm(&read, "home"), -36);
    check_int("and the other's is intact",
              config_site_ppm(&read, "roof-mast"), 12);
}

static void test_a_file_from_before_corrections(void) {
    struct config config;
    /* Written by a build that kept no correction: the label starts where the
       number would be. It must read as an uncorrected site, not be dropped. */
    config_parse("antenna whip\nknown_site home-desk\n"
                 "known_site 12 roof-mast\n", &config);
    check_int("both sites are read", config.site_count, 2);
    check_str("the old-style one keeps its whole label",
              config.sites[0].label, "home-desk");
    check_int("and reads as uncorrected", config.sites[0].ppm, 0);
    check_str("the new-style one is unaffected", config.sites[1].label,
              "roof-mast");
    check_int("with its correction", config.sites[1].ppm, 12);
}

/*
 * The antennas this receiver has used. Same discipline as the sites: levels
 * only compare between sweeps taken with the same antenna, and one antenna
 * spelled two ways is two antennas.
 */
static void test_remembered_antennas(void) {
    struct config config;
    config_defaults(&config);

    check_int("the default is already there", config.antenna_count, 1);
    check_int("a new one is new",
              config_remember_antenna(&config, "discone, roof"), 1);
    check_int("both are held", config.antenna_count, 2);
    check_str("most recent first", config.antennas[0], "discone, roof");
    check_str("with the default behind it", config.antennas[1], "telescopic");

    check_int("one already known is not new",
              config_remember_antenna(&config, "telescopic"), 0);
    check_str("but comes to the front", config.antennas[0], "telescopic");
    check_int("and the list does not grow", config.antenna_count, 2);

    check_int("nothing is not an antenna",
              config_remember_antenna(&config, ""), 0);
}

static void test_antennas_survive_the_file(void) {
    struct config written, read;
    char text[2048];
    int i;

    config_defaults(&written);
    config_remember_antenna(&written, "discone, roof");
    config_remember_antenna(&written, "loop");
    check_true("formats", config_format(&written, text, sizeof(text)) > 0);

    config_parse(text, &read);
    check_int("all three come back", read.antenna_count, 3);
    /* The order is the point -- most recent first is what the picker offers
       first -- so it has to survive the round trip and not be reversed. */
    for (i = 0; i < written.antenna_count; i++)
        check_str("in the same order", read.antennas[i], written.antennas[i]);
    check_str("and the one in use is unchanged", read.antenna,
              written.antenna);
}


/*
 * The Scope's transform size, kept because it describes how somebody likes to
 * work rather than one run -- the test everything else in this file passes.
 */
static void test_the_transform_size(void) {
    struct config config;
    char text[2048];

    config_defaults(&config);
    check_int("nothing is remembered to begin with", config.fft_size, 0);
    check_int("setting it says the file is worth writing",
              config_set_fft_size(&config, 8192), 1);
    check_int("and it is remembered", config.fft_size, 8192);
    check_int("setting the same value again changes nothing",
              config_set_fft_size(&config, 8192), 0);
    check_int("a size the transform cannot do is refused",
              config_set_fft_size(&config, 3000), 0);
    check_int("leaving what was there", config.fft_size, 8192);

    /* It survives a round trip through the file. */
    check_true("it writes",
               config_format(&config, text, sizeof(text)) > 0);
    {
        struct config read_back;
        config_defaults(&read_back);
        config_parse(text, &read_back);
        check_int("and reads back", read_back.fft_size, 8192);
    }

    /*
     * A file written by another version naming a size this build cannot do is
     * dropped rather than carried: a transform that refuses its size draws
     * nothing at all, and a config file is not a good place to learn that.
     */
    {
        struct config odd;
        config_defaults(&odd);
        config_parse("fft_size 3000\n", &odd);
        check_int("a size this build refuses is left at the default",
                  odd.fft_size, 0);
        config_parse("fft_size 1048576\n", &odd);
        check_int("and so is an absurd one", odd.fft_size, 0);
    }
}

int main(void) {
    test_defaults();
    test_reads_a_file();
    test_absent_keys_keep_their_defaults();
    test_unknown_keys_survive();
    test_round_trip();
    test_an_unset_site_is_not_written();
    test_refuses_what_will_not_fit();
    test_a_long_value_is_cut_not_overrun();

    test_remembered_sites();
    test_the_list_survives_the_file();
    test_the_list_has_an_end();

    test_a_correction_per_site();
    test_the_correction_survives_the_file();
    test_a_file_from_before_corrections();

    test_remembered_antennas();
    test_antennas_survive_the_file();

    test_the_transform_size();

    return check_report("the persisted installation");
}
