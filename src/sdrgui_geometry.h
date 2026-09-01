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

#endif
