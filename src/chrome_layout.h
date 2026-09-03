#ifndef CHROME_LAYOUT_H
#define CHROME_LAYOUT_H

#include <raylib.h>

/*
 * The window chrome: the tab bar, and the Settings and Calibration buttons
 * anchored to the right edge.
 *
 * These live here for the same reason the GSM view's rectangles do -- one
 * derivation, so the things that must agree cannot drift apart. In this case
 * what must agree is the buttons and the status text drawn on the same rows:
 * the HUD runs left-to-right and the buttons sit at the right edge, so the
 * text has to know where they start or it draws underneath them. Before this
 * it assumed, and the assumption was written out twice.
 *
 * A pure function of the window size, so tests/gsm_layout_test.c can pin every
 * rectangle without opening a window.
 */
struct chrome_layout {
    /*
     * Survey, on the left where the numbered options used to run from.
     *
     * A button rather than a sixth number because it is not a fifth way of
     * looking at the current tuning: the other four all draw whatever the
     * receiver is pointed at, and this one walks the receiver across a band.
     * Numbering it beside them said it was the same kind of thing.
     */
    Rectangle survey_button;
    float option_row_left;   /* x where the numbered options begin */
    float option_row_y;
    Rectangle settings_button;
    Rectangle calibration_button;
    Rectangle tab[2];      /* Scope, Decode */
    /* The calibration dots, one per technology. Here rather than inside the
       widget because there are two of them and two widgets each choosing
       their own position are two widgets that can overlap. */
    Vector2 gsm_dot;
    Vector2 lte_dot;
    float status_left;     /* x where the buttons begin: text must stop here */
};

static inline struct chrome_layout chrome_layout_for(float width,
                                                     float height) {
    struct chrome_layout l;
    const float button_w = 108.0f;
    const float right_inset = 130.0f;  /* button_w plus the window margin */

    (void)height;
    l.survey_button = (Rectangle){ 22.0f, 44.0f, 104.0f, 28.0f };
    l.option_row_left = l.survey_button.x + l.survey_button.width + 22.0f;
    l.option_row_y = 50.0f;
    l.settings_button = (Rectangle){ width - right_inset, 16.0f, button_w, 34.0f };
    l.calibration_button =
        (Rectangle){ width - right_inset, 58.0f, button_w, 34.0f };
    for (int i = 0; i < 2; i++)
        l.tab[i] = (Rectangle){ width - 512.0f + (float)i * 128.0f, 14.0f,
                                118.0f, 36.0f };
    l.gsm_dot = (Vector2){ width - 152.0f, 33.0f };
    l.lte_dot = (Vector2){ width - 152.0f, 62.0f };
    /* Status text stops a little short of the buttons rather than touching,
       and now short of the captions those dots carry too. */
    l.status_left = width - right_inset - 12.0f;
    return l;
}

static inline struct chrome_layout chrome_layout_now(void) {
    return chrome_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

#endif
