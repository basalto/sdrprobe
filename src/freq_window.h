#ifndef FREQ_WINDOW_H
#define FREQ_WINDOW_H

/*
 * Which part of a frequency range is on screen, how zooming and panning move
 * it, and what a region dragged with a pointer means.
 *
 * Written for the band survey, where the window is also what a sweep will
 * cover. Nothing in it was ever specific to that: a data range that exists, a
 * view range inside it, and the arithmetic between them is the same question
 * every chart with a frequency along the bottom asks.
 *
 * It lives in a header of its own, as plain doubles with no receiver, no
 * raylib and no application state, for the reason survey_layout.h does:
 * tests/freq_window_test.c can then exercise it without opening a window.
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
struct freq_window {
    double data_lower_hz;
    double data_upper_hz;
    double view_lower_hz;
    double view_upper_hz;
};

static inline int freq_window_has_data(const struct freq_window *w) {
    return w->data_upper_hz > w->data_lower_hz;
}

static inline void freq_window_reset(struct freq_window *w) {
    w->view_lower_hz = w->data_lower_hz;
    w->view_upper_hz = w->data_upper_hz;
}

/* Keep the window inside the data and no narrower than min_span. */
static inline void freq_window_clamp(struct freq_window *w,
                                       double min_span) {
    double span;

    if (!freq_window_has_data(w)) {
        freq_window_reset(w);
        return;
    }
    if (w->view_upper_hz <= w->view_lower_hz)
        freq_window_reset(w);
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
static inline void freq_window_zoom(struct freq_window *w, double factor,
                                      double anchor_hz, int has_anchor,
                                      double min_span) {
    double span;
    double anchor;
    double fraction;
    double next;

    if (w->view_upper_hz <= w->view_lower_hz)
        freq_window_reset(w);
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
    freq_window_clamp(w, min_span);
}

/* Walk the window by a fraction of its span. Returns 0 when it could not move,
   which is what a window already covering the whole range should report rather
   than looking like a dead key. */
static inline int freq_window_pan(struct freq_window *w, double fraction,
                                    double min_span) {
    double before;
    double span;

    if (w->view_upper_hz <= w->view_lower_hz)
        freq_window_reset(w);
    before = w->view_lower_hz;
    span = w->view_upper_hz - w->view_lower_hz;
    w->view_lower_hz += span * fraction;
    w->view_upper_hz += span * fraction;
    freq_window_clamp(w, min_span);
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
static inline int freq_window_sweep_target(const struct freq_window *w,
                                             double *from, double *to) {
    if (w->view_upper_hz <= w->view_lower_hz ||
        !freq_window_has_data(w)) {
        *from = w->data_lower_hz;
        *to = w->data_upper_hz;
        return 0;
    }
    *from = w->view_lower_hz;
    *to = w->view_upper_hz;
    return *from > w->data_lower_hz + 1.0 || *to < w->data_upper_hz - 1.0;
}

/* The frequency at the middle of a bin, given how many bins cover the data. */
static inline double freq_window_bin_hz(const struct freq_window *w,
                                          int bins, int bin) {
    if (bins <= 0)
        return w->data_lower_hz;
    return w->data_lower_hz + (w->data_upper_hz - w->data_lower_hz) *
                                  ((double)bin + 0.5) / (double)bins;
}

/* Is that bin's frequency on screen? The candidate list shows only what is. */
static inline int freq_window_bin_visible(const struct freq_window *w,
                                            int bins, int bin) {
    double hz;

    if (w->view_upper_hz <= w->view_lower_hz)
        return 1;
    hz = freq_window_bin_hz(w, bins, bin);
    return hz >= w->view_lower_hz && hz <= w->view_upper_hz;
}

/*
 * Pixels, which is the half a sweep never needed.
 *
 * A chart draws its window across a rectangle, so a pointer at an x maps to a
 * frequency and a frequency maps back to an x. Both here rather than in each
 * chart, because a drag is measured by one and drawn by the other, and two
 * copies of this arithmetic is how a selection ends up landing somewhere
 * other than where it was drawn.
 */
static inline double freq_window_hz_at(const struct freq_window *w,
                                       float plot_x, float plot_width,
                                       float x) {
    double span;

    if (!w || plot_width <= 0.0f)
        return 0.0;
    span = w->view_upper_hz - w->view_lower_hz;
    return w->view_lower_hz +
           span * (double)((x - plot_x) / plot_width);
}

static inline float freq_window_x_at(const struct freq_window *w,
                                     float plot_x, float plot_width,
                                     double hz) {
    double span;

    if (!w)
        return plot_x;
    span = w->view_upper_hz - w->view_lower_hz;
    if (span <= 0.0)
        return plot_x;
    return plot_x + (float)((hz - w->view_lower_hz) / span) *
                        plot_width;
}

/*
 * What a drag between two pixels asks for.
 *
 * Either order -- dragging right to left means the same as left to right, and
 * a reader who does it backwards has not made a mistake. Returns 0 and leaves
 * the outputs alone when the drag is narrower than `min_span`, which is not a
 * failure either: it is a click, or a hand that moved while clicking, and
 * zooming to a few hertz because of one is worse than doing nothing.
 */
static inline int freq_window_drag(const struct freq_window *w,
                                   float plot_x, float plot_width,
                                   float from_x, float to_x, double min_span,
                                   double *lower_hz, double *upper_hz) {
    double a, b, lower, upper;

    if (!w || !lower_hz || !upper_hz)
        return 0;
    a = freq_window_hz_at(w, plot_x, plot_width, from_x);
    b = freq_window_hz_at(w, plot_x, plot_width, to_x);
    lower = a < b ? a : b;
    upper = a < b ? b : a;
    if (upper - lower < min_span)
        return 0;
    *lower_hz = lower;
    *upper_hz = upper;
    return 1;
}

#endif
