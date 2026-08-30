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

    /* The two upper panels differ by 10 px of reserved bottom margin. That
       looks unintentional, but it is preserved here: this refactor is a
       no-op by construction, and changing it is a separate decision. */
    float wf_span = height - 196.0f - 40.0f;
    l.waterfall = (Rectangle){ left, top, width - 112.0f, wf_span * 0.44f };
    float burst_span = height - 196.0f - 50.0f;
    l.burst = (Rectangle){ left, top, width - 112.0f, burst_span * 0.44f };

    /* Lower row: chart on the left, a square constellation panel on the right. */
    float span = height - top - 50.0f;
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
    l.const_amp_button = (Rectangle){ l.constellation.x + l.constellation.width
                                          - 134.0f,
                                      l.constellation.y + 4.0f, 62.0f, 22.0f };
    l.const_derot_button = (Rectangle){ l.constellation.x
                                            + l.constellation.width - 68.0f,
                                        l.constellation.y + 4.0f, 64.0f, 22.0f };
    return l;
}

#endif
