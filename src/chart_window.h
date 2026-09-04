#ifndef CHART_WINDOW_H
#define CHART_WINDOW_H

#include <raylib.h>
#include <stdint.h>

#include "freq_window.h"

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

/* Declared here, defined in chart_window.c: they need raylib's input and the
   chart_key enum from view.h. */
struct app;
enum chart_key;

void chart_window_sync(struct chart_window *w, uint32_t centre_hz,
                       uint32_t sample_rate, double min_span);
/* Handles the drag, the zoom keys and the pan. Returns the hertz the receiver
   must move for the pan to continue past the edge of what it is delivering,
   or 0. Retuning is the caller's, because whether it is allowed differs. */
double chart_window_input(struct chart_window *w, Rectangle plot,
                          enum chart_key key, double min_span);
void chart_window_centre_on(struct chart_window *w, double centre_hz,
                            double half_width_hz, double min_span);
void chart_window_zoom_of(const struct chart_window *w, double *centre_hz,
                          double *half_width_hz);
int chart_window_drag_of(const struct chart_window *w, Rectangle plot,
                         double *lower_hz, double *upper_hz);

#endif
