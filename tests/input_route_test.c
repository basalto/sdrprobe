#include "input_route.h"
#include "check.h"

#include <stdio.h>
#include <string.h>

/*
 * Which control a key press reaches.
 *
 * The precedence is what stops a letter typed into a survey range field from
 * also opening an overlay, and what lets Help be raised over calibration. It
 * is checkable only because it is now a function of flags rather than a chain
 * of `IsKeyPressed` calls: synthetic key events do not reach the raylib window
 * on the desktop this is developed on, so before this the order could only be
 * verified by a person pressing keys (ADR-0012).
 */

static struct input_state state_of(int help, int settings, int calibration,
                                   int scan, int tab, int typing) {
    struct input_state s;

    s.help_open = help;
    s.settings_open = settings;
    s.calibration_open = calibration;
    s.scan_open = scan;
    s.tab = tab;
    s.text_focus = typing;
    return s;
}

static struct input_state scope(void) {
    return state_of(0, 0, 0, 0, TAB_SCOPE, 0);
}

/* The plain cases: no overlay, input goes to the tab. */
static void test_the_tabs(void) {
    struct input_state s = scope();

    check_int("the Scope tab", input_route(&s), INPUT_TARGET_SCOPE);
    s.tab = TAB_DECODE;
    check_int("the Decode tab", input_route(&s), INPUT_TARGET_DECODE);
}

/*
 * The order of the overlays. Each one is tested with everything below it also
 * open, which is the only way the order is visible: an overlay that comes
 * first when it is the only one open proves nothing.
 */
static void test_the_precedence(void) {
    struct input_state s = state_of(1, 1, 1, 1, TAB_DECODE, 1);

    check_int("help outranks everything", input_route(&s), INPUT_TARGET_HELP);
    s.help_open = 0;
    check_int("then settings", input_route(&s), INPUT_TARGET_SETTINGS);
    s.settings_open = 0;
    check_int("then the scan inside calibration", input_route(&s),
              INPUT_TARGET_SCAN);
    s.scan_open = 0;
    check_int("then calibration itself", input_route(&s),
              INPUT_TARGET_CALIBRATION);
    s.calibration_open = 0;
    check_int("and only then the tab", input_route(&s), INPUT_TARGET_DECODE);
}

/*
 * Help is a reading surface: while it is up, every key belongs to it. A key
 * reaching the view behind it acts on something the reader cannot see, which
 * is how a stray press changes a setting nobody meant to touch.
 */
static void test_help_takes_everything(void) {
    struct input_state s = state_of(1, 0, 1, 0, TAB_SCOPE, 0);

    check_int("over calibration", input_route(&s), INPUT_TARGET_HELP);
    s.calibration_open = 0;
    s.settings_open = 1;
    check_int("over settings", input_route(&s), INPUT_TARGET_HELP);
    s.tab = TAB_DECODE;
    check_int("over the decode tab", input_route(&s), INPUT_TARGET_HELP);

    /* And the view keys do not reach the Scope views through it. */
    s = state_of(1, 0, 0, 0, TAB_SCOPE, 0);
    check_int("the view keys are not live under help",
              input_view_keys_live(&s), 0);
}

/*
 * Typed input suppresses the single-letter shortcuts, and this is the rule
 * that was inconsistent: the settings panel was excluded from quit-on-Q, but
 * the survey's range and dwell fields were not -- so a stray letter typed
 * while entering a frequency quit the program.
 */
static void test_typing_suppresses_shortcuts(void) {
    struct input_state s = scope();

    check_int("nothing focused: shortcuts live", input_shortcuts_live(&s), 1);
    check_int("and help opens", input_help_opens(&s), 1);

    s.text_focus = 1;
    check_int("a survey field has focus: no shortcuts",
              input_shortcuts_live(&s), 0);
    check_int("q does not quit", input_takes_typing(&s), 1);
    check_int("h does not open help", input_help_opens(&s), 0);
    check_int("and the digits belong to the field, not the view switcher",
              input_view_keys_live(&s), 0);

    s = state_of(0, 1, 0, 0, TAB_SCOPE, 0);
    check_int("the settings panel is all fields", input_takes_typing(&s), 1);
    check_int("so no shortcuts there either", input_shortcuts_live(&s), 0);
    check_int("and help does not open over it", input_help_opens(&s), 0);
}

/* Help cannot be opened over itself -- the key would otherwise re-enter and
   reset the reader's place in it. */
static void test_help_does_not_reopen(void) {
    struct input_state s = state_of(1, 0, 0, 0, TAB_SCOPE, 0);

    check_int("already open", input_help_opens(&s), 0);
    s.help_open = 0;
    check_int("closed again", input_help_opens(&s), 1);
}

/*
 * The Scope view keys: 1..5 and the scale keys. Live only on the Scope tab,
 * with no overlay up and nothing being typed into.
 */
static void test_the_view_keys(void) {
    struct input_state s = scope();

    check_int("on the Scope tab", input_view_keys_live(&s), 1);
    s.tab = TAB_DECODE;
    check_int("not on the Decode tab, where 1 and 2 pick a decoder",
              input_view_keys_live(&s), 0);

    s = scope();
    s.calibration_open = 1;
    check_int("not under calibration", input_view_keys_live(&s), 0);
    s = scope();
    s.settings_open = 1;
    check_int("not under settings", input_view_keys_live(&s), 0);
    s = scope();
    s.text_focus = 1;
    check_int("not while a field is being typed into",
              input_view_keys_live(&s), 0);
}

/*
 * The whole state space, swept: 64 combinations, each routed to exactly one
 * target, with no combination reaching a target that contradicts its flags. A
 * new branch put in the wrong place shows up here rather than as a key that
 * quietly does the wrong thing.
 */
static void test_every_combination_routes_somewhere_sensible(void) {
    int bad = 0;

    for (int bits = 0; bits < 64; bits++) {
        struct input_state s = state_of(bits & 1, (bits >> 1) & 1,
                                        (bits >> 2) & 1, (bits >> 3) & 1,
                                        (bits >> 4) & 1 ? TAB_DECODE
                                                        : TAB_SCOPE,
                                        (bits >> 5) & 1);
        enum input_target target = input_route(&s);

        switch (target) {
        case INPUT_TARGET_HELP:
            if (!s.help_open)
                bad++;
            break;
        case INPUT_TARGET_SETTINGS:
            if (!s.settings_open || s.help_open)
                bad++;
            break;
        case INPUT_TARGET_SCAN:
            if (!s.scan_open || !s.calibration_open)
                bad++;
            break;
        case INPUT_TARGET_CALIBRATION:
            if (!s.calibration_open || s.scan_open)
                bad++;
            break;
        case INPUT_TARGET_DECODE:
            if (s.tab != TAB_DECODE || s.help_open || s.settings_open ||
                s.calibration_open)
                bad++;
            break;
        case INPUT_TARGET_SCOPE:
            if (s.tab != TAB_SCOPE || s.help_open || s.settings_open ||
                s.calibration_open)
                bad++;
            break;
        }
        /* An overlay is never a place to type a frequency into, so the view
           keys must be dead wherever anything is over the view. */
        if (input_view_keys_live(&s) && target != INPUT_TARGET_SCOPE)
            bad++;
    }
    check_int("all 64 flag combinations route consistently", bad, 0);
}

/* A scan overlay flag left set while calibration is closed must not route
   input to the scan: it is drawn inside calibration, so there is nothing on
   screen to receive it. */
static void test_a_stale_scan_flag(void) {
    struct input_state s = state_of(0, 0, 0, 1, TAB_SCOPE, 0);

    check_int("scan without calibration is not the scan", input_route(&s),
              INPUT_TARGET_SCOPE);
}

int main(void) {
    test_the_tabs();
    test_the_precedence();
    test_help_takes_everything();
    test_typing_suppresses_shortcuts();
    test_help_does_not_reopen();
    test_the_view_keys();
    test_every_combination_routes_somewhere_sensible();
    test_a_stale_scan_flag();

    return check_report("input precedence");
}
