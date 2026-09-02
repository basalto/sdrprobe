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
    check_str("most recent first", config.sites[0], "office");

    check_int("one already known is not new",
              config_remember_site(&config, "home"), 0);
    check_int("and does not grow the list", config.site_count, 2);
    check_str("but moves to the front, where it will be looked for",
              config.sites[0], "home");
    check_str("pushing the other back", config.sites[1], "office");

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
    check_str("in the order they were in", read.sites[0], "office");
    check_str("and the second", read.sites[1], "home");
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
    check_str("keeping the newest", config.sites[0], "site-16");
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

    return check_report("the persisted installation");
}
