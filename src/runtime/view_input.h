#ifndef VIEW_INPUT_H
#define VIEW_INPUT_H

#include "input_route.h"

/*
 * What each view says about itself, and how that becomes the routing state.
 *
 * `input_route.h` made the input *precedence* reachable: plain flags in, one
 * target out. It did not make the step before it reachable -- how those flags
 * get filled in -- and that step is where the decisions about individual views
 * live. `input_state_now()` composed thirteen fields out of predicates that
 * each read `struct app` from a `view_*.c` linking raylib, so none of them,
 * and not the composition, could be checked (ADR-0012).
 *
 * The fault that produces is not a wrong answer but silence.
 * `GetCharPressed()` drains a queue, so whoever reads it first takes every
 * character that frame; `chart_key_pressed()` empties it in a `while` loop and
 * the frame loop calls it gated on `input_takes_typing()`. A field whose view
 * does not report it has its characters gone before its own handler runs, and
 * the report that comes back is "the field does not accept text input". That
 * has happened three times here -- the settings panel's PPM field, the startup
 * form's every field, and the waterfall report popup, which took no typing but
 * was invisible to the routing in the same way and let `q` quit the program
 * out from under a reader.
 *
 * So the surfaces are an enum rather than a list inside an expression, and
 * `view_input_set_typing()` is the one place a surface is mapped to the field
 * that reports it. A surface added to the enum and nowhere else fails
 * `check-input` rather than going quiet: that is the whole of what this buys,
 * and it is worth being clear about what it does not buy. Nothing here can
 * see a *new* typing field in a `view_*.c` that its own predicate does not
 * report. That obligation cannot be removed, only moved -- which is why the
 * registry this ticket considered was rejected rather than built.
 *
 * Plain ints in, plain answers out. No raylib, no `struct app`, links -lm.
 */

/*
 * One value from view.h, mirrored so this header stays standalone.
 * sdrprobe.c asserts at compile time that the two still agree.
 */
#define VIEW_INPUT_SCOPE_FIELD_NONE 0

/*
 * Every surface in this program that takes typed characters.
 *
 * The first five sit inside a view, where the rest of the screen stays live
 * around them. The last two are overlays that are nothing but fields, and
 * `struct input_state` names those in their own right -- so they are surfaces
 * for the purpose of "is anything taking typing", and are deliberately not
 * part of `text_focus`, which means "a field *outside* those two has focus".
 */
enum typing_surface {
    TYPING_SURVEY_RANGE,        /* the survey's range, dwell, site, antenna */
    TYPING_SCOPE_HEADER,        /* the Scope's centre, start and end fields */
    TYPING_FM_FREQUENCY,
    TYPING_SRD_RECORD,          /* the SRD record-duration field */
    TYPING_SRD_FREQUENCY,       /* and its centre frequency, added 2026-09-15 */
    TYPING_SETTINGS_PANEL,
    TYPING_STARTUP_FORM,
    TYPING_SURFACE_COUNT
};

#define TYPING_SURFACE_BIT(s) (1u << (s))

/* The five that sit inside a view. This is what `text_focus` reports. */
#define TYPING_IN_VIEW_MASK                                                   \
    (TYPING_SURFACE_BIT(TYPING_SURVEY_RANGE) |                                \
     TYPING_SURFACE_BIT(TYPING_SCOPE_HEADER) |                                \
     TYPING_SURFACE_BIT(TYPING_FM_FREQUENCY) |                                \
     TYPING_SURFACE_BIT(TYPING_SRD_RECORD) |                                  \
     TYPING_SURFACE_BIT(TYPING_SRD_FREQUENCY))

/*
 * The facts the frame loop reads out of the views, before anything is decided
 * about them. One field per thing a view knows about itself; no derived
 * values, so there is nothing here to get wrong except the reading.
 */
struct view_input {
    /* The overlays, in their own precedence order. */
    int help_open;
    int startup_open;
    int settings_open;
    int calibration_open;
    int scan_open;

    /* Where the program is. */
    int tab;                    /* enum active_tab */
    int view;                   /* enum view_kind, on the Scope tab */
    int decode;                 /* enum decode_kind, on the Decode tab */

    /* What each view says about its own fields. */
    int survey_focused_field;   /* survey.focus, negative when none */
    int scope_focused_field;    /* sv.field_focus, _NONE when none */
    int fm_typing;
    int srd_typing;
    int srd_freq_typing;

    /* Lists that are down over a view. */
    int survey_site_menu_open;
    int survey_antenna_menu_open;
    int survey_band_menu_open;
    int startup_site_menu_open;
    int startup_antenna_menu_open;
    int waterfall_menu_open;

    /*
     * The retrospective signal report, which is modal over whichever
     * waterfall raised it. It takes no typing and still belongs here: it is a
     * reading surface with a receiver running behind it, and the single-letter
     * shortcuts have to stop at it for the reason they stop at Help.
     */
    int waterfall_report_open;

    /* The Scope's frequency window, narrower than the span being received. */
    int scope_zoomed;
};

/* ---- the per-view projections ---------------------------------------- */

/*
 * The survey's fields are live only on its own tab -- the view is not drawn
 * elsewhere, and a focus left behind on a tab switch must not take the digits
 * belonging to another screen.
 */
static inline int survey_input_typing(int tab, int focused_field) {
    return tab == TAB_SURVEY && focused_field >= 0;
}

/* The Scope's three header fields, which report by index rather than by flag. */
static inline int scope_input_typing(int focused_field) {
    return focused_field != VIEW_INPUT_SCOPE_FIELD_NONE;
}

/* The FM view's frequency field. */
static inline int fm_input_typing(int typing) {
    return typing != 0;
}

/*
 * Both of the SRD view's fields. It had one until 2026-09-15, and the second
 * is the reason this enum exists: the only thing that made `srd_editing()`
 * correct when the frequency field arrived was that somebody remembered.
 */
static inline int srd_input_typing(int typing, int freq_typing) {
    return typing || freq_typing;
}

/* ---- the fold --------------------------------------------------------- */

/*
 * Which surfaces are taking typed characters, as a bitmask over
 * `enum typing_surface`.
 */
static inline unsigned view_input_typing_surfaces(const struct view_input *v) {
    unsigned mask = 0;

    if (!v)
        return 0;
    if (survey_input_typing(v->tab, v->survey_focused_field))
        mask |= TYPING_SURFACE_BIT(TYPING_SURVEY_RANGE);
    if (scope_input_typing(v->scope_focused_field))
        mask |= TYPING_SURFACE_BIT(TYPING_SCOPE_HEADER);
    if (fm_input_typing(v->fm_typing))
        mask |= TYPING_SURFACE_BIT(TYPING_FM_FREQUENCY);
    if (v->srd_typing)
        mask |= TYPING_SURFACE_BIT(TYPING_SRD_RECORD);
    if (v->srd_freq_typing)
        mask |= TYPING_SURFACE_BIT(TYPING_SRD_FREQUENCY);
    if (v->settings_open)
        mask |= TYPING_SURFACE_BIT(TYPING_SETTINGS_PANEL);
    if (v->startup_open)
        mask |= TYPING_SURFACE_BIT(TYPING_STARTUP_FORM);
    return mask;
}

/*
 * Make one surface report itself, and nothing else.
 *
 * The one place the enum is mapped back onto the field that carries it, and
 * the reason `check-input` can sweep every surface rather than the five
 * somebody thought of. A value added to the enum and not handled here leaves
 * the state untouched, so the sweep sees a surface that reports no typing and
 * the suite goes red -- which is the point.
 */
static inline void view_input_set_typing(struct view_input *v,
                                         enum typing_surface surface) {
    if (!v)
        return;
    switch (surface) {
    case TYPING_SURVEY_RANGE:
        v->tab = TAB_SURVEY;
        v->survey_focused_field = 0;
        break;
    case TYPING_SCOPE_HEADER:
        v->scope_focused_field = VIEW_INPUT_SCOPE_FIELD_NONE + 1;
        break;
    case TYPING_FM_FREQUENCY:
        v->fm_typing = 1;
        break;
    case TYPING_SRD_RECORD:
        v->srd_typing = 1;
        break;
    case TYPING_SRD_FREQUENCY:
        v->srd_freq_typing = 1;
        break;
    case TYPING_SETTINGS_PANEL:
        v->settings_open = 1;
        break;
    case TYPING_STARTUP_FORM:
        v->startup_open = 1;
        break;
    case TYPING_SURFACE_COUNT:
        break;
    }
}

/* For the log and for a failing check to say which surface it meant. */
static inline const char *typing_surface_name(enum typing_surface surface) {
    switch (surface) {
    case TYPING_SURVEY_RANGE:   return "survey range field";
    case TYPING_SCOPE_HEADER:   return "Scope header field";
    case TYPING_FM_FREQUENCY:   return "FM frequency field";
    case TYPING_SRD_RECORD:     return "SRD record-duration field";
    case TYPING_SRD_FREQUENCY:  return "SRD frequency field";
    case TYPING_SETTINGS_PANEL: return "settings panel";
    case TYPING_STARTUP_FORM:   return "startup form";
    case TYPING_SURFACE_COUNT:  break;
    }
    return "unknown surface";
}

/*
 * Whether a list is down over the view.
 *
 * The waterfall's right-click menu is one of these and reached the routing
 * nowhere until 2026-09-15, so Escape quit the program rather than closing it
 * (`.scratch/iq-ring-buffer/issues/03-*`).
 */
static inline int view_input_menu_open(const struct view_input *v) {
    if (!v)
        return 0;
    return v->survey_site_menu_open || v->survey_antenna_menu_open ||
           v->survey_band_menu_open || v->startup_site_menu_open ||
           v->startup_antenna_menu_open || v->waterfall_menu_open;
}

/*
 * The whole of `struct input_state`, from what the views reported.
 *
 * A fold with nothing of its own to decide: every field is either copied or
 * the answer of a projection above. That is what makes the step before
 * `input_route()` reachable, which is this module's reason to exist.
 */
static inline struct input_state view_input_state(const struct view_input *v) {
    struct input_state s;
    unsigned typing;

    /*
     * Zeroed first, for the reason the check's own helper is: assigning every
     * field by name here is what left a new one holding whatever was on the
     * stack.
     */
    s.help_open = 0;
    s.settings_open = 0;
    s.calibration_open = 0;
    s.scan_open = 0;
    s.startup_open = 0;
    s.tab = 0;
    s.view = 0;
    s.menu_open = 0;
    s.decode = 0;
    s.text_focus = 0;
    s.scope_zoomed = 0;
    s.report_open = 0;
    if (!v)
        return s;

    typing = view_input_typing_surfaces(v);

    s.help_open = v->help_open;
    s.settings_open = v->settings_open;
    s.calibration_open = v->calibration_open;
    s.scan_open = v->scan_open;
    s.startup_open = v->startup_open;
    s.tab = v->tab;
    s.view = v->view;
    s.decode = v->decode;
    s.menu_open = view_input_menu_open(v);
    s.text_focus = (typing & TYPING_IN_VIEW_MASK) != 0;
    s.scope_zoomed = v->scope_zoomed;
    s.report_open = v->waterfall_report_open;
    return s;
}

#endif /* VIEW_INPUT_H */
