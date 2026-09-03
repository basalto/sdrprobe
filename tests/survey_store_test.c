#include "check.h"

#include "survey_store.h"
#include "survey_suspect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app.h"

/*
 * The two parts of saving a sweep that can go wrong quietly.
 *
 * Writing the file needs an app, a receiver and a directory; naming it and
 * escaping a string need none of those, and they are where the mistakes are.
 * A name that disagrees with the ingest script's puts two sweeps of the same
 * band in two places, and an unescaped quote in an antenna's name produces
 * JSON nobody can read back -- both silent, both from a person typing
 * something reasonable.
 */

static struct tm at(int year, int month, int day, int hour, int minute) {
    struct tm when;
    memset(&when, 0, sizeof(when));
    when.tm_year = year - 1900;
    when.tm_mon = month - 1;
    when.tm_mday = day;
    when.tm_hour = hour;
    when.tm_min = minute;
    return when;
}

static void test_the_name_matches_the_script(void) {
    struct tm when = at(2026, 9, 2, 18, 57);
    char name[64];

    check_true("a whole-tuner sweep",
               survey_store_filename(24e6, 1766e6, &when, name, sizeof(name)) > 0);
    check_str("named as the ingest script names it", name,
              "2026-09-02-185700-24M-1766M.json");

    check_true("a band", survey_store_filename(791e6, 821e6, &when, name,
                                               sizeof(name)) > 0);
    check_str("same shape", name, "2026-09-02-185700-791M-821M.json");

    /* Not every edge is a whole megahertz, and a name has to survive that
       rather than rounding two different sweeps onto one file. */
    check_true("a fractional edge",
               survey_store_filename(88.5e6, 108e6, &when, name,
                                     sizeof(name)) > 0);
    check_str("keeps its fraction", name, "2026-09-02-185700-88.5M-108M.json");

    check_int("a buffer too small is refused, not overrun",
              survey_store_filename(24e6, 1766e6, &when, name, 8), -1);
    check_int("and so is no clock at all",
              survey_store_filename(24e6, 1766e6, NULL, name, sizeof(name)),
              -1);
}

/* Two sweeps of the same band an hour apart are two files. This is the one
   the directory exists for: four full sweeps in a day once left one file. */
static void test_two_sweeps_in_a_day_are_two_files(void) {
    struct tm morning = at(2026, 9, 3, 0, 6);
    struct tm evening = at(2026, 9, 3, 5, 13);
    char first[64], second[64];

    survey_store_filename(24e6, 1766e6, &morning, first, sizeof(first));
    survey_store_filename(24e6, 1766e6, &evening, second, sizeof(second));
    check_true("the same band on the same day does not overwrite itself",
               strcmp(first, second) != 0);
    check_str("the earlier one", first, "2026-09-03-000600-24M-1766M.json");
    check_str("and the later", second, "2026-09-03-051300-24M-1766M.json");
    /* Still sorting into chronological order in a directory listing, which is
       the order anybody comparing sweeps wants them in. */
    check_true("and they sort in the order they were taken",
               strcmp(first, second) < 0);
}

static void test_the_date_leads(void) {
    struct tm early = at(2026, 1, 5, 7, 3);
    char name[64];
    survey_store_filename(24e6, 1766e6, &early, name, sizeof(name));
    /* Zero-padded, so a directory listing sorts into chronological order --
       which is the order anybody comparing sweeps wants them in. */
    check_str("single digits are padded", name, "2026-01-05-070300-24M-1766M.json");
}

static void test_escaping(void) {
    char out[128];

    check_true("plain text is untouched",
               survey_json_escape("discone, roof", out, sizeof(out)) > 0);
    check_str("exactly as given", out, "discone, roof");

    survey_json_escape("a \"quoted\" name", out, sizeof(out));
    check_str("quotes are escaped", out, "a \\\"quoted\\\" name");

    survey_json_escape("back\\slash", out, sizeof(out));
    check_str("and backslashes", out, "back\\\\slash");

    survey_json_escape("two\nlines", out, sizeof(out));
    check_str("and newlines", out, "two\\nlines");

    survey_json_escape("bell\x07here", out, sizeof(out));
    check_str("and anything else below a space", out, "bell\\u0007here");

    check_int("nothing in, nothing out",
              survey_json_escape(NULL, out, sizeof(out)), 0);
    check_str("and the buffer is still a string", out, "");

    /* An escape can be six characters, so a value that fits unescaped may not
       fit escaped: refused rather than half-written. */
    check_int("a value that will not fit is refused",
              survey_json_escape("\"\"\"\"\"\"", out, 5), -1);
}

static void test_flag_text(void) {
    char buffer[64];

    check_str("no flags", survey_flag_text(0, buffer, sizeof(buffer)), "-");
    check_str("one", survey_flag_text(SURVEY_SUSPECT_REFERENCE, buffer,
                                      sizeof(buffer)), "reference");
    check_str("two, comma separated and in a fixed order",
              survey_flag_text(SURVEY_SUSPECT_REFERENCE |
                               SURVEY_SUSPECT_STEP_CENTRE,
                               buffer, sizeof(buffer)),
              "reference,step-centre");
}

/*
 * And the file itself, written and read back.
 *
 * The pure helpers above cannot catch the way this went wrong the first time:
 * the indent was folded into the key, so "antenna" was written as "  antenna".
 * Valid JSON, no warning, and every reader silently saw an unrecorded antenna.
 * Only looking at the keys catches that, so this looks at the keys.
 *
 * It needs a filesystem and a struct app, and neither needs a receiver or a
 * window -- the app is a calloc away and the file goes to a temporary
 * directory, so this still runs with nobody watching (ADR-0012).
 */
static void test_the_file_it_writes(void) {
    struct app *app = calloc(1, sizeof(*app));
    struct survey_plan plan;
    struct survey_candidate c[2];
    char path[256], text[8192], cwd[512];
    const char *tmp = getenv("TMPDIR");
    FILE *file;
    size_t got;

    if (!app)
        exit(2);
    if (!getcwd(cwd, sizeof(cwd)) || chdir(tmp && *tmp ? tmp : "/tmp") != 0) {
        printf("  (skipping the write check: no temporary directory)\n");
        free(app);
        return;
    }
    memset(&plan, 0, sizeof(plan));
    plan.lower_hz = 88e6;
    plan.upper_hz = 108e6;
    plan.step_count = 13;
    plan.bins = 8192;
    plan.bin_hz = 2441.4;
    app->survey.dwell_seconds = 0.12;
    app->applied_gain_tenths = 297;
    snprintf(app->config.antenna, sizeof(app->config.antenna),
             "a \"quoted\" whip");
    snprintf(app->config.site, sizeof(app->config.site), "home-desk");

    memset(c, 0, sizeof(c));
    c[0].found_hz = 94492310;
    c[0].power_dbfs = -7.7f;
    c[0].prominence_db = 35.9f;
    c[0].allocation = "FM broadcast";
    c[1].found_hz = 100000000;
    c[1].power_dbfs = -40.0f;
    c[1].prominence_db = 12.0f;
    c[1].suspect = SURVEY_SUSPECT_REFERENCE;

    {
        /* One carrier holding both maxima, which is what the window would
           have worked out from them. */
        struct survey_carrier carrier;
        memset(&carrier, 0, sizeof(carrier));
        carrier.centre_hz = 94400000.0;
        carrier.power_centre_hz = 94410000.0;
        carrier.lower_hz = 94300000.0;
        carrier.upper_hz = 94500000.0;
        carrier.width_hz = 200000.0;
        carrier.peak_dbfs = -7.7f;
        carrier.prominence_db = 35.9f;
        carrier.peaks = 2;
        check_int("it writes", survey_store_write(app, &plan, c, 2, &carrier,
                                                  1, path, sizeof(path)), 0);
    }
    file = fopen(path, "rb");
    check_true("and the file is there", file != NULL);
    if (file) {
        got = fread(text, 1, sizeof(text) - 1, file);
        text[got] = '\0';
        fclose(file);
        remove(path);

        /* The keys, spelled exactly as the reporting tool looks them up. */
        check_true("antenna is under receiver, spelled plainly",
                   strstr(text, "\"antenna\": \"a \\\"quoted\\\" whip\"") != NULL);
        check_true("the site has a label", strstr(text, "\"label\": \"home-desk\"") != NULL);
        check_true("the gain is a number, not a string",
                   strstr(text, "\"gain_db\": 29.7") != NULL);
        check_true("no key was written with its indent inside it",
                   strstr(text, "\"  ") == NULL);
        check_true("the range is an array", strstr(text, "\"range_hz\": [88000000, 108000000]") != NULL);
        check_true("totals count what went in",
                   strstr(text, "\"candidates\": 2, \"suspicious\": 1") != NULL);
        check_true("an unmeasured candidate says null, not zero",
                   strstr(text, "\"centre_hz\": null") != NULL);
        check_true("a flagged one carries its flag",
                   strstr(text, "\"flags\": [\"reference\"]") != NULL);
        check_true("one with no allocation says null",
                   strstr(text, "\"allocation\": null") != NULL);
        check_true("and one with an allocation names it",
                   strstr(text, "\"allocation\": \"FM broadcast\"") != NULL);
        /* The signals as well as the maxima, and each saying how many it
           accounts for -- a carrier of one peak and one of eleven are
           different claims. */
        check_true("the carriers are written too",
                   strstr(text, "\"carriers\": [") != NULL);
        check_true("with the width they were measured at",
                   strstr(text, "\"width_hz\": 200000") != NULL);
        check_true("and how many maxima each accounts for",
                   strstr(text, "\"maxima\": 2") != NULL);
        check_true("the identity and the power centre are both kept",
                   strstr(text, "\"centre_hz\": 94400000") != NULL &&
                   strstr(text, "\"power_centre_hz\": 94410000") != NULL);
    }
    if (chdir(cwd) != 0)
        exit(2);
    free(app);
}

int main(void) {
    test_the_name_matches_the_script();
    test_two_sweeps_in_a_day_are_two_files();
    test_the_date_leads();
    test_escaping();
    test_flag_text();
    test_the_file_it_writes();

    return check_report("saving a sweep");
}
