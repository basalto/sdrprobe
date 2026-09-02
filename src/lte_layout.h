#ifndef LTE_LAYOUT_H
#define LTE_LAYOUT_H

#include <raylib.h>

/*
 * Where the LTE decode view puts things.
 *
 * Same arrangement as gsm_layout.h and adsb_layout.h, and for the same
 * reason: a pure function of the window size, so tests/layout_test.c can pin
 * it without opening a window, and panels sharing a row cannot drift apart
 * when one of them moves.
 *
 * The screen is a controls strip, a waterfall of the carrier, and a row of
 * three panels: what a band scan found, what the synchronisation signals say
 * about the carrier in front of us, and what its broadcast said. They are one
 * row because reading any of them alone misleads -- a cell identity with an
 * empty broadcast beside it is a carrier that is present and too weak to
 * read, and an empty scan list beside both says nobody has looked yet.
 *
 * Analysis mode replaces the waterfall and the two right-hand panels with
 * four charts, the way the ADS-B view replaces its log.
 */

#define LTE_LAYOUT_BANDS 3       /* the bands an RTL-SDR can reach */

struct lte_layout {
    Rectangle band_button[LTE_LAYOUT_BANDS];
    Rectangle scan_button;       /* "Scan band" / "Stop" share this spot */
    Rectangle view_toggle;       /* View: Charts / View: Signal */
    Rectangle record_button;
    Rectangle waterfall;
    Rectangle found_panel;       /* what the scan found, left column */
    Rectangle cell_panel;        /* what PSS and SSS found */
    Rectangle mib_panel;         /* what the broadcast said */
    Rectangle chart[3];          /* analysis: correlation, candidates, channel */
    Rectangle constellation;     /* analysis: the broadcast's elements */
    float header_left;
    float header_right;
};

static inline struct lte_layout lte_layout_for(float width, float height) {
    struct lte_layout l;
    const float left = 22.0f;
    const float right_margin = 22.0f;
    const float bottom_margin = 26.0f;
    const float top = 168.0f;
    const float gap = 14.0f;
    float usable = width - left - right_margin;
    float span, waterfall_h, panel_y, panel_h, found_w, rest, half;
    float chart_w, chart_h, lower_y, lower_h, side;
    int i;

    if (usable < 300.0f)
        usable = 300.0f;

    /* Clear of the Calibration button, which chrome_layout.h anchors to the
       right edge down to y = 92 -- the row the other two decode views put
       their own buttons on. */
    l.view_toggle = (Rectangle){ width - 150.0f, 100.0f, 130.0f, 26.0f };
    l.record_button = (Rectangle){ l.view_toggle.x - 140.0f, 100.0f,
                                   130.0f, 26.0f };
    l.header_left = left;
    l.header_right = l.record_button.x - 12.0f;

    /* The band selector and the scan share a strip of their own below the
       header, because choosing a band retunes the receiver and that is not a
       thing to put next to the record button by accident. */
    for (i = 0; i < LTE_LAYOUT_BANDS; i++)
        l.band_button[i] = (Rectangle){ left + (float)i * 96.0f, 132.0f,
                                        90.0f, 26.0f };
    l.scan_button = (Rectangle){ left + LTE_LAYOUT_BANDS * 96.0f + 12.0f,
                                 132.0f, 130.0f, 26.0f };

    span = height - top - bottom_margin;
    if (span < 200.0f)
        span = 200.0f;

    /* The waterfall takes the smaller share: it is context, not the answer --
       it says whether a carrier is there at all, which is what makes an empty
       cell panel readable. */
    waterfall_h = span * 0.34f;
    l.waterfall = (Rectangle){ left, top, usable, waterfall_h };

    panel_y = top + waterfall_h + gap + 4.0f;
    panel_h = span - waterfall_h - gap - 4.0f;
    if (panel_h < 120.0f)
        panel_h = 120.0f;

    /* The scan list is a fixed narrow column -- its rows are a channel
       number, a frequency and an identity -- and the two panels that have
       prose in them share what is left. */
    found_w = usable * 0.26f;
    if (found_w > 260.0f)
        found_w = 260.0f;
    if (found_w < 150.0f)
        found_w = 150.0f;
    rest = usable - found_w - 2.0f * gap;
    if (rest < 200.0f)
        rest = 200.0f;
    half = rest * 0.5f;
    l.found_panel = (Rectangle){ left, panel_y, found_w, panel_h };
    l.cell_panel = (Rectangle){ left + found_w + gap, panel_y, half, panel_h };
    l.mib_panel = (Rectangle){ l.cell_panel.x + half + gap, panel_y, half,
                               panel_h };

    /* Analysis mode: three charts across the top of the same space the
       waterfall had, and the constellation beside the scan list below. */
    chart_h = span * 0.44f;
    chart_w = (usable - 2.0f * gap) / 3.0f;
    for (i = 0; i < 3; i++)
        l.chart[i] = (Rectangle){ left + (float)i * (chart_w + gap), top,
                                  chart_w, chart_h };
    lower_y = top + chart_h + 16.0f;
    lower_h = span - chart_h - 16.0f;
    if (lower_h < 100.0f)
        lower_h = 100.0f;
    /* Square, and capped so a short wide window does not give the whole row
       to it -- the same clamp adsb_layout.h needs, and for the same reason. */
    side = lower_h;
    if (side > usable * 0.4f)
        side = usable * 0.4f;
    l.constellation = (Rectangle){ left + usable - side, lower_y, side, side };
    return l;
}

#endif
