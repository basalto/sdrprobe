#ifndef STARTUP_LAYOUT_H
#define STARTUP_LAYOUT_H

#include <raylib.h>

#include "panel_rows.h"

/*
 * The startup form's geometry: where the session says what it is, and where
 * the calibration running behind it reports.
 *
 * `.scratch/startup-installation/issues/03-the-form-and-its-geometry.md`,
 * ADR-0024. **All of it, not some of it** -- a header modelling half a screen
 * puts a green tick over the half it does not model, which is worse than
 * having none, and `check-layout` has caught a panel drawing 101 pixels past
 * its own bottom edge at 640x400 exactly because the rows were `y +=` between
 * draw calls.
 */

/* The fields, in the order they are asked. Site leads because it is the one
   with no default that could be right: two sweeps both labelled "unknown"
   compare as the same place. */
enum startup_field {
    STARTUP_FIELD_SITE,
    STARTUP_FIELD_ANTENNA,
    STARTUP_FIELD_LABEL,
    STARTUP_FIELD_COUNT
};

/*
 * The report panel's rows, all of it the view's own -- a panel of
 * eighteen-point network fields and one of fifteen-point decode statistics do
 * not want the same step, which is why panel_rows.h takes them rather than
 * owning them. Past the capacity a row is not drawn at all: off the bottom
 * edge is worse than absent.
 */
#define STARTUP_REPORT_CAPTION_DROP 26.0f
#define STARTUP_REPORT_ROW_HEIGHT 18.0f
#define STARTUP_REPORT_FOOTER_HEIGHT 8.0f
#define STARTUP_REPORT_GUTTER_FRACTION 0.34f
#define STARTUP_REPORT_GUTTER_CAP 190.0f

struct startup_layout {
    Rectangle panel;

    /* One row per field: its caption, the box that takes typing, and the
       button that drops the list of ones already known. */
    Rectangle field[STARTUP_FIELD_COUNT];
    Rectangle field_menu[STARTUP_FIELD_COUNT];

    /* The gain stepper and the band picker for the LTE fall-back. Both are
       the receiver's own business rather than the installation's, so they sit
       below the three that describe where this is. */
    Rectangle gain_down;
    Rectangle gain_value;
    Rectangle gain_up;
    Rectangle band_down;
    Rectangle band_value;
    Rectangle band_up;
    Rectangle frequency;

    /*
     * Where the calibration reports while the form is up.
     *
     * This is the primary surface for it, and that is the decision ADR-0024
     * records: the overlay is modal, so there is a whole screen to report
     * into, and a popup over a modal form would be a second window saying
     * what the first one already says. After Continue the verdict moves to
     * the health indicator, where it stands rather than expires.
     */
    Rectangle report;
    struct panel_rows report_rows;
    Rectangle status;

    Rectangle skip;
    Rectangle cont;
};

static inline struct startup_layout startup_layout_for(float width,
                                                       float height) {
    struct startup_layout l;
    /*
     * Two sets of metrics, because at 640x400 the comfortable ones do not
     * fit: the form needs six rows, a report and two buttons, and at the
     * roomy step that is 502 pixels of a 320-pixel panel. `check-layout`
     * found it rather than an operator, which is the point of checking a
     * layout at sizes nobody runs at -- a control off the panel on the one
     * modal screen is a session that cannot be started.
     */
    int compact = height < 620.0f;
    const float row_h = compact ? 22.0f : 30.0f;
    const float row_step = compact ? 28.0f : 40.0f;
    const float label_w = compact ? 120.0f : 150.0f;
    const float head_h = compact ? 44.0f : 78.0f;
    const float buttons_h = compact ? 44.0f : 54.0f;
    const float status_h = compact ? 20.0f : 26.0f;
    const float margin = compact ? 16.0f : 24.0f;
    const float inset = compact ? 14.0f : 24.0f;
    /* One row at least: caption drop plus a row plus the footer reserve is
       52, so anything under that is a panel with a heading and nothing under
       it -- which `check-layout` calls out rather than letting it draw. */
    const float report_min = compact ? 56.0f : 60.0f;
    float report_h = compact ? 70.0f : 220.0f;
    float controls_h;
    float content_h;
    float field_w;
    float y;
    float left;
    int i;

    /*
     * The panel is sized to its content and centred, rather than filling the
     * window with a report panel that has eight rows to draw at most. Left to
     * take "whatever is left" it swallowed two thirds of a 1100x720 screen
     * and drew one line into it.
     */
    controls_h = (float)STARTUP_FIELD_COUNT * row_step + 8.0f +
                 3.0f * row_step + 12.0f;
    content_h = head_h + controls_h + report_h + status_h + buttons_h + margin;
    l.panel.width = width - 80.0f;
    if (l.panel.width < 380.0f)
        l.panel.width = 380.0f;
    if (l.panel.width > width - 16.0f)
        l.panel.width = width - 16.0f;
    l.panel.height = content_h;
    if (l.panel.height > height - 40.0f) {
        /* A short window takes it out of the report, which is the only part
           that can give: the fields and the buttons are all load-bearing. */
        float over = l.panel.height - (height - 40.0f);

        report_h -= over;
        if (report_h < report_min)
            report_h = report_min;
        l.panel.height = head_h + controls_h + report_h + status_h +
                         buttons_h + margin;
    }
    l.panel.x = (width - l.panel.width) / 2.0f;
    if (l.panel.x < 8.0f)
        l.panel.x = 8.0f;
    l.panel.y = (height - l.panel.height) / 2.0f;
    if (l.panel.y < 8.0f)
        l.panel.y = 8.0f;

    left = l.panel.x + inset;
    /* The field boxes stop short of the panel's right edge by the width of
       the menu button plus its gap, so a long site name never runs under the
       control that drops the list of them. */
    field_w = l.panel.width - 2.0f * inset - label_w - 44.0f;
    if (field_w < 100.0f)
        field_w = 100.0f;

    y = l.panel.y + head_h;
    for (i = 0; i < STARTUP_FIELD_COUNT; i++) {
        l.field[i] = (Rectangle){ left + label_w, y, field_w, row_h };
        l.field_menu[i] = (Rectangle){ left + label_w + field_w + 6.0f, y,
                                       32.0f, row_h };
        y += row_step;
    }

    y += 8.0f;
    l.gain_down = (Rectangle){ left + label_w, y, 34.0f, row_h };
    l.gain_value = (Rectangle){ left + label_w + 38.0f, y, 120.0f, row_h };
    l.gain_up = (Rectangle){ left + label_w + 162.0f, y, 34.0f, row_h };
    y += row_step;
    l.band_down = (Rectangle){ left + label_w, y, 34.0f, row_h };
    l.band_value = (Rectangle){ left + label_w + 38.0f, y, 120.0f, row_h };
    l.band_up = (Rectangle){ left + label_w + 162.0f, y, 34.0f, row_h };
    y += row_step;
    l.frequency = (Rectangle){ left + label_w, y, 200.0f, row_h };
    y += row_step + 12.0f;

    /*
     * The rows are `panel_rows.h`'s rather than a `y +=` between draw calls
     * -- which is how five panels end up with four row heights, and how one
     * of them came to draw 101 pixels past its own bottom edge.
     */
    l.report = (Rectangle){ left, y, l.panel.width - 2.0f * inset, report_h };
    l.report_rows = panel_rows_for(l.report, STARTUP_REPORT_CAPTION_DROP,
                                   STARTUP_REPORT_ROW_HEIGHT,
                                   STARTUP_REPORT_FOOTER_HEIGHT,
                                   STARTUP_REPORT_GUTTER_FRACTION,
                                   STARTUP_REPORT_GUTTER_CAP);
    l.status = (Rectangle){ left, l.report.y + report_h + 2.0f,
                            l.panel.width - 2.0f * inset, status_h - 4.0f };

    l.cont = (Rectangle){ l.panel.x + l.panel.width - inset - 150.0f,
                          l.status.y + status_h, 150.0f, buttons_h - 16.0f };
    l.skip = (Rectangle){ l.cont.x - 10.0f - 190.0f, l.cont.y, 190.0f,
                          l.cont.height };
    if (l.skip.x < left)
        l.skip.x = left;
    return l;
}

static inline struct startup_layout startup_layout_now(void) {
    return startup_layout_for((float)GetScreenWidth(),
                              (float)GetScreenHeight());
}

#endif
