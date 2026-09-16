#include "sdrgui_geometry.h"
#include "sdrgui.h"
#include "survey_suspect.h"
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

/*
 * What a waterfall strip covers.
 *
 * The bug this is here for: the zoom was gated on the calibration overlay's
 * own flag, so the Scope's frequency window computed a range, handed it over,
 * and had it discarded. The drag worked, the axis never moved, and a
 * screenshot was the only thing that could tell the difference -- there was no
 * decision with a name to ask.
 */
static void test_waterfall_span(void) {
    const double centre = 948.4e6, rate = 2.0e6;
    struct sdrgui_waterfall_span s;

    /* No zoom asked for: the whole received span, the whole texture. */
    s = sdrgui_waterfall_span(centre, rate, 0.0, 0.0, 1024.0f);
    check_close("unzoomed lower", s.lower_hz, 947.4e6, 1.0);
    check_close("unzoomed upper", s.upper_hz, 949.4e6, 1.0);
    check_close("and all of the texture", (double)s.source_width, 1024.0, 0.01);
    check_close("from its start", (double)s.source_x, 0.0, 0.01);

    /* A zoom is honoured on its own merits -- no screen has to be named. This
       is the case that silently did nothing. */
    s = sdrgui_waterfall_span(centre, rate, 948.45e6, 150e3, 1024.0f);
    check_close("zoomed lower", s.lower_hz, 948.3e6, 1.0);
    check_close("zoomed upper", s.upper_hz, 948.6e6, 1.0);
    check_close("a 300 kHz slice of a 2 MHz texture",
                (double)s.source_width, 1024.0 * 0.15, 0.5);
    check_close("starting 0.9 MHz in",
                (double)s.source_x, 1024.0 * 0.45, 0.5);

    /* The texture slice and the labelled range describe the same band: the
       one property that stops a zoom drawing a band it did not measure. */
    {
        double left = 947.4e6 + (double)s.source_x / 1024.0 * rate;
        double right = 947.4e6 + (double)(s.source_x + s.source_width) /
                                 1024.0 * rate;
        check_close("the pixels drawn start where the axis says",
                    left, s.lower_hz, 2000.0);
        check_close("and end where it says", right, s.upper_hz, 2000.0);
    }

    /* A request past the edge is clamped, not refused: panning off the end is
       how a retune is triggered, and it must keep drawing meanwhile. */
    s = sdrgui_waterfall_span(centre, rate, 947.45e6, 200e3, 1024.0f);
    check_close("clamped to what was received", s.lower_hz, 947.4e6, 1.0);
    check_close("keeping the part that overlaps", s.upper_hz, 947.65e6, 1.0);
    check_true("and still narrower than the whole", s.upper_hz < 949.4e6);
    check_close("from the texture's start", (double)s.source_x, 0.0, 0.01);

    /* A window entirely off the received span has no overlap to show. It
       falls back to the whole rather than drawing an empty strip, because the
       retune that will fix it takes a frame or two to arrive. */
    s = sdrgui_waterfall_span(centre, rate, 947.0e6, 200e3, 1024.0f);
    check_close("no overlap falls back to the whole span",
                s.upper_hz - s.lower_hz, rate, 1.0);

    /* Degenerate requests fall back rather than dividing by zero. */
    s = sdrgui_waterfall_span(centre, rate, 948.4e6, 0.4, 1024.0f);
    check_close("a sub-hertz zoom is no zoom", s.upper_hz - s.lower_hz,
                rate, 1.0);
    s = sdrgui_waterfall_span(centre, 0.0, 948.4e6, 150e3, 1024.0f);
    check_close("nor is one on a stopped receiver",
                s.upper_hz - s.lower_hz, 0.0, 1.0);
}

/*
 * The band a reader is dragging out. Three charts draw it, and for a while
 * only two did -- the waterfall zoomed on release with nothing shown on the
 * way, so the reader found out what they had selected afterwards.
 */
static void test_drag_band(void) {
    Rectangle plot = { 100.0f, 0.0f, 800.0f, 400.0f };
    struct sdrgui_drag_band b;

    /* A drag across the middle half. */
    b = sdrgui_drag_band_at(plot, 947.4e6, 949.4e6, 947.9e6, 948.9e6);
    check_true("the band is drawn", b.visible);
    check_close("its left edge", (double)b.x0, 300.0, 0.5);
    check_close("its right edge", (double)b.x1, 700.0, 0.5);

    /* Dragged the other way, it is the same band: a reader who selects right
       to left means what a reader who selects left to right means. */
    {
        struct sdrgui_drag_band r =
            sdrgui_drag_band_at(plot, 947.4e6, 949.4e6, 948.9e6, 947.9e6);
        check_close("backwards is the same left edge", (double)r.x0,
                    (double)b.x0, 0.01);
        check_close("and the same right edge", (double)r.x1, (double)b.x1,
                    0.01);
    }

    /* Clamped to the plot rather than drawn outside it. */
    b = sdrgui_drag_band_at(plot, 947.4e6, 949.4e6, 900e6, 1000e6);
    check_true("an oversized drag still shows", b.visible);
    check_close("clamped to the left edge", (double)b.x0, 100.0, 0.01);
    check_close("and the right", (double)b.x1, 900.0, 0.01);

    /* Degenerate cases draw nothing rather than a zero-width sliver or a
       division by zero. */
    b = sdrgui_drag_band_at(plot, 947.4e6, 949.4e6, 948e6, 948e6);
    check_true("a drag that never moved is not a band", !b.visible);
    b = sdrgui_drag_band_at(plot, 948e6, 948e6, 947.9e6, 948.1e6);
    check_true("nor is any drag on a chart with no span", !b.visible);
}

/*
 * Which mark a candidate gets above the survey's trace.
 *
 * The tick was one filled dot for everything, so a spur, an empty frequency
 * and a broadcast station drew identically on the one screen where telling
 * them apart matters most -- the candidate list said so in a column nobody
 * reading the chart was looking at.
 *
 * The precedence is the decision worth pinning: **empty wins over
 * receiver-like**, because "there is nothing here" is what a reader acts on,
 * and because the candidate list resolves it the same way. If the two
 * disagreed, the chart and the list would show different things about the
 * same peak.
 */
static void test_peak_marks(void) {
    check_int("nothing known against it draws a filled dot",
              sdrgui_survey_peak_mark(0u), SDRGUI_PEAK_SIGNAL);
    check_int("the receiver's comb draws a cross",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_RECEIVER),
              SDRGUI_PEAK_RECEIVER);
    check_int("so does a step centre, where its DC offset lands",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_STEP),
              SDRGUI_PEAK_RECEIVER);
    check_int("a closer look finding nothing draws a hollow dot",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_EMPTY),
              SDRGUI_PEAK_EMPTY);
    check_int("and empty wins when a frequency is both",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_EMPTY |
                                      SDRGUI_PEAK_FLAG_RECEIVER),
              SDRGUI_PEAK_EMPTY);
    check_int("however many other flags are set",
              sdrgui_survey_peak_mark(0xffffffffu), SDRGUI_PEAK_EMPTY);
    /*
     * The bits are the survey's own, duplicated in sdrgui.h because a
     * component may not include the survey's headers (ADR-0007). Nothing but
     * a check can hold the two definitions together, and a chart drawing the
     * wrong mark for the right flag is exactly the kind of fault that looks
     * like a rendering bug for an afternoon.
     */
    check_int("the receiver bit is the survey's",
              (int)SDRGUI_PEAK_FLAG_RECEIVER, (int)SURVEY_SUSPECT_REFERENCE);
    check_int("the step-centre bit is the survey's",
              (int)SDRGUI_PEAK_FLAG_STEP, (int)SURVEY_SUSPECT_STEP_CENTRE);
    check_int("the empty bit is the survey's",
              (int)SDRGUI_PEAK_FLAG_EMPTY, (int)SURVEY_SUSPECT_NO_CARRIER);
    check_int("and so is the displaced bit",
              (int)SDRGUI_PEAK_FLAG_DISPLACED, (int)SURVEY_SUSPECT_DISPLACED);

    /*
     * The fourth mark, and the two resolutions it exists to refuse.
     *
     * On the comb *and* reading displaced: a plain cross would tell a reader
     * to stop looking at the one candidate they should look at, and a plain
     * dot would silently discard the comb mark, which
     * `.scratch/reading-origin/issues/01-*` decided against. One mark per
     * peak means the shape has to carry both.
     */
    check_int("on the comb and displaced draws the contested mark",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_RECEIVER |
                                      SDRGUI_PEAK_FLAG_DISPLACED),
              SDRGUI_PEAK_CONTESTED);
    check_int("a step centre that reads displaced too",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_STEP |
                                      SDRGUI_PEAK_FLAG_DISPLACED),
              SDRGUI_PEAK_CONTESTED);
    /* Displaced on its own is not contested: nothing is contradicting it, it
       is simply a signal. */
    check_int("displaced alone is an ordinary signal",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_DISPLACED),
              SDRGUI_PEAK_SIGNAL);
    /* And empty still beats everything, including the contradiction: a
       frequency the pass found nothing at is empty whatever its reading
       implied. */
    check_int("empty still wins over contested",
              sdrgui_survey_peak_mark(SDRGUI_PEAK_FLAG_EMPTY |
                                      SDRGUI_PEAK_FLAG_RECEIVER |
                                      SDRGUI_PEAK_FLAG_DISPLACED),
              SDRGUI_PEAK_EMPTY);
}

/*
 * The calibration dot's hover.
 *
 * A hit test the widget used to make for itself, and the reason it is here is
 * that it decides something -- whether the hover panel showing what the
 * receiver is corrected by is drawn -- and a function that draws may not also
 * decide (ADR-0012).
 */
static void test_the_pointer_is_on_the_dot(void) {
    Vector2 centre = { 100.0f, 50.0f };
    const float r = SDRGUI_HEALTH_DOT_RADIUS;

    check_int("dead centre", sdrgui_point_in_circle(centre, r, 100.0f, 50.0f),
              1);
    check_int("just inside on the right",
              sdrgui_point_in_circle(centre, r, 100.0f + r - 0.5f, 50.0f), 1);
    check_int("on the edge counts",
              sdrgui_point_in_circle(centre, r, 100.0f + r, 50.0f), 1);
    check_int("just outside does not",
              sdrgui_point_in_circle(centre, r, 100.0f + r + 0.5f, 50.0f), 0);
    check_int("and above", sdrgui_point_in_circle(centre, r, 100.0f,
                                                  50.0f - r - 0.5f), 0);
    /*
     * The corner of the bounding box is outside the circle, which is the
     * whole reason this is not a rectangle test: r/sqrt(2) is about 0.707 r,
     * so a point at (r, r) from the centre is 1.41 r away and must miss.
     */
    check_int("the corner of the box is not on the dot",
              sdrgui_point_in_circle(centre, r, 100.0f + r, 50.0f + r), 0);
    /* A dot with no radius is not hoverable rather than hoverable
       everywhere, which is what an unguarded distance test would give. */
    check_int("a zero radius is nothing",
              sdrgui_point_in_circle(centre, 0.0f, 100.0f, 50.0f), 0);
}

/*
 * Message-log columns, and the overlap they were reported for.
 *
 * The SRD decode log drew "UNDECODED" in an 84 px KIND column and "2FSK" in
 * the MOD column beside it, and the two came out on top of each other --
 * "UNDECOD2BSK" on screen. Neither column left its panel, so check-layout saw
 * nothing; the values were drawn with a bare DrawText while only the headings
 * were clipped.
 *
 * The property that has to hold is one line: **no column starts before the
 * one before it has finished its text**. Everything below is that, at the
 * widths the real callers use and at the extremes.
 */
static void check_columns_do_not_overlap(const char *what,
                                         struct sdrgui_log_columns c) {
    char label[128];

    snprintf(label, sizeof(label), "%s: the id starts after the time's text", what);
    check_true(label, c.id_x >= c.time_x + c.time_width);
    snprintf(label, sizeof(label), "%s: the label starts after the id's text", what);
    check_true(label, c.label_x >= c.id_x + c.id_width);
    if (c.show_freq) {
        snprintf(label, sizeof(label), "%s: the frequency starts after the time's text", what);
        check_true(label, c.freq_x >= c.time_x + c.time_width);
        snprintf(label, sizeof(label), "%s: the id starts after the frequency's text", what);
        check_true(label, c.id_x >= c.freq_x + c.freq_width);
    }
    if (c.show_type) {
        snprintf(label, sizeof(label), "%s: the type starts after the label's text", what);
        check_true(label, c.type_x >= c.label_x + c.label_width);
        snprintf(label, sizeof(label), "%s: the detail starts after the type's text", what);
        check_true(label, c.detail_x >= c.type_x + c.type_width);
    } else {
        snprintf(label, sizeof(label), "%s: the detail starts after the label's text", what);
        check_true(label, c.detail_x >= c.label_x + c.label_width);
    }
    if (c.show_raw) {
        snprintf(label, sizeof(label), "%s: the raw starts after the detail's text", what);
        check_true(label, c.raw_x >= c.detail_x + c.detail_width);
    }
    snprintf(label, sizeof(label), "%s: every column has room for something", what);
    check_true(label, c.time_width > 0 && c.id_width > 0 &&
                      c.label_width > 0 && c.detail_width > 0);
}

static void test_message_log_columns(void) {
    /*
     * The reported case. "UNDECODED" measures about 95 px at 18 point in this
     * font; the exact number is the drawing's to find, so the check works in
     * the currency this function is handed -- a text width -- and 95 is what
     * it was measured at.
     */
    struct sdrgui_log_columns wide =
        sdrgui_message_log_columns(12, 1900, (struct sdrgui_log_widths){ 95, 40, 0, 0 });

    check_columns_do_not_overlap("a full-width SRD log", wide);
    check_true("the id column grew past its minimum to hold UNDECODED",
               wide.id_width >= 95);
    check_true("and the raw column still fits", wide.show_raw);

    /*
     * The case that must not regress: ADS-B, whose identifier is six hex
     * characters and fits the minimum. Growing the column for a caller that
     * does not need it would move every other column for no reason.
     */
    struct sdrgui_log_columns adsb =
        sdrgui_message_log_columns(12, 1900, (struct sdrgui_log_widths){ 58, 40, 0, 0 });

    check_columns_do_not_overlap("an ADS-B log", adsb);
    check_int("a short id leaves the column at its minimum",
              adsb.id_x + SDRGUI_LOG_ID_PITCH_MIN, adsb.label_x);

    /*
     * A narrow panel. The raw column is given up first -- it already was --
     * and the decoded message takes the room, which is the column carrying
     * the reading.
     */
    struct sdrgui_log_columns narrow =
        sdrgui_message_log_columns(12, 700, (struct sdrgui_log_widths){ 95, 40, 0, 0 });

    check_columns_do_not_overlap("a narrow log", narrow);
    check_int("a narrow panel gives up the raw column", narrow.show_raw, 0);
    check_true("and the detail column ends inside the panel",
               narrow.detail_x + narrow.detail_width <= 700);

    /*
     * An absurd identifier in a narrow panel: the growth is refused rather
     * than taken out of the decoded message, which keeps its minimum. The
     * text is then clipped by the drawing, which is a legible answer where
     * overlap is not.
     */
    struct sdrgui_log_columns squeezed =
        sdrgui_message_log_columns(12, 620, (struct sdrgui_log_widths){ 600, 300, 0, 0 });

    check_columns_do_not_overlap("an absurd id in a narrow log", squeezed);
    check_true("the decoded message keeps its minimum",
               squeezed.detail_width >= SDRGUI_LOG_DETAIL_MIN - 1 ||
               squeezed.detail_x + SDRGUI_LOG_DETAIL_MIN > 620);

    /*
     * A panel too small for any of it. Nothing may come back zero or
     * negative, because a width of zero is a division waiting to happen and a
     * negative one is a rectangle drawn backwards.
     */
    struct sdrgui_log_columns tiny =
        sdrgui_message_log_columns(12, 120, (struct sdrgui_log_widths){ 95, 40, 0, 0 });

    check_columns_do_not_overlap("a log with no room at all", tiny);
    check_int("a hopeless panel gives up the raw column", tiny.show_raw, 0);

    /*
     * The optional type column. A caller with no type for its rows passes 0
     * and gets exactly the layout it had before the column existed, which is
     * what keeps the ADS-B log unmoved by an SRD feature -- asserted field by
     * field rather than trusted.
     */
    {
        struct sdrgui_log_columns without =
            sdrgui_message_log_columns(12, 1900, (struct sdrgui_log_widths){ 58, 40, 0, 0 });
        struct sdrgui_log_columns with =
            sdrgui_message_log_columns(12, 1900, (struct sdrgui_log_widths){ 58, 40, 110, 0 });

        check_int("no type asked for, no type column", without.show_type, 0);
        check_int("a type asked for gets one", with.show_type, 1);
        check_columns_do_not_overlap("a log with a type column", with);

        check_int("without a type the detail column is where it always was",
                  without.detail_x, without.label_x + SDRGUI_LOG_LABEL_PITCH_MIN);
        check_int("and the raw column too",
                  without.raw_x, without.detail_x + SDRGUI_LOG_DETAIL_PITCH);
        check_int("the type column pushes the detail right by its own width",
                  with.detail_x - without.detail_x,
                  with.type_x + with.type_width + SDRGUI_LOG_GUTTER - with.type_x);
        check_int("and moves nothing to its left",
                  with.label_x, without.label_x);

        check_true("a wide type gets the room it asks for",
                   with.type_width >= 110);
    }

    /*
     * A type column in a narrow panel is squeezed like the others rather than
     * pushing the decoded message out of the panel.
     */
    {
        struct sdrgui_log_columns tight =
            sdrgui_message_log_columns(12, 640, (struct sdrgui_log_widths){ 95, 40, 300, 0 });

        check_columns_do_not_overlap("a type column with no room", tight);
        check_true("the decoded message survives a greedy type column",
                   tight.detail_width >= 1 &&
                   tight.detail_x + tight.detail_width <= 640);
    }

    /*
     * The frequency column, which the SRD log carries and ADS-B does not.
     * Same rule as the type column: absent costs nothing, present pushes only
     * what is to its right.
     */
    {
        struct sdrgui_log_widths bare = { 58, 40, 0, 0 };
        struct sdrgui_log_widths tuned = { 58, 40, 0, 90 };
        struct sdrgui_log_columns without =
            sdrgui_message_log_columns(12, 1900, bare);
        struct sdrgui_log_columns with =
            sdrgui_message_log_columns(12, 1900, tuned);

        check_int("no frequency asked for, no frequency column",
                  without.show_freq, 0);
        check_int("a frequency asked for gets one", with.show_freq, 1);
        check_columns_do_not_overlap("a log with a frequency column", with);

        check_int("without a frequency the id column is where it always was",
                  without.id_x, without.time_x + SDRGUI_LOG_TIME_PITCH);
        check_true("the frequency column pushes the id right",
                   with.id_x > without.id_x);
        check_int("and moves nothing to its left", with.time_x, without.time_x);

        /* Both optional columns at once, which is what the SRD log asks for. */
        {
            struct sdrgui_log_widths both = { 95, 40, 110, 90 };
            struct sdrgui_log_columns all =
                sdrgui_message_log_columns(12, 1900, both);

            check_columns_do_not_overlap("a log with every column", all);
            check_true("both optional columns are present",
                       all.show_freq && all.show_type);
            check_true("and the decoded message still has room",
                       all.detail_width >= SDRGUI_LOG_DETAIL_MIN);
        }

        /* And in a panel with no room, neither may push the message out. */
        {
            struct sdrgui_log_widths greedy = { 300, 200, 300, 300 };
            struct sdrgui_log_columns squeezed =
                sdrgui_message_log_columns(12, 700, greedy);

            check_columns_do_not_overlap("every column, no room", squeezed);
            check_true("the decoded message survives every greedy column",
                       squeezed.detail_width >= 1 &&
                       squeezed.detail_x + squeezed.detail_width <= 700);
        }
    }

    /*
     * Growth is monotone in what it is asked for: a wider identifier never
     * moves a column left. The columns are added left to right, so a caller
     * cannot make the log narrower by having more to say.
     */
    {
        int previous_label_x = 0;
        int w;

        for (w = 20; w <= 400; w += 20) {
            struct sdrgui_log_columns c =
                sdrgui_message_log_columns(12, 1900, (struct sdrgui_log_widths){ w, 40, 0, 0 });
            check_true("a wider id never moves the label column left",
                       c.label_x >= previous_label_x);
            previous_label_x = c.label_x;
        }
    }
}


/*
 * Which row of a message log the pointer is over.
 *
 * It lives here because clicking a row in the SRD log both selected it and
 * retuned the receiver, from inside `draw_log()` -- a function taking
 * `const struct app *` and casting the const away to act. Moving the decision
 * to the input phase means finding the row without drawing it, and a hit test
 * that disagrees with the drawing selects a row the reader did not click,
 * which is what `test_the_gutter_is_the_bug` above is about for bars.
 */
static void test_which_log_row_the_pointer_is_over(void) {
    Rectangle outer = { 0.0f, 0.0f, 900.0f, 400.0f };
    struct sdrgui_log_rows band = sdrgui_message_log_rows(outer);
    Vector2 at;
    int row;

    /*
     * Anchored on the numbers the drawing used before this band was lifted
     * out of it, not on the band's own fields.
     *
     * The first version of this check was phrased entirely against
     * `band.first_y` -- so dropping the heading block moved the expectations
     * with the answer and the check stayed green through the mutation. That
     * is the round trip CLAUDE.md warns about, in the small: a convention both
     * sides share cannot be checked by comparing the two sides.
     *
     * These four constants were literals inside `sdrgui_message_log()`. If
     * the extraction changed any of them, every message log in the program
     * moved, and this is what says so.
     */
    check_close("the caption strip is the drawing's 25 px",
                (double)SDRGUI_LOG_CAPTION_STRIP, 25.0, 1e-6);
    check_int("the padding is its 12", SDRGUI_LOG_PAD, 12);
    check_int("the heading block its 22", SDRGUI_LOG_HEADING_HEIGHT, 22);
    check_int("and a row its 24", SDRGUI_LOG_ROW_HEIGHT, 24);

    check_int("row 0 starts one heading block below the first text line",
              band.first_y, (int)band.plot.y + 12 + 22);
    check_int("the row band starts at the TIME column",
              band.left, (int)band.plot.x + 12);
    check_int("rows are the drawing's pitch", band.row_height,
              SDRGUI_LOG_ROW_HEIGHT);

    /*
     * And the capacity against the arithmetic rather than against itself:
     * 400 px less the caption strip, the frame's own padding top and bottom,
     * and the headings, over 24.
     */
    check_int("the capacity is what fits below the headings",
              band.capacity,
              ((int)(band.plot.y + band.plot.height) - 12 -
               ((int)band.plot.y + 12 + 22)) / 24);
    check_true("which on a 400 px log is a useful number of rows",
               band.capacity > 5);

    /* The middle of row 0, and of row 3. */
    at.x = (float)band.left + 20.0f;
    at.y = (float)band.first_y + (float)band.row_height / 2.0f;
    check_int("the first row", sdrgui_message_log_row_at(outer, 10, at), 0);

    at.y = (float)band.first_y + 3.0f * (float)band.row_height + 4.0f;
    check_int("the fourth", sdrgui_message_log_row_at(outer, 10, at), 3);

    /* Above the first row is the heading rule, not a row. */
    at.y = (float)band.first_y - 8.0f;
    check_int("the headings are not a row",
              sdrgui_message_log_row_at(outer, 10, at), -1);

    /* Left and right of the row band. */
    at.y = (float)band.first_y + 4.0f;
    at.x = (float)band.left - 40.0f;
    check_int("left of the band", sdrgui_message_log_row_at(outer, 10, at), -1);
    at.x = (float)(band.left + band.width) + 40.0f;
    check_int("right of it", sdrgui_message_log_row_at(outer, 10, at), -1);

    /*
     * Past the last row the log *holds*, which is the case an empty log is
     * always in -- and the reason `srd_log_row_intent()` has a no-row answer
     * rather than treating -1 as row 0.
     */
    at.x = (float)band.left + 20.0f;
    at.y = (float)band.first_y + 4.0f * (float)band.row_height;
    check_int("past the last row the log holds",
              sdrgui_message_log_row_at(outer, 3, at), -1);
    check_int("and an empty log has no rows at all",
              sdrgui_message_log_row_at(outer, 0, at), -1);

    /*
     * Past the last row that *fits*. A log longer than its panel draws only
     * what fits, and a hit test answering for an undrawn row would select
     * something the reader cannot see -- `panel_rows.h`'s rule, one level in.
     */
    at.y = (float)band.first_y +
           (float)(band.capacity + 2) * (float)band.row_height;
    check_int("and past the last row that fits",
              sdrgui_message_log_row_at(outer, 500, at), -1);

    /* Every row that fits is reachable, and none maps to two. */
    {
        int seen = 0;
        int i;

        for (i = 0; i < band.capacity; i++) {
            at.y = (float)band.first_y + ((float)i + 0.5f) *
                                             (float)band.row_height;
            row = sdrgui_message_log_row_at(outer, 500, at);
            if (row == i)
                seen++;
        }
        check_int("every row that fits is reachable at its own centre",
                  seen, band.capacity);
    }

    /* A panel too short for any row answers for none rather than for row 0. */
    {
        Rectangle tiny = { 0.0f, 0.0f, 900.0f, 40.0f };

        at.x = 20.0f;
        at.y = 30.0f;
        check_int("a panel with no room for a row",
                  sdrgui_message_log_row_at(tiny, 10, at), -1);
    }
}


/* ------------------------------------------------------------------ */
/* Waterfall markers: where each one lands, and which one a click hits. */

/*
 * These used to be computed inside `sdrgui_waterfall()`'s draw loop, which
 * also wrote which marker had been clicked -- so nothing without a window
 * could ask where a marker was, and the inverse of the mapping did not exist
 * at all. `sdrgui.h` already records what that costs, for the survey chart:
 *
 *   a marker drawn at a frequency the hit test maps somewhere else is the
 *   same bug the bar charts had, one or two positions out and only visible
 *   by clicking.
 */

#define MARK_PLOT rect(100.0f, 50.0f, 800.0f, 400.0f)

static struct sdrgui_marker_axes mark_axes(void) {
    struct sdrgui_marker_axes a;

    a.plot = MARK_PLOT;
    a.lower_hz = 433000000.0;
    a.upper_hz = 435000000.0;   /* 2 MHz across 800 px: 2.5 kHz a pixel */
    a.visible_seconds = 20.0;
    return a;
}

static struct sdrgui_waterfall_marker a_marker(double hz, double age,
                                               const char *label) {
    struct sdrgui_waterfall_marker m;

    m.frequency_hz = hz;
    m.bandwidth_hz = 25000.0;
    m.age_seconds = age;
    m.duration_seconds = 0.025;
    m.label = label;
    m.highlighted = 0;
    m.id = 0;
    m.color = (Color){ 0, 0, 0, 0 };
    return m;
}

/* The centre of the span sits at the centre of the plot, and "now" at the
   top: the two axes, each at a point that cannot come out right by accident
   if either is inverted. */
static void test_a_marker_lands_where_its_frequency_and_age_say(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[2];
    struct sdrgui_marker_layout out[2];
    int widths[2] = { 0, 0 };
    int n;

    m[0] = a_marker(434000000.0, 0.0, NULL);     /* mid-span, now */
    m[1] = a_marker(433500000.0, 10.0, NULL);    /* quarter across, half down */
    m[1].id = 1;

    n = sdrgui_waterfall_marker_layout(m, 2, widths, axes, out, 2);
    check_int("both are on screen", n, 2);
    check_close("mid-span is mid-plot", out[0].centre_x, 500.0f, 0.01f);
    check_close("a quarter across is a quarter along", out[1].centre_x, 300.0f,
                0.01f);
    /* Age 0 is the top row, and the dot is clamped 3 px inside the plot. */
    check_close("now is at the top", out[0].dot_y, 53.0f, 0.01f);
    check_close("ten of twenty seconds is halfway down", out[1].dot_y, 250.0f,
                0.01f);
}

/* Off either end of the span, or older than the history, is not drawn at all
   -- so the count is what is on screen and an index into it is not an index
   into the caller's array. That is why the layout carries `index`. */
static void test_what_is_off_screen_is_dropped(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[4];
    struct sdrgui_marker_layout out[4];
    int widths[4] = { 0, 0, 0, 0 };
    int n;

    m[0] = a_marker(432000000.0, 1.0, NULL);     /* below the span */
    m[1] = a_marker(436000000.0, 1.0, NULL);     /* above it */
    m[2] = a_marker(434000000.0, 25.0, NULL);    /* older than the history */
    m[3] = a_marker(434000000.0, 1.0, NULL);     /* the only one on screen */
    m[3].id = 7;

    n = sdrgui_waterfall_marker_layout(m, 4, widths, axes, out, 4);
    check_int("one of four is drawn", n, 1);
    check_int("and it is the one that fits", out[0].id, 7);
    check_int("which remembers where it came from", out[0].index, 3);
}

/*
 * The brackets are the burst's bandwidth and duration, with a floor on each
 * so a narrow or brief one is still something to point at.
 */
static void test_the_brackets_are_the_burst(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[2];
    struct sdrgui_marker_layout out[2];
    int widths[2] = { 0, 0 };

    /* 500 kHz of 2 MHz across 800 px is 200 px. */
    m[0] = a_marker(434000000.0, 5.0, NULL);
    m[0].bandwidth_hz = 500000.0;
    m[0].duration_seconds = 5.0;      /* a quarter of 20 s is 100 px */
    /* And one far under both floors. */
    m[1] = a_marker(434000000.0, 5.0, NULL);
    m[1].bandwidth_hz = 100.0;
    m[1].duration_seconds = 0.000001;

    sdrgui_waterfall_marker_layout(m, 2, widths, axes, out, 2);
    check_close("bandwidth becomes width", out[0].bracket.width, 200.0f, 0.01f);
    check_close("duration becomes height", out[0].bracket.height, 100.0f, 0.01f);
    check_close("a narrow burst still gets 20 px", out[1].bracket.width, 20.0f,
                0.01f);
    check_close("and a brief one 8", out[1].bracket.height, 8.0f, 0.01f);
}

/* Nothing may be drawn outside the plot: a burst at the very top or bottom of
   the history has its brackets pushed back inside. */
static void test_the_brackets_stay_inside_the_plot(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[2];
    struct sdrgui_marker_layout out[2];
    int widths[2] = { 0, 0 };

    m[0] = a_marker(434000000.0, 0.0, NULL);       /* now: half the bracket
                                                      would be above the plot */
    m[1] = a_marker(434000000.0, 20.0, NULL);      /* the oldest row */

    sdrgui_waterfall_marker_layout(m, 2, widths, axes, out, 2);
    check_true("the newest is not above the plot",
               out[0].bracket.y >= MARK_PLOT.y + 2.0f - 0.001f);
    check_true("the oldest is not below it",
               out[1].bracket.y + out[1].bracket.height <=
                   MARK_PLOT.y + MARK_PLOT.height - 2.0f + 0.001f);
    check_true("and neither dot leaves it",
               out[0].dot_y >= MARK_PLOT.y &&
                   out[1].dot_y <= MARK_PLOT.y + MARK_PLOT.height);
}

/*
 * **The forward and the inverse agree.** Every marker laid out is found by a
 * point at its own centre -- which is the property that was unavailable while
 * this lived inside the drawing, and the one the survey chart has had all
 * along.
 */
static void test_every_marker_is_found_at_its_own_centre(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[24];
    struct sdrgui_marker_layout out[24];
    int widths[24];
    int i, n, wrong = 0;

    /* A populated waterfall: spread across the span and the history, so the
       pills collide and are moved. */
    for (i = 0; i < 24; i++) {
        m[i] = a_marker(433100000.0 + i * 75000.0, 0.5 + i * 0.75, "seq 1234");
        m[i].id = 100 + i;
        widths[i] = 52;
    }
    n = sdrgui_waterfall_marker_layout(m, 24, widths, axes, out, 24);
    check_int("all of them are on screen", n, 24);

    for (i = 0; i < n; i++) {
        float cx = out[i].bracket.x + out[i].bracket.width / 2.0f;
        float cy = out[i].bracket.y + out[i].bracket.height / 2.0f;

        if (sdrgui_waterfall_marker_at(out, n, cx, cy) != out[i].id)
            wrong++;
    }
    check_msg(wrong == 0, "every marker is found at its own centre (%d of %d "
              "were not)", wrong, n);

    /* And a point well outside every bracket finds nothing. */
    check_int("empty space selects nothing",
              sdrgui_waterfall_marker_at(out, n, MARK_PLOT.x + 1.0f,
                                         MARK_PLOT.y + MARK_PLOT.height - 1.0f),
              -1);
}

/*
 * **The topmost wins.** Two markers at the same place: the later one is drawn
 * over the earlier, so a click belongs to it. The loop this replaced got the
 * same answer by overwriting its output on every match -- correct by accident,
 * which is not something a check could state or a reader rely on.
 */
static void test_the_topmost_marker_wins(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[2];
    struct sdrgui_marker_layout out[2];
    int widths[2] = { 0, 0 };
    int n;

    m[0] = a_marker(434000000.0, 5.0, NULL);
    m[0].id = 11;
    m[1] = a_marker(434000000.0, 5.0, NULL);   /* exactly on top of it */
    m[1].id = 22;

    n = sdrgui_waterfall_marker_layout(m, 2, widths, axes, out, 2);
    check_int("both laid out", n, 2);
    check_int("the one drawn last takes the click",
              sdrgui_waterfall_marker_at(out, n, out[0].centre_x,
                                         out[0].bracket.y + 1.0f), 22);
}

/*
 * Pills are moved off each other, and the y is never touched -- a label must
 * stay at the moment its detection happened, or it is a label for a different
 * burst.
 */
static void test_pills_are_moved_apart_but_never_in_time(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[3];
    struct sdrgui_marker_layout out[3];
    int widths[3] = { 60, 60, 60 };
    int i, n;

    /* Three at nearly the same time and frequency: their pills must collide. */
    for (i = 0; i < 3; i++) {
        m[i] = a_marker(434000000.0 + i * 2000.0, 10.0, "FULL");
        m[i].id = i;
    }
    n = sdrgui_waterfall_marker_layout(m, 3, widths, axes, out, 3);
    check_int("three laid out", n, 3);

    for (i = 0; i < n; i++)
        check_close("the pill stays at its own time", out[i].pill.y,
                    out[i].pill_target.y, 0.001f);

    /* And no two overlap once resolved. */
    {
        int overlaps = 0;

        for (i = 0; i < n; i++) {
            int j;
            for (j = i + 1; j < n; j++)
                if (sdrgui_rects_overlap(out[i].pill, out[j].pill))
                    overlaps++;
        }
        check_int("no two pills overlap", overlaps, 0);
    }
}

/*
 * A pill pushed off the edge is pulled back: a label half outside the chart
 * is a label nobody can read.
 *
 * **This check was green for the wrong reason first.** A single marker hard
 * against the right edge with a wide label never reaches the clamp: the pill
 * is flipped to the *left* of the brackets before placement, and lands inside
 * the plot on its own. Removing the clamp entirely left the suite passing.
 * What reaches it is a *collision*, which throws the pill to `marker_cx + 8`
 * and past the edge -- so this needs two markers at the same moment, not one.
 */
static void test_a_pill_is_clamped_into_the_plot(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[2];
    struct sdrgui_marker_layout out[2];
    int widths[2] = { 120, 120 };
    int i, n;

    /* Both hard against the right edge, at the same time: the second one's
       pill collides with the first and is thrown outward. */
    m[0] = a_marker(434985000.0, 10.0, "seq 4C1D");
    m[0].id = 0;
    m[1] = a_marker(434995000.0, 10.0, "seq 4C1E");
    m[1].id = 1;

    n = sdrgui_waterfall_marker_layout(m, 2, widths, axes, out, 2);
    check_int("both laid out", n, 2);
    for (i = 0; i < n; i++) {
        check_true("the pill starts inside the plot",
                   out[i].pill.x >= MARK_PLOT.x + 2.0f - 0.001f);
        check_true("and ends inside it",
                   out[i].pill.x + out[i].pill.width <=
                       MARK_PLOT.x + MARK_PLOT.width - 2.0f + 0.001f);
    }
    /* And the one that had to move is genuinely somewhere else, so this is
       not passing because nothing was resolved. */
    check_true("the second pill was moved by the collision",
               out[1].pill.x != out[1].pill_target.x ||
                   out[1].pill.y != out[1].pill_target.y);
}

/*
 * An unlabelled marker takes no pill and no slot in the budget, and is hit by
 * its brackets alone. On this band most detections are unlabelled, so a pill
 * for each would spend the whole budget on markers saying nothing.
 */
static void test_an_unlabelled_marker_is_its_brackets(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[1];
    struct sdrgui_marker_layout out[1];
    int widths[1] = { 0 };

    m[0] = a_marker(434000000.0, 10.0, NULL);
    m[0].id = 5;
    sdrgui_waterfall_marker_layout(m, 1, widths, axes, out, 1);
    check_int("it is not labelled", out[0].labelled, 0);
    check_int("its brackets take the click",
              sdrgui_waterfall_marker_at(out, 1, out[0].centre_x,
                                         out[0].bracket.y + 1.0f), 5);
    /* Where its pill would have been, nothing is selected. */
    check_int("and the space beside it does not",
              sdrgui_waterfall_marker_at(out, 1,
                                         out[0].bracket.x +
                                             out[0].bracket.width + 8.0f,
                                         out[0].pill.y + 8.0f), -1);
}

/* Degenerate axes answer nothing rather than dividing by zero. */
static void test_axes_that_say_nothing(void) {
    struct sdrgui_marker_axes axes = mark_axes();
    struct sdrgui_waterfall_marker m[1];
    struct sdrgui_marker_layout out[1];
    int widths[1] = { 0 };

    m[0] = a_marker(434000000.0, 1.0, NULL);
    axes.visible_seconds = 0.0;
    check_int("no history, no markers",
              sdrgui_waterfall_marker_layout(m, 1, widths, axes, out, 1), 0);
    axes = mark_axes();
    axes.upper_hz = axes.lower_hz;
    check_int("no span, no markers",
              sdrgui_waterfall_marker_layout(m, 1, widths, axes, out, 1), 0);
    check_int("and nothing is over an empty layout",
              sdrgui_waterfall_marker_at(out, 0, 100.0f, 100.0f), -1);
}

/* How much history is on screen, which both the drawing and the hit test
   divide an age by. */
static void test_visible_seconds(void) {
    /* 400 rows of 131072 pairs at 2 MS/s: 0.065536 s a row. */
    check_close("height wins when there is one",
                (float)sdrgui_waterfall_visible_seconds(200, 400, 131072,
                                                        131072, 2000000.0),
                26.2144f, 0.001f);
    check_close("rows when there is not",
                (float)sdrgui_waterfall_visible_seconds(200, 0, 131072, 131072,
                                                        2000000.0),
                13.1072f, 0.001f);
    check_close("and the fallback when no block has arrived",
                (float)sdrgui_waterfall_visible_seconds(200, 0, 0, 131072,
                                                        2000000.0),
                13.1072f, 0.001f);
    check_close("a rate of zero is no history at all",
                (float)sdrgui_waterfall_visible_seconds(200, 400, 131072,
                                                        131072, 0.0),
                0.0f, 0.001f);
}

int main(void) {
    test_message_log_columns();
    test_the_plot_sits_inside_its_chart();
    test_a_tiny_chart();
    test_the_pointer_finds_the_bar_it_is_over();
    test_the_gutter_is_the_bug();
    test_the_edges();
    test_no_bars();
    test_a_narrow_chart();

    test_waterfall_span();
    test_drag_band();

    test_peak_marks();
    test_the_pointer_is_on_the_dot();
    test_which_log_row_the_pointer_is_over();

    test_a_marker_lands_where_its_frequency_and_age_say();
    test_what_is_off_screen_is_dropped();
    test_the_brackets_are_the_burst();
    test_the_brackets_stay_inside_the_plot();
    test_every_marker_is_found_at_its_own_centre();
    test_the_topmost_marker_wins();
    test_pills_are_moved_apart_but_never_in_time();
    test_a_pill_is_clamped_into_the_plot();
    test_an_unlabelled_marker_is_its_brackets();
    test_axes_that_say_nothing();
    test_visible_seconds();

    return check_report("chart geometry");
}
