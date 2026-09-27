#include "check.h"

#include "server/viewer_command.h"

#include <string.h>

/*
 * The Viewer command line parser, alone: no socket, no struct app, no
 * receiver. Ticket 06's own words -- "A parser reading bytes from
 * outside the process is checked for what it rejects, not only for what
 * it accepts" -- so every case below is either a line this parser must
 * accept, with the exact value it must read out of it, or one it must
 * refuse, with a reason a Viewer can be shown.
 */

static void test_a_valid_tune_line(void) {
    struct viewer_command cmd;

    check_int("a well-formed tune line is accepted",
             viewer_command_parse("tune 948400000", 14, &cmd, NULL, 0), 0);
    check_int("its type is TUNE", cmd.type, VIEWER_COMMAND_TUNE);
    check_true("its frequency is read exactly",
              cmd.hz == 948400000u);
}

static void test_trailing_whitespace_is_tolerated(void) {
    struct viewer_command cmd;
    const char *line = "tune 948400000   \r\n";

    check_int("trailing whitespace after the value is not a trailing field",
             viewer_command_parse(line, strlen(line), &cmd, NULL, 0), 0);
    check_true("the frequency is still read correctly", cmd.hz == 948400000u);
}

static void test_leading_whitespace_is_tolerated(void) {
    struct viewer_command cmd;
    const char *line = "   tune 948400000";

    check_int("leading whitespace before the command word is accepted",
             viewer_command_parse(line, strlen(line), &cmd, NULL, 0), 0);
    check_true("the frequency is still read correctly", cmd.hz == 948400000u);
}

static void test_empty_line_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("an empty line is refused",
             viewer_command_parse("", 0, &cmd, error, sizeof(error)), -1);
    check_true("it says why", strstr(error, "empty") != NULL);
}

static void test_whitespace_only_line_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("a whitespace-only line is refused",
             viewer_command_parse("   \t  ", 6, &cmd, error, sizeof(error)), -1);
    check_true("it says why", strstr(error, "empty") != NULL);
}

static void test_malformed_value_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("a non-numeric value is refused",
             viewer_command_parse("tune abc", 8, &cmd, error, sizeof(error)),
             -1);
    check_true("it says why", strlen(error) > 0);
}

static void test_missing_value_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("tune with no value at all is refused",
             viewer_command_parse("tune", 4, &cmd, error, sizeof(error)), -1);
    check_true("it says why", strlen(error) > 0);
}

static void test_negative_value_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("a negative value is refused",
             viewer_command_parse("tune -5", 7, &cmd, error, sizeof(error)),
             -1);
    check_true("it says why", strlen(error) > 0);
}

static void test_unrecognized_command_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("an unrecognized command word is refused",
             viewer_command_parse("sweep 100 200", 13, &cmd, error,
                                  sizeof(error)),
             -1);
    check_true("it says why", strstr(error, "unrecognized") != NULL);
}

static void test_trailing_field_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("a value followed by another field is refused",
             viewer_command_parse("tune 948400000 extra", 20, &cmd, error,
                                  sizeof(error)),
             -1);
    check_true("it says why", strstr(error, "trailing") != NULL);
}

static void test_out_of_range_value_is_refused(void) {
    struct viewer_command cmd;
    char error[64];
    const char *line = "tune 99999999999999999999"; /* far past UINT32_MAX */

    check_int("a value that overflows uint32_t is refused",
             viewer_command_parse(line, strlen(line), &cmd, error,
                                  sizeof(error)),
             -1);
    check_true("it says why", strstr(error, "range") != NULL);
}

static void test_a_line_at_the_bound_is_refused(void) {
    struct viewer_command cmd;
    char error[64];
    char line[VIEWER_COMMAND_LINE_MAX + 32];
    size_t i;

    /* "tune " followed by digits well past VIEWER_COMMAND_LINE_MAX --
       this parser must refuse it outright rather than reading past its
       own fixed buffer. */
    memcpy(line, "tune ", 5);
    for (i = 5; i < sizeof(line) - 1; i++)
        line[i] = '9';
    line[sizeof(line) - 1] = '\0';

    check_int("a line at or past the bound is refused",
             viewer_command_parse(line, strlen(line), &cmd, error,
                                  sizeof(error)),
             -1);
    check_true("it says why", strstr(error, "long") != NULL);
}

static void test_error_is_truncated_to_fit_rather_than_overflowing(void) {
    struct viewer_command cmd;
    char tiny[4];

    check_int("a refusal still returns -1 with a tiny error buffer",
             viewer_command_parse("", 0, &cmd, tiny, sizeof(tiny)), -1);
    check_true("the tiny buffer is left NUL-terminated",
              tiny[sizeof(tiny) - 1] == '\0' || strlen(tiny) < sizeof(tiny));
}

/*
 * Ticket 07's second command: "view scope" and "view survey", the same
 * shape as "tune <hz>" but naming a screen instead of a value.
 */
static void test_a_valid_view_scope_line(void) {
    struct viewer_command cmd;

    check_int("view scope is accepted",
             viewer_command_parse("view scope", 10, &cmd, NULL, 0), 0);
    check_int("its type is VIEW", cmd.type, VIEWER_COMMAND_VIEW);
    check_int("naming the Scope", cmd.screen, VIEWER_SCREEN_SCOPE);
}

static void test_a_valid_view_survey_line(void) {
    struct viewer_command cmd;

    check_int("view survey is accepted",
             viewer_command_parse("view survey", 11, &cmd, NULL, 0), 0);
    check_int("its type is VIEW", cmd.type, VIEWER_COMMAND_VIEW);
    check_int("naming the Survey", cmd.screen, VIEWER_SCREEN_SURVEY);
}

/*
 * Ticket 14's Phase 4 adds the first screen name that is not a tab: FM is
 * the Decode tab with DECODE_FM chosen, so `viewer_session.c` answers it
 * with a `set_decode()` as well as a `set_tab()`. None of that is this
 * parser's business -- what is, is that the name resolves at all, which it
 * did not until the two hand-written `strcmp`s became a table.
 */
static void test_a_valid_view_fm_line(void) {
    struct viewer_command cmd;

    check_int("view fm is accepted",
             viewer_command_parse("view fm", 7, &cmd, NULL, 0), 0);
    check_int("its type is VIEW", cmd.type, VIEWER_COMMAND_VIEW);
    check_int("naming FM", cmd.screen, VIEWER_SCREEN_FM);
}

/*
 * A prefix of a real screen name, and a real name with something appended.
 * The table is matched with strcmp against a token sscanf already delimited,
 * so neither can pass -- which is worth pinning precisely because a
 * length-prefix match is the plausible way to write this wrong, and `fm`
 * being two characters makes an accidental prefix match cheap to hit.
 */
static void test_a_screen_name_matches_whole_or_not_at_all(void) {
    struct viewer_command cmd;
    char error[64];

    check_true("a prefix of a screen name is refused",
              viewer_command_parse("view f", 6, &cmd, error, sizeof(error)) < 0);
    check_str("and says why", error, "unrecognized screen");
    check_true("a screen name with more after it is refused",
              viewer_command_parse("view fmx", 8, &cmd, error,
                                   sizeof(error)) < 0);
    check_str("and says why", error, "unrecognized screen");
}

static void test_view_tolerates_the_same_whitespace_tune_does(void) {
    struct viewer_command cmd;
    const char *leading = "   view survey";
    const char *trailing = "view survey   \r\n";

    check_int("leading whitespace before the command word",
             viewer_command_parse(leading, strlen(leading), &cmd, NULL, 0), 0);
    check_int("still reads Survey", cmd.screen, VIEWER_SCREEN_SURVEY);
    check_int("trailing whitespace after the screen name",
             viewer_command_parse(trailing, strlen(trailing), &cmd, NULL, 0),
             0);
    check_int("still reads Survey", cmd.screen, VIEWER_SCREEN_SURVEY);
}

static void test_view_with_no_screen_is_refused(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("view with nothing after it is refused",
             viewer_command_parse("view", 4, &cmd, error, sizeof(error)), -1);
    check_true("it says why", strstr(error, "screen") != NULL);
}

/*
 * A name the table does not hold.
 *
 * This asked about `view lte` until the LTE view landed and the table grew a
 * row for it -- the last of ticket 07's seven, so there is no unserved
 * screen left to name. A check written against "the one that is missing" has
 * a shelf life; one written against a name nothing will ever serve does not.
 */
static void test_view_of_an_unrecognized_screen_is_refused(void) {
    struct viewer_command cmd;
    char error[64];
    const char *line = "view nosuchscreen";

    check_int("a screen this link does not serve is refused",
             viewer_command_parse(line, strlen(line), &cmd, error,
                                  sizeof(error)),
             -1);
    check_true("it says why", strstr(error, "screen") != NULL);
}

static void test_view_with_a_trailing_field_is_refused(void) {
    struct viewer_command cmd;
    char error[64];
    const char *line = "view survey now";

    check_int("a third word is refused",
             viewer_command_parse(line, strlen(line), &cmd, error,
                                  sizeof(error)),
             -1);
    check_true("it says why", strstr(error, "trailing") != NULL);
}

/* Two commands, one parser, and each still refuses the word the other
   command owns as a value/screen it does not recognise the other way --
   tune does not accept a screen name, view does not accept a frequency. */
static void test_the_two_commands_do_not_bleed_into_each_other(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("tune with a screen name where a number belongs is refused",
             viewer_command_parse("tune survey", 11, &cmd, error,
                                  sizeof(error)),
             -1);
    check_int("view with a frequency where a screen name belongs is refused",
             viewer_command_parse("view 948400000", 14, &cmd, error,
                                  sizeof(error)),
             -1);
}

static void test_a_null_error_buffer_is_accepted(void) {
    struct viewer_command cmd;

    check_int("a caller that does not want a reason gets -1 with no crash",
             viewer_command_parse("", 0, &cmd, NULL, 0), -1);
}

/* The parser, with the line's length taken for you. */
static int parse(const char *line, struct viewer_command *cmd, char *error) {
    return viewer_command_parse(line, strlen(line), cmd, error, 64);
}

/*
 * `set <field> <value>` stages, and `apply` commits.
 *
 * Two commands rather than one, and that is load-bearing: one step of a
 * stepper must not restart acquisition, and `settings_apply()` validates the
 * staged set *together*, so a rejected PPM must not also lose a transform
 * size the reader had just chosen -- which is already why that function
 * applies the size first (`web-visualization/17`).
 */
static void test_set_stages_one_field(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("a signed correction", parse("set ppm -32", &cmd, error), 0);
    check_int("is a set", cmd.type, VIEWER_COMMAND_SET);
    check_int("of the tuning correction", cmd.setting, VIEWER_SETTING_PPM);
    check_int("with its value", cmd.value, -32);

    check_int("a transform size", parse("set fft 16384", &cmd, error), 0);
    check_int("names the field", cmd.setting, VIEWER_SETTING_FFT);
    check_int("and the size", cmd.value, 16384);

    check_int("a gain step", parse("set gain 12", &cmd, error), 0);
    check_int("by index", cmd.setting, VIEWER_SETTING_GAIN);
    check_int("into the device's own list", cmd.value, 12);
}

/*
 * A toggle takes `on` and `off` as well as 1 and 0, because typing
 * `set dc on` is what a person does and refusing it to save a line of
 * parsing would be a rule nobody wants.
 */
static void test_a_toggle_takes_words_as_well_as_numbers(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("on", parse("set dc on", &cmd, error), 0);
    check_int("is one", cmd.value, 1);
    check_int("off", parse("set drift off", &cmd, error), 0);
    check_int("is zero", cmd.value, 0);
    check_int("and so is 0", parse("set dc 0", &cmd, error), 0);
    check_int("still zero", cmd.value, 0);

    check_int("but not a number that is neither",
              parse("set dc 7", &cmd, error), -1);
    check_true("and it says why", strstr(error, "on or off") != NULL);
}

/*
 * The bounds are the applier's own, stated in the command table so a value
 * is refused where it was typed rather than two layers later with a
 * different sentence.
 */
static void test_a_value_out_of_range_is_refused_here(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("the applier's own bound", parse("set ppm 1000", &cmd, error),
              0);
    check_int("one past it", parse("set ppm 1001", &cmd, error), -1);
    check_true("and it says so", strstr(error, "range") != NULL);
    check_int("and the other end", parse("set ppm -1001", &cmd, error), -1);

    check_int("a value that is not a number at all",
              parse("set ppm twelve", &cmd, error), -1);
    check_true("says that instead", strstr(error, "integer") != NULL);
}

static void test_set_and_apply_are_refused_when_malformed(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("a field nobody has", parse("set colour 3", &cmd, error), -1);
    check_true("is named as such", strstr(error, "setting") != NULL);
    check_int("a field with no value", parse("set ppm", &cmd, error), -1);
    check_int("no field at all", parse("set", &cmd, error), -1);
    check_int("and a trailing field", parse("set ppm 3 now", &cmd, error),
              -1);

    check_int("apply takes nothing", parse("apply", &cmd, error), 0);
    check_int("and is an apply", cmd.type, VIEWER_COMMAND_APPLY);
    check_int("so a trailing field is refused",
              parse("apply now", &cmd, error), -1);
}

/* The two overlays are screens, because that is what `receiver_state.screen`
   reports when one is up: a full-screen modal over whatever tab is
   underneath (ADR-0008). */
static void test_the_overlays_are_screens(void) {
    struct viewer_command cmd;
    char error[64];

    check_int("settings", parse("view settings", &cmd, error), 0);
    check_int("is a screen", cmd.screen, VIEWER_SCREEN_SETTINGS);
    check_int("calibration", parse("view calibration", &cmd, error), 0);
    check_int("too", cmd.screen, VIEWER_SCREEN_CALIBRATION);
}

int main(void) {
    test_set_stages_one_field();
    test_a_toggle_takes_words_as_well_as_numbers();
    test_a_value_out_of_range_is_refused_here();
    test_set_and_apply_are_refused_when_malformed();
    test_the_overlays_are_screens();
    test_a_valid_tune_line();
    test_trailing_whitespace_is_tolerated();
    test_leading_whitespace_is_tolerated();
    test_empty_line_is_refused();
    test_whitespace_only_line_is_refused();
    test_malformed_value_is_refused();
    test_missing_value_is_refused();
    test_negative_value_is_refused();
    test_unrecognized_command_is_refused();
    test_trailing_field_is_refused();
    test_out_of_range_value_is_refused();
    test_a_line_at_the_bound_is_refused();
    test_error_is_truncated_to_fit_rather_than_overflowing();
    test_a_valid_view_scope_line();
    test_a_valid_view_survey_line();
    test_a_valid_view_fm_line();
    test_a_screen_name_matches_whole_or_not_at_all();
    test_view_tolerates_the_same_whitespace_tune_does();
    test_view_with_no_screen_is_refused();
    test_view_of_an_unrecognized_screen_is_refused();
    test_view_with_a_trailing_field_is_refused();
    test_the_two_commands_do_not_bleed_into_each_other();
    test_a_null_error_buffer_is_accepted();
    return check_report("the Viewer command line parser");
}
