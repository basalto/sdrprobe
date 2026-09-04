#ifndef SDRGUI_GEOMETRY_H
#define SDRGUI_GEOMETRY_H

#include <raylib.h>

/*
 * Where a chart's parts go inside the rectangle it was handed, and which part
 * a pointer is over.
 *
 * These decisions used to sit inside the drawing, and one of them shipped
 * wrong: the scan chart's bars are drawn inside a label gutter, the hit test
 * mapped the pointer across the whole rectangle instead, and clicking selected
 * a channel one or two to the right of the bar under the cursor.
 *
 * A header rather than a `.c` for one reason: raylib is needed here for the
 * `Rectangle` type and nothing else, so a check can compile this with raylib's
 * headers and link `-lm`, the way tests/layout_test.c already does. Anything
 * needing `MeasureText` -- which needs a font, which needs a window -- stays
 * with the drawing and passes its answer in as `gutter` (ADR-0012).
 */

/*
 * The plotting area inside a chart's outer rectangle: the caption strip comes
 * off the top, the value labels off the left, and 8 px off the bottom for the
 * axis captions. A caller cannot compute this, because the gutter depends on
 * how wide the labels turn out to be -- which is why the chart reserves it
 * rather than being told.
 */
static inline Rectangle sdrgui_chart_area(Rectangle outer, float gutter,
                                          float caption_h) {
    Rectangle plot = { outer.x + gutter, outer.y + caption_h,
                       outer.width - gutter, outer.height - caption_h - 8.0f };
    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
    return plot;
}

/*
 * The next sensible number at or above this one: 1, 2 or 5 times a power of
 * ten.
 *
 * For an axis whose maximum follows the data. A chart scaled to exactly
 * 1.15 times whatever the largest value happens to be redraws its axis every
 * time that value moves, and a chart component reserves its left gutter from
 * how wide the labels render -- so the plot itself shifts sideways. Counts
 * hovering around ten make it flicker: 9.2 needs three characters and 10.4
 * needs four, and the boundary gets crossed every few seconds.
 *
 * Snapping to a round number costs a little headroom and makes the axis hold
 * still until the data genuinely outgrows it.
 */
static inline float sdrgui_nice_ceiling(float value) {
    float decade = 1.0f;
    float scaled;

    if (!(value > 0.0f))
        return 1.0f;
    while (value >= 10.0f * decade)
        decade *= 10.0f;
    while (value < decade)
        decade /= 10.0f;
    scaled = value / decade;
    if (scaled <= 1.0f)
        return decade;
    if (scaled <= 2.0f)
        return 2.0f * decade;
    if (scaled <= 5.0f)
        return 5.0f * decade;
    return 10.0f * decade;
}

static inline int sdrgui_point_in(Rectangle rect, float x, float y) {
    return x >= rect.x && x <= rect.x + rect.width && y >= rect.y &&
           y <= rect.y + rect.height;
}

/*
 * How wide one bar is when `count` of them share a plot. Drawing and hit
 * testing must agree on this or the two describe different bars; they share it
 * here so they cannot drift apart.
 */
static inline float sdrgui_bar_width(Rectangle plot, int count) {
    if (count <= 0)
        return 0.0f;
    return plot.width / (float)count;
}

/*
 * Where bar `index` (0-based) starts. The bar drawn is a pixel narrower so the
 * bars read as separate, but it starts here, and the hit test below must agree
 * with this and not with the drawn width.
 */
static inline float sdrgui_bar_left(Rectangle plot, int count, int index) {
    return plot.x + (float)index * sdrgui_bar_width(plot, count);
}

/*
 * Which bar a point is over, 0-based, or -1 when the point is outside the
 * plot. Note `plot`, not the chart's outer rectangle: passing the outer one is
 * exactly the bug this exists to prevent, and it is off by however wide the
 * label gutter is -- about two channels on a 124-channel chart.
 */
static inline int sdrgui_bar_index_at(Rectangle plot, int count, float x,
                                      float y) {
    float width = sdrgui_bar_width(plot, count);
    int index;

    if (count <= 0 || width <= 0.0f || !sdrgui_point_in(plot, x, y))
        return -1;
    index = (int)((x - plot.x) / width);
    if (index < 0)
        index = 0;
    if (index >= count)
        index = count - 1;
    return index;
}

/*
 * What a waterfall strip actually covers, and which slice of its texture to
 * draw for that.
 *
 * The texture always holds the whole received span -- one column per bin, from
 * `center - rate/2` to `center + rate/2` -- because that is what the rows were
 * written with. Showing part of it is therefore a source-rectangle question,
 * not a re-render, which is why a zoom costs nothing.
 *
 * This was gated on the calibration overlay's own flag for as long as the
 * overlay was the only caller that zoomed. The Scope's frequency window then
 * arrived, computed a range, handed it over, and was silently discarded: the
 * drag worked, the axis never moved, and nothing anywhere said why. The gate
 * is gone rather than widened -- a zoom is requested by asking for one, and a
 * flag naming a *screen* has no business deciding whether the request is
 * honoured.
 *
 * A request outside the received span is clamped to it rather than refused: a
 * pan that runs off the edge is how retuning is triggered, and it must keep
 * drawing something while the receiver catches up.
 */
struct sdrgui_waterfall_span {
    double lower_hz;      /* what the drawn strip covers */
    double upper_hz;
    float source_x;       /* the slice of the texture that covers it */
    float source_width;
};

static inline struct sdrgui_waterfall_span
sdrgui_waterfall_span(double center_hz, double sample_rate,
                      double zoom_center_hz, double zoom_half_width_hz,
                      float texture_width) {
    double full_lower = center_hz - sample_rate / 2.0;
    double full_upper = center_hz + sample_rate / 2.0;
    struct sdrgui_waterfall_span span;
    double lower, upper;

    span.lower_hz = full_lower;
    span.upper_hz = full_upper;
    span.source_x = 0.0f;
    span.source_width = texture_width;
    if (zoom_center_hz <= 0.0 || zoom_half_width_hz <= 0.0 ||
        full_upper <= full_lower)
        return span;

    lower = zoom_center_hz - zoom_half_width_hz;
    upper = zoom_center_hz + zoom_half_width_hz;
    if (lower < full_lower)
        lower = full_lower;
    if (upper > full_upper)
        upper = full_upper;
    if (upper - lower <= 1.0)
        return span;

    span.lower_hz = lower;
    span.upper_hz = upper;
    span.source_x = (float)((lower - full_lower) / (full_upper - full_lower)) *
                    texture_width;
    span.source_width =
        (float)((upper - lower) / (full_upper - full_lower)) * texture_width;
    return span;
}

#endif
