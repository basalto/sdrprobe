#include "input_route.h"
#include "calibration_nav.h"
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
    s.tab = TAB_SURVEY;
    check_int("and the Survey tab, which is one now",
              input_route(&s), INPUT_TARGET_SURVEY);
    /* It leads, and it is the zero the app zero-initialises to -- which is
       what makes the survey the screen a session opens on. */
    check_int("Survey is the first tab", (int)TAB_SURVEY, 0);
    check_int("and there are three", TAB_COUNT, 3);
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

    /* Every overlay combination against every tab, which is 3 * 32 now that
       the survey is a tab rather than a view inside Scope. */
    for (int bits = 0; bits < 32 * TAB_COUNT; bits++) {
        struct input_state s = state_of(bits & 1, (bits >> 1) & 1,
                                        (bits >> 2) & 1, (bits >> 3) & 1,
                                        bits / 32, (bits >> 4) & 1);
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
        case INPUT_TARGET_SURVEY:
            if (s.tab != TAB_SURVEY || s.help_open || s.settings_open ||
                s.calibration_open)
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


/*
 * Where Back goes in the calibration overlay.
 *
 * Back used to close the whole overlay from anywhere in it, so an operator who
 * had scanned a band, picked a cell and wanted a different one had to leave
 * calibration and scan again. It is one step up now, and what "up" means
 * depends on where the channel came from -- which is a decision, and so is
 * checkable without a window (ADR-0012).
 */
static void test_where_back_goes(void) {
    /* Measuring a 4G cell: back to the list it was picked from. The list is
       drawn by the overlay itself, so stopping is all it takes. */
    check_int("4G measuring goes back to the cell list",
              calibration_back_target(1, 1, 0), CALIBRATION_BACK_STOP);
    check_int("and a stale 2G scan does not change that",
              calibration_back_target(1, 1, 1), CALIBRATION_BACK_STOP);

    /* Measuring a 2G channel that came from a scan: reopen the scan. */
    check_int("2G measuring a scanned channel reopens the scan",
              calibration_back_target(0, 1, 1), CALIBRATION_BACK_SCAN);
    /* A typed ARFCN never opened a scan, and running one uninvited would
       retune the receiver for half a minute. Stopping is the step back. */
    check_int("2G measuring a typed channel just stops",
              calibration_back_target(0, 1, 0), CALIBRATION_BACK_STOP);

    /* Not measuring is the top of the stack, whatever the technology: the
       list, or the empty screen offering to fill it, is already on show. */
    check_int("2G idle has nowhere above it",
              calibration_back_target(0, 0, 1), CALIBRATION_BACK_NONE);
    check_int("nor 4G idle", calibration_back_target(1, 0, 0),
              CALIBRATION_BACK_NONE);
    check_int("nor 4G idle after a scan", calibration_back_target(1, 0, 1),
              CALIBRATION_BACK_NONE);
    check_int("nor 5G, which measures nothing yet",
              calibration_back_target(2, 0, 0), CALIBRATION_BACK_NONE);
    check_int("5G measuring would stop like the rest",
              calibration_back_target(2, 1, 0), CALIBRATION_BACK_STOP);

    /*
     * The property that matters over any single row: Back is offered exactly
     * when there is a measurement to step out of. A dim Back on a screen with
     * somewhere to go is a dead end; a live one with nowhere to go swallows
     * the click and reads as a stuck screen.
     */
    {
        int tech, running, scanned;
        for (tech = 0; tech <= 2; tech++)
        for (running = 0; running <= 1; running++)
        for (scanned = 0; scanned <= 1; scanned++) {
            enum calibration_back target =
                calibration_back_target(tech, running, scanned);
            check_msg((target != CALIBRATION_BACK_NONE) == (running != 0),
                      "tech %d running %d scanned %d: Back is %s\n",
                      tech, running, scanned,
                      target == CALIBRATION_BACK_NONE ? "dim with a step to"
                                                        " take"
                                                      : "live with nowhere"
                                                        " to go");
        }
    }
}


/*
 * The Decode tab's numbered keys, and the field that takes them instead.
 *
 * This gate did not exist until the FM view put a text field in that tab.
 * Typing 100.3 into its frequency field read the 3 as "switch to GSM" and
 * did, which is a screen change nobody asked for and a frequency thrown away
 * with it. The Scope tab has had the same rule since it grew survey fields.
 */
static void test_decode_keys_yield_to_a_field(void) {
    struct input_state typing = state_of(0, 0, 0, 0, TAB_DECODE, 0);
    struct input_state idle = state_of(0, 0, 0, 0, TAB_DECODE, 0);

    typing.text_focus = 1;
    idle.text_focus = 0;

    check_true("the numbered keys reach the Decode tab normally",
               input_decode_keys_live(&idle));
    check_true("and not while a field has focus",
               !input_decode_keys_live(&typing));

    /* The two tabs' gates are separate questions and must not answer for each
       other: a digit typed in one tab has nothing to do with the other. */
    {
        struct input_state scope = state_of(0, 0, 0, 0, TAB_SCOPE, 0);
        check_true("the Decode gate is shut on the Scope tab",
                   !input_decode_keys_live(&scope));
        check_true("where the Scope gate is open",
                   input_view_keys_live(&scope));
        check_true("and the Scope gate is shut on the Decode tab",
                   !input_view_keys_live(&idle));
    }

    /* An overlay closes it too, the same as everything else in the chain. */
    {
        struct input_state overlaid = state_of(1, 0, 0, 0, TAB_DECODE, 0);
        check_true("an overlay shuts it", !input_decode_keys_live(&overlaid));
    }
}


/*
 * The scale keys, against every screen there is.
 *
 * This is the check the bug wanted. Up and Down adjusted the waterfall in the
 * GSM view and the calibration overlay and nowhere else, so the LTE and FM
 * decode views drew a waterfall nobody could rescale -- and a missing key
 * binding is invisible: no test fails, nothing is drawn wrong, and the only
 * symptom is a person pressing a key and nothing happening.
 *
 * So the rule is a table now, and this walks every screen past it. The part
 * that matters is the last loop: every screen that draws a waterfall must
 * take the keys, and the list of which screens those are is written out here
 * rather than derived from the thing under test.
 */
static void test_the_scale_keys_reach_every_chart(void) {
    struct input_state s;

    /* The Scope tab's four views each have a scale of their own. */
    s = state_of(0, 0, 0, 0, TAB_SCOPE, 0);
    for (s.view = 0; s.view < 4; s.view++)
        check_msg(input_scale_keys(&s) == INPUT_SCALE_ACTIVE_CHART,
                  "Scope view %d does not take the scale keys\n", s.view);

    /* The survey has no scale: Up and Down walk its candidates instead, and
       a scale change stealing them would be worse than not having one. */
    s = state_of(0, 0, 0, 0, TAB_SURVEY, 0);
    check_int("the survey has no scale", input_scale_keys(&s),
              INPUT_SCALE_NONE);

    /* The Decode tab: every technology that draws a waterfall. */
    s = state_of(0, 0, 0, 0, TAB_DECODE, 0);
    s.decode = 0;   /* FM */
    check_int("FM takes them", input_scale_keys(&s), INPUT_SCALE_WATERFALL);
    s.decode = 2;   /* GSM */
    check_int("GSM takes them", input_scale_keys(&s), INPUT_SCALE_WATERFALL);
    s.decode = 3;   /* LTE */
    check_int("LTE takes them", input_scale_keys(&s), INPUT_SCALE_WATERFALL);
    s.decode = DECODE_KIND_ADSB;
    check_int("ADS-B has no waterfall", input_scale_keys(&s),
              INPUT_SCALE_NONE);

    /* The calibration overlay draws one over whatever is underneath. */
    s = state_of(0, 0, 1, 0, TAB_SCOPE, 0);
    check_int("the calibration overlay takes them", input_scale_keys(&s),
              INPUT_SCALE_WATERFALL);
    /* The scan overlay inside it draws a channel chart, not a waterfall. */
    s = state_of(0, 0, 1, 1, TAB_SCOPE, 0);
    check_int("the scan overlay does not", input_scale_keys(&s),
              INPUT_SCALE_NONE);

    /* Nothing reaches a screen that is taking typed input, or one with an
       overlay over it that has no chart at all. */
    s = state_of(0, 0, 0, 0, TAB_SCOPE, 0);
    s.text_focus = 1;
    check_int("a focused field keeps them", input_scale_keys(&s),
              INPUT_SCALE_NONE);
    s = state_of(1, 0, 0, 0, TAB_SCOPE, 0);
    check_int("help keeps them", input_scale_keys(&s), INPUT_SCALE_NONE);
    s = state_of(0, 1, 0, 0, TAB_SCOPE, 0);
    check_int("so does the settings panel", input_scale_keys(&s),
              INPUT_SCALE_NONE);
    check_int("and no state at all is not a screen",
              input_scale_keys(NULL), INPUT_SCALE_NONE);

    /*
     * The property, over every screen: one that draws a waterfall takes the
     * keys. The list is spelled out here so it is an independent claim rather
     * than a restatement of the function.
     */
    {
        int tab, view, decode;
        for (tab = 0; tab < TAB_COUNT; tab++)
        for (view = 0; view < 4; view++)
        for (decode = 0; decode < 4; decode++) {
            struct input_state screen = state_of(0, 0, 0, 0, tab, 0);
            int draws_waterfall;

            screen.view = view;
            screen.decode = decode;
            draws_waterfall = tab == TAB_DECODE
                                  ? decode != DECODE_KIND_ADSB
                                  : tab == TAB_SCOPE &&
                                        view == 3 /* the Scope waterfall */;
            if (!draws_waterfall)
                continue;
            check_msg(input_scale_keys(&screen) != INPUT_SCALE_NONE,
                      "tab %d view %d decode %d draws a waterfall and takes "
                      "no scale keys\n", tab, view, decode);
        }
    }
}


/*
 * What Escape does, on every screen.
 *
 * This is the check the bug asked for, and the bug has now happened twice by
 * the same route. The FM view shipped with no Escape handler at all. Then the
 * survey lost its when it stopped being a Scope view and became a tab: the
 * Scope branch of the frame loop carried the quit and the survey had been
 * inheriting it. Neither failed anything -- a key simply did nothing, which
 * is invisible to every test that does not name the key.
 *
 * So the rule is a table, and this walks every screen past it.
 */
static void test_what_escape_does(void) {
    struct input_state s;

    /* The two screens that are the top of their own stack. */
    s = state_of(0, 0, 0, 0, TAB_SURVEY, 0);
    check_int("the survey is where the program opens, so Escape leaves it",
              input_escape(&s), INPUT_ESCAPE_QUIT);
    s = state_of(0, 0, 0, 0, TAB_SCOPE, 0);
    check_int("and the Scope views the same", input_escape(&s),
              INPUT_ESCAPE_QUIT);

    /* A decode view is one level in. */
    s = state_of(0, 0, 0, 0, TAB_DECODE, 0);
    check_int("a decode view goes back to Scope", input_escape(&s),
              INPUT_ESCAPE_TO_SCOPE);

    /* Nearer the surface first: a field, then a list over it. */
    s = state_of(0, 0, 0, 0, TAB_SURVEY, 0);
    s.text_focus = 1;
    check_int("a focused field is left before the screen is",
              input_escape(&s), INPUT_ESCAPE_LEAVE_FIELD);
    s.menu_open = 1;
    check_int("and a list over it before that", input_escape(&s),
              INPUT_ESCAPE_CLOSE_MENU);
    s.text_focus = 0;
    check_int("a list on its own too", input_escape(&s),
              INPUT_ESCAPE_CLOSE_MENU);

    /* An overlay owns the key: its own handler knows what step it is on, and
       two things deciding would be two things acting. */
    s = state_of(1, 0, 0, 0, TAB_SURVEY, 0);
    check_int("help keeps it", input_escape(&s), INPUT_ESCAPE_NOTHING);
    s = state_of(0, 1, 0, 0, TAB_SCOPE, 0);
    check_int("so does the settings panel", input_escape(&s),
              INPUT_ESCAPE_NOTHING);
    s = state_of(0, 0, 1, 0, TAB_DECODE, 0);
    check_int("and calibration, which has two steps of its own",
              input_escape(&s), INPUT_ESCAPE_NOTHING);
    check_int("no state is no answer", input_escape(NULL),
              INPUT_ESCAPE_NOTHING);

    /*
     * The property, over every screen there is: Escape always does
     * *something*. A key that silently does nothing is how both of these bugs
     * reached an operator.
     */
    {
        int tab, bits, silent = 0;

        for (tab = 0; tab < TAB_COUNT; tab++)
        for (bits = 0; bits < 4; bits++) {
            struct input_state screen = state_of(0, 0, 0, 0, tab, 0);

            screen.text_focus = bits & 1;
            screen.menu_open = (bits >> 1) & 1;
            if (input_escape(&screen) == INPUT_ESCAPE_NOTHING)
                silent++;
        }
        check_msg(silent == 0,
                  "%d screens with no overlay leave Escape doing nothing\n",
                  silent);
    }
}


/*
 * Who owns the spectrum.
 *
 * One array, five readers, and only one of them may change the transform's
 * size: the other four had their floors and thresholds chosen against 977 Hz
 * bins and would not fail if handed something else -- they would quietly
 * measure differently, which is worse.
 *
 * The case worth the check is the one a tab test would miss: calibration is
 * an overlay, not a tab, and it measures a centroid and an FCCH tone in that
 * very array while sitting over the Scope.
 */
static void test_who_owns_the_spectrum(void) {
    struct input_state s;

    s = state_of(0, 0, 0, 0, TAB_SCOPE, 0);
    s.view = VIEW_KIND_SPECTRUM;
    check_true("the spectrum view owns it", input_scope_owns_spectrum(&s));
    s.view = VIEW_KIND_WATERFALL;
    check_true("and the waterfall", input_scope_owns_spectrum(&s));

    /* The other Scope views do not read it, so they do not get to change it. */
    s.view = 0;
    check_true("not the magnitude view", !input_scope_owns_spectrum(&s));
    s.view = 2;
    check_true("nor the scatter", !input_scope_owns_spectrum(&s));

    /* The four consumers, each while its own screen is up. */
    s = state_of(0, 0, 0, 0, TAB_SURVEY, 0);
    check_true("not while the survey is sweeping into it",
               !input_scope_owns_spectrum(&s));
    s = state_of(0, 0, 0, 0, TAB_DECODE, 0);
    check_true("nor while a decode view is up",
               !input_scope_owns_spectrum(&s));

    /*
     * And the two that are overlays rather than tabs, which is the whole
     * reason this is not a test on app->tab.
     */
    s = state_of(0, 0, 1, 0, TAB_SCOPE, 0);
    s.view = VIEW_KIND_SPECTRUM;
    check_true("not with calibration open over it",
               !input_scope_owns_spectrum(&s));
    s = state_of(0, 0, 1, 1, TAB_SCOPE, 0);
    s.view = VIEW_KIND_WATERFALL;
    check_true("nor the channel scan inside it",
               !input_scope_owns_spectrum(&s));
    check_true("and no state owns nothing", !input_scope_owns_spectrum(NULL));

    /*
     * The property: over every screen there is, the Scope owns it only when
     * none of the four consumers could possibly be running. Spelled out here
     * rather than derived from the function, so it is a claim and not a
     * restatement.
     */
    {
        int tab, view, bits, wrong = 0;

        for (tab = 0; tab < TAB_COUNT; tab++)
        for (view = 0; view < 4; view++)
        for (bits = 0; bits < 4; bits++) {
            struct input_state screen = state_of(0, 0, bits & 1,
                                                 (bits >> 1) & 1, tab, 0);
            int consumer_could_run;

            screen.view = view;
            consumer_could_run = tab != TAB_SCOPE || screen.calibration_open ||
                                 screen.scan_open;
            if (consumer_could_run && input_scope_owns_spectrum(&screen))
                wrong++;
        }
        check_msg(wrong == 0,
                  "%d screens let the Scope resize a spectrum a consumer "
                  "could be reading\n", wrong);
    }
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
    test_where_back_goes();
    test_decode_keys_yield_to_a_field();
    test_the_scale_keys_reach_every_chart();
    test_what_escape_does();
    test_who_owns_the_spectrum();

    return check_report("input precedence");
}
