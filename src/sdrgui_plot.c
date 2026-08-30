#include "sdrgui.h"

#include <math.h>
#include <stdio.h>

/*
 * Primitives every chart uses: cursor hit-testing and its readout box, the
 * plot frame, value-to-pixel mapping, and the inset each component takes for
 * its own caption and axis labels.
 */

int sdrgui_plot_cursor(Rectangle plot, float *x_fraction, float *y_fraction,
                       Vector2 *mouse) {
    *mouse = GetMousePosition();
    if (!CheckCollisionPointRec(*mouse, plot))
        return 0;
    *x_fraction = (mouse->x - plot.x) / plot.width;
    *y_fraction = (mouse->y - plot.y) / plot.height;
    return 1;
}

void sdrgui_cursor_readout(Rectangle plot, Vector2 mouse, const char *text) {
    const int font_size = 16;
    const int padding = 7;
    int text_width = MeasureText(text, font_size);
    int box_width = text_width + padding * 2;
    int box_height = font_size + padding * 2;
    int box_x = (int)mouse.x + 14;
    int box_y = (int)mouse.y - box_height - 10;

    if (box_x + box_width > (int)(plot.x + plot.width))
        box_x = (int)mouse.x - box_width - 14;
    if (box_y < (int)plot.y)
        box_y = (int)mouse.y + 10;

    DrawLine((int)plot.x, (int)mouse.y, (int)(plot.x + plot.width),
             (int)mouse.y, (Color){ 255, 206, 92, 150 });
    DrawLine((int)mouse.x, (int)plot.y, (int)mouse.x,
             (int)(plot.y + plot.height), (Color){ 255, 206, 92, 150 });
    DrawCircleV(mouse, 3.0f, (Color){ 255, 225, 130, 255 });
    DrawRectangle(box_x, box_y, box_width, box_height,
                  (Color){ 7, 12, 19, 235 });
    DrawRectangleLines(box_x, box_y, box_width, box_height,
                       (Color){ 255, 190, 65, 255 });
    DrawText(text, box_x + padding, box_y + padding, font_size,
             (Color){ 255, 231, 170, 255 });
}

void sdrgui_plot_frame(Rectangle plot, int quarter_grid) {
    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    if (quarter_grid) {
        for (int i = 1; i < 4; i++) {
            int y = (int)(plot.y + plot.height * (float)i / 4.0f);
            DrawLine((int)plot.x, y, (int)(plot.x + plot.width), y,
                     (Color){ 31, 47, 59, 255 });
        }
    }
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });
}

float sdrgui_plot_y(Rectangle plot, float value, float lower, float upper) {
    float fraction = (value - lower) / (upper - lower);
    if (fraction < 0.0f)
        fraction = 0.0f;
    if (fraction > 1.0f)
        fraction = 1.0f;
    return plot.y + plot.height * (1.0f - fraction);
}

void sdrgui_format_frequency_offset(char *text, size_t size, double offset) {
    double absolute = fabs(offset);
    if (absolute >= 1000000.0)
        snprintf(text, size, "%+.3f MHz", offset / 1000000.0);
    else if (absolute >= 1000.0)
        snprintf(text, size, "%+.3f kHz", offset / 1000.0);
    else
        snprintf(text, size, "%+.0f Hz", offset);
}

Rectangle sdrgui_chart_area(Rectangle outer, float gutter,
                                 float caption_h) {
    Rectangle plot = { outer.x + gutter, outer.y + caption_h,
                       outer.width - gutter, outer.height - caption_h - 8.0f };
    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
    return plot;
}
