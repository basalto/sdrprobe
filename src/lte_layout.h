#ifndef LTE_LAYOUT_H
#define LTE_LAYOUT_H

#include <raylib.h>

/*
 * Where the LTE decode view puts things.
 *
 * Same arrangement as gsm_layout.h and adsb_layout.h, and for the same
 * reason: a pure function of the window size, so tests/layout_test.c can pin
 * every rectangle without opening a window, and two panels sharing a row
 * cannot drift apart when one of them moves.
 *
 * The screen is a waterfall of the carrier over two panels -- what the
 * synchronisation signals found, and what the broadcast said. They sit side by
 * side because reading one without the other is misleading: a cell identity
 * with no message behind it means the search locked onto something, and only
 * the empty panel next to it says the message never came.
 */
struct lte_layout {
    Rectangle record_button;
    Rectangle waterfall;
    Rectangle cell_panel;      /* what PSS and SSS found */
    Rectangle mib_panel;       /* what the broadcast channel said */
    float header_left;
    float header_right;
};

static inline struct lte_layout lte_layout_for(float width, float height) {
    struct lte_layout l;
    const float left = 22.0f;
    const float right_margin = 22.0f;
    const float bottom_margin = 26.0f;
    const float top = 132.0f;
    const float gap = 14.0f;
    float usable = width - left - right_margin;
    float span, waterfall_h, panel_y, panel_h, panel_w;

    if (usable < 240.0f)
        usable = 240.0f;

    /* Clear of the Calibration button, which chrome_layout.h anchors to the
       right edge down to y = 92 -- the same row the other two decode views
       put their own buttons on. */
    l.record_button = (Rectangle){ width - 150.0f, 100.0f, 130.0f, 26.0f };
    l.header_left = left;
    l.header_right = l.record_button.x - 12.0f;

    span = height - top - bottom_margin;
    if (span < 200.0f)
        span = 200.0f;

    /* The waterfall takes the smaller share. It is context rather than the
       answer: it says whether a carrier is there at all, which is what makes
       an empty cell panel readable. */
    waterfall_h = span * 0.38f;
    l.waterfall = (Rectangle){ left, top, usable, waterfall_h };

    panel_y = top + waterfall_h + gap + 4.0f;
    panel_h = span - waterfall_h - gap - 4.0f;
    if (panel_h < 120.0f)
        panel_h = 120.0f;
    panel_w = (usable - gap) * 0.5f;
    l.cell_panel = (Rectangle){ left, panel_y, panel_w, panel_h };
    l.mib_panel = (Rectangle){ left + panel_w + gap, panel_y, panel_w,
                               panel_h };
    return l;
}

#endif
