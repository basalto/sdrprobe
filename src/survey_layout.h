#ifndef SURVEY_LAYOUT_H
#define SURVEY_LAYOUT_H

#include <raylib.h>

/*
 * Where the band survey view puts things: the range entry and its buttons on
 * one row, the swept power across the top, and below it what was found beside
 * what is known about the one selected.
 *
 * A pure function of the window size, like gsm_layout.h and adsb_layout.h, so
 * tests/layout_test.c can pin every rectangle without opening a window.
 */
struct survey_layout {
    Rectangle from_field;      /* range entry: low edge */
    Rectangle to_field;        /* range entry: high edge */
    Rectangle dwell_field;     /* seconds spent on each step */
    Rectangle sweep_button;    /* sweeps whatever the chart is showing */
    Rectangle reset_button;    /* back out of a zoom, or of a region sweep */
    /* Choosing a band fills the two range fields in, so it sits in front of
       them: the reading order is the doing order. */
    Rectangle band_button;
    Rectangle stop_button;
    /* A second row: what the sweep will be recorded as, and the button that
       records it. They sit with the sweep rather than in Settings because
       they are what makes this sweep comparable to the last one, and that is
       a thing to notice before pressing Sweep, not after. */
    Rectangle site_field;
    Rectangle antenna_field;
    Rectangle save_button;
    Rectangle site_menu_button;     /* the list of sites already visited */
    Rectangle antenna_menu_button;  /* and of antennas already used */
    Rectangle confirm_button;    /* asks again about what the sweep called new
                                    or missing */
    Rectangle watch_button;      /* keep sweeping, and say what changes */
    Rectangle chart;           /* power across the swept range */
    Rectangle peak_list;       /* lower left: the candidates found */
    Rectangle detail;          /* lower right: the selected one */
    Rectangle scan_button;      /* inside detail: sweep around the candidate */
    Rectangle waterfall_button; /* inside detail: watch it over time */
    Rectangle inspect_button;   /* inside detail, when a decoder fits the band */
    float status_y;            /* baseline of the status line, below both rows */
    /*
     * How far above a field its caption sits, and how tall captions are.
     *
     * In the layout rather than the draw call because it is geometry, and
     * geometry the layout does not own is geometry no check can see. A second
     * control row once landed its captions on the row above, and the sweep's
     * progress line was drawn at a literal 168 that became the caption strip
     * -- both invisible to check-layout, which could only compare the
     * rectangles it was given.
     */
    float label_offset;
    float label_height;
    float header_left;         /* x where the status text starts */
    float header_right;        /* x where it must stop */
};

/* One row of a picker, and the picker itself. Separate from the layout struct
   because its height depends on how many entries there are, which the layout
   cannot know -- and a caller cannot compute it either without knowing the row
   height, so it is here rather than in the view.

   Shared by the site and the antenna, which are the same control twice. */
#define SURVEY_SITE_ROW_H 26.0f

static inline Rectangle survey_menu_rect(Rectangle field, int count) {
    Rectangle menu;
    if (count < 1)
        count = 1;
    menu.x = field.x;
    menu.y = field.y + field.height + 2.0f;
    menu.width = field.width + 26.0f;
    menu.height = SURVEY_SITE_ROW_H * (float)count + 8.0f;
    return menu;
}

/* Which row of that list a point is over, or -1. The list is drawn over the
   chart, so getting this wrong picks something the operator did not point at
   -- and a wrong site files a sweep under a place it was not taken. */
static inline int survey_menu_row_at(Rectangle field, int count,
                                     Vector2 point) {
    Rectangle menu = survey_menu_rect(field, count);
    int row;
    if (count < 1 || point.x < menu.x || point.x > menu.x + menu.width ||
        point.y < menu.y + 4.0f || point.y > menu.y + menu.height - 4.0f)
        return -1;
    row = (int)((point.y - menu.y - 4.0f) / SURVEY_SITE_ROW_H);
    return (row >= 0 && row < count) ? row : -1;
}

static inline struct survey_layout survey_layout_for(float width,
                                                     float height) {
    struct survey_layout l;
    const float left = 82.0f;
    const float right_margin = 30.0f;
    const float bottom_margin = 30.0f;
    const float top = 248.0f;   /* below both control rows and the status */
    float usable = width - left - right_margin;

    if (usable < 240.0f)
        usable = 240.0f;

    /* The controls sit on one row at the left. Not at y = 84, where the other
       Scope views put their first HUD line: the Scope tab draws its numbered
       option row across y = 100-120, and the field labels above the row landed
       on top of it. */
    l.from_field = (Rectangle){ 82.0f, 128.0f, 150.0f, 30.0f };
    l.to_field = (Rectangle){ 248.0f, 128.0f, 150.0f, 30.0f };
    l.dwell_field = (Rectangle){ 414.0f, 128.0f, 100.0f, 30.0f };
    /* Five controls on one row, comfortably inside the 1000 px minimum window
       now that one Sweep does what two used to. tests/layout_test.c asserts
       they neither overlap nor run off the edge. */
    l.sweep_button = (Rectangle){ 530.0f, 128.0f, 120.0f, 30.0f };
    l.reset_button = (Rectangle){ 666.0f, 128.0f, 130.0f, 30.0f };
    l.band_button = (Rectangle){ 812.0f, 128.0f, 150.0f, 30.0f };
    l.stop_button = (Rectangle){ 812.0f, 128.0f, 90.0f, 30.0f };
    /* The second row. A survey saved without a site cannot be compared with
       anything, so the field is here in front of the reader rather than in a
       configuration file they have never opened. */
    l.site_field = (Rectangle){ 82.0f, 180.0f, 164.0f, 30.0f };
    l.site_menu_button = (Rectangle){ 248.0f, 180.0f, 26.0f, 30.0f };
    l.antenna_field = (Rectangle){ 288.0f, 180.0f, 180.0f, 30.0f };
    l.antenna_menu_button = (Rectangle){ 470.0f, 180.0f, 26.0f, 30.0f };
    l.save_button = (Rectangle){ 510.0f, 180.0f, 120.0f, 30.0f };
    l.confirm_button = (Rectangle){ 646.0f, 180.0f, 150.0f, 30.0f };
    l.watch_button = (Rectangle){ 812.0f, 180.0f, 110.0f, 30.0f };
    l.status_y = 220.0f;
    l.label_offset = 20.0f;
    l.label_height = 16.0f;
    l.header_left = 82.0f;
    l.header_right = width - 150.0f;

    float span = height - top - bottom_margin;
    if (span < 160.0f)
        span = 160.0f;
    float chart_h = span * 0.45f;
    l.chart = (Rectangle){ left, top, usable, chart_h };

    /* The lower row: what was found, and what is known about one of them. The
       list is the narrower half -- a frequency and a level are short, and the
       detail panel carries sentences. */
    float lower_y = top + chart_h + 26.0f;
    float lower_h = span * 0.55f - 26.0f;
    if (lower_h < 90.0f)
        lower_h = 90.0f;
    float list_w = usable * 0.42f;
    if (list_w < 180.0f)
        list_w = 180.0f;
    l.peak_list = (Rectangle){ left, lower_y, list_w, lower_h };
    float detail_x = left + list_w + 20.0f;
    float detail_w = left + usable - detail_x;
    if (detail_w < 160.0f)
        detail_w = 160.0f;
    l.detail = (Rectangle){ detail_x, lower_y, detail_w, lower_h };

    /* The panel's actions, two rows along its bottom: the two that always
       apply share the upper row, and the handoff to a decoder -- which only
       appears when the band plan names one, and carries a long label -- takes
       the lower row to itself. Clamped inside the panel, because a short
       window makes it small and gsm_layout.h records what happens to a button
       positioned only by an offset from its panel's edge. */
    float button_w = (detail_w - 36.0f) / 2.0f;
    if (button_w < 80.0f)
        button_w = 80.0f;
    float lower_row = l.detail.y + l.detail.height - 40.0f;
    float upper_row = lower_row - 36.0f;
    if (upper_row < l.detail.y) {
        upper_row = l.detail.y;
        lower_row = l.detail.y;
    }
    l.scan_button = (Rectangle){ l.detail.x + 12.0f, upper_row, button_w,
                                 28.0f };
    l.waterfall_button = (Rectangle){ l.detail.x + 24.0f + button_w, upper_row,
                                      button_w, 28.0f };
    l.inspect_button = (Rectangle){ l.detail.x + 12.0f, lower_row,
                                    detail_w - 24.0f, 28.0f };
    return l;
}

#endif
