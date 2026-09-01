#ifndef TESTS_CHECK_H
#define TESTS_CHECK_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The whole test framework: two counters, a handful of comparisons, and one
 * line of output per suite.
 *
 * There is no framework here on purpose -- a check is a `main()` that calls
 * `test_*()` functions -- but every suite had grown its own copy of these
 * comparisons, and no suite could say how much it had actually proved. Now
 * they share one copy and report a count, so `make check` reads as a summary
 * rather than a wall of compiler commands.
 *
 * A suite ends with `return check_report("what it covers");`.
 */

static int check_count;
static int check_failures;

/* Failures are indented under the suite line and prefixed, so one lost in a
   long run is still findable by eye. */
#define CHECK_FAIL_PREFIX "    FAIL  "

/*
 * A check with its own message. Use it when the values are worth printing in a
 * shape the generic comparisons below cannot manage.
 *
 *     check_msg(spread <= 0.15, "steady run spread %.3f\n", spread);
 */
#define check_msg(condition, ...)                                              \
    do {                                                                       \
        check_count++;                                                         \
        if (!(condition)) {                                                    \
            fprintf(stderr, CHECK_FAIL_PREFIX);                                \
            fprintf(stderr, __VA_ARGS__);                                      \
            check_failures++;                                                  \
        }                                                                      \
    } while (0)

static inline void check_true(const char *name, int condition) {
    check_msg(condition, "%s\n", name);
}

static inline void check_int(const char *name, long actual, long expected) {
    check_msg(actual == expected, "%s: got %ld, expected %ld\n", name, actual,
              expected);
}

static inline void check_size(const char *name, size_t actual,
                              size_t expected) {
    check_msg(actual == expected, "%s: got %zu, expected %zu\n", name, actual,
              expected);
}

/* Takes doubles; float callers promote. A non-finite result fails whatever the
   tolerance -- a NaN compares false against everything, including a test that
   was meant to catch it. */
static inline void check_close(const char *name, double actual, double expected,
                               double tolerance) {
    check_msg(isfinite(actual) && fabs(actual - expected) <= tolerance,
              "%s: got %.6f, expected %.6f (+/- %.6f)\n", name, actual,
              expected, tolerance);
}

static inline void check_str(const char *name, const char *actual,
                             const char *expected) {
    check_msg(strcmp(actual, expected) == 0,
              "%s: got \"%s\", expected \"%s\"\n", name, actual, expected);
}

/*
 * One line saying what the suite covers and how much it proved, and the
 * process's exit code. `make check` sums the counts through CHECK_TALLY; when
 * it is unset -- a suite run on its own -- nothing is written.
 */
static inline int check_report(const char *suite) {
    const char *tally = getenv("CHECK_TALLY");

    if (tally && tally[0]) {
        FILE *f = fopen(tally, "a");
        if (f) {
            fprintf(f, "%d %d\n", check_count, check_failures);
            fclose(f);
        }
    }
    if (check_failures) {
        fflush(stdout);
        fprintf(stderr, "  %-34s %4d checks   %d FAILED\n", suite, check_count,
                check_failures);
        return 1;
    }
    printf("  %-34s %4d checks   ok\n", suite, check_count);
    return 0;
}

#endif
