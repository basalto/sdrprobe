#ifndef PANEL_ROWS_H
#define PANEL_ROWS_H

#include <raylib.h>

/*
 * Where the rows inside a panel go, and how many of them fit.
 *
 * This lived in each view's drawing as `y += 20` between calls, and it put a
 * green tick over a fault check-layout could not see: the layout headers model
 * the rectangles, so a check comparing rectangles passes while a panel draws
 * ten rows into room for six. Measured before this existed, at the sizes
 * check-layout already walks:
 *
 *   1000x540   fm signal panel 152 px, worst case 191   over by 39
 *    640x400   fm signal panel  90 px, worst case 191   over by 101
 *   1000x540   tetra identity  133 px, worst case 146   over by 13
 *
 * A header rather than a `.c`, for `sdrgui_geometry.h`'s reason: raylib is
 * needed here for `Rectangle` and nothing else, so a check compiles it with
 * raylib's headers and links `-lm`, with no window and no receiver (ADR-0012).
 *
 * Shared rather than copied into each layout header. The per-view headers
 * stand alone on purpose and this is the one thing they should not each own:
 * four copies of a row step is how five panels end up with four different row
 * heights, which is the fault this was written to prevent wearing a different
 * coat.
 *
 * `capacity` is the load-bearing field. **A row past it is not drawn at all**
 * -- off the bottom edge is worse than absent -- so a caller orders its rows
 * and the ones that fit are the ones that matter.
 */

struct panel_rows {
    float first_y;      /* baseline of row 0 */
    float step;
    float label_x;
    float label_width;  /* before the value begins */
    float value_x;
    float value_width;
    float footer_y;     /* the lowest a footer may sit */
    int capacity;       /* rows that fit without leaving the panel */
};

/*
 * `caption_drop` is how far under the panel's top the first row sits, `step`
 * the distance between rows and `footer_height` what to reserve underneath.
 * All three are the view's, because a panel of eighteen-point network fields
 * and one of fifteen-point decode statistics do not want the same spacing --
 * what they want is the same arithmetic.
 *
 * `gutter_fraction` splits label from value. A flat gutter in pixels was what
 * the LTE panel had, and at 170 px a half-panel on a narrow window left about
 * thirteen characters for the value, which truncated the numbers rather than
 * the labels. Proportional with a cap keeps the value readable where it
 * matters; pass 0 for a panel whose rows are single strings.
 */
static inline struct panel_rows panel_rows_for(Rectangle panel,
                                               float caption_drop, float step,
                                               float footer_height,
                                               float gutter_fraction,
                                               float gutter_cap) {
    struct panel_rows r;
    float inner = panel.width - 24.0f;
    float gutter, room;

    r.first_y = panel.y + caption_drop;
    r.step = step > 1.0f ? step : 1.0f;
    r.label_x = panel.x + 12.0f;

    gutter = inner * gutter_fraction;
    if (gutter_cap > 0.0f && gutter > gutter_cap)
        gutter = gutter_cap;
    if (gutter < 0.0f)
        gutter = 0.0f;
    r.label_width = gutter > 8.0f ? gutter - 8.0f : (inner > 0.0f ? inner : 1.0f);
    r.value_x = r.label_x + gutter;
    r.value_width = panel.x + panel.width - 12.0f - r.value_x;
    if (r.value_width < 1.0f)
        r.value_width = 1.0f;
    if (r.label_width < 1.0f)
        r.label_width = 1.0f;

    room = panel.y + panel.height - footer_height - r.first_y;
    r.capacity = room > 0.0f ? (int)(room / r.step) : 0;
    r.footer_y = r.first_y + (float)r.capacity * r.step;
    return r;
}

/*
 * Where a footer goes when the caller drew fewer rows than fit.
 *
 * `footer_y` above is the panel's own floor, and putting a "last seen" line
 * there on a tall panel leaves a hand's width of gap above it -- and drops
 * whatever follows onto the window's corner. The footer belongs under what was
 * actually drawn, and never past where it would leave the panel.
 */
static inline float panel_footer_after(const struct panel_rows *rows,
                                       int used) {
    float y;

    if (used < 0)
        used = 0;
    if (used > rows->capacity)
        used = rows->capacity;
    y = rows->first_y + (float)used * rows->step + 4.0f;
    return y > rows->footer_y ? rows->footer_y : y;
}

/* Whether a row index may be drawn. The one thing every caller must ask. */
static inline int panel_row_visible(const struct panel_rows *rows, int index) {
    return index >= 0 && index < rows->capacity;
}

/* The baseline of a row the caller has checked is visible. */
static inline float panel_row_y(const struct panel_rows *rows, int index) {
    return rows->first_y + (float)index * rows->step;
}

#endif
