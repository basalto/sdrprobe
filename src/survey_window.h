#ifndef SURVEY_WINDOW_H
#define SURVEY_WINDOW_H

#include "app.h"
#include "freq_window.h"

/*
 * The adapters between `freq_window.h`'s plain doubles and the survey view's
 * own state.
 *
 * The window arithmetic itself lives in `freq_window.h`, where
 * `tests/freq_window_test.c` exercises it with no window and no receiver.
 * These two say which numbers to hand it: the range that *exists* is what
 * was swept once there is a sweep, and what the fields say before that --
 * without that second half the view has no extent until the first sweep, and
 * zooming, panning and dragging a rectangle all quietly did nothing on a
 * freshly opened survey, dividing by a span of zero.
 *
 * They were `static` in `view_survey.c`, which is why the four functions
 * built on them were too -- and three of those four are called from
 * `survey_runtime.c`, which is in the raylib-free set
 * (`.scratch/layer-boundaries/issues/04-*`). A header rather than a second
 * copy: two spellings of "which hertz is this bin" is what
 * `chart_window.h`'s own comment says this program has already paid for
 * once.
 */
static inline struct freq_window
survey_freq_window_of(const struct survey_view *s) {
    const struct survey_session *ss = &s->session;
    struct freq_window w;

    w.data_lower_hz = ss->bins > 0 ? ss->lower_hz : s->field_lower_hz;
    w.data_upper_hz = ss->bins > 0 ? ss->upper_hz : s->field_upper_hz;
    w.view_lower_hz = s->view_lower_hz;
    w.view_upper_hz = s->view_upper_hz;
    return w;
}

static inline void survey_freq_window_put(struct survey_view *s,
                                          const struct freq_window *w) {
    s->view_lower_hz = w->view_lower_hz;
    s->view_upper_hz = w->view_upper_hz;
}

#endif
