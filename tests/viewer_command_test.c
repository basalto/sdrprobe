#include "check.h"

#include "viewer_command.h"

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

static void test_a_null_error_buffer_is_accepted(void) {
    struct viewer_command cmd;

    check_int("a caller that does not want a reason gets -1 with no crash",
             viewer_command_parse("", 0, &cmd, NULL, 0), -1);
}

int main(void) {
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
    test_a_null_error_buffer_is_accepted();
    return check_report("the Viewer command line parser");
}
