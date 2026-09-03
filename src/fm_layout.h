#ifndef FM_LAYOUT_H
#define FM_LAYOUT_H

#include <raylib.h>

/*
 * Where the FM decode view puts things.
 *
 * A pure function of the window size, the same as gsm_layout.h and its
 * neighbours, so tests/layout_test.c can pin every rectangle without opening
 * a window and two panels sharing a row cannot drift apart when one moves.
 *
 * The screen is a controls strip, a waterfall of the multiplex, and a row of
 * three panels. They are one row because each of them is misleading alone:
 *
 *   - the *signal* panel says whether there is a pilot and a subcarrier at
 *     all, which is the question "is this even an FM station carrying RDS";
 *   - the *station* panel says what it calls itself, which is the answer;
 *   - the *funnel* says where a decode stopped, which is the only thing that
 *     tells "nothing is transmitting" from "every block is failing its
 *     syndrome". Two empty panels look identical without it.
 *
 * Two arrangements. The signal one is above; the analysis one replaces the
 * waterfall and the three panels with four charts, the way the ADS-B and LTE
 * views replace theirs. What the charts are for is in view_fm.c, but the
 * reason there are four rather than three is here: the multiplex spectrum
 * answers a question none of the panels can, which is whether this station
 * carries RDS at all -- a pilot with no 57 kHz hump is a perfectly good
 * station that simply is not sending any, and every number on the signal
 * panel looks identical to a station that is sending it badly.
 *
 * The scan takes the waterfall's place rather than covering it, which is the
 * rule calibration_layout.h had to learn: a panel over a chart is the overlap
 * a layout header exists to prevent.
 */

#define FM_LAYOUT_STATIONS 3
#define FM_LAYOUT_CHARTS 4

struct fm_layout {
    Rectangle frequency_field;
    Rectangle tune_button;
    Rectangle station_button[FM_LAYOUT_STATIONS];
    Rectangle scan_button;
    Rectangle view_toggle;         /* View: Charts / View: Signal */
    Rectangle waterfall;
    Rectangle scan_list;           /* the same place, while a scan has results */
    Rectangle signal_panel;        /* pilot, subcarrier, lock */
    Rectangle station_panel;       /* identification, name, programme type */
    Rectangle funnel_panel;        /* where the decode stopped */
    Rectangle chart[FM_LAYOUT_CHARTS];   /* analysis: two rows of two */
    float header_left;
    float header_right;
};

static inline struct fm_layout fm_layout_for(float width, float height) {
    struct fm_layout l;
    const float left = 22.0f;
    const float right_margin = 22.0f;
    const float bottom_margin = 26.0f;
    const float top = 168.0f;
    const float gap = 14.0f;
    float usable = width - left - right_margin;
    float span, waterfall_h, panel_y, panel_h, third;
    int i;

    if (usable < 300.0f)
        usable = 300.0f;

    /* The controls row sits above the chart, clear of the tab bar and of the
       Calibration button chrome_layout.h anchors to the right down to y = 92. */
    l.frequency_field = (Rectangle){ left, 100.0f, 130.0f, 26.0f };
    l.tune_button = (Rectangle){ left + 140.0f, 100.0f, 78.0f, 26.0f };
    for (i = 0; i < FM_LAYOUT_STATIONS; i++)
        l.station_button[i] = (Rectangle){ left + 232.0f + (float)i * 96.0f,
                                          100.0f, 88.0f, 26.0f };
    l.scan_button = (Rectangle){ left + 232.0f +
                                     (float)FM_LAYOUT_STATIONS * 96.0f,
                                 100.0f, 100.0f, 26.0f };
    /* Clear of the Calibration button, which chrome_layout.h anchors to the
       right edge down to y = 92 -- the same clearance lte_layout.h takes. */
    l.view_toggle = (Rectangle){ width - 150.0f, 100.0f, 130.0f, 26.0f };

    span = height - top - bottom_margin;
    if (span < 200.0f)
        span = 200.0f;
    /* The waterfall takes the larger share: it is the only thing on screen
       that shows a station being tuned past, and the panels are text. */
    waterfall_h = span * 0.52f;
    panel_y = top + waterfall_h + gap;
    panel_h = span - waterfall_h - gap;
    if (panel_h < 90.0f)
        panel_h = 90.0f;

    l.waterfall = (Rectangle){ left, top, usable, waterfall_h };

    third = (usable - 2.0f * gap) / 3.0f;
    l.signal_panel = (Rectangle){ left, panel_y, third, panel_h };
    l.station_panel = (Rectangle){ left + third + gap, panel_y, third,
                                   panel_h };
    l.funnel_panel = (Rectangle){ left + 2.0f * (third + gap), panel_y, third,
                                  panel_h };

    l.scan_list = l.waterfall;

    /*
     * Analysis: two rows of two, over the whole of the space the waterfall
     * and the panels share. Equal quarters, because no one of the four is the
     * main one -- which of them matters depends entirely on where the decode
     * is failing.
     */
    {
        float chart_w = (usable - gap) / 2.0f;
        float chart_h = (span - gap) / 2.0f;

        if (chart_h < 80.0f)
            chart_h = 80.0f;
        for (i = 0; i < FM_LAYOUT_CHARTS; i++)
            l.chart[i] = (Rectangle){
                left + (float)(i % 2) * (chart_w + gap),
                top + (float)(i / 2) * (chart_h + gap),
                chart_w, chart_h
            };
    }

    l.header_left = left;
    l.header_right = width - right_margin;
    return l;
}

static inline struct fm_layout fm_layout_now(void) {
    return fm_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

#endif
