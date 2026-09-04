#ifndef SCAN_LAYOUT_H
#define SCAN_LAYOUT_H

#include <raylib.h>

/*
 * The channel scan overlay's two buttons.
 *
 * Two rectangles is not much to extract, and that is the point: they were
 * declared once in draw_scan and again in handle_scan_input, with the same
 * four literals in both. Two is where the Settings panel started, and that
 * one reached ten before anybody looked -- by which time a caption had
 * shipped on top of a checkbox.
 *
 * They sit at the top right, where the chrome's own buttons live on every
 * other screen, so whether they collide with anything at a narrow window is
 * worth being able to ask.
 */

#define SCAN_BUTTON_W 88.0f
#define SCAN_BUTTON_H 34.0f

struct scan_layout {
    Rectangle rescan;
    Rectangle back;
};

static inline struct scan_layout scan_layout_for(float width) {
    struct scan_layout l;

    l.back = (Rectangle){ width - 112.0f, 18.0f, SCAN_BUTTON_W,
                          SCAN_BUTTON_H };
    l.rescan = (Rectangle){ l.back.x - 100.0f, 18.0f, SCAN_BUTTON_W,
                            SCAN_BUTTON_H };
    return l;
}

#endif
