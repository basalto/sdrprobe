#include "input_route.h"
#include "view_input.h"
#include "srd_log.h"
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

    /*
     * Zeroed first, and that is not tidiness. Every field used to be assigned
     * by name here, so adding one to `struct input_state` left it holding
     * whatever was on the stack -- and the sweep below reads every field of
     * every combination, so an uninitialised flag makes the exhaustive test
     * nondeterministic while still passing most runs.
     */
    memset(&s, 0, sizeof(s));
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
 * The whole state space, swept: 192 combinations, each routed to exactly one
 * target, with no combination reaching a target that contradicts its flags. A
 * new branch put in the wrong place shows up here rather than as a key that
 * quietly does the wrong thing.
 */
static void test_every_combination_routes_somewhere_sensible(void) {
    int bad = 0;

    /* Every overlay combination against every tab: 3 * 64 now that the
       startup form is a fifth overlay. */
    for (int bits = 0; bits < 64 * TAB_COUNT; bits++) {
        struct input_state s = state_of(bits & 1, (bits >> 1) & 1,
                                        (bits >> 2) & 1, (bits >> 3) & 1,
                                        bits / 64, (bits >> 4) & 1);
        enum input_target target;

        s.startup_open = (bits >> 5) & 1;
        target = input_route(&s);

        switch (target) {
        case INPUT_TARGET_HELP:
            if (!s.help_open)
                bad++;
            break;
        case INPUT_TARGET_STARTUP:
            /* Under Help and over everything else. */
            if (!s.startup_open || s.help_open)
                bad++;
            break;
        case INPUT_TARGET_SETTINGS:
            if (!s.settings_open || s.help_open || s.startup_open)
                bad++;
            break;
        case INPUT_TARGET_SCAN:
            if (!s.scan_open || !s.calibration_open || s.help_open ||
                s.settings_open || s.startup_open)
                bad++;
            break;
        case INPUT_TARGET_CALIBRATION:
            if (!s.calibration_open || s.scan_open || s.help_open ||
                s.settings_open || s.startup_open)
                bad++;
            break;
        case INPUT_TARGET_SURVEY:
            if (s.tab != TAB_SURVEY || s.help_open || s.settings_open ||
                s.calibration_open || s.startup_open)
                bad++;
            break;
        case INPUT_TARGET_DECODE:
            if (s.tab != TAB_DECODE || s.help_open || s.settings_open ||
                s.calibration_open || s.startup_open)
                bad++;
            break;
        case INPUT_TARGET_SCOPE:
            if (s.tab != TAB_SCOPE || s.help_open || s.settings_open ||
                s.calibration_open || s.startup_open)
                bad++;
            break;
        }
        /* An overlay is never a place to type a frequency into, so the view
           keys must be dead wherever anything is over the view. */
        if (input_view_keys_live(&s) && target != INPUT_TARGET_SCOPE)
            bad++;
    }
    check_int("all 192 flag combinations route consistently", bad, 0);
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
    /* SRD now draws a waterfall too, so the claim this function was already
       making about it -- that Up/Down scale one -- is true rather than a
       promise about a chart nothing drew. */
    s.decode = 5;   /* SRD */
    check_int("SRD takes them", input_scale_keys(&s), INPUT_SCALE_WATERFALL);

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

/*
 * Escape backs out of one thing at a time, and the zoom is now a rung on that
 * ladder. The ordering is the whole content: a reader with a menu open, a
 * field focused and a zoom applied should get the menu back first.
 */
static void test_escape_unzooms(void) {
    struct input_state s;

    memset(&s, 0, sizeof(s));
    s.tab = TAB_SCOPE;
    s.scope_zoomed = 1;
    check_int("a zoom is what Escape backs out of", input_escape(&s),
              INPUT_ESCAPE_UNZOOM);

    s.scope_zoomed = 0;
    check_int("and with none, it leaves", input_escape(&s),
              INPUT_ESCAPE_QUIT);

    /* Nearer the surface wins. */
    s.scope_zoomed = 1;
    s.text_focus = 1;
    check_int("a field is nearer than the zoom", input_escape(&s),
              INPUT_ESCAPE_LEAVE_FIELD);
    s.menu_open = 1;
    check_int("and a menu is nearer than the field", input_escape(&s),
              INPUT_ESCAPE_CLOSE_MENU);

    /* An overlay still owns its own key, zoomed or not. */
    memset(&s, 0, sizeof(s));
    s.tab = TAB_SCOPE;
    s.scope_zoomed = 1;
    s.settings_open = 1;
    check_int("an overlay keeps it", input_escape(&s), INPUT_ESCAPE_NOTHING);

    /* Escape still always does something on every screen -- the property the
       suite already holds, re-asked with the new rung in place. */
    {
        int tab, zoomed, did_nothing = 0;
        for (tab = 0; tab < TAB_COUNT; tab++)
            for (zoomed = 0; zoomed < 2; zoomed++) {
                struct input_state t;
                memset(&t, 0, sizeof(t));
                t.tab = tab;
                t.scope_zoomed = zoomed;
                if (input_escape(&t) == INPUT_ESCAPE_NOTHING)
                    did_nothing++;
            }
        check_int("Escape is bound on every screen", did_nothing, 0);
    }
}

/*
 * Every surface that reads typed characters must say so.
 *
 * `chart_key_pressed()` empties raylib's character queue in a `while` loop
 * and the frame loop calls it once, gated on this predicate. So a surface
 * that takes typed input without being named here has every keystroke
 * swallowed before its own handler runs -- which is what happened to the
 * settings panel's PPM field and to the startup form's receiver label, the
 * latter reported as "does not accept text input".
 *
 * `text_focus` is not that predicate: it names three surfaces and is not
 * where a new one gets added. `input_takes_typing()` is, and this pins it.
 */
static void test_every_typing_surface_suppresses_the_chart_keys(void) {
    struct input_state s = scope();

    check_int("nothing typing, so the chart keys are read",
              input_takes_typing(&s), 0);

    s = scope();
    s.settings_open = 1;
    check_int("the settings panel takes typing", input_takes_typing(&s), 1);

    s = scope();
    s.startup_open = 1;
    check_int("and the startup form", input_takes_typing(&s), 1);

    s = scope();
    s.text_focus = 1;
    check_int("and a focused field in a view", input_takes_typing(&s), 1);

    /* Raised over a form, Help does not un-suppress it: the form is still
       there with a half-typed value in it. */
    s = scope();
    s.startup_open = 1;
    s.help_open = 1;
    check_int("help over the form still suppresses", input_takes_typing(&s),
              1);
}


/*
 * The step before the precedence: how `struct input_state` gets filled in.
 *
 * Everything above takes the struct as given. What follows takes what the
 * views report and folds it, which is where the three faults this program has
 * paid for actually were -- a typing surface that no predicate named, and a
 * modal that reached the routing nowhere.
 */

static struct view_input plain_view(void) {
    struct view_input v;

    memset(&v, 0, sizeof(v));
    v.tab = TAB_SCOPE;
    /* Negative is "nothing has focus" for the survey, and its own _NONE for
       the Scope -- two conventions, which is why each has a projection rather
       than a comparison written out at the fold. */
    v.survey_focused_field = -1;
    v.scope_focused_field = VIEW_INPUT_SCOPE_FIELD_NONE;
    return v;
}

/*
 * Every typing surface, swept by the enum rather than by the five somebody
 * thought of.
 *
 * This is the check the ticket was written for. A surface added to
 * `enum typing_surface` and not wired into `view_input_set_typing()` or
 * `view_input_typing_surfaces()` fails here -- where the old arrangement went
 * *silent*, because `chart_key_pressed()` drains `GetCharPressed()` and a
 * surface nothing reports has its characters taken before its handler runs.
 *
 * What it cannot see, and the ticket says so: a new typing field inside a
 * `view_*.c` whose own predicate does not report it. That obligation moves,
 * it does not vanish.
 */
static void test_every_typing_surface_reaches_the_fold(void) {
    int surface;

    for (surface = 0; surface < TYPING_SURFACE_COUNT; surface++) {
        struct view_input v = plain_view();
        struct input_state s;
        unsigned mask;

        view_input_set_typing(&v, (enum typing_surface)surface);
        mask = view_input_typing_surfaces(&v);
        s = view_input_state(&v);

        check_msg(mask == TYPING_SURFACE_BIT(surface),
                  "%s reports itself and nothing else (mask %u)",
                  typing_surface_name((enum typing_surface)surface), mask);
        check_msg(input_takes_typing(&s) == 1,
                  "%s takes typing, so the chart keys yield",
                  typing_surface_name((enum typing_surface)surface));
        check_msg(input_shortcuts_live(&s) == 0,
                  "%s suppresses the single-letter shortcuts",
                  typing_surface_name((enum typing_surface)surface));
    }

    {
        struct view_input v = plain_view();
        struct input_state s = view_input_state(&v);

        check_int("and nothing typing takes no typing",
                  input_takes_typing(&s), 0);
    }
}

/*
 * `text_focus` still means what it meant: a field *outside* the two overlays.
 *
 * The distinction is load-bearing rather than tidy. `input_escape()` reads
 * `text_focus` for LEAVE_FIELD, and the startup form with nothing focused must
 * not claim a field to leave.
 */
static void test_text_focus_is_the_in_view_surfaces(void) {
    int surface;

    for (surface = 0; surface < TYPING_SURFACE_COUNT; surface++) {
        struct view_input v = plain_view();
        struct input_state s;
        int in_view = (TYPING_IN_VIEW_MASK & TYPING_SURFACE_BIT(surface)) != 0;

        view_input_set_typing(&v, (enum typing_surface)surface);
        s = view_input_state(&v);
        check_msg(s.text_focus == in_view,
                  "%s %s text_focus",
                  typing_surface_name((enum typing_surface)surface),
                  in_view ? "sets" : "does not set");
    }
}

/* The survey's fields are live on its own tab and nowhere else. */
static void test_a_survey_focus_left_on_another_tab(void) {
    struct view_input v = plain_view();
    struct input_state s;

    v.tab = TAB_SCOPE;
    v.survey_focused_field = 2;
    s = view_input_state(&v);
    check_int("a survey focus does not take the Scope's digits",
              s.text_focus, 0);

    v.tab = TAB_SURVEY;
    s = view_input_state(&v);
    check_int("and does take its own tab's", s.text_focus, 1);
}

/* Both SRD fields, named one at a time. The second arrived on 2026-09-15 and
   the only thing that made srd_editing() correct was that somebody
   remembered; this is what remembers now. */
static void test_both_srd_fields(void) {
    struct view_input v = plain_view();
    struct input_state s;

    v.srd_typing = 1;
    s = view_input_state(&v);
    check_int("the record-duration field takes typing",
              input_takes_typing(&s), 1);

    v = plain_view();
    v.srd_freq_typing = 1;
    s = view_input_state(&v);
    check_int("and so does the frequency field",
              input_takes_typing(&s), 1);

    check_int("srd_input_typing() agrees about neither",
              srd_input_typing(0, 0), 0);
    check_int("and about either", srd_input_typing(0, 1) &&
              srd_input_typing(1, 0), 1);
}

/*
 * Every list that is down over a view, including the waterfall's.
 *
 * Two lists of menus existed -- the routing's and the debug log's -- and they
 * already disagreed: the survey's band menu was in one and not the other. The
 * waterfall's right-click menu was in neither, so Escape quit the program
 * rather than closing it.
 */
static void test_every_menu_closes_before_escape_leaves(void) {
    struct view_input base = plain_view();
    struct view_input v;
    struct input_state s;

    v = base; v.survey_site_menu_open = 1;
    check_int("the survey's site list", view_input_menu_open(&v), 1);
    v = base; v.survey_antenna_menu_open = 1;
    check_int("its antenna list", view_input_menu_open(&v), 1);
    v = base; v.survey_band_menu_open = 1;
    check_int("its band list", view_input_menu_open(&v), 1);
    v = base; v.startup_site_menu_open = 1;
    check_int("the startup form's site list", view_input_menu_open(&v), 1);
    v = base; v.startup_antenna_menu_open = 1;
    check_int("its antenna list", view_input_menu_open(&v), 1);
    v = base; v.waterfall_menu_open = 1;
    check_int("and the waterfall's right-click menu",
              view_input_menu_open(&v), 1);

    s = view_input_state(&v);
    check_int("which Escape closes rather than quitting",
              input_escape(&s), INPUT_ESCAPE_CLOSE_MENU);

    v = base;
    check_int("nothing down, nothing to close", view_input_menu_open(&v), 0);
}

/*
 * The retrospective signal report is modal, and `q` was live behind it.
 *
 * It takes no typing, so `input_takes_typing()` is not the predicate that
 * covers it -- which is exactly how it was missed. The popup is a reading
 * surface with a receiver running behind it, and the shortcuts stop at it for
 * the reason they stop at Help.
 */
static void test_the_signal_report_stops_the_shortcuts(void) {
    struct view_input v = plain_view();
    struct input_state s;

    v.waterfall_report_open = 1;
    s = view_input_state(&v);

    check_int("the report is open", s.report_open, 1);
    check_int("it takes no typing", input_takes_typing(&s), 0);
    check_int("and q does not quit from behind it",
              input_shortcuts_live(&s), 0);
    check_int("nor does h open help over it", input_help_opens(&s), 0);
    check_int("its own handler owns Escape",
              input_escape(&s), INPUT_ESCAPE_NOTHING);
    check_int("and Up/Down do not reach the chart behind it",
              input_scale_keys(&s), INPUT_SCALE_NONE);
}

/*
 * The fold decides nothing the projections have not already decided.
 *
 * Read the other way: a `struct view_input` with every overlay flag copied
 * through arrives as the same `struct input_state` the old hand-written
 * composition produced, so the precedence above is unchanged.
 */
static void test_the_fold_only_copies(void) {
    struct view_input v = plain_view();
    struct input_state s;

    v.help_open = 1;
    v.settings_open = 1;
    v.calibration_open = 1;
    v.scan_open = 1;
    v.startup_open = 1;
    v.tab = TAB_DECODE;
    v.view = VIEW_KIND_WATERFALL;
    v.decode = DECODE_KIND_ADSB;
    v.scope_zoomed = 1;
    s = view_input_state(&v);

    check_int("help", s.help_open, 1);
    check_int("settings", s.settings_open, 1);
    check_int("calibration", s.calibration_open, 1);
    check_int("scan", s.scan_open, 1);
    check_int("startup", s.startup_open, 1);
    check_int("tab", s.tab, TAB_DECODE);
    check_int("view", s.view, VIEW_KIND_WATERFALL);
    check_int("decode", s.decode, DECODE_KIND_ADSB);
    check_int("zoom", s.scope_zoomed, 1);
    check_int("and Help still outranks all of it",
              input_route(&s), INPUT_TARGET_HELP);

    /* A NULL is the zero state rather than a crash: the frame loop cannot
       hand one over, and a check sweeping the fold should not have to know
       that. */
    s = view_input_state(NULL);
    check_int("a NULL folds to nothing", s.help_open || s.tab || s.text_focus,
              0);
}

/*
 * What clicking a row in the SRD log asks for.
 *
 * It used to happen inside `draw_log()`, which takes `const struct app *` and
 * cast the const away to retune -- the only cast of its kind in that file.
 */
static void test_what_an_srd_row_click_asks_for(void) {
    struct srd_row_intent intent;

    /* The ordinary case: select it, and put the receiver where it was. */
    intent = srd_log_row_intent(2, 8, 434417000.0, 1);
    check_int("a live receiver tunes to the row",
              intent.action, SRD_ROW_SELECT_AND_TUNE);
    check_int("the row it clicked", intent.row, 2);
    check_msg(intent.tune_hz == 434417000u,
              "at the frequency the row carries (%u)", intent.tune_hz);

    /* A capture holds one tuning, baked in. The row still selects: selection
       is how a reader marks their place. */
    intent = srd_log_row_intent(2, 8, 434417000.0, 0);
    check_int("file playback selects without retuning",
              intent.action, SRD_ROW_SELECT);
    check_int("and still says which row", intent.row, 2);

    /* An entry from before absolute_hz existed, and a zeroed one. */
    intent = srd_log_row_intent(0, 8, 0.0, 1);
    check_int("an entry with no frequency selects only",
              intent.action, SRD_ROW_SELECT);

    /* Off the end of the log, which is what an empty log's geometry gives. */
    intent = srd_log_row_intent(-1, 8, 434417000.0, 1);
    check_int("a click on no row asks for nothing",
              intent.action, SRD_ROW_NONE);
    check_int("and names no row", intent.row, -1);
    intent = srd_log_row_intent(8, 8, 434417000.0, 1);
    check_int("nor does one past the last", intent.action, SRD_ROW_NONE);
    intent = srd_log_row_intent(0, 0, 434417000.0, 1);
    check_int("nor one in an empty log", intent.action, SRD_ROW_NONE);

    /* Rounded rather than truncated. 434417000.6 is nearer 434417001. */
    intent = srd_log_row_intent(0, 1, 434417000.6, 1);
    check_msg(intent.tune_hz == 434417001u,
              "the tuning is rounded, not truncated (%u)", intent.tune_hz);
}

/*
 * A burst's frequency belongs to the tuning that heard it.
 *
 * The waterfall placed its marker at `current tuning + offset` every frame,
 * so once the SRD arrows could walk ten megahertz, one arrow press moved every
 * historical label by a megahertz. This is one addition and it is checked
 * because the fault was never the arithmetic -- it was which tuning went in.
 */
static void test_a_burst_keeps_the_frequency_it_was_heard_at(void) {
    double heard = srd_log_absolute_hz(434000000u, 417000.0);

    check_close("heard at 434.417 MHz", heard, 434417000.0, 1.0);

    /* The receiver then moves half a span. The entry does not. */
    check_close("and a retune does not move it",
                heard, srd_log_absolute_hz(434000000u, 417000.0), 1.0);
    check_close("where the current tuning would have said 435.417",
                srd_log_absolute_hz(435000000u, 417000.0), 435417000.0, 1.0);

    /* A negative offset is ordinary: the carrier can sit below the tuning. */
    check_close("an offset below the tuning",
                srd_log_absolute_hz(434000000u, -250000.0), 433750000.0, 1.0);
}

int main(void) {
    test_the_tabs();
    test_the_precedence();
    test_help_takes_everything();
    test_typing_suppresses_shortcuts();
    test_help_does_not_reopen();
    test_the_view_keys();
    test_every_typing_surface_suppresses_the_chart_keys();
    test_every_combination_routes_somewhere_sensible();
    test_a_stale_scan_flag();
    test_where_back_goes();
    test_decode_keys_yield_to_a_field();
    test_the_scale_keys_reach_every_chart();
    test_what_escape_does();
    test_who_owns_the_spectrum();

    test_escape_unzooms();

    test_every_typing_surface_reaches_the_fold();
    test_text_focus_is_the_in_view_surfaces();
    test_a_survey_focus_left_on_another_tab();
    test_both_srd_fields();
    test_every_menu_closes_before_escape_leaves();
    test_the_signal_report_stops_the_shortcuts();
    test_the_fold_only_copies();
    test_what_an_srd_row_click_asks_for();
    test_a_burst_keeps_the_frequency_it_was_heard_at();

    return check_report("input precedence, and what the views report into it");
}
