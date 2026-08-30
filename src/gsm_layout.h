#ifndef GSM_LAYOUT_H
#define GSM_LAYOUT_H

#include <raylib.h>

/*
 * Where the GSM decode view puts things.
 *
 * Kept apart from the drawing and from the application state, which is the
 * "HUD composition" split the decoder context's glossary already names. It is
 * a pure function of the window size -- it calls nothing, reads no globals --
 * so tests/gsm_layout_test.c can pin every rectangle at several window sizes
 * without a window, a receiver, or a frame loop.
 *
 * That test is the point. Layout here was previously a set of accessors that
 * each re-derived the whole screen, so panels sharing a row assumed each
 * other's geometry independently and drifted apart when one was moved, with
 * nothing to notice.
 */

struct gsm_layout {
    Rectangle scan_button;      /* "Scan" and "Back to Scan" share this spot */
    Rectangle record_button;
    Rectangle opt_button[3];
    Rectangle view_toggle;
    Rectangle waterfall;        /* upper panel in waterfall mode */
    Rectangle burst;            /* upper panel in burst-analysis mode */
    Rectangle scan;             /* lower-left chart */
    Rectangle constellation;    /* lower-right square panel */
    Rectangle const_amp_button;
    Rectangle const_derot_button;
};

static inline struct gsm_layout gsm_layout_for(float width, float height) {
    struct gsm_layout l;
    const float left = 82.0f;
    const float top = 196.0f;

    l.scan_button = (Rectangle){ 22.0f, 84.0f, 150.0f, 30.0f };
    l.record_button = (Rectangle){ 182.0f, 84.0f, 130.0f, 30.0f };
    for (int i = 0; i < 3; i++)
        l.opt_button[i] =
            (Rectangle){ 396.0f + (float)i * 72.0f, 134.0f, 66.0f, 20.0f };
    l.view_toggle = (Rectangle){ width - 150.0f, 100.0f, 130.0f, 26.0f };

    /* One vertical span for the whole view: everything below the header, less
       the window's bottom margin. The upper panel takes 0.44 of it, the lower
       row the rest. The two upper panels each used to subtract their own
       bottom margin -- 40 for the waterfall, 50 for burst analysis -- before
       taking that share, so they came out different heights and swapping
       between them nudged the display. They are the same panel in two modes
       and now have the same geometry. */
    const float bottom_margin = 50.0f;
    float span = height - top - bottom_margin;
    Rectangle upper = { left, top, width - 112.0f, span * 0.44f };
    l.waterfall = upper;
    l.burst = upper;

    /* Lower row: chart on the left, a square constellation panel on the right. */
    float y = top + span * 0.44f + 120.0f; /* clears the waterfall caption and
                                              the SCH readout above the row */
    float h = span * 0.56f - 120.0f;
    float right = width - 30.0f;
    float side = h;
    float scan_w = (right - side - 24.0f) - left;
    if (scan_w < 200.0f)
        scan_w = 200.0f;
    l.scan = (Rectangle){ left, y, scan_w, h };
    l.constellation = (Rectangle){ right - h, y, h, h };
    /* The two toggles sit inside the panel's top-right corner. They are
       offset from its right edge, so a panel narrower than the offsets used
       to push them out of the panel entirely and onto the scan chart beside
       it -- which happens below roughly a 700 px window height, and the app
       allows 540. Clamp them to the panel: on a panel that small they
       overlap each other, which is ugly but stays where it belongs. */
    float amp_x = l.constellation.x + l.constellation.width - 134.0f;
    float derot_x = l.constellation.x + l.constellation.width - 68.0f;
    if (amp_x < l.constellation.x)
        amp_x = l.constellation.x;
    if (derot_x < l.constellation.x)
        derot_x = l.constellation.x;
    l.const_amp_button =
        (Rectangle){ amp_x, l.constellation.y + 4.0f, 62.0f, 22.0f };
    l.const_derot_button =
        (Rectangle){ derot_x, l.constellation.y + 4.0f, 64.0f, 22.0f };
    return l;
}

#endif
