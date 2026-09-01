#include "sdrgui.h"

#include <math.h>
#include <stdio.h>

/*
 * What the Decode tab draws: the channel-power scan, the decode
 * constellation, the per-burst analysis charts, and the message log.
 */

/* Where the bars actually go inside the rectangle the chart was handed. One
   derivation, used by the drawing and by the hit test below, so the two cannot
   disagree about where a bar is -- which is exactly how the selection came to
   sit a channel or two off. */
static Rectangle scan_chart_area(Rectangle outer) {
    return sdrgui_chart_area(outer, (float)(MeasureText("-100", 16) + 10.0f),
                             25.0f);
}

int sdrgui_scan_chart_channel_at(Rectangle outer, int count, Vector2 point) {
    /* Channels are 1-based; the geometry is 0-based, and 0 means "no
       channel". */
    int index = sdrgui_bar_index_at(scan_chart_area(outer), count, point.x,
                                    point.y);

    return index < 0 ? 0 : index + 1;
}

void sdrgui_scan_chart(const struct sdrgui_scan_chart_params *params) {
    char text[160];
    Rectangle outer = params->plot;
    Rectangle plot = scan_chart_area(outer);

    /* Power range for the vertical axis. */
    float minimum = 0.0f;
    float maximum = -300.0f;
    int measured = 0;
    for (int chan = 1; chan <= params->count; chan++) {
        float power = params->power[chan];
        if (power <= params->sentinel)
            continue;
        if (!measured || power < minimum)
            minimum = power;
        if (!measured || power > maximum)
            maximum = power;
        measured = 1;
    }
    if (!measured) {
        minimum = -100.0f;
        maximum = -30.0f;
    }
    minimum -= 3.0f;
    maximum += 3.0f;
    if (maximum - minimum < 10.0f)
        maximum = minimum + 10.0f;

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    /* Vertical dBFS gridlines. */
    for (int division = 0; division <= 4; division++) {
        float y = plot.y + plot.height * division / 4.0f;
        float value = maximum - (maximum - minimum) * division / 4.0f;
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 (Color){ 170, 190, 200, division == 4 ? 100 : 40 });
        snprintf(text, sizeof(text), "%.0f", value);
        DrawText(text, (int)plot.x - MeasureText(text, 16) - 10,
                 (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
    }

    float bar_width = sdrgui_bar_width(plot, params->count);
    Vector2 mouse = GetMousePosition();
    int hover = params->hover;
    for (int chan = 1; chan <= params->count; chan++) {
        float power = params->power[chan];
        if (power <= params->sentinel)
            continue;
        float level = (power - minimum) / (maximum - minimum);
        if (level < 0.0f)
            level = 0.0f;
        if (level > 1.0f)
            level = 1.0f;
        float x = sdrgui_bar_left(plot, params->count, chan - 1);
        float height = level * plot.height;
        int is_bcch = params->bcch_conf[chan] >= params->bcch_min_conf;
        Color color = is_bcch ? (Color){ 99, 228, 170, 255 }
                              : (Color){ 90, 140, 210, 220 };
        if (chan == hover)
            color = (Color){ 255, 202, 105, 255 };
        int width = (int)(bar_width > 1.0f ? bar_width - 1.0f : 1.0f);
        DrawRectangle((int)x, (int)(plot.y + plot.height - height),
                      width, (int)height, color);
        /* A cap marker keeps a short BCCH bar visible. */
        if (is_bcch)
            DrawRectangle((int)x, (int)(plot.y + plot.height - height) - 4,
                          width, 4, (Color){ 139, 255, 205, 255 });
    }

    /* Selected/inspected channel marker: a bright vertical band and a cap. */
    if (params->selected >= 1 && params->selected <= params->count) {
        float x = sdrgui_bar_left(plot, params->count, params->selected - 1);
        float w = bar_width > 2.0f ? bar_width : 2.0f;
        DrawRectangle((int)x, (int)plot.y, (int)w, (int)plot.height,
                      (Color){ 120, 230, 255, 40 });
        DrawRectangleLines((int)x, (int)plot.y, (int)w, (int)plot.height,
                           (Color){ 120, 230, 255, 200 });
        DrawRectangle((int)x, (int)plot.y - 6, (int)w, 5,
                      (Color){ 150, 236, 255, 255 });
    }

    /* Channel axis labels every 10 channels. */
    for (int chan = 10; chan <= params->count; chan += 10) {
        float x = plot.x + ((float)(chan - 1) + 0.5f) * bar_width;
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + 6,
                 (Color){ 126, 151, 166, 255 });
        snprintf(text, sizeof(text), "%d", chan);
        DrawText(text, (int)x - MeasureText(text, 16) / 2,
                 (int)(plot.y + plot.height) + 10, 16,
                 (Color){ 151, 174, 188, 255 });
    }
    DrawText("ARFCN (GSM 900 downlink, 200 kHz spacing)   green = BCCH (FCCH tone present)",
             (int)plot.x, (int)(plot.y + plot.height + 34), 16,
             (Color){ 187, 205, 216, 255 });

    if (hover > 0 && params->power[hover] > params->sentinel) {
        double frequency = params->base_hz + (double)hover * params->spacing_hz;
        if (params->bcch_conf[hover] >= params->bcch_min_conf)
            snprintf(text, sizeof(text),
                     "ARFCN %d   %.3f MHz   %.1f dBFS   BCCH conf %.2f",
                     hover, frequency / 1000000.0, params->power[hover],
                     params->bcch_conf[hover]);
        else
            snprintf(text, sizeof(text),
                     "ARFCN %d   %.3f MHz   %.1f dBFS   FCCH coh %.2f",
                     hover, frequency / 1000000.0, params->power[hover],
                     params->bcch_conf[hover]);
        DrawText(text, (int)mouse.x + 12, (int)mouse.y - 24, 16,
                 (Color){ 235, 242, 246, 255 });
    }
}

void sdrgui_constellation(const struct sdrgui_constellation_params *params) {
    Rectangle outer = params->plot;
    Rectangle plot = sdrgui_chart_area(outer, (float)(0.0f),
                                    25.0f);
    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    if (params->caption && params->caption[0])
        DrawText(params->caption, (int)plot.x, (int)outer.y, 16,
                 (Color){ 151, 174, 188, 255 });

    float cx = plot.x + plot.width / 2.0f;
    float cy = plot.y + plot.height / 2.0f;
    DrawLine((int)plot.x, (int)cy, (int)(plot.x + plot.width), (int)cy,
             (Color){ 170, 190, 200, 40 });
    DrawLine((int)cx, (int)plot.y, (int)cx, (int)(plot.y + plot.height),
             (Color){ 170, 190, 200, 40 });

    if (params->count <= 0) {
        const char *notice = params->empty_notice ? params->empty_notice : "";
        DrawText(notice, (int)plot.x + 10, (int)cy - 8, 16,
                 (Color){ 150, 172, 188, 255 });
        return;
    }

    float half = 0.46f * (plot.width < plot.height ? plot.width : plot.height);
    for (int i = 0; i < params->count; i++) {
        float px = cx + params->x[i] / 1.4f * half;
        float py = cy - params->y[i] / 1.4f * half;
        Color color = (Color){ 120, 180, 235, 200 };
        if (params->bit)
            color = params->bit[i] ? (Color){ 255, 170, 90, 220 }
                                   : (Color){ 120, 220, 170, 220 };
        DrawCircle((int)px, (int)py, 2.0f, color);
    }
}

void sdrgui_burst_chart(const struct sdrgui_burst_chart_params *params) {
    Rectangle outer = params->plot;

    /* Reserve the room this chart's own furniture needs, instead of drawing
       outside the rectangle it was handed. The title sits above the frame and
       the y-axis labels to its left; when those overhung the rect, a caller
       packing two charts side by side had to know how wide a label would be to
       leave a gap, and could not -- the width depends on the values, which
       only this function sees. Charts overlapped their neighbours as a result.
       Everything below draws into `plot`, which is what is left. */
    float gutter = 0.0f;
    for (int i = 0; i <= 4; i++) {
        char text[32];
        float value = params->y_min +
                      (params->y_max - params->y_min) * ((float)i / 4.0f);
        snprintf(text, sizeof(text), "%.1f", value);
        float w = (float)MeasureText(text, 16);
        if (w > gutter)
            gutter = w;
    }
    gutter += 10.0f;
    /* The topmost y-axis label is centred on the plot's top edge, so it reaches
       half a line above it. The strip reserved for the title has to clear the
       title itself plus that overhang, or the two collide. */
    const float label_font = 16.0f;
    const float title_h = label_font          /* the title */
                        + 6.0f                /* gap under it */
                        + label_font / 2.0f;  /* the top label's overhang */

    /* The bottom label overhangs the plot's lower edge by the same half line,
       so reserve for it too -- otherwise the chart still draws outside the
       rectangle it was given, just at the other end. */
    const float bottom_h = label_font / 2.0f;
    Rectangle plot = { outer.x + gutter, outer.y + title_h,
                       outer.width - gutter,
                       outer.height - title_h - bottom_h };
    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    /* Three of these sit side by side in both decode views, so a title wider
       than its own chart would print across the neighbour's. */
    if (params->title && params->title[0])
        sdrgui_text_fit(params->title, (int)plot.x, (int)outer.y, 16,
                        plot.width, (Color){ 151, 174, 188, 255 });

    if (params->count <= 0) {
        const char *notice = params->empty_notice ? params->empty_notice : "";
        DrawText(notice, (int)plot.x + 10, (int)(plot.y + plot.height / 2.0f) - 8,
                 16, (Color){ 150, 172, 188, 255 });
        return;
    }

    /* Draw zero-line if 0 is within y_min/y_max. */
    if (params->y_min < 0.0f && params->y_max > 0.0f) {
        float zero_y = sdrgui_plot_y(plot, 0.0f, params->y_min, params->y_max);
        DrawLine((int)plot.x, (int)zero_y, (int)(plot.x + plot.width), (int)zero_y,
                 (Color){ 170, 190, 200, 40 });
    }

    /* Draw horizontal axis markers. */
    for (int i = 0; i <= 4; i++) {
        float y_val = params->y_min + (params->y_max - params->y_min) * (i / 4.0f);
        float line_y = sdrgui_plot_y(plot, y_val, params->y_min, params->y_max);
        if (i > 0 && i < 4) {
            DrawLine((int)plot.x, (int)line_y, (int)(plot.x + plot.width), (int)line_y,
                     (Color){ 170, 190, 200, 20 });
        }
        char text[32];
        snprintf(text, sizeof(text), "%.1f", y_val);
        DrawText(text, (int)plot.x - MeasureText(text, 16) - 10, (int)line_y - 8, 16,
                 (Color){ 151, 174, 188, 255 });
    }

    /* X-axis mapping. */
    float dx = plot.width / (float)params->count;
    
    if (params->type == SDRGUI_BURST_LINE) {
        /* Draw as continuous line. */
        for (int i = 0; i < params->count - 1; i++) {
            float x1 = plot.x + i * dx;
            float y1 = sdrgui_plot_y(plot, params->data[i], params->y_min, params->y_max);
            float x2 = plot.x + (i + 1) * dx;
            float y2 = sdrgui_plot_y(plot, params->data[i + 1], params->y_min, params->y_max);
            DrawLineEx((Vector2){x1, y1}, (Vector2){x2, y2}, 1.5f, (Color){ 120, 230, 255, 200 });
        }
    } else if (params->type == SDRGUI_BURST_BAR) {
        /* Draw as individual bars, at least a pixel wide. The gap between
           bars is worth having only when there is room for it: at 148 bits in
           a 285 px panel the spacing is 1.9 px, and subtracting a pixel of gap
           left 0.9, which truncates to a rectangle 0 px wide -- the whole
           chart drew nothing. It looked like the decoder had produced no soft
           magnitudes, and only showed up at some window widths. */
        int bar_width = dx > 2.0f ? (int)(dx - 1.0f) : (int)dx;
        if (bar_width < 1)
            bar_width = 1;
        float base_y = sdrgui_plot_y(plot, 0.0f, params->y_min, params->y_max);
        
        /* Highlight the 64-bit training sequence region in a distinct color. */
        int train_start = 39 + 3; /* 3 tail bits + 39 data bits */
        int train_end = train_start + 64;

        for (int i = 0; i < params->count; i++) {
            float x = plot.x + i * dx;
            float y = sdrgui_plot_y(plot, params->data[i], params->y_min, params->y_max);
            
            Color color = (i >= train_start && i < train_end) 
                          ? (Color){ 255, 174, 62, 220 }   /* Amber for training seq */
                          : (Color){ 149, 205, 232, 220 }; /* Light blue for data */

            if (params->data[i] >= 0.0f) {
                DrawRectangle((int)x, (int)y, bar_width, (int)(base_y - y), color);
            } else {
                DrawRectangle((int)x, (int)base_y, bar_width, (int)(y - base_y), color);
            }
        }
    }
}

void sdrgui_message_log(const struct sdrgui_message_log_params *params) {
    /* The caption goes above the frame, in a strip reserved inside the
       rectangle, which is what every chart here does. It used to sit inside
       the box, so a log packed beside a chart looked shorter than its
       neighbour by exactly the strip the neighbour reserved. */
    Rectangle outer = params->plot;
    Rectangle plot = sdrgui_chart_area(outer, 0.0f, 25.0f);
    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    const int pad = 12;
    const int row_height = 24;
    int x_time = (int)plot.x + pad;
    int x_icao = x_time + 96;
    int x_label = x_icao + 84;
    int x_detail = x_label + 64;
    int x_raw = x_detail + 430;
    int right = (int)(plot.x + plot.width) - pad;
    int y = (int)plot.y + pad;

    /* The raw column sits at a fixed offset, which is fine in a full-width
       panel and not fine in a narrow one: it used to draw past the panel's own
       right edge and over whatever sat beside it. Give it up when there is no
       room, and let the decoded text take the space instead -- every field is
       drawn through sdrgui_text_fit now, so a column that does not fit is
       shortened rather than spilled. */
    const int raw_minimum = 170;
    int show_raw = x_raw + raw_minimum <= right;
    int detail_width = (show_raw ? x_raw - 12 : right) - x_detail;

    if (params->caption && params->caption[0])
        sdrgui_text_fit(params->caption, (int)plot.x, (int)outer.y, 16,
                        (float)(right - (int)plot.x),
                        (Color){ 151, 174, 188, 255 });

    /* Column header. */
    DrawText("TIME", x_time, y, 16, (Color){ 126, 151, 166, 255 });
    DrawText("ICAO", x_icao, y, 16, (Color){ 126, 151, 166, 255 });
    DrawText("TYPE", x_label, y, 16, (Color){ 126, 151, 166, 255 });
    sdrgui_text_fit("DECODED MESSAGE", x_detail, y, 16, (float)detail_width,
                    (Color){ 126, 151, 166, 255 });
    if (show_raw)
        DrawText("RAW (hex)", x_raw, y, 16, (Color){ 126, 151, 166, 255 });
    y += 22;
    DrawLine(x_time, y - 4, (int)(plot.x + plot.width) - pad, y - 4,
             (Color){ 82, 109, 126, 160 });

    if (params->count <= 0) {
        const char *notice = params->empty_notice ? params->empty_notice : "";
        DrawText(notice, x_time, y + 8, 18, (Color){ 187, 205, 216, 255 });
        return;
    }

    int usable = (int)(plot.y + plot.height) - pad - y;
    int max_rows = usable / row_height;
    int rows = params->count < max_rows ? params->count : max_rows;
    for (int i = 0; i < rows; i++) {
        const struct sdrgui_message_log_row *row = &params->rows[i];
        int row_y = y + i * row_height;
        if (i % 2 == 1)
            DrawRectangle(x_time - 4, row_y - 2,
                          (int)plot.width - 2 * pad + 8, row_height,
                          (Color){ 255, 255, 255, 8 });
        Color id_color = row->highlight ? (Color){ 255, 202, 105, 255 }
                                        : (Color){ 235, 242, 246, 255 };
        DrawText(row->time, x_time, row_y, 18, (Color){ 160, 178, 190, 255 });
        DrawText(row->icao, x_icao, row_y, 18, id_color);
        DrawText(row->label, x_label, row_y, 18, (Color){ 149, 205, 232, 255 });
        sdrgui_text_fit(row->detail, x_detail, row_y, 18,
                        (float)detail_width, (Color){ 213, 226, 234, 255 });
        if (show_raw)
            sdrgui_text_fit(row->raw, x_raw, row_y, 18,
                            (float)(right - x_raw),
                            (Color){ 130, 150, 162, 255 });
    }
}
