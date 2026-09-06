#ifndef TETRA_LAYOUT_H
#define TETRA_LAYOUT_H

#include <raylib.h>

/*
 * Where the TETRA decode view puts things.
 *
 * Same arrangement as gsm_layout.h and adsb_layout.h and for the same reason:
 * a pure function of the window size, so tests/layout_test.c can pin every
 * rectangle without a window, and panels that share a row cannot drift apart
 * when one moves.
 *
 * **All of the geometry, and only geometry that is drawn.** A header modelling
 * half a screen puts a green tick over the half it does not, which is worse
 * than having none -- but a rectangle for a control the view never draws is
 * the same fault wearing the opposite coat, and this had one: a Record button
 * copied over from adsb_layout.h that nothing ever rendered. A check would
 * have pinned its position happily for ever.
 *
 * The view has the two arrangements every decode view here has. The log shows
 * what the network said and when; the analysis shows how it was read -- the
 * constellation the dibits came off, and how much of each 255-symbol slot
 * repeats. Both are derived unconditionally: deriving the analysis geometry
 * only when analysis is on would put the mode inside a function whose whole
 * value is being a function of the window size alone.
 */
struct tetra_layout {
    Rectangle view_toggle;     /* View: Log / View: Analysis */
    Rectangle log_full;        /* log mode: the whole panel */
    Rectangle identity;        /* analysis mode: what the network says */
    Rectangle constellation;   /* analysis mode: square, the phase steps */
    Rectangle profile;         /* analysis mode: what repeats in the slot */
    Rectangle log_split;       /* analysis mode: under the identity */
    float header_left;         /* x where the header text starts */
    float header_right;        /* x where it must stop, clear of the buttons */
};

static inline struct tetra_layout tetra_layout_for(float width, float height) {
    struct tetra_layout l;
    const float left = 82.0f;
    const float right_margin = 30.0f;
    const float bottom_margin = 30.0f;
    /* Two header rows above, as the ADS-B view has: the identity line and the
       funnel under it. The analysis arrangement needs one more for the caption
       naming what the charts were drawn from. */
    const float log_top = 138.0f;
    const float analysis_top = 156.0f;
    const float gap = 14.0f;
    float usable = width - left - right_margin;
    float span, upper_h, lower_y, lower_h, side, identity_w;

    if (usable < 240.0f)
        usable = 240.0f;

    /* Clear of the Calibration button, which chrome_layout.h anchors to the
       right edge down to y = 92. The GSM and ADS-B toggles sit at the same y
       for the same reason. */
    l.view_toggle = (Rectangle){ width - 150.0f, 100.0f, 130.0f, 26.0f };
    l.header_left = 22.0f;
    l.header_right = l.view_toggle.x - 12.0f;

    l.log_full = (Rectangle){ left, log_top, usable,
                              height - log_top - bottom_margin };

    span = height - analysis_top - bottom_margin;
    if (span < 140.0f)
        span = 140.0f;
    /* The charts get the larger share, which is the other way round from the
       ADS-B view and for a reason: there the lower row holds a message log
       that fills up, and here it holds six short fields and one row per
       *identity* -- which changes when the cell does and not otherwise. Split
       the ADS-B way, the two panels below sat almost empty over half a screen. */
    upper_h = span * 0.58f;
    lower_y = analysis_top + upper_h + 16.0f;
    lower_h = span * 0.42f - 16.0f;
    if (lower_h < 90.0f)
        lower_h = 90.0f;

    /*
     * The constellation is square, so its height sets its width -- capped, or
     * on a short wide window it takes the whole row and leaves no room for the
     * profile beside it. adsb_layout.h records the same clamp for the same
     * reason.
     */
    side = upper_h;
    if (side > usable * 0.4f)
        side = usable * 0.4f;
    l.constellation = (Rectangle){ left, analysis_top, side, side };

    /* The profile fills what is left of the upper row: 255 positions across,
       so it wants width more than anything else here does. */
    l.profile = (Rectangle){ left + side + gap, analysis_top,
                             usable - side - gap, upper_h };

    /* The identity panel is narrow -- it holds six short fields -- and the log
       takes the rest, since it is the one that grows. */
    identity_w = usable * 0.34f;
    if (identity_w < 200.0f)
        identity_w = 200.0f;
    if (identity_w > usable - 200.0f)
        identity_w = usable - 200.0f;
    l.identity = (Rectangle){ left, lower_y, identity_w, lower_h };
    l.log_split = (Rectangle){ left + identity_w + gap, lower_y,
                               usable - identity_w - gap, lower_h };
    return l;
}

#endif
