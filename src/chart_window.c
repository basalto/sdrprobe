#include "chart_window.h"

#include <math.h>

#include "view.h"

/*
 * The gestures, once, for every chart with a frequency axis.
 *
 * Retuning is deliberately not done here. This returns the hertz the receiver
 * would have to move for a pan to continue, and the caller decides whether
 * that is allowed: the Scope retunes freely, the LTE view may move its centre
 * but never its rate (ADR-0014), and a view reading a capture cannot retune
 * at all. A shared helper that reached for the device would have had to know
 * all three.
 */

void chart_window_sync(struct chart_window *w, uint32_t centre_hz,
                       uint32_t sample_rate, double min_span) {
    double half = (double)sample_rate / 2.0;
    double lower = (double)centre_hz - half;
    double upper = (double)centre_hz + half;

    if (!w)
        return;
    if (w->anchor_hz != centre_hz || w->anchor_rate != sample_rate) {
        double span = w->freq.view_upper_hz - w->freq.view_lower_hz;
        double middle = (w->freq.view_upper_hz + w->freq.view_lower_hz) / 2.0;
        double shift = (double)centre_hz - (double)w->anchor_hz;

        w->freq.data_lower_hz = lower;
        w->freq.data_upper_hz = upper;
        if (w->anchor_rate == 0 || span <= 0.0 ||
            span >= (double)w->anchor_rate) {
            freq_window_reset(&w->freq);
        } else {
            middle += shift;
            w->freq.view_lower_hz = middle - span / 2.0;
            w->freq.view_upper_hz = middle + span / 2.0;
            freq_window_clamp(&w->freq, min_span);
        }
        w->anchor_hz = centre_hz;
        w->anchor_rate = sample_rate;
        return;
    }
    w->freq.data_lower_hz = lower;
    w->freq.data_upper_hz = upper;
    if (w->freq.view_upper_hz <= w->freq.view_lower_hz)
        freq_window_reset(&w->freq);
}

double chart_window_input(struct chart_window *w, Rectangle plot,
                          enum chart_key key, double min_span) {
    Vector2 mouse = GetMousePosition();
    double want = 0.0;

    if (!w)
        return 0.0;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, plot)) {
        w->dragging = 1;
        w->drag_from_x = mouse.x;
        w->drag_to_x = mouse.x;
    } else if (w->dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        w->drag_to_x = mouse.x;
    } else if (w->dragging) {
        double lower = 0.0, upper = 0.0;

        w->dragging = 0;
        if (freq_window_drag(&w->freq, plot.x, plot.width, w->drag_from_x,
                             w->drag_to_x, min_span, &lower, &upper)) {
            w->freq.view_lower_hz = lower;
            w->freq.view_upper_hz = upper;
            freq_window_clamp(&w->freq, min_span);
        }
    }

    {
        /* Zoom about the pointer when it is over the chart, so the feature
           under the cursor stays under it -- and about the middle otherwise,
           which is what a reader using only the keyboard expects. */
        int over = CheckCollisionPointRec(mouse, plot);
        double anchor = freq_window_hz_at(&w->freq, plot.x, plot.width,
                                          mouse.x);
        int in = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
        int out = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);

        if (in)
            freq_window_zoom(&w->freq, 1.0 / CHART_ZOOM_STEP, anchor, over,
                             min_span);
        else if (out)
            freq_window_zoom(&w->freq, CHART_ZOOM_STEP, anchor, over,
                             min_span);
        else if (key == CHART_KEY_RESET_ZOOM)
            freq_window_reset(&w->freq);
    }

    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT) ||
        IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        double direction = (IsKeyPressed(KEY_RIGHT) ||
                            IsKeyPressedRepeat(KEY_RIGHT)) ? 1.0 : -1.0;
        want = freq_window_pan_overflow(&w->freq,
                                        direction * CHART_PAN_FRACTION,
                                        min_span);
    }
    return want;
}

/*
 * Put the window on one channel, which is what the GSM view and the
 * calibration overlay want the moment a channel is chosen.
 *
 * A default rather than a lock: the reader can widen it, drag elsewhere, or
 * press 0 for the whole span, exactly as on any other chart. It used to be a
 * lock, because it arrived as an argument to the drawing rather than as a
 * window -- there was no way to express "start here" separately from "stay
 * here".
 */
void chart_window_centre_on(struct chart_window *w, double centre_hz,
                            double half_width_hz, double min_span) {
    if (!w || centre_hz <= 0.0 || half_width_hz <= 0.0)
        return;
    w->freq.view_lower_hz = centre_hz - half_width_hz;
    w->freq.view_upper_hz = centre_hz + half_width_hz;
    freq_window_clamp(&w->freq, min_span);
}

/* What to hand the waterfall so it draws the window: a centre and a half
   width, or zero when the whole received span is on screen. */
void chart_window_zoom_of(const struct chart_window *w, double *centre_hz,
                          double *half_width_hz) {
    double span, data;

    *centre_hz = 0.0;
    *half_width_hz = 0.0;
    if (!w)
        return;
    span = w->freq.view_upper_hz - w->freq.view_lower_hz;
    data = w->freq.data_upper_hz - w->freq.data_lower_hz;
    if (span > 0.0 && data > 0.0 && span < data - 1.0) {
        *centre_hz = (w->freq.view_lower_hz + w->freq.view_upper_hz) / 2.0;
        *half_width_hz = span / 2.0;
    }
}

/* The band being dragged out, in hertz, for the chart to draw. */
int chart_window_drag_of(const struct chart_window *w, Rectangle plot,
                         double *lower_hz, double *upper_hz) {
    if (!w || !w->dragging)
        return 0;
    *lower_hz = freq_window_hz_at(&w->freq, plot.x, plot.width,
                                  w->drag_from_x);
    *upper_hz = freq_window_hz_at(&w->freq, plot.x, plot.width, w->drag_to_x);
    return 1;
}
