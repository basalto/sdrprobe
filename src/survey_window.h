#ifndef SURVEY_WINDOW_H
#define SURVEY_WINDOW_H

/*
 * The band survey's window arithmetic: which part of a swept range is on
 * screen, how zooming and panning move it, and what pressing Sweep would
 * therefore sweep.
 *
 * It lives in a header of its own, as plain doubles with no receiver, no
 * raylib and no application state, for the reason survey_layout.h does:
 * tests/survey_window_test.c can then exercise it without opening a window.
 * That matters more here than for geometry. Every one of these decisions had
 * to be checked by hand -- building an instrumented binary, running it against
 * the receiver, reading a printf -- because synthetic clicks and keys do not
 * reach the window on every desktop, and two of them shipped wrong anyway:
 * a zoom that did nothing before the first sweep because the window had no
 * extent, and a Sweep that ignored the region you had just selected because
 * the test for "is this narrower" also required a sweep to exist.
 *
 * Both were arithmetic, and arithmetic can be checked in a millisecond with no
 * hardware at all.
 */

/*
 * `data` is the range that exists: what was swept once there is a sweep, and
 * what is typed in the range fields before that. `view` is the part of it on
 * screen. The window is never wider than the data and never narrower than
 * min_span.
 */
struct survey_window {
    double data_lower_hz;
    double data_upper_hz;
    double view_lower_hz;
    double view_upper_hz;
};

static inline int survey_window_has_data(const struct survey_window *w) {
    return w->data_upper_hz > w->data_lower_hz;
}

static inline void survey_window_reset(struct survey_window *w) {
    w->view_lower_hz = w->data_lower_hz;
    w->view_upper_hz = w->data_upper_hz;
}

/* Keep the window inside the data and no narrower than min_span. */
static inline void survey_window_clamp(struct survey_window *w,
                                       double min_span) {
    double span;

    if (!survey_window_has_data(w)) {
        survey_window_reset(w);
        return;
    }
    if (w->view_upper_hz <= w->view_lower_hz)
        survey_window_reset(w);
    span = w->view_upper_hz - w->view_lower_hz;
    if (span < min_span)
        span = min_span;
    if (span > w->data_upper_hz - w->data_lower_hz)
        span = w->data_upper_hz - w->data_lower_hz;
    if (w->view_lower_hz < w->data_lower_hz)
        w->view_lower_hz = w->data_lower_hz;
    if (w->view_lower_hz + span > w->data_upper_hz)
        w->view_lower_hz = w->data_upper_hz - span;
    w->view_upper_hz = w->view_lower_hz + span;
}

/*
 * Zoom by `factor` (below 1 zooms in) about `anchor_hz` when `has_anchor`,
 * otherwise about the middle of the window. Anchoring exists so that zooming
 * in keeps a selected candidate in sight instead of walking off the edge.
 */
static inline void survey_window_zoom(struct survey_window *w, double factor,
                                      double anchor_hz, int has_anchor,
                                      double min_span) {
    double span;
    double anchor;
    double fraction;
    double next;

    if (w->view_upper_hz <= w->view_lower_hz)
        survey_window_reset(w);
    span = w->view_upper_hz - w->view_lower_hz;
    if (span <= 0.0)
        return;
    anchor = w->view_lower_hz + span / 2.0;
    if (has_anchor && anchor_hz >= w->view_lower_hz &&
        anchor_hz <= w->view_upper_hz)
        anchor = anchor_hz;
    fraction = (anchor - w->view_lower_hz) / span;
    next = span * factor;
    if (next < min_span)
        next = min_span;
    w->view_lower_hz = anchor - next * fraction;
    w->view_upper_hz = w->view_lower_hz + next;
    survey_window_clamp(w, min_span);
}

/* Walk the window by a fraction of its span. Returns 0 when it could not move,
   which is what a window already covering the whole range should report rather
   than looking like a dead key. */
static inline int survey_window_pan(struct survey_window *w, double fraction,
                                    double min_span) {
    double before;
    double span;

    if (w->view_upper_hz <= w->view_lower_hz)
        survey_window_reset(w);
    before = w->view_lower_hz;
    span = w->view_upper_hz - w->view_lower_hz;
    w->view_lower_hz += span * fraction;
    w->view_upper_hz += span * fraction;
    survey_window_clamp(w, min_span);
    return w->view_lower_hz != before;
}

/*
 * What Sweep will sweep: the window when it is narrower than the data it sits
 * on, the whole data range otherwise. Returns 1 when that narrows the sweep,
 * which is when the survey being replaced is worth keeping a copy of.
 *
 * The data range is the swept one once a sweep exists and the range in the
 * fields before that, which is the distinction this got wrong: requiring a
 * sweep to already exist made the first sweep of a freshly opened survey
 * ignore the region just selected.
 */
static inline int survey_window_sweep_target(const struct survey_window *w,
                                             double *from, double *to) {
    if (w->view_upper_hz <= w->view_lower_hz ||
        !survey_window_has_data(w)) {
        *from = w->data_lower_hz;
        *to = w->data_upper_hz;
        return 0;
    }
    *from = w->view_lower_hz;
    *to = w->view_upper_hz;
    return *from > w->data_lower_hz + 1.0 || *to < w->data_upper_hz - 1.0;
}

/* The frequency at the middle of a bin, given how many bins cover the data. */
static inline double survey_window_bin_hz(const struct survey_window *w,
                                          int bins, int bin) {
    if (bins <= 0)
        return w->data_lower_hz;
    return w->data_lower_hz + (w->data_upper_hz - w->data_lower_hz) *
                                  ((double)bin + 0.5) / (double)bins;
}

/* Is that bin's frequency on screen? The candidate list shows only what is. */
static inline int survey_window_bin_visible(const struct survey_window *w,
                                            int bins, int bin) {
    double hz;

    if (w->view_upper_hz <= w->view_lower_hz)
        return 1;
    hz = survey_window_bin_hz(w, bins, bin);
    return hz >= w->view_lower_hz && hz <= w->view_upper_hz;
}

#endif
