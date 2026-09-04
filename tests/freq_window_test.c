#include "check.h"
#include "freq_window.h"

#include <math.h>
#include <stdio.h>

/*
 * The band survey's window arithmetic, checked without a window, a receiver or
 * a pointer.
 *
 * Two of these cases are regressions with a history. A freshly opened survey
 * had no window extent until its first sweep, so zoom, pan and drag divided by
 * a span of zero and silently did nothing. And Sweep ignored a selected region
 * on that same first sweep, because the test for "is this narrower than the
 * range it sits on" also demanded that a sweep already exist. Both were found
 * by a person using the program, and both are arithmetic.
 */

static void check_span(const char *name, const struct freq_window *w,
                       double lower_mhz, double upper_mhz) {
    double lower = w->view_lower_hz / 1e6;
    double upper = w->view_upper_hz / 1e6;

    check_msg(fabs(lower - lower_mhz) <= 0.001 &&
                  fabs(upper - upper_mhz) <= 0.001,
              "%s: got %.3f-%.3f MHz, expected %.3f-%.3f\n", name, lower, upper,
              lower_mhz, upper_mhz);
}

#define MIN_SPAN 100000.0
#define ZOOM_STEP 1.6

/* A window over the tuner's full span, as a freshly opened survey has. */
static struct freq_window full_range(void) {
    struct freq_window w = { 24e6, 1766e6, 0.0, 0.0 };
    freq_window_reset(&w);
    return w;
}

/* Before the first sweep the data range is whatever the fields say, and the
   window has to work against it: this is where zoom used to do nothing. */
static void test_window_before_any_sweep(void) {
    struct freq_window w = { 24e6, 1766e6, 0.0, 0.0 };

    check_int("an unset window has no extent",
              w.view_upper_hz > w.view_lower_hz, 0);
    freq_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_msg(w.view_upper_hz > w.view_lower_hz,
              "zooming an unset window left it unset\n");
    if (w.view_upper_hz <= w.view_lower_hz)
        return;
    /* One step in from the full 1742 MHz, about the middle. */
    check_close("zoomed span", (w.view_upper_hz - w.view_lower_hz) / 1e6,
                1742.0 / ZOOM_STEP, 0.01);
    check_close("still centred", (w.view_upper_hz + w.view_lower_hz) / 2e6,
                (24.0 + 1766.0) / 2.0, 0.01);
}

static void test_zoom_in_and_out(void) {
    struct freq_window w = full_range();
    double full = w.data_upper_hz - w.data_lower_hz;

    freq_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    freq_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_close("two steps in", (w.view_upper_hz - w.view_lower_hz) / 1e6,
                full / 1e6 / (ZOOM_STEP * ZOOM_STEP), 0.01);
    freq_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
    freq_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_span("back out to the whole range", &w, 24.0, 1766.0);

    /* Out again cannot go wider than the data. */
    freq_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_span("zooming out past the data", &w, 24.0, 1766.0);

    /* And in cannot go below the floor. */
    for (int i = 0; i < 40; i++)
        freq_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_close("zoom floor", w.view_upper_hz - w.view_lower_hz, MIN_SPAN,
                1.0);
}

/* Zooming keeps the anchor -- the selected candidate -- on screen, which is
   the whole reason the anchor exists. */
static void test_zoom_anchored_on_a_candidate(void) {
    struct freq_window w = full_range();
    double candidate = 1090e6;

    for (int i = 0; i < 12; i++) {
        freq_window_zoom(&w, 1.0 / ZOOM_STEP, candidate, 1, MIN_SPAN);
        check_msg(candidate >= w.view_lower_hz && candidate <= w.view_upper_hz,
                  "anchor left the window after %d steps\n", i + 1);
        if (candidate < w.view_lower_hz || candidate > w.view_upper_hz)
            return;
    }
    check_close("zoomed onto the anchor",
                (w.view_upper_hz + w.view_lower_hz) / 2e6, 1090.0, 1.0);
}

static void test_pan(void) {
    struct freq_window w = full_range();

    check_int("panning the whole range cannot move it",
              freq_window_pan(&w, 0.25, MIN_SPAN), 0);

    freq_window_zoom(&w, 1.0 / (ZOOM_STEP * ZOOM_STEP * ZOOM_STEP), 0.0, 0,
                       MIN_SPAN);
    double span = w.view_upper_hz - w.view_lower_hz;
    double lower = w.view_lower_hz;
    check_int("panning a zoomed window moves it",
              freq_window_pan(&w, 0.25, MIN_SPAN), 1);
    check_close("panned by a quarter of the span",
                w.view_lower_hz - lower, span * 0.25, 1.0);

    /* Walk to the top and it stops there rather than running off. */
    for (int i = 0; i < 200; i++)
        freq_window_pan(&w, 0.25, MIN_SPAN);
    check_close("stops at the top", w.view_upper_hz / 1e6, 1766.0, 0.001);
    check_int("and says so", freq_window_pan(&w, 0.25, MIN_SPAN), 0);
    check_int("but can still come back",
              freq_window_pan(&w, -0.25, MIN_SPAN), 1);
}

/*
 * The regression: on a fresh survey with a region selected, Sweep must sweep
 * the region. It used to sweep everything, because the narrowing test required
 * a completed sweep.
 */
static void test_sweep_target(void) {
    struct freq_window fresh = { 24e6, 1766e6, 0.0, 0.0 };
    double from;
    double to;

    freq_window_reset(&fresh);
    check_int("unzoomed: not narrowing",
              freq_window_sweep_target(&fresh, &from, &to), 0);
    check_close("unzoomed: sweeps the whole range", from / 1e6, 24.0, 0.001);
    check_close("unzoomed: to the top", to / 1e6, 1766.0, 0.001);

    fresh.view_lower_hz = 940e6;
    fresh.view_upper_hz = 950e6;
    check_int("zoomed before any sweep: narrowing",
              freq_window_sweep_target(&fresh, &from, &to), 1);
    check_close("sweeps the selected region", from / 1e6, 940.0, 0.001);
    check_close("and only that", to / 1e6, 950.0, 0.001);

    /* After a sweep of that region the window equals the data again, so the
       next Sweep repeats it rather than narrowing further. */
    struct freq_window swept = { 940e6, 950e6, 940e6, 950e6 };
    check_int("after the region sweep: not narrowing",
              freq_window_sweep_target(&swept, &from, &to), 0);
    check_close("repeats the region", from / 1e6, 940.0, 0.001);

    /* A window that was never given an extent falls back to the data. */
    struct freq_window unset = { 100e6, 200e6, 0.0, 0.0 };
    check_int("unset window: not narrowing",
              freq_window_sweep_target(&unset, &from, &to), 0);
    check_close("unset window sweeps the data", from / 1e6, 100.0, 0.001);
}

static void test_bins_and_visibility(void) {
    struct freq_window w = { 935e6, 960e6, 935e6, 960e6 };
    const int bins = 1000;

    check_close("first bin", freq_window_bin_hz(&w, bins, 0) / 1e6,
                935.0125, 0.0001);
    check_close("last bin", freq_window_bin_hz(&w, bins, bins - 1) / 1e6,
                959.9875, 0.0001);
    check_int("everything is visible unzoomed",
              freq_window_bin_visible(&w, bins, 0) &&
                  freq_window_bin_visible(&w, bins, bins - 1), 1);

    w.view_lower_hz = 940e6;
    w.view_upper_hz = 945e6;
    check_int("a bin below the window is hidden",
              freq_window_bin_visible(&w, bins, 0), 0);
    check_int("a bin inside it is not",
              freq_window_bin_visible(&w, bins, 240), 1);
    check_int("a bin above the window is hidden",
              freq_window_bin_visible(&w, bins, bins - 1), 0);
}

/* Whatever the sequence, the window stays inside the data and above the
   floor: the property every one of the above is a special case of. */
static void test_window_stays_legal(void) {
    struct freq_window w = full_range();
    unsigned seed = 12345;
    int broke_at = -1;

    for (int step = 0; step < 4000 && broke_at < 0; step++) {
        seed = seed * 1103515245u + 12345u;
        switch ((seed >> 16) % 4) {
        case 0:
            freq_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
            break;
        case 1:
            freq_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
            break;
        case 2:
            freq_window_pan(&w, 0.25, MIN_SPAN);
            break;
        default:
            freq_window_pan(&w, -0.25, MIN_SPAN);
            break;
        }
        if (w.view_lower_hz < w.data_lower_hz - 0.5 ||
            w.view_upper_hz > w.data_upper_hz + 0.5 ||
            w.view_upper_hz - w.view_lower_hz < MIN_SPAN - 0.5)
            broke_at = step;
    }
    /* One property, not four thousand assertions: the walk is the evidence,
       and the step it broke at is what a reader needs. */
    check_msg(broke_at < 0,
              "4000 random zoom/pan steps: step %d left the window at "
              "%.3f-%.3f MHz\n",
              broke_at, w.view_lower_hz / 1e6, w.view_upper_hz / 1e6);
}


/*
 * Pixels to frequency and back.
 *
 * A drag is measured with one of these and drawn with the other, so they have
 * to be each other's inverse -- two copies of this arithmetic is how a
 * selection lands somewhere other than where the box was drawn.
 */
static void test_pixels_and_frequencies(void) {
    struct freq_window w = { 88000000.0, 108000000.0,
                             90000000.0, 100000000.0 };
    const float x = 100.0f, width = 500.0f;

    check_close("the left edge is the window's lower edge",
                freq_window_hz_at(&w, x, width, x), 90000000.0, 1.0);
    check_close("the right edge is its upper", 
                freq_window_hz_at(&w, x, width, x + width), 100000000.0, 1.0);
    check_close("and the middle is the middle",
                freq_window_hz_at(&w, x, width, x + width / 2.0f),
                95000000.0, 1.0);
    check_close("back again", freq_window_x_at(&w, x, width, 95000000.0),
                x + width / 2.0f, 0.01);

    /* Each other's inverse across the whole plot. */
    {
        int k, wrong = 0;
        for (k = 0; k <= 100; k++) {
            float px = x + width * (float)k / 100.0f;
            double hz = freq_window_hz_at(&w, x, width, px);
            if (fabs(freq_window_x_at(&w, x, width, hz) - px) > 0.01)
                wrong++;
        }
        check_int("every pixel round-trips", wrong, 0);
    }

    /* A window with no width answers rather than dividing by zero. */
    check_close("no plot, no frequency", freq_window_hz_at(&w, x, 0.0f, x),
                0.0, 1e-9);
    {
        struct freq_window flat = { 0.0, 0.0, 100.0, 100.0 };
        check_close("and a window of no span maps to the left edge",
                    freq_window_x_at(&flat, x, width, 100.0), x, 0.01);
    }
}

static void test_what_a_drag_means(void) {
    struct freq_window w = { 88000000.0, 108000000.0,
                             88000000.0, 108000000.0 };
    const float x = 0.0f, width = 1000.0f;
    double lower = 0.0, upper = 0.0;

    check_int("a drag across a fifth of the plot",
              freq_window_drag(&w, x, width, 200.0f, 400.0f, 100000.0,
                               &lower, &upper), 1);
    check_close("starts a fifth in", lower, 92000000.0, 1.0);
    check_close("and ends two fifths in", upper, 96000000.0, 1.0);

    /* Backwards is the same drag. Somebody who selects right to left has not
       made a mistake. */
    {
        double back_lower = 0.0, back_upper = 0.0;
        check_int("dragged the other way", 
                  freq_window_drag(&w, x, width, 400.0f, 200.0f, 100000.0,
                                   &back_lower, &back_upper), 1);
        check_close("gives the same lower edge", back_lower, lower, 1.0);
        check_close("and the same upper", back_upper, upper, 1.0);
    }

    /*
     * A drag too narrow to mean anything is refused and changes nothing. That
     * is a click, or a hand that moved while clicking, and zooming to a few
     * hertz because of one is worse than doing nothing.
     */
    {
        double keep_lower = 1.0, keep_upper = 2.0;
        check_int("a click is not a drag",
                  freq_window_drag(&w, x, width, 300.0f, 300.5f, 100000.0,
                                   &keep_lower, &keep_upper), 0);
        check_close("and leaves the outputs alone", keep_lower, 1.0, 1e-9);
        check_close("both of them", keep_upper, 2.0, 1e-9);
    }
    check_int("nowhere to put the answer",
              freq_window_drag(&w, x, width, 100.0f, 900.0f, 1000.0, NULL,
                               &upper), 0);
}


/*
 * Panning past the end of the data.
 *
 * The window can only move inside what exists, and on a live receiver that is
 * not a wall: the tuner can be pointed somewhere else. So a pan that runs out
 * of data reports the part it could not absorb, and a caller with a tuner
 * moves it by that much -- which is how panning turns into retuning at the
 * edge of the span and nowhere before it.
 */
static void test_panning_past_the_end(void) {
    struct freq_window w;
    double over;

    /* Inside the data, nothing overflows. */
    w = (struct freq_window){ 88000000.0, 108000000.0,
                              94000000.0, 98000000.0 };
    over = freq_window_pan_overflow(&w, 0.5, 100000.0);
    check_close("a pan with room takes it all", over, 0.0, 1.0);
    check_close("and moves half a window", w.view_lower_hz, 96000000.0, 1.0);

    /* Against the top edge, all of it overflows. */
    w = (struct freq_window){ 88000000.0, 108000000.0,
                              104000000.0, 108000000.0 };
    over = freq_window_pan_overflow(&w, 0.5, 100000.0);
    check_close("a pan with no room at all overflows entirely", over,
                2000000.0, 1.0);
    check_close("and the window does not move", w.view_lower_hz, 104000000.0,
                1.0);

    /* Partly against it: what fits is taken and the rest reported. */
    w = (struct freq_window){ 88000000.0, 108000000.0,
                              103000000.0, 107000000.0 };
    over = freq_window_pan_overflow(&w, 0.5, 100000.0);
    check_close("a pan that half fits takes half", over, 1000000.0, 1.0);
    check_close("moving as far as it can", w.view_upper_hz, 108000000.0, 1.0);

    /* Downwards too, with the sign kept: a caller retunes by it. */
    w = (struct freq_window){ 88000000.0, 108000000.0,
                              88000000.0, 92000000.0 };
    over = freq_window_pan_overflow(&w, -0.5, 100000.0);
    check_close("panning down at the bottom overflows downwards", over,
                -2000000.0, 1.0);

    /*
     * The property: what the window moved plus what overflowed is what was
     * asked for. Anything else loses movement silently, which reads as a key
     * that sometimes works.
     */
    {
        double start, fraction;
        int wrong = 0;
        for (start = 88000000.0; start < 104000000.0; start += 1000000.0)
        for (fraction = -1.0; fraction <= 1.0; fraction += 0.25) {
            struct freq_window p = { 88000000.0, 108000000.0, start,
                                     start + 4000000.0 };
            double before = p.view_lower_hz;
            double spill = freq_window_pan_overflow(&p, fraction, 100000.0);
            double asked = 4000000.0 * fraction;
            double got = (p.view_lower_hz - before) + spill;

            if (fabs(got - asked) > 1.0)
                wrong++;
        }
        check_int("no movement is lost between the window and the overflow",
                  wrong, 0);
    }
}

int main(void) {
    test_window_before_any_sweep();
    test_zoom_in_and_out();
    test_zoom_anchored_on_a_candidate();
    test_pan();
    test_sweep_target();
    test_bins_and_visibility();
    test_window_stays_legal();

    test_pixels_and_frequencies();
    test_what_a_drag_means();
    test_panning_past_the_end();

    return check_report("the frequency window");
}
