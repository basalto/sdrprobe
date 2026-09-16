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

/*
 * The gap between the spectrum and the waterfall in the combined view.
 *
 * sdrgui_chart_area() only reserves 8 px below a chart's own plot rectangle,
 * but sdrgui_spectrum() draws its axis-summary caption *below that*, at
 * `plot.y + plot.height + 36` in 16 px text -- 44 px past the bottom edge of
 * the rectangle it was handed, because until this view existed nothing else
 * was ever drawn there. A 12 px gap left that caption overlapping the
 * waterfall's own top row, so this clears the caption's worst case (44 px)
 * with a few pixels to spare rather than the plot-only margin.
 */
#define SCOPE_SPLIT_GAP 52.0f

struct scope_plot_layout {
    Rectangle spectrum;
    Rectangle waterfall;
};

/* Splits one Scope-tab plot rectangle into a spectrum half (top) and a
   waterfall half (bottom) for the combined Spectrum+Waterfall view. Pure
   geometry, so tests/layout_test.c can pin it without a window -- the
   same reason scope_header_layout_for() is here rather than beside the
   drawing. */
static inline struct scope_plot_layout scope_plot_split(Rectangle plot) {
    struct scope_plot_layout out;
    float half = (plot.height - SCOPE_SPLIT_GAP) / 2.0f;
    float wf_y, wf_h;

    if (half < 1.0f)
        half = 1.0f;
    if (half > plot.height)
        half = plot.height;
    out.spectrum = (Rectangle){ plot.x, plot.y, plot.width, half };

    wf_y = plot.y + half + SCOPE_SPLIT_GAP;
    wf_h = plot.height - half - SCOPE_SPLIT_GAP;
    if (wf_h < 1.0f) {
        /* A plot too short for the gap and a usable waterfall row both --
           calculate_plot() clamps to as little as one pixel on a squeezed
           window. Drop the gap rather than let the waterfall run past the
           plot's own bottom edge: off the edge is worse than touching the
           spectrum. */
        wf_y = plot.y + half;
        wf_h = plot.height - half;
        if (wf_h < 0.0f)
            wf_h = 0.0f;
    }
    out.waterfall = (Rectangle){ plot.x, wf_y, plot.width, wf_h };
    return out;
}

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
