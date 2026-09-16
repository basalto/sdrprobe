#ifndef SDRGUI_GEOMETRY_H
#define SDRGUI_GEOMETRY_H

#include <stddef.h>
#include <raylib.h>

/*
 * Where a chart's parts go inside the rectangle it was handed, and which part
 * a pointer is over.
 *
 * These decisions used to sit inside the drawing, and one of them shipped
 * wrong: the scan chart's bars are drawn inside a label gutter, the hit test
 * mapped the pointer across the whole rectangle instead, and clicking selected
 * a channel one or two to the right of the bar under the cursor.
 *
 * A header rather than a `.c` for one reason: raylib is needed here for the
 * `Rectangle` type and nothing else, so a check can compile this with raylib's
 * headers and link `-lm`, the way tests/layout_test.c already does. Anything
 * needing `MeasureText` -- which needs a font, which needs a window -- stays
 * with the drawing and passes its answer in as `gutter` (ADR-0012).
 */

/*
 * The plotting area inside a chart's outer rectangle: the caption strip comes
 * off the top, the value labels off the left, and 8 px off the bottom for the
 * axis captions. A caller cannot compute this, because the gutter depends on
 * how wide the labels turn out to be -- which is why the chart reserves it
 * rather than being told.
 */
static inline Rectangle sdrgui_chart_area(Rectangle outer, float gutter,
                                          float caption_h) {
    Rectangle plot = { outer.x + gutter, outer.y + caption_h,
                       outer.width - gutter, outer.height - caption_h - 8.0f };
    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
    return plot;
}

/*
 * The next sensible number at or above this one: 1, 2 or 5 times a power of
 * ten.
 *
 * For an axis whose maximum follows the data. A chart scaled to exactly
 * 1.15 times whatever the largest value happens to be redraws its axis every
 * time that value moves, and a chart component reserves its left gutter from
 * how wide the labels render -- so the plot itself shifts sideways. Counts
 * hovering around ten make it flicker: 9.2 needs three characters and 10.4
 * needs four, and the boundary gets crossed every few seconds.
 *
 * Snapping to a round number costs a little headroom and makes the axis hold
 * still until the data genuinely outgrows it.
 */
static inline float sdrgui_nice_ceiling(float value) {
    float decade = 1.0f;
    float scaled;

    if (!(value > 0.0f))
        return 1.0f;
    while (value >= 10.0f * decade)
        decade *= 10.0f;
    while (value < decade)
        decade /= 10.0f;
    scaled = value / decade;
    if (scaled <= 1.0f)
        return decade;
    if (scaled <= 2.0f)
        return 2.0f * decade;
    if (scaled <= 5.0f)
        return 5.0f * decade;
    return 10.0f * decade;
}

static inline int sdrgui_point_in(Rectangle rect, float x, float y) {
    return x >= rect.x && x <= rect.x + rect.width && y >= rect.y &&
           y <= rect.y + rect.height;
}

/*
 * How wide one bar is when `count` of them share a plot. Drawing and hit
 * testing must agree on this or the two describe different bars; they share it
 * here so they cannot drift apart.
 */
/*
 * Whether a point is inside a circle of `radius` about `centre`.
 *
 * Here rather than as a distance test inside a draw call, for the same reason
 * "which bar is under the pointer" is here: it decides something -- whether a
 * hover panel is drawn -- and a function that draws may not also decide
 * (ADR-0012). Squared throughout, so no square root and no tolerance.
 */
static inline int sdrgui_point_in_circle(Vector2 centre, float radius,
                                         float x, float y) {
    float dx = x - centre.x;
    float dy = y - centre.y;

    if (radius <= 0.0f)
        return 0;
    return dx * dx + dy * dy <= radius * radius;
}

static inline float sdrgui_bar_width(Rectangle plot, int count) {
    if (count <= 0)
        return 0.0f;
    return plot.width / (float)count;
}

/*
 * Where bar `index` (0-based) starts. The bar drawn is a pixel narrower so the
 * bars read as separate, but it starts here, and the hit test below must agree
 * with this and not with the drawn width.
 */
static inline float sdrgui_bar_left(Rectangle plot, int count, int index) {
    return plot.x + (float)index * sdrgui_bar_width(plot, count);
}

/*
 * Which bar a point is over, 0-based, or -1 when the point is outside the
 * plot. Note `plot`, not the chart's outer rectangle: passing the outer one is
 * exactly the bug this exists to prevent, and it is off by however wide the
 * label gutter is -- about two channels on a 124-channel chart.
 */
static inline int sdrgui_bar_index_at(Rectangle plot, int count, float x,
                                      float y) {
    float width = sdrgui_bar_width(plot, count);
    int index;

    if (count <= 0 || width <= 0.0f || !sdrgui_point_in(plot, x, y))
        return -1;
    index = (int)((x - plot.x) / width);
    if (index < 0)
        index = 0;
    if (index >= count)
        index = count - 1;
    return index;
}

/*
 * What a waterfall strip actually covers, and which slice of its texture to
 * draw for that.
 *
 * The texture always holds the whole received span -- one column per bin, from
 * `center - rate/2` to `center + rate/2` -- because that is what the rows were
 * written with. Showing part of it is therefore a source-rectangle question,
 * not a re-render, which is why a zoom costs nothing.
 *
 * This was gated on the calibration overlay's own flag for as long as the
 * overlay was the only caller that zoomed. The Scope's frequency window then
 * arrived, computed a range, handed it over, and was silently discarded: the
 * drag worked, the axis never moved, and nothing anywhere said why. The gate
 * is gone rather than widened -- a zoom is requested by asking for one, and a
 * flag naming a *screen* has no business deciding whether the request is
 * honoured.
 *
 * A request outside the received span is clamped to it rather than refused: a
 * pan that runs off the edge is how retuning is triggered, and it must keep
 * drawing something while the receiver catches up.
 */
struct sdrgui_waterfall_span {
    double lower_hz;      /* what the drawn strip covers */
    double upper_hz;
    float source_x;       /* the slice of the texture that covers it */
    float source_width;
};

static inline struct sdrgui_waterfall_span
sdrgui_waterfall_span(double center_hz, double sample_rate,
                      double zoom_center_hz, double zoom_half_width_hz,
                      float texture_width) {
    double full_lower = center_hz - sample_rate / 2.0;
    double full_upper = center_hz + sample_rate / 2.0;
    struct sdrgui_waterfall_span span;
    double lower, upper;

    span.lower_hz = full_lower;
    span.upper_hz = full_upper;
    span.source_x = 0.0f;
    span.source_width = texture_width;
    if (zoom_center_hz <= 0.0 || zoom_half_width_hz <= 0.0 ||
        full_upper <= full_lower)
        return span;

    lower = zoom_center_hz - zoom_half_width_hz;
    upper = zoom_center_hz + zoom_half_width_hz;
    if (lower < full_lower)
        lower = full_lower;
    if (upper > full_upper)
        upper = full_upper;
    if (upper - lower <= 1.0)
        return span;

    span.lower_hz = lower;
    span.upper_hz = upper;
    span.source_x = (float)((lower - full_lower) / (full_upper - full_lower)) *
                    texture_width;
    span.source_width =
        (float)((upper - lower) / (full_upper - full_lower)) * texture_width;
    return span;
}

/*
 * Where the band a reader is dragging out lands on a chart.
 *
 * Three charts draw this -- the spectrum, the waterfall and the survey -- and
 * for a while only two did, because it was written out by hand in each one.
 * The two that had it had already drifted apart in colour. This is the
 * arithmetic; the drawing stays with the drawing.
 *
 * `from` and `to` are in whichever order the reader dragged, since a drag
 * rightwards and a drag leftwards select the same band.
 */
struct sdrgui_drag_band {
    float x0;
    float x1;
    int visible;        /* 0 when the band falls outside the drawn range */
};

static inline struct sdrgui_drag_band
sdrgui_drag_band_at(Rectangle plot, double lower_hz, double upper_hz,
                    double from_hz, double to_hz) {
    struct sdrgui_drag_band band = { 0.0f, 0.0f, 0 };
    double span = upper_hz - lower_hz;
    double low = from_hz < to_hz ? from_hz : to_hz;
    double high = from_hz < to_hz ? to_hz : from_hz;

    if (span <= 0.0 || high <= low)
        return band;
    band.x0 = plot.x + plot.width * (float)((low - lower_hz) / span);
    band.x1 = plot.x + plot.width * (float)((high - lower_hz) / span);
    if (band.x0 < plot.x)
        band.x0 = plot.x;
    if (band.x1 > plot.x + plot.width)
        band.x1 = plot.x + plot.width;
    band.visible = band.x1 > band.x0;
    return band;
}

/*
 * Where the columns of a message log go.
 *
 * Five columns -- time, an identifier, a short label, the decoded text and
 * the raw bytes -- and until 2026-09-15 their x positions were five constants
 * added up inside the drawing, with the *headings* clipped to fit and the
 * *values* drawn with a bare DrawText. The comment beside them claimed "every
 * field is drawn through sdrgui_text_fit now, so a column that does not fit is
 * shortened rather than spilled", and that was true of two fields out of five.
 *
 * What it looked like: the SRD log's KIND column is 84 px and its longest
 * value is "UNDECODED", which is wider, so it was drawn straight through the
 * MOD column beside it and the two read as "UNDECOD2BSK". check-layout cannot
 * see that -- it compares rectangles, and both columns are inside the panel --
 * which is exactly the case panel_rows.h was written for, one level further
 * in.
 *
 * So the widths follow the content. The caller measures its widest identifier
 * and label -- MeasureText needs a font, which needs a window, so measuring
 * stays with the drawing (see this file's header) -- and this decides what to
 * do with the answer. Growth is allowed only while the decoded-message column
 * keeps its own minimum, because that column is the one carrying the reading;
 * past that the columns stay at their minimums and the text is clipped, which
 * is a legible answer where overlap is not.
 *
 * Every width here is the room for the *text*, already inside the gutter that
 * separates it from the next column.
 */

#define SDRGUI_LOG_GUTTER 12
#define SDRGUI_LOG_TIME_PITCH 96
#define SDRGUI_LOG_ID_PITCH_MIN 84
#define SDRGUI_LOG_LABEL_PITCH_MIN 64
#define SDRGUI_LOG_TYPE_PITCH_MIN 96
#define SDRGUI_LOG_FREQ_PITCH_MIN 110
#define SDRGUI_LOG_DETAIL_PITCH 430
#define SDRGUI_LOG_DETAIL_MIN 180
#define SDRGUI_LOG_RAW_MIN 170

/*
 * What the caller measured, by name.
 *
 * Positional ints were fine for two optional columns and stopped being fine
 * at three: `(left, right, 95, 40, 110)` says nothing about which 110 is
 * which, and view_adsb.c had already been bitten once by a positional
 * initialiser silently shifting when a field was added beside it. A zero
 * means "this caller has no such column" for the optional ones.
 */
struct sdrgui_log_widths {
    int id;
    int label;
    int type;   /* 0 when the caller has no type for its rows */
    int freq;   /* 0 when the caller has no frequency for its rows */
};

struct sdrgui_log_columns {
    int time_x, time_width;
    int freq_x, freq_width;
    int id_x, id_width;
    int label_x, label_width;
    int type_x, type_width;
    int detail_x, detail_width;
    int raw_x, raw_width;
    int show_freq; /* 0 when the caller has no frequency for its rows */
    int show_type; /* 0 when the caller has no type for its rows */
    int show_raw;  /* 0 when the raw column would not fit and is given up */
};

/* One column's pitch: at least its minimum, more if its content needs it,
   and nothing at all when the caller has no such column. */
static inline int sdrgui_log_pitch(int text, int minimum, int optional) {
    int pitch;

    if (optional && text <= 0)
        return 0;
    pitch = minimum;
    if (text + SDRGUI_LOG_GUTTER > pitch)
        pitch = text + SDRGUI_LOG_GUTTER;
    return pitch;
}

/* Hand back whatever this column wanted past its minimum that there is room
   for, and take it out of `room`. */
static inline int sdrgui_log_squeeze(int pitch, int minimum, int *room) {
    int give;

    if (pitch <= 0)
        return 0;
    give = pitch - minimum;
    if (give > *room)
        give = *room;
    *room -= give;
    return minimum + give;
}

static inline struct sdrgui_log_columns
sdrgui_message_log_columns(int left, int right,
                           struct sdrgui_log_widths w) {
    struct sdrgui_log_columns c;
    int id_pitch = sdrgui_log_pitch(w.id, SDRGUI_LOG_ID_PITCH_MIN, 0);
    int label_pitch = sdrgui_log_pitch(w.label, SDRGUI_LOG_LABEL_PITCH_MIN, 0);
    int type_pitch = sdrgui_log_pitch(w.type, SDRGUI_LOG_TYPE_PITCH_MIN, 1);
    int freq_pitch = sdrgui_log_pitch(w.freq, SDRGUI_LOG_FREQ_PITCH_MIN, 1);
    int wanted, room;

    /*
     * What the columns want past their minimums, against what is left once
     * the decoded-message column has taken its own. The identifier is served
     * first: it is the widest by design, and it is what overflows.
     *
     * The optional columns cost nothing when absent -- a caller with no
     * frequency or type passes 0 for it and the rest land exactly where they
     * did before the column existed, which is what keeps the ADS-B log
     * unmoved by an SRD feature.
     */
    wanted = (id_pitch - SDRGUI_LOG_ID_PITCH_MIN) +
             (label_pitch - SDRGUI_LOG_LABEL_PITCH_MIN) +
             (type_pitch > 0 ? type_pitch - SDRGUI_LOG_TYPE_PITCH_MIN : 0) +
             (freq_pitch > 0 ? freq_pitch - SDRGUI_LOG_FREQ_PITCH_MIN : 0);
    room = right - (left + SDRGUI_LOG_TIME_PITCH +
                    (freq_pitch > 0 ? SDRGUI_LOG_FREQ_PITCH_MIN : 0) +
                    SDRGUI_LOG_ID_PITCH_MIN + SDRGUI_LOG_LABEL_PITCH_MIN +
                    (type_pitch > 0 ? SDRGUI_LOG_TYPE_PITCH_MIN : 0) +
                    SDRGUI_LOG_DETAIL_MIN);
    if (room < 0)
        room = 0;
    if (wanted > room) {
        id_pitch = sdrgui_log_squeeze(id_pitch, SDRGUI_LOG_ID_PITCH_MIN, &room);
        label_pitch = sdrgui_log_squeeze(label_pitch,
                                         SDRGUI_LOG_LABEL_PITCH_MIN, &room);
        freq_pitch = sdrgui_log_squeeze(freq_pitch,
                                        SDRGUI_LOG_FREQ_PITCH_MIN, &room);
        type_pitch = sdrgui_log_squeeze(type_pitch,
                                        SDRGUI_LOG_TYPE_PITCH_MIN, &room);
    }

    c.time_x = left;
    c.freq_x = c.time_x + SDRGUI_LOG_TIME_PITCH;
    c.id_x = c.freq_x + freq_pitch;
    c.label_x = c.id_x + id_pitch;
    c.type_x = c.label_x + label_pitch;
    c.detail_x = c.type_x + type_pitch;
    c.raw_x = c.detail_x + SDRGUI_LOG_DETAIL_PITCH;

    c.time_width = SDRGUI_LOG_TIME_PITCH - SDRGUI_LOG_GUTTER;
    c.freq_width = freq_pitch > 0 ? freq_pitch - SDRGUI_LOG_GUTTER : 0;
    c.id_width = id_pitch - SDRGUI_LOG_GUTTER;
    c.label_width = label_pitch - SDRGUI_LOG_GUTTER;
    c.type_width = type_pitch > 0 ? type_pitch - SDRGUI_LOG_GUTTER : 0;
    c.show_freq = freq_pitch > 0;
    c.show_type = type_pitch > 0;

    c.show_raw = c.raw_x + SDRGUI_LOG_RAW_MIN <= right;
    c.detail_width = (c.show_raw ? c.raw_x - SDRGUI_LOG_GUTTER : right) -
                     c.detail_x;
    c.raw_width = c.show_raw ? right - c.raw_x : 0;

    if (c.time_width < 1)
        c.time_width = 1;
    if (c.id_width < 1)
        c.id_width = 1;
    if (c.label_width < 1)
        c.label_width = 1;
    if (c.show_type && c.type_width < 1)
        c.type_width = 1;
    if (c.show_freq && c.freq_width < 1)
        c.freq_width = 1;
    if (c.detail_width < 1)
        c.detail_width = 1;
    if (c.raw_width < 0)
        c.raw_width = 0;
    return c;
}

/*
 * The message log's rows, and which one a pointer is over.
 *
 * The row band is the one part of that log needing no `MeasureText`: the
 * caption strip, the padding, the heading rule and the row pitch are all
 * constants, and only the *columns* depend on the widths of the text about to
 * be drawn. So the hit test can live here while the column arithmetic needs
 * its answer passed in, which is the split this header already makes
 * (ADR-0012).
 *
 * It exists because clicking a row in the SRD log both selected it and
 * retuned the receiver, from inside `draw_log()` -- a function taking
 * `const struct app *` and casting the const away to act. Drawing reports;
 * the input phase decides. Reaching the decision from the input phase means
 * finding the row without drawing it, and that is this.
 */
#define SDRGUI_LOG_CAPTION_STRIP 25.0f
#define SDRGUI_LOG_PAD 12
#define SDRGUI_LOG_ROW_HEIGHT 24
#define SDRGUI_LOG_HEADING_HEIGHT 22

/* Where row 0 starts, and how many rows fit, inside a log's outer rect. */
struct sdrgui_log_rows {
    Rectangle plot;     /* the framed area inside the caption strip */
    int first_y;        /* top of row 0, before its 2 px overhang */
    int left;           /* the TIME column's x, which the row band starts at */
    int width;          /* the row band's width */
    int row_height;
    int capacity;       /* how many rows fit; a row past it is not drawn */
};

static inline struct sdrgui_log_rows sdrgui_message_log_rows(Rectangle outer) {
    struct sdrgui_log_rows r;
    int usable;

    r.plot = sdrgui_chart_area(outer, 0.0f, SDRGUI_LOG_CAPTION_STRIP);
    r.left = (int)r.plot.x + SDRGUI_LOG_PAD;
    r.width = (int)r.plot.width - 2 * SDRGUI_LOG_PAD + 8;
    r.first_y = (int)r.plot.y + SDRGUI_LOG_PAD + SDRGUI_LOG_HEADING_HEIGHT;
    r.row_height = SDRGUI_LOG_ROW_HEIGHT;

    usable = (int)(r.plot.y + r.plot.height) - SDRGUI_LOG_PAD - r.first_y;
    r.capacity = usable / SDRGUI_LOG_ROW_HEIGHT;
    if (r.capacity < 0)
        r.capacity = 0;
    if (r.width < 0)
        r.width = 0;
    return r;
}

/*
 * Which row the pointer is over, or -1.
 *
 * `count` is how many rows the log holds; only the ones that fit are drawn,
 * so a pointer below the last drawn row is over nothing even when the log is
 * longer. That is deliberate and it is the same rule `panel_rows.h` states: a
 * row past the capacity is not drawn at all, and a hit test that answered for
 * an undrawn row would select something the reader cannot see.
 */
static inline int sdrgui_message_log_row_at(Rectangle outer, int count,
                                            Vector2 at) {
    struct sdrgui_log_rows r = sdrgui_message_log_rows(outer);
    int rows = count < r.capacity ? count : r.capacity;
    int row;

    if (rows <= 0)
        return -1;
    if (at.x < (float)(r.left - 4) || at.x > (float)(r.left - 4 + r.width))
        return -1;
    if (at.y < (float)(r.first_y - 2))
        return -1;
    row = (int)((at.y - (float)(r.first_y - 2)) / (float)r.row_height);
    if (row < 0 || row >= rows)
        return -1;
    return row;
}

struct sdrgui_waterfall_marker {
    double frequency_hz;        /* detection center frequency */
    double bandwidth_hz;        /* estimated bandwidth */
    double age_seconds;         /* age in the past (0 = now = top of waterfall) */
    double duration_seconds;    /* duration of burst in time */
    /*
     * Tag / packet summary, or NULL for a marker that has nothing to add
     * beyond being there -- which draws brackets and a dot and no pill.
     * On a busy band most detections are of that kind, and a box saying
     * the same word forty times hides the few that say something else.
     */
    const char *label;
    int highlighted;            /* 1 if selected/hovered in table */
    int id;                     /* entry identifier */
    Color color;                /* custom color (or default if 0) */
};

/*
 * Whether two rectangles overlap.
 *
 * raylib's `CheckCollisionRecs()` to the character, reproduced here because
 * it is in raylib's *library* and this header is compiled by checks that link
 * `-lm` alone. The inequalities are strict, so rectangles that merely touch
 * do not collide -- copying that exactly is the point, since the label
 * placement below was tuned against it and a `<=` here would move pills that
 * currently sit edge to edge.
 */
static inline int sdrgui_rects_overlap(Rectangle a, Rectangle b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
           a.y < b.y + b.height && a.y + a.height > b.y;
}

/*
 * How many seconds of history the waterfall is showing.
 *
 * Shared by the drawing and by whoever hit-tests a marker, because a marker's
 * y is its age over this and the two must divide by the same number.
 */
static inline double sdrgui_waterfall_visible_seconds(int rows, int height,
                                                      size_t pair_count,
                                                      size_t fallback_pairs,
                                                      double sample_rate) {
    double row_seconds;

    if (sample_rate <= 0.0)
        return 0.0;
    row_seconds = (pair_count > 0 ? (double)pair_count
                                  : (double)fallback_pairs) / sample_rate;
    return (height > 0 ? (double)height : (double)rows) * row_seconds;
}

/* What the markers are laid out against: the plot, the frequency span across
   it, and how much time it covers. */
struct sdrgui_marker_axes {
    Rectangle plot;
    double lower_hz;
    double upper_hz;
    double visible_seconds;
};

/*
 * Where one marker's parts ended up. `pill` is meaningful only when
 * `labelled`; `index` is the marker's place in the caller's array, so the
 * drawing can go back for its colour and its text.
 */
struct sdrgui_marker_layout {
    Rectangle bracket;
    Rectangle pill;
    /*
     * Where the pill would have gone before collisions were resolved. Kept
     * because the drawing leads the eye to a pill that moved, and "moved" is
     * measured against both this and the default position beside the bracket
     * -- two terms, and dropping either changes which markers grow a leader
     * line.
     */
    Rectangle pill_target;
    float centre_x;
    float dot_y;
    int labelled;
    int index;
    int id;
};

/*
 * How many pills may be placed. Past this a marker still draws its brackets
 * and its dot; it just does not compete for a label position.
 */
#define SDRGUI_MARKER_PILL_BUDGET 64

/*
 * How many markers may be laid out at once: the larger of the two logs that
 * draw them, `ADSB_LOG_CAPACITY` at 256 (`SRD_LOG_CAPACITY` is 64). So this
 * narrows nothing today -- it is the bound that lets the layout be an array
 * on the stack rather than an allocation inside a draw.
 */
#define SDRGUI_MARKER_MAX 256

/*
 * One marker's label pill, moved out of the collisions already placed.
 *
 * Y is physical time and is never modified: a label must stay at the
 * detection's own moment. Only x moves, through eight attempts either side of
 * the marker, then a clamp into the plot.
 */
static inline Rectangle sdrgui_marker_pill_resolve(Rectangle target,
                                                   const Rectangle *placed,
                                                   int placed_count,
                                                   Rectangle plot,
                                                   float marker_cx) {
    Rectangle r = target;
    int max_attempts = 8;
    int attempt = 0;

    while (attempt < max_attempts) {
        int collides = 0;
        int i;

        for (i = 0; i < placed_count; i++) {
            Rectangle p = { placed[i].x - 4.0f, placed[i].y - 2.0f,
                            placed[i].width + 8.0f, placed[i].height + 4.0f };
            if (sdrgui_rects_overlap(r, p)) {
                collides = 1;
                break;
            }
        }
        if (!collides)
            break;

        attempt++;
        {
            float span_x = target.width + 10.0f;

            if (attempt == 1)
                r.x = marker_cx + 8.0f;
            else if (attempt == 2)
                r.x = marker_cx - r.width - 8.0f;
            else if (attempt == 3)
                r.x = marker_cx + 8.0f + span_x;
            else if (attempt == 4)
                r.x = marker_cx - 8.0f - span_x - r.width;
            else if (attempt == 5)
                r.x = marker_cx + 8.0f + 2.0f * span_x;
            else if (attempt == 6)
                r.x = marker_cx - 8.0f - 2.0f * span_x - r.width;
            else
                r.x = target.x + (float)attempt * 12.0f;
        }
    }

    if (r.x < plot.x + 2.0f)
        r.x = plot.x + 2.0f;
    if (r.x + r.width > plot.x + plot.width - 2.0f)
        r.x = plot.x + plot.width - r.width - 2.0f;
    if (r.y < plot.y + 2.0f)
        r.y = plot.y + 2.0f;
    if (r.y + r.height > plot.y + plot.height - 2.0f)
        r.y = plot.y + plot.height - r.height - 2.0f;
    return r;
}

/*
 * Every marker's brackets and label pill, in one pass.
 *
 * One pass and one array because pill placement is **order-dependent**: each
 * pill is resolved against the ones already placed, so marker N's rectangle
 * depends on 0..N-1. A hit test that re-derived a single marker's placement
 * independently would be a second implementation agreeing only by luck. The
 * drawing and the hit test read this instead.
 *
 * `label_widths[i]` is `MeasureText(markers[i].label, 12)` -- measured by the
 * caller because it needs a font, which needs a window, and this header may
 * not (ADR-0012). It is read only for markers that have a label.
 *
 * Markers outside the frequency span or the time window are dropped rather
 * than emitted, so the returned count is what is on screen.
 */
static inline int sdrgui_waterfall_marker_layout(
        const struct sdrgui_waterfall_marker *markers, int count,
        const int *label_widths, struct sdrgui_marker_axes axes,
        struct sdrgui_marker_layout *out, int out_max) {
    Rectangle placed[SDRGUI_MARKER_PILL_BUDGET];
    int placed_count = 0;
    int n = 0;
    int i;
    double span_hz = axes.upper_hz - axes.lower_hz;

    if (!markers || !out || out_max <= 0 || span_hz <= 0.0 ||
        axes.visible_seconds <= 0.0)
        return 0;

    for (i = 0; i < count && n < out_max; i++) {
        const struct sdrgui_waterfall_marker *m = &markers[i];
        struct sdrgui_marker_layout *l = &out[n];
        float mx, my, marker_w, marker_h, pw, ph, tx;
        Rectangle target;
        int tw;

        if (m->age_seconds < 0.0 || m->age_seconds > axes.visible_seconds)
            continue;
        if (m->frequency_hz < axes.lower_hz || m->frequency_hz > axes.upper_hz)
            continue;

        mx = axes.plot.x + (float)((m->frequency_hz - axes.lower_hz) /
                                   span_hz) * axes.plot.width;
        my = axes.plot.y + (float)(m->age_seconds / axes.visible_seconds) *
                           axes.plot.height;

        marker_w = 26.0f;
        if (m->bandwidth_hz > 0.0) {
            marker_w = (float)(m->bandwidth_hz / span_hz) * axes.plot.width;
            if (marker_w < 20.0f)
                marker_w = 20.0f;
        }
        marker_h = 12.0f;
        if (m->duration_seconds > 0.0) {
            marker_h = (float)(m->duration_seconds / axes.visible_seconds) *
                       axes.plot.height;
            if (marker_h < 8.0f)
                marker_h = 8.0f;
        }

        l->bracket = (Rectangle){ mx - marker_w / 2.0f, my - marker_h / 2.0f,
                                  marker_w, marker_h };
        if (l->bracket.y < axes.plot.y + 2.0f)
            l->bracket.y = axes.plot.y + 2.0f;
        if (l->bracket.y + l->bracket.height >
            axes.plot.y + axes.plot.height - 2.0f)
            l->bracket.y = axes.plot.y + axes.plot.height -
                           l->bracket.height - 2.0f;

        l->labelled = m->label && m->label[0];
        tw = l->labelled && label_widths ? label_widths[i] : 0;
        pw = (float)(tw + 14);
        ph = 16.0f;
        tx = l->bracket.x + l->bracket.width + 4.0f;
        if (tx + pw > axes.plot.x + axes.plot.width - 2.0f)
            tx = l->bracket.x - pw - 4.0f;
        target = (Rectangle){ tx, my - ph / 2.0f, pw, ph };
        if (target.y < axes.plot.y + 2.0f)
            target.y = axes.plot.y + 2.0f;
        if (target.y + target.height > axes.plot.y + axes.plot.height - 2.0f)
            target.y = axes.plot.y + axes.plot.height - target.height - 2.0f;

        l->pill_target = target;
        l->pill = target;
        if (l->labelled && placed_count < SDRGUI_MARKER_PILL_BUDGET) {
            l->pill = sdrgui_marker_pill_resolve(target, placed, placed_count,
                                                 axes.plot, mx);
            placed[placed_count++] = l->pill;
        }

        l->centre_x = mx;
        l->dot_y = my;
        if (l->dot_y < axes.plot.y + 3.0f)
            l->dot_y = axes.plot.y + 3.0f;
        else if (l->dot_y > axes.plot.y + axes.plot.height - 3.0f)
            l->dot_y = axes.plot.y + axes.plot.height - 3.0f;

        l->index = i;
        l->id = m->id;
        n++;
    }
    return n;
}

/*
 * Which marker a point is over: its id, or -1.
 *
 * **The topmost wins**, which is the later one in the layout, because that is
 * the one drawn over the others. The loop it replaced reached the same answer
 * by overwriting its output on every match and happening to draw in the same
 * order -- right by accident, and so not something a reader could rely on or
 * a check could state. It is stated here.
 *
 * An unlabelled marker is hit by its brackets, which are all there is of it.
 */
static inline int sdrgui_waterfall_marker_at(
        const struct sdrgui_marker_layout *layout, int count, float x,
        float y) {
    int found = -1;
    int i;

    for (i = 0; i < count; i++) {
        if (sdrgui_point_in(layout[i].bracket, x, y) ||
            (layout[i].labelled && sdrgui_point_in(layout[i].pill, x, y)))
            found = layout[i].id;
    }
    return found;
}

#endif
