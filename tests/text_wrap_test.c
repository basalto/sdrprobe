#include "check.h"

#include "text_wrap.h"

#include <string.h>

/*
 * Breaking radio text across the lines a panel has room for.
 *
 * Sixty-four characters into a panel that holds about forty. Handing the
 * whole of it to a component that ellipsises gave "Cultura em
 * antena2.rtp..." -- cut exactly where the useful part starts.
 */

static void one(const char *text, int columns, const char **want, int count) {
    struct text_wrap_line lines[8];
    int n = text_wrap(text, columns, lines, 8);
    int i;

    check_msg(n == count, "'%s' at %d columns broke into %d lines, not %d\n",
              text, columns, n, count);
    for (i = 0; i < n && i < count; i++) {
        char got[128];
        int length = lines[i].length;

        if (length > (int)sizeof(got) - 1)
            length = (int)sizeof(got) - 1;
        memcpy(got, text + lines[i].start, (size_t)length);
        got[length] = '\0';
        check_msg(strcmp(got, want[i]) == 0,
                  "line %d of '%s' is \"%s\", expected \"%s\"\n", i, text,
                  got, want[i]);
        check_msg(lines[i].length <= columns,
                  "line %d of '%s' is %d characters, over %d\n", i, text,
                  lines[i].length, columns);
    }
}

static void test_breaking_at_spaces(void) {
    static const char *two[] = { "hello", "world" };
    static const char *three[] = { "the quick", "brown fox", "jumps" };

    one("hello world", 6, two, 2);
    one("the quick brown fox jumps", 10, three, 3);

    /* Short enough is one line and not touched. */
    {
        static const char *whole[] = { "ANTENA 2" };
        one("ANTENA 2", 40, whole, 1);
    }
}

/*
 * The real message, at the width the panel actually has. This is the case the
 * wrapper exists for.
 */
static void test_the_message_that_prompted_this(void) {
    static const char *want[] = { "Cultura em", "antena2.rtp.pt" };
    one("Cultura em antena2.rtp.pt", 15, want, 2);
}

/*
 * A word wider than a whole line, which radio text is full of: a URL has no
 * spaces to break at, and it must be broken rather than dropped or allowed to
 * run off the panel.
 */
static void test_a_word_too_long_to_fit(void) {
    struct text_wrap_line lines[8];
    int n = text_wrap("https://www.example.com/a/very/long/path", 12, lines, 8);
    int i, over = 0, total = 0;

    check_true("it breaks into several", n > 2);
    for (i = 0; i < n; i++) {
        if (lines[i].length > 12)
            over++;
        total += lines[i].length;
    }
    check_int("and no line overruns", over, 0);
    check_msg(total >= 38,
              "only %d of 40 characters survived the wrap\n", total);
}

static void test_no_line_starts_or_ends_with_a_space(void) {
    struct text_wrap_line lines[8];
    const char *text = "a  b   c    d     e      f       g";
    int n = text_wrap(text, 8, lines, 8);
    int i, bad = 0;

    check_true("it wrapped", n > 1);
    for (i = 0; i < n; i++) {
        if (lines[i].length == 0)
            continue;
        if (text[lines[i].start] == ' ')
            bad++;
        if (text[lines[i].start + lines[i].length - 1] == ' ')
            bad++;
    }
    check_int("no line has a space at either end", bad, 0);
}

static void test_it_refuses_nonsense(void) {
    struct text_wrap_line lines[4];

    check_int("no text, no lines", text_wrap(NULL, 10, lines, 4), 0);
    check_int("nor no room for them", text_wrap("hello", 10, lines, 0), 0);
    check_int("nor a zero width", text_wrap("hello", 0, lines, 4), 0);
    check_int("an empty string is no lines", text_wrap("", 10, lines, 4), 0);
    /* More text than lines: it stops rather than overrunning the array. */
    check_int("it stops at the room it was given",
              text_wrap("one two three four five six seven", 4, lines, 4), 4);
}

int main(void) {
    test_breaking_at_spaces();
    test_the_message_that_prompted_this();
    test_a_word_too_long_to_fit();
    test_no_line_starts_or_ends_with_a_space();
    test_it_refuses_nonsense();

    return check_report("wrapping text into a panel");
}
