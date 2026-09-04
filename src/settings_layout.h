#ifndef SETTINGS_LAYOUT_H
#define SETTINGS_LAYOUT_H

#include <raylib.h>

/*
 * Where the Settings panel's controls go.
 *
 * A header rather than inline arithmetic, for a reason this panel has already
 * demonstrated: its ten rectangles used to be declared twice, once in
 * handle_settings_input() and once in draw_settings(), with nothing but
 * simultaneous editing keeping the two copies equal. That is the same failure
 * `row_list.h` was extracted to prevent for lists whose draw and hit test had
 * drifted, and `chrome_tab_rect()` for a tab whose lookup and rectangle had.
 *
 * It is also the panel that shipped an overlapping caption: the resolution
 * row went in at y + 262 where the checkbox above runs to y + 272, and a
 * screenshot was the only thing that could say so, because check-layout had
 * never seen this screen.
 *
 * Rows are laid out from a running cursor rather than from literals, so
 * inserting or removing one moves what follows instead of leaving a hole or
 * an overlap. The panel's height is derived from where the rows end, which is
 * what stops a new row landing under the buttons -- the bug that made the
 * panel 420 px in the first place, by hand.
 *
 * Pure: raylib only for `Rectangle`, no window, no font.
 */

#define SETTINGS_PANEL_W 520.0f
#define SETTINGS_MARGIN 28.0f
#define SETTINGS_FIELD_H 38.0f
#define SETTINGS_TOGGLE 22.0f
#define SETTINGS_BUTTON_W 92.0f
#define SETTINGS_BUTTON_H 34.0f

/* A caption sits this far above the control it names, and needs this much
   room. Both are here rather than in the drawing because the gap between two
   rows has to clear the lower one's caption, which is a geometry question. */
#define SETTINGS_CAPTION_GAP 23.0f
#define SETTINGS_CAPTION_H 17.0f

struct settings_layout {
    Rectangle panel;
    Rectangle frequency;
    Rectangle ppm;
    Rectangle gain_previous;
    Rectangle gain_next;
    Rectangle gain_value;       /* between the two steppers */
    Rectangle dc_toggle;
    Rectangle drift_toggle;
    Rectangle fft_previous;
    Rectangle fft_next;
    Rectangle fft_value;
    Rectangle cancel;
    Rectangle apply;
};

static inline struct settings_layout settings_layout_for(float screen_width,
                                                         float screen_height) {
    struct settings_layout l;
    const float width = SETTINGS_PANEL_W;
    const float inner = width - SETTINGS_MARGIN * 2.0f;
    /*
     * The rows, in order, measured from the panel's top: a title strip, then
     * each row's caption and control, then the buttons. Adding a row here is
     * the only edit needed -- the height follows.
     */
    const float title_h = 60.0f;
    const float row_gap = 20.0f;
    float y = title_h;
    float height;

    /* frequency and PPM share a row */
    float frequency_y = y + SETTINGS_CAPTION_GAP;
    y = frequency_y + SETTINGS_FIELD_H + row_gap + SETTINGS_CAPTION_GAP;
    {
        float gain_y = y;
        float dc_y = gain_y + SETTINGS_FIELD_H + 16.0f;
        float drift_y = dc_y + SETTINGS_TOGGLE + 10.0f;
        float fft_y = drift_y + SETTINGS_TOGGLE + SETTINGS_CAPTION_GAP +
                      row_gap;

        height = fft_y + SETTINGS_FIELD_H + row_gap + SETTINGS_BUTTON_H +
                 SETTINGS_MARGIN;
        l.panel = (Rectangle){ (screen_width - width) * 0.5f,
                               (screen_height - height) * 0.5f, width,
                               height };

        l.frequency = (Rectangle){ l.panel.x + SETTINGS_MARGIN,
                                   l.panel.y + frequency_y, 300.0f,
                                   SETTINGS_FIELD_H };
        l.ppm = (Rectangle){ l.panel.x + 342.0f, l.panel.y + frequency_y,
                             width - 370.0f, SETTINGS_FIELD_H };

        l.gain_previous = (Rectangle){ l.panel.x + SETTINGS_MARGIN,
                                       l.panel.y + gain_y, 42.0f,
                                       SETTINGS_FIELD_H };
        l.gain_next = (Rectangle){ l.panel.x + width - SETTINGS_MARGIN - 42.0f,
                                   l.panel.y + gain_y, 42.0f,
                                   SETTINGS_FIELD_H };
        l.gain_value = (Rectangle){ l.gain_previous.x + 42.0f,
                                    l.panel.y + gain_y,
                                    inner - 84.0f, SETTINGS_FIELD_H };

        l.dc_toggle = (Rectangle){ l.panel.x + SETTINGS_MARGIN,
                                   l.panel.y + dc_y, SETTINGS_TOGGLE,
                                   SETTINGS_TOGGLE };
        l.drift_toggle = (Rectangle){ l.panel.x + SETTINGS_MARGIN,
                                      l.panel.y + drift_y, SETTINGS_TOGGLE,
                                      SETTINGS_TOGGLE };

        l.fft_previous = (Rectangle){ l.panel.x + SETTINGS_MARGIN,
                                      l.panel.y + fft_y, 42.0f,
                                      SETTINGS_FIELD_H };
        l.fft_next = (Rectangle){ l.panel.x + width - SETTINGS_MARGIN - 42.0f,
                                  l.panel.y + fft_y, 42.0f,
                                  SETTINGS_FIELD_H };
        l.fft_value = (Rectangle){ l.fft_previous.x + 42.0f,
                                   l.panel.y + fft_y, inner - 84.0f,
                                   SETTINGS_FIELD_H };
    }

    l.apply = (Rectangle){ l.panel.x + width - SETTINGS_MARGIN -
                               SETTINGS_BUTTON_W,
                           l.panel.y + height - SETTINGS_MARGIN -
                               SETTINGS_BUTTON_H,
                           SETTINGS_BUTTON_W, SETTINGS_BUTTON_H };
    l.cancel = (Rectangle){ l.apply.x - SETTINGS_BUTTON_W - 12.0f, l.apply.y,
                            SETTINGS_BUTTON_W, SETTINGS_BUTTON_H };
    return l;
}

/* Where a control's caption goes: above it, right-aligned to its left edge by
   the drawing. Here so the checks can assert a caption never lands on the row
   above -- which is the bug this panel actually shipped. */
static inline Rectangle settings_caption_of(Rectangle control) {
    return (Rectangle){ control.x, control.y - SETTINGS_CAPTION_GAP,
                        control.width, SETTINGS_CAPTION_H };
}

#endif
