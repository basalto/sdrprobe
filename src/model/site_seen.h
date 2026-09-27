#ifndef SITE_SEEN_H
#define SITE_SEEN_H

/*
 * How a signal has behaved at a site, as a value that crosses the wire.
 *
 * It lived in `site_history.h`, which is `runtime/` -- it reads and writes a
 * file. But the *verdict* is a model: `survey_view_model` carries it, the
 * Viewer link spells it as `"seen":"<name>"`, and `views/survey.js` keys a
 * table by that name. A contract a reader depends on cannot live in the
 * layer that happens to compute it (ADR-0028), which is why this is here and
 * `site_history_seen()` -- the computing half -- is not.
 */
/*
 * How a signal has behaved here, for the operator rather than for the code.
 *
 * `site_status` answers the question the marks need -- has this site heard it
 * before -- and nothing more. This is the fuller answer: heard every time,
 * heard sometimes, heard before but not now.
 */
enum site_seen {
    SITE_SEEN_UNKNOWN = 0,   /* too little history to say anything */
    SITE_SEEN_NEW,
    SITE_SEEN_STEADY,
    SITE_SEEN_INTERMITTENT,
    /* Intermittent, and the intermittency follows the clock. */
    SITE_SEEN_DIURNAL,
    SITE_SEEN_MISSING
};

/*
 * The name a reader sees -- the window's candidate list and `survey_state`
 * both. It crosses the wire as this name and never as the enum's integer: a
 * second reader that re-declares an enum's order gets it wrong silently,
 * which is what `web-visualization/15` cost.
 *
 * Inline here rather than in site_history.c, so a reader that only needs to
 * *spell* a verdict does not have to link the whole history -- the same
 * reason `survey_shape_name()` and `survey_mark_name()` are inline.
 */
static inline const char *site_seen_name(enum site_seen seen) {
    switch (seen) {
    case SITE_SEEN_NEW:          return "new";
    case SITE_SEEN_STEADY:       return "steady";
    case SITE_SEEN_INTERMITTENT: return "on/off";
    case SITE_SEEN_DIURNAL:      return "by hour";
    case SITE_SEEN_MISSING:      return "gone";
    case SITE_SEEN_UNKNOWN:      break;
    }
    return "-";
}

#endif
