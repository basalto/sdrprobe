#ifndef SRD_LAYOUT_H
#define SRD_LAYOUT_H

#include <raylib.h>
#include "panel_rows.h"

#define SRD_PANEL_CAPTION_DROP 30.0f
#define SRD_PANEL_ROW_HEIGHT 26.0f

/* The tuning group: an arrow either side of a typed frequency. Field width
   is the Scope's, because the two hold the same number to the same four
   decimal places and a reader moving between them should not have to
   re-learn where it sits. */
#define SRD_FREQ_STEP_W 26.0f
#define SRD_FREQ_FIELD_W 100.0f
#define SRD_FREQ_GAP 4.0f
/* Room for the "MHz" after the second arrow. Modelled rather than drawn
   blind: it was drawn at a bare offset first and ran straight into the
   transmission count beside it, which check-layout could not see because
   nothing in the layout knew the label existed. */
#define SRD_FREQ_UNIT_W 34.0f
#define SRD_CONTROL_ROW_Y 100.0f
#define SRD_CONTROL_ROW_H 26.0f

/* The narrowest a "Retune to 434 MHz" button may be and still say so. Below
   it the button is not drawn at all -- the frequency field can reach 434 by
   two keystrokes, so a button clipped to an unreadable stub is worse than
   absent, which is panel_rows.h's rule one level out. */
#define SRD_RETUNE_MIN_W 150.0f

/*
 * Where the SRD (430-440 MHz) decode view puts things.
 *
 * Pure function of window size, testable without raylib:
 * - Log mode: a waterfall on top (FM's shape -- the only thing on screen
 *   that shows a burst arrive), log_full underneath.
 * - Analysis mode:
 *     Upper row: envelope trace and chip/bit waveform.
 *     Lower row: identity/parameter summary card and recent log.
 */
struct srd_layout {
    Rectangle view_toggle;     /* Show charts / Show log */
    Rectangle record_button;   /* Record, beside the view toggle */
    Rectangle record_seconds;  /* typed duration, to Record's left */
    Rectangle auto_save_button;/* Auto-save staging toggle */
    Rectangle freq_down;       /* step the centre down one half-span */
    Rectangle freq_field;      /* the centre, in MHz, typed */
    Rectangle freq_up;         /* step the centre up one half-span */
    Rectangle retune_button;   /* shown only when tuned away from 434 MHz,
                                  and zero width when there is no room */
    Rectangle waterfall;       /* log mode: above log_full */
    Rectangle log_full;        /* log mode: under the waterfall */
    Rectangle identity;        /* analysis mode: parameter summary */
    Rectangle envelope;        /* analysis mode: envelope waveform */
    Rectangle chips;           /* analysis mode: chip/bit sequence */
    Rectangle log_split;       /* analysis mode: under identity */
    float header_left;         /* x where header text starts */
    float header_right;        /* x where it must stop before the controls */
    float header_second_left;  /* x where the *second* header line starts --
                                  right of the tuning group, which owns the
                                  left of that row */
};

static inline struct srd_layout srd_layout_for(float width, float height) {
    struct srd_layout l;
    const float left = 82.0f;
    const float right_margin = 30.0f;
    const float bottom_margin = 30.0f;
    const float log_top = 138.0f;
    const float analysis_top = 156.0f;
    const float gap = 14.0f;
    float usable = width - left - right_margin;
    float span, upper_h, lower_y, lower_h, env_w, chips_w, identity_w;
    float log_span, waterfall_h, log_panel_y, log_panel_h;

    if (usable < 240.0f)
        usable = 240.0f;

    /* Clear of the Calibration button, which chrome_layout.h anchors to the
       right edge down to y = 92 -- the same clearance every other decode
       view's toggle takes. Record and its duration field sit to its left,
       both acting on the view as a whole rather than one panel. */
    l.view_toggle = (Rectangle){ width - 150.0f, 100.0f, 150.0f, 26.0f };
    l.record_button = (Rectangle){ l.view_toggle.x - 100.0f, 100.0f,
                                   90.0f, 26.0f };
    l.record_seconds = (Rectangle){ l.record_button.x - 60.0f, 100.0f,
                                    50.0f, 26.0f };
    l.auto_save_button = (Rectangle){ l.record_seconds.x - 110.0f, 100.0f,
                                      100.0f, 26.0f };
    l.header_left = 22.0f;
    l.header_right = l.auto_save_button.x - 12.0f;

    /*
     * The tuning group owns the left of the control row.
     *
     * It is here rather than in a corner because it is the only control on
     * this screen that moves the receiver, and a control whose effect is on
     * the chart below it belongs on that chart's screen -- which is
     * scope_layout.h's argument for putting the Scope's centre field beside
     * its axis rather than two clicks away in Settings.
     */
    l.freq_down = (Rectangle){ l.header_left, SRD_CONTROL_ROW_Y,
                               SRD_FREQ_STEP_W, SRD_CONTROL_ROW_H };
    l.freq_field = (Rectangle){ l.freq_down.x + SRD_FREQ_STEP_W + SRD_FREQ_GAP,
                                SRD_CONTROL_ROW_Y, SRD_FREQ_FIELD_W,
                                SRD_CONTROL_ROW_H };
    l.freq_up = (Rectangle){ l.freq_field.x + SRD_FREQ_FIELD_W + SRD_FREQ_GAP,
                             SRD_CONTROL_ROW_Y, SRD_FREQ_STEP_W,
                             SRD_CONTROL_ROW_H };
    l.header_second_left = l.freq_up.x + SRD_FREQ_STEP_W +
                           SRD_FREQ_UNIT_W + 12.0f;
    /*
     * At a window narrow enough, the control row holds the tuning group and
     * the right-anchored buttons and nothing else. The second header line is
     * then **absent** rather than drawn into whatever is left, which is
     * panel_rows.h's rule -- a row that does not fit is not drawn, because
     * off the edge is worse than missing. It is the count of transmissions,
     * which the log below repeats; the tuning group is the control, and
     * controls win the room.
     */
    if (l.header_second_left > l.header_right)
        l.header_second_left = l.header_right;

    /*
     * The retune button follows the group, in whatever room is left before
     * the right-anchored controls -- and in none at all when there is none.
     *
     * It used to sit at header_left with a fixed 220 px, which **overlapped
     * the Auto-save button at 640x480**: auto-save lands at x=220 there and
     * the button ran to 242. check-layout did not see it because the
     * assertion compares the button against `record_seconds`, the leftmost
     * control on that row when it was written, and Auto-save was added to
     * its left afterwards without the assertion following. It compares
     * against the tuning group and Auto-save now.
     */
    {
        float retune_x = l.header_second_left;
        float retune_w = 220.0f;

        if (retune_x + retune_w > l.header_right)
            retune_w = l.header_right - retune_x;
        if (retune_w < SRD_RETUNE_MIN_W) {
            retune_x = 0.0f;
            retune_w = 0.0f;
        }
        l.retune_button = (Rectangle){ retune_x, SRD_CONTROL_ROW_Y, retune_w,
                                       SRD_CONTROL_ROW_H };
    }

    log_span = height - log_top - bottom_margin;
    if (log_span < 160.0f)
        log_span = 160.0f;
    /* The waterfall takes the larger share, as it does on every other decode
       view that has one: it is the only thing on screen that shows a burst
       arrive, and the log is text. */
    waterfall_h = log_span * 0.52f;
    log_panel_y = log_top + waterfall_h + gap;
    log_panel_h = log_span - waterfall_h - gap;
    if (log_panel_h < 90.0f)
        log_panel_h = 90.0f;

    l.waterfall = (Rectangle){ left, log_top, usable, waterfall_h };
    l.log_full = (Rectangle){ left, log_panel_y, usable, log_panel_h };

    span = height - analysis_top - bottom_margin;
    if (span < 140.0f)
        span = 140.0f;

    upper_h = span * 0.50f;
    lower_y = analysis_top + upper_h + 16.0f;
    lower_h = span * 0.50f - 16.0f;
    if (lower_h < 90.0f)
        lower_h = 90.0f;

    /* Upper row: envelope (45%) and chips (55%) */
    env_w = (usable - gap) * 0.45f;
    chips_w = usable - gap - env_w;
    l.envelope = (Rectangle){ left, analysis_top, env_w, upper_h };
    l.chips = (Rectangle){ left + env_w + gap, analysis_top, chips_w, upper_h };

    /* Lower row: identity card (38%) and log split (62%) */
    identity_w = (usable - gap) * 0.38f;
    if (identity_w < 260.0f && usable > 400.0f)
        identity_w = 260.0f;
    if (identity_w > usable - 100.0f)
        identity_w = usable * 0.5f;

    l.identity = (Rectangle){ left, lower_y, identity_w, lower_h };
    l.log_split = (Rectangle){ left + identity_w + gap, lower_y,
                               usable - identity_w - gap, lower_h };

    return l;
}

#endif /* SRD_LAYOUT_H */
