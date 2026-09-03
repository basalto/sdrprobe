#ifndef CALIBRATION_LAYOUT_H
#define CALIBRATION_LAYOUT_H

#include <raylib.h>

/*
 * Every rectangle and every text row of the calibration overlay.
 *
 * All of it, not some of it. This header once held only the LTE controls
 * while the rest of the overlay stayed as literals in the draw call, and the
 * result was worse than having no layout at all: the check compared the new
 * rectangles against each other, passed, and shipped a screen where the band
 * buttons sat on the status line, the band caption ran under the Scan button,
 * and the cell list covered three rows of text. A partial layout gives false
 * confidence, which is more expensive than none.
 *
 * The overlay is a stack of rows, so rows are what this models. Text widths
 * need a font and cannot be checked without a window; row occupancy needs
 * neither and is what collisions are actually made of. Each text line gets the
 * band it is drawn in, and the check asserts the bands are disjoint and that
 * the chart begins below the last of them.
 *
 * The layout depends on which technology is selected, because 4G has a row 2G
 * does not and everything below it moves. That is a state-dependent layout,
 * which is unusual here and preferable to a row of empty space or, worse, two
 * layouts that have to be kept in step.
 */

#define CALIBRATION_LTE_BANDS 3
#define CALIBRATION_CELL_ROW_H 26.0f
#define CALIBRATION_STATUS_ROWS 4
#define CALIBRATION_ROW_H 34.0f
#define CALIBRATION_TEXT_H 22.0f

struct calibration_layout {
    /* Back is one step up -- a measurement to the list it was chosen from --
       and Exit leaves calibration for the survey. Two buttons because they
       became two actions; one button doing both is what sent an operator who
       wanted the list back to the top of the program. */
    Rectangle back;
    Rectangle exit;
    Rectangle tech[3];
    Rectangle scan;            /* the GSM channel scan */
    Rectangle channel;         /* ARFCN or EARFCN entry */
    Rectangle start;
    Rectangle apply_ppm;
    /* 4G only; zero-width when 2G is selected, so a caller that draws them
       anyway draws nothing rather than drawing them somewhere wrong. */
    Rectangle lte_band[CALIBRATION_LTE_BANDS];
    Rectangle lte_scan;
    /* The found-cell picker takes the chart's place rather than covering it:
       before a cell is chosen the chart has nothing to say, and a panel over a
       chart is the overlap this header exists to prevent. */
    Rectangle cell_list;
    Rectangle band_label;      /* the "Band: ..." line */
    Rectangle status[CALIBRATION_STATUS_ROWS];
    Rectangle chart;
    int lte;                   /* which arrangement this is */
};

static inline struct calibration_layout calibration_layout_for(float width,
                                                               float height,
                                                               int lte) {
    struct calibration_layout l;
    float right = width - 24.0f;
    float y;
    int i;

    l.lte = lte ? 1 : 0;
    l.exit = (Rectangle){ width - 112.0f, 18.0f, 88.0f, 34.0f };
    l.back = (Rectangle){ width - 206.0f, 18.0f, 88.0f, 34.0f };

    /* Row one: the technology, the channel, and the two actions. */
    y = 72.0f;
    for (i = 0; i < 3; i++)
        l.tech[i] = (Rectangle){ 24.0f + (float)i * 82.0f, y, 74.0f,
                                 CALIBRATION_ROW_H };
    l.scan = (Rectangle){ 288.0f, y, 90.0f, CALIBRATION_ROW_H };
    l.channel = (Rectangle){ right - 346.0f, y, 110.0f, CALIBRATION_ROW_H };
    l.start = (Rectangle){ right - 224.0f, y, 88.0f, CALIBRATION_ROW_H };
    l.apply_ppm = (Rectangle){ right - 124.0f, y, 124.0f, CALIBRATION_ROW_H };
    y += CALIBRATION_ROW_H + 6.0f;

    /* Row two, 4G only: the band to scan, and the button that scans it. */
    if (l.lte) {
        for (i = 0; i < CALIBRATION_LTE_BANDS; i++)
            l.lte_band[i] = (Rectangle){ 24.0f + (float)i * 76.0f, y, 70.0f,
                                         28.0f };
        l.lte_scan = (Rectangle){ 256.0f, y, 100.0f, 28.0f };
        y += 28.0f + 6.0f;
    } else {
        for (i = 0; i < CALIBRATION_LTE_BANDS; i++)
            l.lte_band[i] = (Rectangle){ 24.0f, y, 0.0f, 0.0f };
        l.lte_scan = (Rectangle){ 24.0f, y, 0.0f, 0.0f };
    }

    /* Then the caption and the status rows, each in its own band. */
    l.band_label = (Rectangle){ 24.0f, y, width - 48.0f, CALIBRATION_TEXT_H };
    y += CALIBRATION_TEXT_H;
    for (i = 0; i < CALIBRATION_STATUS_ROWS; i++) {
        l.status[i] = (Rectangle){ 24.0f, y, width - 48.0f,
                                   CALIBRATION_TEXT_H };
        y += CALIBRATION_TEXT_H;
    }

    /* And the chart gets whatever is left, which is where the picker goes
       too. */
    l.chart = (Rectangle){ 16.0f, y + 8.0f, width - 46.0f,
                           height - (y + 8.0f) - 60.0f };
    if (l.chart.height < 80.0f)
        l.chart.height = 80.0f;
    l.cell_list = l.chart;
    return l;
}

static inline struct calibration_layout calibration_layout_now(int lte) {
    return calibration_layout_for((float)GetScreenWidth(),
                                  (float)GetScreenHeight(), lte);
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
    if (row < 0 || row >= count)
        return -1;
    return row;
}

/* How many rows the picker can show at this size. */
static inline int calibration_cell_rows(Rectangle list) {
    int rows = (int)((list.height - 34.0f) / CALIBRATION_CELL_ROW_H);
    return rows < 0 ? 0 : rows;
}

#endif
