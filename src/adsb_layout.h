#ifndef ADSB_LAYOUT_H
#define ADSB_LAYOUT_H

#include <raylib.h>

/*
 * Where the ADS-B decode view puts things.
 *
 * Same arrangement as gsm_layout.h and for the same reason: a pure function of
 * the window size, so tests/layout_test.c can pin every rectangle without a
 * window, and panels that share a row cannot drift apart when one moves.
 *
 * The view has two modes and they differ only in where the message log goes --
 * the whole panel in log mode, the lower left in analysis mode -- so both
 * rectangles are derived here and the view picks one. Deriving the analysis
 * geometry only when analysis is on would put the mode inside a function whose
 * whole value is being a function of the window size alone.
 */
struct adsb_layout {
    Rectangle retune_button;   /* shown only when tuned away from 1090 MHz */
    Rectangle view_toggle;     /* View: Log / View: Analysis */
    Rectangle hold_button;     /* Hold last good, inside the scatter panel */
    Rectangle chart[3];        /* landscape, bit confidence, envelope */
    Rectangle log_full;        /* log mode: the whole panel */
    Rectangle log_split;       /* analysis mode: lower left */
    Rectangle scatter;         /* analysis mode: square, lower right */
    float header_right;        /* x where the header text must stop */
};

static inline struct adsb_layout adsb_layout_for(float width, float height) {
    struct adsb_layout l;
    const float left = 82.0f;
    const float right_margin = 30.0f;
    const float bottom_margin = 30.0f;
    const float log_top = 124.0f;    /* log mode, unchanged from before */
    const float analysis_top = 150.0f;
    const float gap = 14.0f;
    float usable = width - left - right_margin;

    if (usable < 240.0f)
        usable = 240.0f;

    l.retune_button = (Rectangle){ 470.0f, 82.0f, 220.0f, 30.0f };
    /* Clear of the Calibration button, which chrome_layout.h anchors to the
       right edge down to y = 92. The GSM view's toggle sits at the same y for
       the same reason. */
    l.view_toggle = (Rectangle){ width - 150.0f, 100.0f, 130.0f, 26.0f };
    l.header_right = l.view_toggle.x - 12.0f;

    l.log_full = (Rectangle){ left, log_top, usable,
                              height - log_top - bottom_margin };

    /* One vertical span for the analysis mode, split 0.42 / 0.58 between the
       chart row and the row under it, as the GSM view splits its own. */
    float span = height - analysis_top - bottom_margin;
    if (span < 120.0f)
        span = 120.0f;
    float chart_h = span * 0.42f;
    float chart_w = (usable - 2.0f * gap) / 3.0f;
    for (int i = 0; i < 3; i++)
        l.chart[i] = (Rectangle){ left + (float)i * (chart_w + gap),
                                  analysis_top, chart_w, chart_h };

    float lower_y = analysis_top + chart_h + 16.0f;
    float lower_h = span * 0.58f - 16.0f;
    if (lower_h < 80.0f)
        lower_h = 80.0f;
    /* The scatter is square, so its height sets its width -- capped, or on a
       short wide window it would take the whole row and leave no log. */
    float side = lower_h;
    if (side > usable * 0.4f)
        side = usable * 0.4f;
    l.scatter = (Rectangle){ left + usable - side, lower_y, side, side };

    float log_w = l.scatter.x - 24.0f - left;
    if (log_w < 200.0f)
        log_w = 200.0f;
    l.log_split = (Rectangle){ left, lower_y, log_w, lower_h };

    /* The toggle sits inside the scatter's top-right corner. gsm_layout.h
       records what happens without the clamp: on a short window the offset
       pushes the button out of its own panel and onto the one beside it. */
    float hold_x = l.scatter.x + l.scatter.width - 116.0f;
    if (hold_x < l.scatter.x)
        hold_x = l.scatter.x;
    l.hold_button = (Rectangle){ hold_x, l.scatter.y + 4.0f, 112.0f, 22.0f };
    return l;
}

#endif
