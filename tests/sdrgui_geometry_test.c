#include "sdrgui_geometry.h"
#include "check.h"

#include <stdio.h>

/*
 * Where a chart's parts go, and which one the pointer is over.
 *
 * This is the arithmetic behind a bug that reached the operator: the scan
 * chart selected a channel one or two to the right of the bar under the
 * cursor, because the bars are drawn inside a label gutter and the hit test
 * mapped the pointer across the whole rectangle. It was reported as "the
 * cursor is not selecting the bar that overlaps, but one or two in right"
 * (ADR-0012 -- and it is in the list of six).
 *
 * Needs raylib's headers for Rectangle and nothing else, so it links -lm and
 * opens no window, the same way tests/layout_test.c does.
 */

/* A gutter about as wide as MeasureText("-100", 16) + 10 comes out. */
#define GUTTER 38.0f
#define CAPTION 25.0f

static Rectangle rect(float x, float y, float w, float h) {
    Rectangle r = { x, y, w, h };
    return r;
}

static void test_the_plot_sits_inside_its_chart(void) {
    Rectangle outer = rect(100.0f, 50.0f, 800.0f, 300.0f);
    Rectangle plot = sdrgui_chart_area(outer, GUTTER, CAPTION);

    check_close("the gutter comes off the left", plot.x, 138.0, 0.01);
    check_close("the caption off the top", plot.y, 75.0, 0.01);
    check_close("the width loses the gutter", plot.width, 762.0, 0.01);
    check_close("the height loses the caption and the axis strip",
                plot.height, 267.0, 0.01);
    /* Whatever else, the plot must stay inside the rectangle the chart was
       handed: a chart that draws outside its own rect lands on the panel
       beside it, which is how the message log came to run across the
       scatter. */
    check_msg(plot.x >= outer.x && plot.y >= outer.y &&
                  plot.x + plot.width <= outer.x + outer.width + 0.01f &&
                  plot.y + plot.height <= outer.y + outer.height + 0.01f,
              "the plot escapes its chart: %.1f,%.1f %.1fx%.1f in "
              "%.1f,%.1f %.1fx%.1f\n",
              plot.x, plot.y, plot.width, plot.height, outer.x, outer.y,
              outer.width, outer.height);
}

/* A rectangle too small for its own furniture must still produce something
   drawable rather than a negative size. */
static void test_a_tiny_chart(void) {
    Rectangle plot = sdrgui_chart_area(rect(0.0f, 0.0f, 10.0f, 10.0f), GUTTER,
                                       CAPTION);

    check_msg(plot.width >= 1.0f, "width came out %.2f\n", plot.width);
    check_msg(plot.height >= 1.0f, "height came out %.2f\n", plot.height);
}

/*
 * The hit test against the drawing: for every bar, the point in the middle of
 * where that bar is drawn must map back to that bar. This is the round trip
 * the scan chart got wrong, and it is the only check that could have caught it
 * without a person and a screenshot.
 */
static void test_the_pointer_finds_the_bar_it_is_over(void) {
    Rectangle outer = rect(100.0f, 50.0f, 800.0f, 300.0f);
    Rectangle plot = sdrgui_chart_area(outer, GUTTER, CAPTION);
    const int count = 124;
    float middle_y = plot.y + plot.height / 2.0f;
    int wrong = 0;
    int worst = -1;
    int worst_got = -1;

    for (int index = 0; index < count; index++) {
        float left = sdrgui_bar_left(plot, count, index);
        float x = left + sdrgui_bar_width(plot, count) / 2.0f;
        int got = sdrgui_bar_index_at(plot, count, x, middle_y);

        if (got != index) {
            wrong++;
            worst = index;
            worst_got = got;
        }
    }
    check_msg(wrong == 0,
              "%d of %d bars hit-test to a different bar (e.g. bar %d "
              "reports %d)\n",
              wrong, count, worst, worst_got);
}

/*
 * And the failure the round trip above is a proxy for: hit-testing against the
 * chart's *outer* rectangle instead of its plot. That is what the code used to
 * do, and on this geometry it is off by two channels -- small enough to look
 * like a clumsy click rather than a bug.
 */
static void test_the_gutter_is_the_bug(void) {
    Rectangle outer = rect(100.0f, 50.0f, 800.0f, 300.0f);
    Rectangle plot = sdrgui_chart_area(outer, GUTTER, CAPTION);
    const int count = 124;
    float middle_y = plot.y + plot.height / 2.0f;
    int index = 60;
    float x = sdrgui_bar_left(plot, count, index) +
              sdrgui_bar_width(plot, count) / 2.0f;
    int from_plot = sdrgui_bar_index_at(plot, count, x, middle_y);
    int from_outer = sdrgui_bar_index_at(outer, count, x, middle_y);

    check_int("against the plot, the right bar", from_plot, index);
    check_msg(from_outer != index,
              "the outer rectangle happens to agree here, so this check no "
              "longer demonstrates anything\n");
    check_msg(from_outer - index >= 1 && from_outer - index <= 4,
              "using the outer rectangle is off by %d bars, which is not the "
              "one or two that was reported\n",
              from_outer - index);
}

/* The edges, where an off-by-one lands on nothing or on the wrong end. */
static void test_the_edges(void) {
    Rectangle plot = sdrgui_chart_area(rect(100.0f, 50.0f, 800.0f, 300.0f),
                                       GUTTER, CAPTION);
    const int count = 124;
    float middle_y = plot.y + plot.height / 2.0f;

    check_int("the very left edge is the first bar",
              sdrgui_bar_index_at(plot, count, plot.x, middle_y), 0);
    check_int("the very right edge is the last",
              sdrgui_bar_index_at(plot, count, plot.x + plot.width, middle_y),
              count - 1);
    check_int("a pixel left of the plot is nowhere",
              sdrgui_bar_index_at(plot, count, plot.x - 1.0f, middle_y), -1);
    check_int("a pixel right of it is nowhere",
              sdrgui_bar_index_at(plot, count, plot.x + plot.width + 1.0f,
                                  middle_y),
              -1);
    /* The gutter is part of the chart but not part of the plot: a click on a
       value label selects nothing rather than the first channel. */
    check_int("a click in the label gutter selects nothing",
              sdrgui_bar_index_at(plot, count, plot.x - GUTTER / 2.0f,
                                  middle_y),
              -1);
    check_int("above the plot is nowhere",
              sdrgui_bar_index_at(plot, count, plot.x + 10.0f, plot.y - 1.0f),
              -1);
    check_int("below it too",
              sdrgui_bar_index_at(plot, count, plot.x + 10.0f,
                                  plot.y + plot.height + 1.0f),
              -1);
}

/* Degenerate counts must not divide by zero or index off the end. */
static void test_no_bars(void) {
    Rectangle plot = sdrgui_chart_area(rect(0.0f, 0.0f, 400.0f, 200.0f), 0.0f,
                                       0.0f);

    check_int("no bars, no index", sdrgui_bar_index_at(plot, 0, 10.0f, 10.0f),
              -1);
    check_int("a negative count either",
              sdrgui_bar_index_at(plot, -5, 10.0f, 10.0f), -1);
    check_close("and no width", sdrgui_bar_width(plot, 0), 0.0, 1e-9);
    check_int("one bar covers the plot",
              sdrgui_bar_index_at(plot, 1, plot.x + plot.width / 2.0f, 10.0f),
              0);
}

/* Bars narrower than a pixel: a 124-channel chart in a narrow window, which is
   where a hit test tends to fall apart. */
static void test_a_narrow_chart(void) {
    Rectangle plot = sdrgui_chart_area(rect(0.0f, 0.0f, 200.0f, 120.0f),
                                       GUTTER, CAPTION);
    const int count = 124;
    float middle_y = plot.y + plot.height / 2.0f;
    int wrong = 0;

    check_msg(sdrgui_bar_width(plot, count) < 2.0f,
              "this check is meant to exercise sub-pixel bars, and they are "
              "%.2f px wide\n",
              sdrgui_bar_width(plot, count));
    for (int index = 0; index < count; index++) {
        float x = sdrgui_bar_left(plot, count, index) +
                  sdrgui_bar_width(plot, count) / 2.0f;
        if (sdrgui_bar_index_at(plot, count, x, middle_y) != index)
            wrong++;
    }
    check_int("every sub-pixel bar still finds itself", wrong, 0);
}

int main(void) {
    test_the_plot_sits_inside_its_chart();
    test_a_tiny_chart();
    test_the_pointer_finds_the_bar_it_is_over();
    test_the_gutter_is_the_bug();
    test_the_edges();
    test_no_bars();
    test_a_narrow_chart();

    return check_report("chart geometry");
}
