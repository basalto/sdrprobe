#ifndef INPUT_ROUTE_H
#define INPUT_ROUTE_H

/*
 * Which control a key press or a click reaches.
 *
 * The frame loop resolves input through a fixed precedence chain, and the
 * order is a decision: it is what stops a letter typed into a text field from
 * also opening an overlay, and what lets Help be raised over calibration but
 * not over the settings panel. It decided all that from inside the loop, where
 * nothing could reach it, and where every new branch is a chance to put
 * something in the wrong place.
 *
 * Plain flags in, one target and a few predicates out (ADR-0012). What each
 * target then does with the keys stays in its own handler; this only decides
 * who gets them.
 */

#define TAB_COUNT 2

enum active_tab {
    TAB_SCOPE,
    TAB_DECODE
};

/* Everything the routing depends on, and nothing else. */
struct input_state {
    int help_open;
    int settings_open;
    int calibration_open;
    int scan_open;      /* the scan overlay sits inside calibration */
    int tab;            /* enum active_tab */
    int text_focus;     /* a text field outside the settings panel has focus */
};

enum input_target {
    INPUT_TARGET_HELP,
    INPUT_TARGET_SETTINGS,
    INPUT_TARGET_SCAN,
    INPUT_TARGET_CALIBRATION,
    INPUT_TARGET_DECODE,
    INPUT_TARGET_SCOPE
};

/*
 * Who receives this frame's input.
 *
 * Help is outermost: it can be raised over a view or over calibration and
 * takes every key while it is up, because it is a reading surface and any key
 * reaching what is behind it acts on something the reader cannot see.
 * Settings comes next, then calibration -- with its scan overlay inside it --
 * and only then the tabs.
 */
static inline enum input_target input_route(const struct input_state *s) {
    if (s->help_open)
        return INPUT_TARGET_HELP;
    if (s->settings_open)
        return INPUT_TARGET_SETTINGS;
    if (s->calibration_open)
        return s->scan_open ? INPUT_TARGET_SCAN : INPUT_TARGET_CALIBRATION;
    if (s->tab == TAB_DECODE)
        return INPUT_TARGET_DECODE;
    return INPUT_TARGET_SCOPE;
}

/*
 * Whether something on screen is taking typed input.
 *
 * The settings panel is nothing but fields. Outside it, a survey range or
 * dwell field can hold focus while the rest of the view is live, and while it
 * does the letters belong to the field: losing a half-entered range to a
 * stray keystroke that opened an overlay is worse than having to click away
 * first.
 */
static inline int input_takes_typing(const struct input_state *s) {
    return s->settings_open || s->text_focus;
}

/*
 * Whether the single-letter shortcuts -- quit, help, settings, calibration --
 * are live. They are not while a field is taking typed input, and that is the
 * only thing that suppresses them: they mean the same from every screen, which
 * is the point of having them.
 */
static inline int input_shortcuts_live(const struct input_state *s) {
    return !input_takes_typing(s);
}

/* Help can be opened from anywhere it is not already open, including over
   calibration -- that is where it is most wanted. */
static inline int input_help_opens(const struct input_state *s) {
    return !s->help_open && input_shortcuts_live(s);
}

/*
 * Whether the numbered view keys and the scale keys reach the Scope views. Not
 * while an overlay is up, and not while a survey field has focus -- the digits
 * belong to the field.
 */
static inline int input_view_keys_live(const struct input_state *s) {
    return input_route(s) == INPUT_TARGET_SCOPE && !s->text_focus;
}

/*
 * And whether the Decode tab's numbered keys reach it. The same rule for the
 * same reason, and it did not exist until there was a text field in that tab
 * to take the digits instead: typing 100.3 into the FM view's frequency field
 * read the 3 as "switch to GSM" and did.
 */
static inline int input_decode_keys_live(const struct input_state *s) {
    return input_route(s) == INPUT_TARGET_DECODE && !s->text_focus;
}

#endif
