#ifndef SCOPE_LAYOUT_H
#define SCOPE_LAYOUT_H

#include <raylib.h>

#include <stdlib.h>

/*
 * The Scope's control row: where the tuning is typed rather than read.
 *
 * Until this existed the centre frequency lived in the Settings overlay, two
 * clicks away from the chart it moves, and the drawn range could only be
 * changed by dragging -- so a reader could see 948.300 to 948.600 on the axis
 * and have no way to say those numbers. The row puts the three that matter on
 * the screen they act on: the centre, and the two edges of the drawn window.
 *
 * `centre` is a *retune*: it moves the receiver. `start` and `end` are a
 * *zoom*: they move the window over samples already received, exactly as a
 * drag does, and they clamp to what the receiver is hearing. Keeping those
 * two ideas in separate fields is the whole reason there are three rather
 * than two -- a reader who wants to look closer should not have to retune to
 * do it, and a reader who wants to go somewhere else should not have to work
 * out an edge pair that means it.
 *
 * The resolution stepper is here rather than only in Settings for the same
 * reason: it changes what the chart in front of you resolves, and a control
 * whose effect is on screen belongs on that screen.
 *
 * Pure geometry, so tests/layout_test.c can check it without a window. The
 * label widths are the one thing it does not know -- MeasureText needs a font
 * which needs a window (ADR-0012) -- so labels sit at a fixed offset left of
 * their field and the drawing right-aligns into it.
 */

#define SCOPE_ROW_Y 185.0f
#define SCOPE_ROW_H 24.0f
#define SCOPE_FIELD_W 96.0f
#define SCOPE_STEP_W 26.0f

/* How far the plot sits below the row. calculate_plot() adds this, and the
   layout check asserts the two agree rather than trusting them to. */
#define SCOPE_ROW_BOTTOM (SCOPE_ROW_Y + SCOPE_ROW_H + 10.0f)

struct scope_header_layout {
    Rectangle centre_field;
    Rectangle start_field;      /* zero width when the view has no frequency
                                   axis -- magnitude and I/Q scatter */
    Rectangle end_field;
    Rectangle fft_previous;
    Rectangle fft_next;
    Rectangle fft_value;
    int has_window;             /* whether the four above are drawn at all */
    float label_gap;            /* a label's right edge sits this far left of
                                   its field */
    float label_height;
};

static inline struct scope_header_layout
scope_header_layout_for(float width, int has_frequency_axis) {
    struct scope_header_layout l;
    float x;
    const float y = SCOPE_ROW_Y;

    l.label_gap = 8.0f;
    l.label_height = 16.0f;
    l.has_window = has_frequency_axis ? 1 : 0;

    /*
     * start, centre, end -- in that order, because that is the order they
     * appear on the axis below. The centre sits between the two edges it is
     * the middle of, which is also the only arrangement where reading the row
     * left to right and reading the chart left to right agree.
     */
    if (!l.has_window) {
        /* Nothing but the centre to show: no frequency axis to bound. */
        l.centre_field = (Rectangle){ 78.0f, y, SCOPE_FIELD_W, SCOPE_ROW_H };
        l.start_field = (Rectangle){ 0.0f, y, 0.0f, SCOPE_ROW_H };
        l.end_field = l.start_field;
        l.fft_previous = l.start_field;
        l.fft_next = l.start_field;
        l.fft_value = l.start_field;
        return l;
    }

    x = 62.0f;                  /* clear of the "start" label */
    l.start_field = (Rectangle){ x, y, SCOPE_FIELD_W, SCOPE_ROW_H };
    x += SCOPE_FIELD_W + 66.0f; /* room for the "centre" label */
    l.centre_field = (Rectangle){ x, y, SCOPE_FIELD_W, SCOPE_ROW_H };
    x += SCOPE_FIELD_W + 46.0f; /* room for the "end" label */
    l.end_field = (Rectangle){ x, y, SCOPE_FIELD_W, SCOPE_ROW_H };
    x += SCOPE_FIELD_W + 62.0f; /* room for the "FFT" label */

    l.fft_previous = (Rectangle){ x, y, SCOPE_STEP_W, SCOPE_ROW_H };
    l.fft_value = (Rectangle){ x + SCOPE_STEP_W, y, 58.0f, SCOPE_ROW_H };
    l.fft_next = (Rectangle){ x + SCOPE_STEP_W + 58.0f, y, SCOPE_STEP_W,
                              SCOPE_ROW_H };

    /* A narrow window loses the resolution stepper before it loses the
       frequencies: the edges say what is on screen, and the stepper only
       changes how finely it is resolved. */
    if (l.fft_next.x + l.fft_next.width > width - 16.0f) {
        l.fft_previous.width = 0.0f;
        l.fft_next.width = 0.0f;
        l.fft_value.width = 0.0f;
    }
    return l;
}


/*
 * What a field says, in hertz -- or a negative number when it does not yet say
 * a frequency.
 *
 * A half-typed "94." is not an error, it is a reader mid-edit, so this is
 * asked only when they commit. Trailing rubbish is refused rather than
 * silently truncated: strtod("948.4x") is 948.4 and a field that quietly
 * discarded the "x" would retune somewhere the reader did not type.
 */
static inline double scope_field_hz(const char *text) {
    char *end = NULL;
    double mhz;

    if (!text || !text[0])
        return -1.0;
    mhz = strtod(text, &end);
    if (!end || end == text || *end)
        return -1.0;
    if (mhz <= 0.0)
        return -1.0;
    return mhz * 1e6;
}

#endif
