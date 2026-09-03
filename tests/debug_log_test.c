#include "check.h"

#include "debug_log.h"
#include "input_route.h"

#include <string.h>

/*
 * The debug log's decisions: what a key is called, what a screen is called,
 * and whether a screen changed.
 *
 * These are checked rather than eyeballed for a specific reason. The log
 * exists to be believed when nothing else can be -- a window's keystroke
 * handling is the one thing in this program no check reaches, and keys cannot
 * be injected on this machine at all. A log that mislabels what it saw is
 * worse than no log, because it turns an unanswered question into a wrong
 * answer.
 */

static void test_key_names(void) {
    /* The keys this program binds. A name here is a claim about raylib's
       numbering, and getting one wrong sends a reader hunting for a handler
       that was never called. */
    check_str("escape", debug_key_name(256), "ESCAPE");
    check_str("enter", debug_key_name(257), "ENTER");
    check_str("backspace", debug_key_name(259), "BACKSPACE");
    check_str("up", debug_key_name(265), "UP");
    check_str("down", debug_key_name(264), "DOWN");
    check_str("left", debug_key_name(263), "LEFT");
    check_str("right", debug_key_name(262), "RIGHT");
    check_str("the digit keys", debug_key_name(49), "1");
    check_str("and the rest of them", debug_key_name(52), "4");
    check_str("h, which opens help", debug_key_name(72), "H");
    check_str("q, which quits", debug_key_name(81), "Q");
    check_str("s, for settings", debug_key_name(83), "S");
    check_str("c, for calibration", debug_key_name(67), "C");
    check_str("the keypad plus the survey zooms with",
              debug_key_name(334), "KP_ADD");

    /*
     * A key with no name prints its number rather than nothing. The whole
     * value of the log is saying what arrived, and "an unnamed key arrived"
     * is a far more useful line than silence -- it is exactly what a
     * synthetic keypress landing on a scratch keycode looks like.
     */
    check_str("an unnamed key says so", debug_key_name(999), "key 999");
    check_str("and so does a nonsense one", debug_key_name(-3), "key -3");
}

/*
 * The target names must line up with input_route.h's enum. If that enum is
 * reordered and this table is not, every line of the log names the wrong
 * handler -- silently, and in the one file a reader turns to when they
 * already do not trust what they are seeing.
 */
static void test_target_names_match_the_enum(void) {
    check_str("help", debug_target_name(INPUT_TARGET_HELP), "help");
    check_str("settings", debug_target_name(INPUT_TARGET_SETTINGS),
              "settings");
    check_str("scan", debug_target_name(INPUT_TARGET_SCAN), "scan");
    check_str("calibration", debug_target_name(INPUT_TARGET_CALIBRATION),
              "calibration");
    check_str("decode", debug_target_name(INPUT_TARGET_DECODE), "decode");
    check_str("scope", debug_target_name(INPUT_TARGET_SCOPE), "scope");
    check_str("and anything else is not guessed at",
              debug_target_name(99), "?");
    check_str("nor a negative", debug_target_name(-1), "?");
}

static struct debug_screen screen_of(int tab, int view, int decode) {
    struct debug_screen s;
    memset(&s, 0, sizeof(s));
    s.tab = tab;
    s.view = view;
    s.decode = decode;
    return s;
}

static void test_describing_a_screen(void) {
    char text[64];
    struct debug_screen s;

    s = screen_of(0, 4, 0);   /* Scope tab, survey */
    debug_screen_describe(&s, text, sizeof(text));
    check_str("the survey", text, "scope/survey");

    s = screen_of(0, 1, 0);
    debug_screen_describe(&s, text, sizeof(text));
    check_str("the spectrum", text, "scope/spectrum");

    /* The Decode tab names its technology, in the order the enum has them --
       FM first, which is what the header draws. */
    s = screen_of(1, 0, 0);
    debug_screen_describe(&s, text, sizeof(text));
    check_str("FM", text, "decode/fm");
    s = screen_of(1, 0, 3);
    debug_screen_describe(&s, text, sizeof(text));
    check_str("LTE", text, "decode/lte");

    /*
     * An overlay is appended, not substituted. Which view is underneath is
     * half the question when something goes wrong: calibration over the
     * survey and calibration over the GSM view leave the receiver in
     * different places.
     */
    s = screen_of(0, 4, 0);
    s.calibration_open = 1;
    debug_screen_describe(&s, text, sizeof(text));
    check_str("calibration over the survey", text,
              "scope/survey +calibration");

    s = screen_of(1, 0, 2);
    s.help_open = 1;
    s.analysis = 1;
    debug_screen_describe(&s, text, sizeof(text));
    check_str("help over the GSM charts", text, "decode/gsm +help +analysis");

    /* Nonsense indices are reported as unknown rather than read off the end
       of the table. */
    s = screen_of(0, 99, 0);
    debug_screen_describe(&s, text, sizeof(text));
    check_str("a view that does not exist", text, "scope/?");
    s = screen_of(1, 0, 99);
    debug_screen_describe(&s, text, sizeof(text));
    check_str("nor a technology", text, "decode/?");

    /* A buffer too small truncates rather than overruns. */
    {
        char small[8];
        s = screen_of(0, 4, 0);
        s.calibration_open = 1;
        debug_screen_describe(&s, small, sizeof(small));
        check_true("a short buffer is still terminated",
                   strlen(small) < sizeof(small));
    }
    debug_screen_describe(NULL, text, sizeof(text));
    check_str("and no screen describes as nothing", text, "");
}

/*
 * The change test, which is what keeps the log readable: a screen line every
 * frame is sixty lines a second and nobody reads it.
 */
static void test_noticing_a_change(void) {
    struct debug_screen a = screen_of(0, 4, 0);
    struct debug_screen b = screen_of(0, 4, 0);

    check_true("the same screen is not a change", !debug_screen_differs(&a, &b));
    b.view = 1;
    check_true("a different view is", debug_screen_differs(&a, &b));
    b = a;
    b.settings_open = 1;
    check_true("so is an overlay opening", debug_screen_differs(&a, &b));
    b = a;
    b.menu_open = 1;
    check_true("and a menu dropping down", debug_screen_differs(&a, &b));
    b = a;
    b.decode = 2;
    check_true("and the decode changing under the Decode tab",
               debug_screen_differs(&a, &b));
    check_true("a missing snapshot counts as a change",
               debug_screen_differs(&a, NULL));

    /*
     * Every field is compared. A field added to the struct and forgotten here
     * is a screen change the log would not report, so this walks the bytes
     * rather than naming them.
     */
    {
        unsigned char *bytes = (unsigned char *)&b;
        size_t i;
        int missed = 0;

        for (i = 0; i < sizeof(b); i++) {
            b = a;
            bytes[i] ^= 0xFF;
            if (!debug_screen_differs(&a, &b))
                missed++;
        }
        check_int("no byte of the snapshot is ignored", missed, 0);
    }
}

int main(void) {
    test_key_names();
    test_target_names_match_the_enum();
    test_describing_a_screen();
    test_noticing_a_change();

    return check_report("the debug log's names and change test");
}
