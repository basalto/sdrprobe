#ifndef CHART_WINDOW_H
#define CHART_WINDOW_H

#include <stdint.h>

#include "core/freq_window.h"

/*
 * A frequency chart's view of what the receiver is delivering, and the
 * gestures that move it.
 *
 * The Scope had this to itself: drag a region to zoom, Up and Down to zoom
 * about the pointer, Left and Right to pan, 0 to go back to the whole
 * received span, and a pan that runs off the end retunes rather than
 * stopping. Every decode view draws a waterfall of the same samples and had
 * none of it, which made a zoom something you could only do by leaving the
 * view you were decoding in.
 *
 * The state is bundled rather than copied into each view for the usual
 * reason: five copies of "which pixel is which hertz" is five chances to get
 * the label gutter wrong, and this program has already paid for that once.
 *
 * `anchor_hz` and `anchor_rate` are what the window was last synchronised
 * against. When the receiver moves, the *width* the reader chose is carried
 * to the new tuning rather than thrown away -- retuning is how panning
 * continues past the edge, and a zoom that reset itself every time would make
 * that unusable.
 */
struct chart_window {
    struct freq_window freq;
    int dragging;
    float drag_from_x;
    float drag_to_x;
    uint32_t anchor_hz;
    uint32_t anchor_rate;
};

#define CHART_ZOOM_STEP 1.4
#define CHART_PAN_FRACTION 0.20
/* Nothing narrower than this, on a linear axis. Twenty kilohertz is already
   finer than any bin the Scope produces. */
#define CHART_MIN_SPAN_HZ 20000.0

/*
 * How narrow a window may get, which is not the same question on the two
 * kinds of axis.
 *
 * On a channel axis the labels are channel *centres*, drawn wherever one
 * falls inside the drawn range -- so a window narrower than the channel
 * spacing can contain no centre at all and leave the axis blank, an ARFCN
 * chart with no ARFCN on it. A window exactly one spacing wide always
 * contains exactly one multiple of the spacing, whatever its phase, so that
 * is the floor.
 *
 * Pass 0 for `channel_spacing_hz` on a linear axis.
 */
static inline double chart_min_span(double channel_spacing_hz) {
    if (channel_spacing_hz > CHART_MIN_SPAN_HZ)
        return channel_spacing_hz;
    return CHART_MIN_SPAN_HZ;
}

/*
 * Declared here, defined in chart_window.c. The window's state above is plain
 * arithmetic and stays; the one entry point that takes a `Rectangle` is in
 * `view.h` instead, because a rectangle is raylib's and this header is
 * included by `app.h` (`.scratch/layer-boundaries/issues/01-*`).
 */
struct app;

void chart_window_sync(struct chart_window *w, uint32_t centre_hz,
                       uint32_t sample_rate, double min_span);
void chart_window_centre_on(struct chart_window *w, double centre_hz,
                            double half_width_hz, double min_span);
void chart_window_zoom_of(const struct chart_window *w, double *centre_hz,
                          double *half_width_hz);

/*
 * One frame of pointer and key state, as plain numbers.
 *
 * `chart_window.c` was the **only** file outside the GUI set that called
 * raylib, and it called it for exactly six things: the pointer's position,
 * whether the left button went down or is held, whether it is over the plot,
 * and the four arrow keys (`.scratch/layer-boundaries/issues/04-*`). Reading
 * those is the window's job; deciding what a drag or a zoom *means* is not,
 * which is this repository's own rule -- a function that reads input may not
 * also decide (ADR-0012).
 *
 * The two flags are raylib's own distinction and both are needed: `press` is
 * the edge (the button went down this frame) and `held` is the level. A drag
 * starts on the edge, continues on the level, and ends when neither is true,
 * so collapsing them to one would make a drag either never start or never
 * end.
 */
struct chart_gesture_input {
    double pointer_x;   /* in the plot's own coordinates */
    int pointer_over;   /* the pointer is inside the plot */
    int press;          /* the left button went down this frame */
    int held;           /* the left button is down */
    int zoom_in;        /* Up, pressed or repeating */
    int zoom_out;       /* Down */
    int pan;            /* +1 for Right, -1 for Left, 0 for neither */
};

/*
 * What a frame of that input does to the window, and what it could not do:
 * the return is the hertz the receiver would have to move for a pan to
 * continue, and the caller decides whether that is allowed (see the file
 * comment in chart_window.c).
 *
 * `reset_zoom` rather than an `enum chart_key`, because that enum lives in
 * `view.h` and only one of its four values reaches this arithmetic.
 */
double chart_window_gesture(struct chart_window *w, double plot_x,
                            double plot_width,
                            const struct chart_gesture_input *in,
                            int reset_zoom, double min_span);

/* The band being dragged out, in hertz, for the chart to draw. Plot geometry
   as two doubles rather than a `Rectangle`: it only ever read those two
   fields, and the type was the whole of what kept this out of a raylib-free
   build. */
int chart_window_drag_of(const struct chart_window *w, double plot_x,
                         double plot_width, double *lower_hz,
                         double *upper_hz);

#endif
