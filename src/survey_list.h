#ifndef SURVEY_LIST_H
#define SURVEY_LIST_H

#include <raylib.h>

/*
 * The candidate list's rows: how many fit, which one the pointer is over, and
 * how far down the list is scrolled.
 *
 * It did not scroll at all. The panel drew the first however-many candidates
 * that fitted -- about seventeen -- and stopped, so a full-tuner sweep that
 * found sixty put forty-three of them somewhere no pointer could reach. The
 * keyboard walk did work, and looked broken for the same reason: Up and Down
 * moved a selection that had left the drawn rows, so the screen did not change
 * and there was nothing to say the list had a below.
 *
 * Clicking a peak in the *chart* made it worse, because a chart peak can be
 * any rank at all: pick the loudest thing at 900 MHz, and the list highlights
 * a row thirty places down that was never drawn.
 *
 * The row geometry used to be two literals -- 44 and 22 -- written once in the
 * draw and again in the hit test, which is how a list ends up selecting a
 * different row from the one under the pointer. It is here once now, with the
 * scroll arithmetic that has to agree with it.
 */

#define SURVEY_LIST_ROW_H 22.0f
/* Rows per wheel notch. Three, because one is tedious over sixty candidates
   and a whole page loses your place. */
#define SURVEY_LIST_WHEEL_ROWS 3
#define SURVEY_LIST_HEADER_H 44.0f   /* caption and column titles */
/* The strip along the bottom, where the list says where in itself it is.
   Reserved rather than drawn over the last row, which is what a footer
   placed at height - 20 did when the rows filled the panel. */
#define SURVEY_LIST_FOOTER_H 24.0f

/* How many rows the panel can draw. */
static inline int survey_list_rows(Rectangle rect) {
    float room = rect.height - SURVEY_LIST_HEADER_H - SURVEY_LIST_FOOTER_H;
    int rows = (int)(room / SURVEY_LIST_ROW_H);

    return rows > 0 ? rows : 0;
}

/* The furthest down the list can be scrolled: enough to bring the last
   candidate onto the last row, and no further. A list that fits entirely
   never scrolls. */
static inline int survey_list_max_scroll(int count, int rows) {
    int max;

    /* A panel with no room for a row has nothing to scroll through. Without
       this, count - rows is the whole list and the scroll runs away into a
       panel that cannot draw any of it. */
    if (rows <= 0)
        return 0;
    max = count - rows;
    return max > 0 ? max : 0;
}

static inline int survey_list_clamp_scroll(int scroll, int count, int rows) {
    int max = survey_list_max_scroll(count, rows);

    if (scroll > max)
        scroll = max;
    return scroll > 0 ? scroll : 0;
}

/*
 * Where the list has to be scrolled to for `rank` to be on screen.
 *
 * The minimum move, so stepping down a long list scrolls one row at a time
 * rather than jumping the selection to the middle every press -- but a rank
 * that is nowhere near the current window (the chart was clicked) lands
 * against whichever edge it came from, which is the same rule applied to a
 * bigger distance.
 */
static inline int survey_list_scroll_to(int scroll, int rank, int count,
                                        int rows) {
    if (rank < 0 || rows <= 0)
        return survey_list_clamp_scroll(scroll, count, rows);
    if (rank < scroll)
        scroll = rank;
    else if (rank > scroll + rows - 1)
        scroll = rank - rows + 1;
    return survey_list_clamp_scroll(scroll, count, rows);
}

/* The rank under the pointer, or -1. Rows are counted from the scroll offset,
   so this and the draw cannot disagree about which candidate a row is. */
static inline int survey_list_rank_at(Rectangle rect, int scroll, int count,
                                      int rows, Vector2 point) {
    float top = rect.y + SURVEY_LIST_HEADER_H;
    int row;

    /* Written out rather than CheckCollisionPointRec, which is in the raylib
       library rather than its header -- and this has to link against nothing
       but libm to stay checkable (ADR-0012). */
    if (point.x < rect.x || point.x > rect.x + rect.width ||
        point.y < top || point.y > rect.y + rect.height)
        return -1;
    row = (int)((point.y - top) / SURVEY_LIST_ROW_H);
    if (row < 0 || row >= rows)
        return -1;
    if (scroll + row >= count)
        return -1;
    return scroll + row;
}

/* Where a row is drawn. */
static inline float survey_list_row_y(Rectangle rect, int row) {
    return rect.y + SURVEY_LIST_HEADER_H + (float)row * SURVEY_LIST_ROW_H;
}

#endif
