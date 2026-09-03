#ifndef CALIBRATION_LAYOUT_H
#define CALIBRATION_LAYOUT_H

#include <raylib.h>

/*
 * Where the LTE half of the calibration overlay puts things.
 *
 * The rest of that overlay is still literal rectangles in the draw call, which
 * is how a caption once landed on the row above it in the survey view. These
 * are here because one of them is a *decision*: which row of a list of found
 * cells a click lands on. That mapping has been wrong twice in this program --
 * once in a bar chart, once in a site picker -- and both times it selected the
 * neighbour of what was pointed at, which no amount of looking at the screen
 * makes obvious.
 */

#define CALIBRATION_LTE_BANDS 3
#define CALIBRATION_CELL_ROW_H 26.0f
#define CALIBRATION_CELL_ROWS 8

struct calibration_layout {
    Rectangle band[CALIBRATION_LTE_BANDS];
    Rectangle scan_button;
    Rectangle cell_list;
};

static inline struct calibration_layout calibration_layout_for(float width,
                                                               float height) {
    struct calibration_layout l;
    int i;

    (void)width;
    for (i = 0; i < CALIBRATION_LTE_BANDS; i++)
        l.band[i] = (Rectangle){ 24.0f + (float)i * 66.0f, 112.0f, 60.0f,
                                 28.0f };
    l.scan_button = (Rectangle){ 228.0f, 112.0f, 90.0f, 28.0f };
    /* The list sits under the controls and above where the chart begins, tall
       enough for CALIBRATION_CELL_ROWS and no taller: a list that grew with
       the window would run off the bottom of a short one. */
    l.cell_list = (Rectangle){ 24.0f, 150.0f, 460.0f,
                               CALIBRATION_CELL_ROW_H *
                                   (float)CALIBRATION_CELL_ROWS + 34.0f };
    if (l.cell_list.y + l.cell_list.height > height - 20.0f)
        l.cell_list.height = height - 20.0f - l.cell_list.y;
    return l;
}

static inline struct calibration_layout calibration_layout_now(void) {
    return calibration_layout_for((float)GetScreenWidth(),
                                  (float)GetScreenHeight());
}

/* Which row of the found-cell list a point is over, or -1. */
static inline int calibration_cell_row_at(Rectangle list, int count,
                                          Vector2 point) {
    float top = list.y + 30.0f;
    int row;

    if (count < 1 || point.x < list.x || point.x > list.x + list.width ||
        point.y < top || point.y > list.y + list.height)
        return -1;
    row = (int)((point.y - top) / CALIBRATION_CELL_ROW_H);
    if (row < 0 || row >= count || row >= CALIBRATION_CELL_ROWS)
        return -1;
    return row;
}

#endif
