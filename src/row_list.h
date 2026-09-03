#ifndef ROW_LIST_H
#define ROW_LIST_H

#include <raylib.h>

/*
 * A list of rows in a panel: how many fit, which one the pointer is over, and
 * how far down it is scrolled.
 *
 * Written for the survey's candidate list and generalised the moment the FM
 * scan grew a list of its own, because it had arrived at the same bug by the
 * same route. Two copies of this arithmetic would be two chances to get the
 * hit test and the draw disagreeing, which is the exact failure it exists to
 * prevent.
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

/* Rows per wheel notch. Three, because one is tedious over sixty rows and a
   whole page loses your place. */
#define ROW_LIST_WHEEL_ROWS 3

/*
 * What a particular list's rows look like. Passed rather than fixed, because
 * the survey's list has a caption and a column header where the scan's also
 * carries a line of status, and a shared header that assumed one of them
 * would put the other's hit test a row out.
 */
struct row_list_metrics {
    float header_h;   /* caption, column titles, whatever precedes the rows */
    float row_h;
    float footer_h;   /* the strip that says where in the list this is */
};


/* How many rows the panel can draw. */
static inline int row_list_rows(Rectangle rect,
                                struct row_list_metrics m) {
    float room = rect.height - m.header_h - m.footer_h;
    int rows = m.row_h > 0.0f ? (int)(room / m.row_h) : 0;

    return rows > 0 ? rows : 0;
}

/* The furthest down the list can be scrolled: enough to bring the last
   candidate onto the last row, and no further. A list that fits entirely
   never scrolls. */
static inline int row_list_max_scroll(int count, int rows) {
    int max;

    /* A panel with no room for a row has nothing to scroll through. Without
       this, count - rows is the whole list and the scroll runs away into a
       panel that cannot draw any of it. */
    if (rows <= 0)
        return 0;
    max = count - rows;
    return max > 0 ? max : 0;
}

static inline int row_list_clamp_scroll(int scroll, int count, int rows) {
    int max = row_list_max_scroll(count, rows);

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
static inline int row_list_scroll_to(int scroll, int rank, int count,
                                        int rows) {
    if (rank < 0 || rows <= 0)
        return row_list_clamp_scroll(scroll, count, rows);
    if (rank < scroll)
        scroll = rank;
    else if (rank > scroll + rows - 1)
        scroll = rank - rows + 1;
    return row_list_clamp_scroll(scroll, count, rows);
}

/* The rank under the pointer, or -1. Rows are counted from the scroll offset,
   so this and the draw cannot disagree about which candidate a row is. */
static inline int row_list_rank_at(Rectangle rect, struct row_list_metrics m,
                                   int scroll, int count, int rows,
                                   Vector2 point) {
    float top = rect.y + m.header_h;
    int row;

    /* Written out rather than CheckCollisionPointRec, which is in the raylib
       library rather than its header -- and this has to link against nothing
       but libm to stay checkable (ADR-0012). */
    if (point.x < rect.x || point.x > rect.x + rect.width ||
        point.y < top || point.y > rect.y + rect.height)
        return -1;
    row = (int)((point.y - top) / m.row_h);
    if (row < 0 || row >= rows)
        return -1;
    if (scroll + row >= count)
        return -1;
    return scroll + row;
}

/* Where a row is drawn. */
static inline float row_list_row_y(Rectangle rect, struct row_list_metrics m,
                                   int row) {
    return rect.y + m.header_h + (float)row * m.row_h;
}

/*
 * The two lists that use this. Their metrics live here rather than beside
 * each drawing call so the hit test and the draw cannot be given different
 * ones -- which is the whole bug this header exists to make unwritable.
 */
#define SURVEY_LIST_METRICS ((struct row_list_metrics){ 44.0f, 22.0f, 24.0f })
#define FM_SCAN_LIST_METRICS ((struct row_list_metrics){ 80.0f, 20.0f, 24.0f })

#endif
