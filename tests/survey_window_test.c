#include "survey_window.h"

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

static int failures;

static void check_span(const char *name, const struct survey_window *w,
                       double lower_mhz, double upper_mhz) {
    double lower = w->view_lower_hz / 1e6;
    double upper = w->view_upper_hz / 1e6;

    if (fabs(lower - lower_mhz) > 0.001 || fabs(upper - upper_mhz) > 0.001) {
        fprintf(stderr, "%s: got %.3f-%.3f MHz, expected %.3f-%.3f\n", name,
                lower, upper, lower_mhz, upper_mhz);
        failures++;
    }
}

static void check_int(const char *name, long actual, long expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %ld, expected %ld\n", name, actual, expected);
        failures++;
    }
}

static void check_close(const char *name, double actual, double expected,
                        double tolerance) {
    if (!isfinite(actual) || fabs(actual - expected) > tolerance) {
        fprintf(stderr, "%s: got %.6f, expected %.6f (+/- %.6f)\n", name,
                actual, expected, tolerance);
        failures++;
    }
}

#define MIN_SPAN 100000.0
#define ZOOM_STEP 1.6

/* A window over the tuner's full span, as a freshly opened survey has. */
static struct survey_window full_range(void) {
    struct survey_window w = { 24e6, 1766e6, 0.0, 0.0 };
    survey_window_reset(&w);
    return w;
}

/* Before the first sweep the data range is whatever the fields say, and the
   window has to work against it: this is where zoom used to do nothing. */
static void test_window_before_any_sweep(void) {
    struct survey_window w = { 24e6, 1766e6, 0.0, 0.0 };

    check_int("an unset window has no extent",
              w.view_upper_hz > w.view_lower_hz, 0);
    survey_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    if (!(w.view_upper_hz > w.view_lower_hz)) {
        fprintf(stderr, "zooming an unset window left it unset\n");
        failures++;
        return;
    }
    /* One step in from the full 1742 MHz, about the middle. */
    check_close("zoomed span", (w.view_upper_hz - w.view_lower_hz) / 1e6,
                1742.0 / ZOOM_STEP, 0.01);
    check_close("still centred", (w.view_upper_hz + w.view_lower_hz) / 2e6,
                (24.0 + 1766.0) / 2.0, 0.01);
}

static void test_zoom_in_and_out(void) {
    struct survey_window w = full_range();
    double full = w.data_upper_hz - w.data_lower_hz;

    survey_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    survey_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_close("two steps in", (w.view_upper_hz - w.view_lower_hz) / 1e6,
                full / 1e6 / (ZOOM_STEP * ZOOM_STEP), 0.01);
    survey_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
    survey_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_span("back out to the whole range", &w, 24.0, 1766.0);

    /* Out again cannot go wider than the data. */
    survey_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_span("zooming out past the data", &w, 24.0, 1766.0);

    /* And in cannot go below the floor. */
    for (int i = 0; i < 40; i++)
        survey_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
    check_close("zoom floor", w.view_upper_hz - w.view_lower_hz, MIN_SPAN,
                1.0);
}

/* Zooming keeps the anchor -- the selected candidate -- on screen, which is
   the whole reason the anchor exists. */
static void test_zoom_anchored_on_a_candidate(void) {
    struct survey_window w = full_range();
    double candidate = 1090e6;

    for (int i = 0; i < 12; i++) {
        survey_window_zoom(&w, 1.0 / ZOOM_STEP, candidate, 1, MIN_SPAN);
        if (candidate < w.view_lower_hz || candidate > w.view_upper_hz) {
            fprintf(stderr, "anchor left the window after %d steps\n", i + 1);
            failures++;
            return;
        }
    }
    check_close("zoomed onto the anchor",
                (w.view_upper_hz + w.view_lower_hz) / 2e6, 1090.0, 1.0);
}

static void test_pan(void) {
    struct survey_window w = full_range();

    check_int("panning the whole range cannot move it",
              survey_window_pan(&w, 0.25, MIN_SPAN), 0);

    survey_window_zoom(&w, 1.0 / (ZOOM_STEP * ZOOM_STEP * ZOOM_STEP), 0.0, 0,
                       MIN_SPAN);
    double span = w.view_upper_hz - w.view_lower_hz;
    double lower = w.view_lower_hz;
    check_int("panning a zoomed window moves it",
              survey_window_pan(&w, 0.25, MIN_SPAN), 1);
    check_close("panned by a quarter of the span",
                w.view_lower_hz - lower, span * 0.25, 1.0);

    /* Walk to the top and it stops there rather than running off. */
    for (int i = 0; i < 200; i++)
        survey_window_pan(&w, 0.25, MIN_SPAN);
    check_close("stops at the top", w.view_upper_hz / 1e6, 1766.0, 0.001);
    check_int("and says so", survey_window_pan(&w, 0.25, MIN_SPAN), 0);
    check_int("but can still come back",
              survey_window_pan(&w, -0.25, MIN_SPAN), 1);
}

/*
 * The regression: on a fresh survey with a region selected, Sweep must sweep
 * the region. It used to sweep everything, because the narrowing test required
 * a completed sweep.
 */
static void test_sweep_target(void) {
    struct survey_window fresh = { 24e6, 1766e6, 0.0, 0.0 };
    double from;
    double to;

    survey_window_reset(&fresh);
    check_int("unzoomed: not narrowing",
              survey_window_sweep_target(&fresh, &from, &to), 0);
    check_close("unzoomed: sweeps the whole range", from / 1e6, 24.0, 0.001);
    check_close("unzoomed: to the top", to / 1e6, 1766.0, 0.001);

    fresh.view_lower_hz = 940e6;
    fresh.view_upper_hz = 950e6;
    check_int("zoomed before any sweep: narrowing",
              survey_window_sweep_target(&fresh, &from, &to), 1);
    check_close("sweeps the selected region", from / 1e6, 940.0, 0.001);
    check_close("and only that", to / 1e6, 950.0, 0.001);

    /* After a sweep of that region the window equals the data again, so the
       next Sweep repeats it rather than narrowing further. */
    struct survey_window swept = { 940e6, 950e6, 940e6, 950e6 };
    check_int("after the region sweep: not narrowing",
              survey_window_sweep_target(&swept, &from, &to), 0);
    check_close("repeats the region", from / 1e6, 940.0, 0.001);

    /* A window that was never given an extent falls back to the data. */
    struct survey_window unset = { 100e6, 200e6, 0.0, 0.0 };
    check_int("unset window: not narrowing",
              survey_window_sweep_target(&unset, &from, &to), 0);
    check_close("unset window sweeps the data", from / 1e6, 100.0, 0.001);
}

static void test_bins_and_visibility(void) {
    struct survey_window w = { 935e6, 960e6, 935e6, 960e6 };
    const int bins = 1000;

    check_close("first bin", survey_window_bin_hz(&w, bins, 0) / 1e6,
                935.0125, 0.0001);
    check_close("last bin", survey_window_bin_hz(&w, bins, bins - 1) / 1e6,
                959.9875, 0.0001);
    check_int("everything is visible unzoomed",
              survey_window_bin_visible(&w, bins, 0) &&
                  survey_window_bin_visible(&w, bins, bins - 1), 1);

    w.view_lower_hz = 940e6;
    w.view_upper_hz = 945e6;
    check_int("a bin below the window is hidden",
              survey_window_bin_visible(&w, bins, 0), 0);
    check_int("a bin inside it is not",
              survey_window_bin_visible(&w, bins, 240), 1);
    check_int("a bin above the window is hidden",
              survey_window_bin_visible(&w, bins, bins - 1), 0);
}

/* Whatever the sequence, the window stays inside the data and above the
   floor: the property every one of the above is a special case of. */
static void test_window_stays_legal(void) {
    struct survey_window w = full_range();
    unsigned seed = 12345;

    for (int step = 0; step < 4000; step++) {
        seed = seed * 1103515245u + 12345u;
        switch ((seed >> 16) % 4) {
        case 0:
            survey_window_zoom(&w, 1.0 / ZOOM_STEP, 0.0, 0, MIN_SPAN);
            break;
        case 1:
            survey_window_zoom(&w, ZOOM_STEP, 0.0, 0, MIN_SPAN);
            break;
        case 2:
            survey_window_pan(&w, 0.25, MIN_SPAN);
            break;
        default:
            survey_window_pan(&w, -0.25, MIN_SPAN);
            break;
        }
        if (w.view_lower_hz < w.data_lower_hz - 0.5 ||
            w.view_upper_hz > w.data_upper_hz + 0.5 ||
            w.view_upper_hz - w.view_lower_hz < MIN_SPAN - 0.5) {
            fprintf(stderr, "step %d left the window at %.3f-%.3f MHz\n", step,
                    w.view_lower_hz / 1e6, w.view_upper_hz / 1e6);
            failures++;
            return;
        }
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

    if (failures) {
        fprintf(stderr, "%d survey window check(s) failed\n", failures);
        return 1;
    }
    puts("survey window checks passed");
    return 0;
}
