#include "sdrgui.h"

#include <math.h>
#include <stdio.h>

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

void sdrgui_health_dot(const struct sdrgui_health_params *params) {
    float cx = (float)GetScreenWidth() - 152.0f;
    float cy = 33.0f;
    Color color;
    switch (params->state) {
    case SDRGUI_HEALTH_GOOD:
        color = (Color){ 99, 228, 170, 255 };
        break;
    case SDRGUI_HEALTH_DRIFT:
        color = (Color){ 235, 90, 90, 255 };
        break;
    case SDRGUI_HEALTH_CHECKING:
        color = (Color){ 250, 190, 74, 255 };
        break;
    default:
        color = (Color){ 110, 122, 133, 255 };
        break;
    }
    const char *cap = "GSM cal";
    DrawText(cap, (int)(cx - 12.0f - (float)MeasureText(cap, 16)),
             (int)cy - 8, 16, (Color){ 150, 170, 184, 255 });
    DrawCircle((int)cx, (int)cy, 9.0f, color);
    DrawCircleLines((int)cx, (int)cy, 9.0f, (Color){ 12, 19, 28, 255 });

    if (params->state == SDRGUI_HEALTH_CHECKING) {
        char text[96];
        snprintf(text, sizeof(text),
                 "Checking GSM drift on ARFCN %d...", params->arfcn);
        DrawText(text, 22, 178, 17, (Color){ 250, 190, 74, 255 });
    } else if (params->state == SDRGUI_HEALTH_DRIFT && params->notice &&
               params->notice[0]) {
        DrawText(params->notice, 22, 178, 17, (Color){ 255, 120, 120, 255 });
    }
}


/* Space a chart keeps inside its own rectangle: a strip at the top for the
   caption, a gutter on the left for the widest axis label it will draw, and
   half a line at the bottom, because the lowest label is centred on the plot's
   lower edge and would otherwise hang below it.
 *
 * Components that use this draw entirely within the rectangle they are given,
 * so a caller can pack them by rectangle alone. Without it the caller has to
 * leave clearance for a label width it cannot know -- the width depends on the
 * values, which only the component sees. */
static Rectangle chart_plot_area(Rectangle outer, float gutter,
                                 float caption_h) {
    Rectangle plot = { outer.x + gutter, outer.y + caption_h,
                       outer.width - gutter, outer.height - caption_h - 8.0f };
    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
    return plot;
}

void sdrgui_magnitude(const struct sdrgui_magnitude_params *params) {
    char text[256];
    Rectangle outer = params->plot;
    Rectangle plot = chart_plot_area(outer, (float)(MeasureText("-000.00", 16) + 11.0f),
                                    25.0f);

    sdrgui_plot_frame(plot, 1);
    snprintf(text, sizeof(text), "%.2f", params->have_samples
                                             ? params->upper
                                             : params->physical_max);
    DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
             (int)plot.y - 8, 16, (Color){ 151, 174, 188, 255 });
    snprintf(text, sizeof(text), "%.2f", params->have_samples
                                             ? params->lower
                                             : 0.0f);
    DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
             (int)(plot.y + plot.height) - 8, 16,
             (Color){ 151, 174, 188, 255 });
    DrawText("magnitude (sample units)", (int)outer.x, (int)outer.y, 16,
             (Color){ 151, 174, 188, 255 });

    if (params->bin_count > 0) {
        if (params->bin_count == 1) {
            DrawCircleV((Vector2){ plot.x,
                                   sdrgui_plot_y(plot, params->peaks[0],
                                          params->lower,
                                          params->upper) },
                        2.0f, (Color){ 72, 221, 189, 255 });
        }
        for (size_t i = 1; i < params->bin_count; i++) {
            float x0 = plot.x + plot.width * (float)(i - 1) /
                                  (float)(params->bin_count - 1);
            float x1 = plot.x + plot.width * (float)i /
                                  (float)(params->bin_count - 1);
            float y0 = sdrgui_plot_y(plot, params->peaks[i - 1],
                              params->lower, params->upper);
            float y1 = sdrgui_plot_y(plot, params->peaks[i],
                              params->lower, params->upper);
            DrawLineEx((Vector2){ x0, y0 }, (Vector2){ x1, y1 }, 1.5f,
                       (Color){ 72, 221, 189, 255 });
        }
    }

    DrawText("0 ms", (int)plot.x, (int)(plot.y + plot.height + 11), 16,
             (Color){ 151, 174, 188, 255 });
    double duration_ms = params->duration_ms;
    snprintf(text, sizeof(text), "%.3f ms", duration_ms);
    DrawText(text, (int)(plot.x + plot.width) - MeasureText(text, 16),
             (int)(plot.y + plot.height + 11), 16,
             (Color){ 151, 174, 188, 255 });
    snprintf(text, sizeof(text),
                 "absolute min %.2f   mean %.2f   max %.2f   manual axis %.2f..%.2f sample units   Up/Down scale",
             params->min, params->mean, params->max,
             params->lower, params->upper);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
             (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (sdrgui_plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        double time_ms = duration_ms * x_fraction;
        float lower = params->have_samples ? params->lower : 0.0f;
        float upper = params->have_samples ? params->upper
                                           : params->physical_max;
        float magnitude = upper - y_fraction * (upper - lower);
        snprintf(text, sizeof(text), "time %.3f ms   magnitude %.2f",
                 time_ms, magnitude);
        sdrgui_cursor_readout(plot, mouse, text);
    }
}

void sdrgui_spectrum(const struct sdrgui_spectrum_params *params) {
    char text[256];
    Rectangle outer = params->plot;
    Rectangle plot = chart_plot_area(outer, (float)(MeasureText("-120", 16) + 11.0f),
                                    25.0f);
    double lower_frequency = params->center_hz - params->sample_rate / 2.0;
    double upper_frequency = params->center_hz + params->sample_rate / 2.0;
    const double frequency_steps[] = {
        1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0,
        100000.0, 200000.0, 500000.0, 1000000.0, 2000000.0,
        5000000.0, 10000000.0, 20000000.0, 50000000.0
    };
    double frequency_step = frequency_steps[
        sizeof(frequency_steps) / sizeof(frequency_steps[0]) - 1];
    double minimum_frequency_step = params->sample_rate * 105.0 /
                                    fmaxf(plot.width, 1.0f);
    for (size_t i = 0;
         i < sizeof(frequency_steps) / sizeof(frequency_steps[0]); i++) {
        if (frequency_steps[i] >= minimum_frequency_step) {
            frequency_step = frequency_steps[i];
            break;
        }
    }
    int db_step = 10;
    while ((float)db_step * plot.height /
               (params->top_dbfs - params->lower_dbfs) < 25.0f)
        db_step += 10;

    sdrgui_plot_frame(plot, 0);
    int minor_db_step = db_step / 2;
    int first_minor_db = (int)ceilf(params->lower_dbfs /
                                    (float)minor_db_step) * minor_db_step;
    for (int db = first_minor_db; db <= (int)params->top_dbfs;
         db += minor_db_step) {
        float y = sdrgui_plot_y(plot, (float)db, params->lower_dbfs,
                          params->top_dbfs);
        int major = db % db_step == 0;
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 major ? (Color){ 42, 61, 74, 255 }
                       : (Color){ 24, 37, 48, 255 });
        DrawLine((int)plot.x - (major ? 7 : 4), (int)y, (int)plot.x, (int)y,
                 major ? (Color){ 126, 151, 166, 255 }
                       : (Color){ 70, 91, 105, 255 });
        if (major) {
            snprintf(text, sizeof(text), "%d", db);
            DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
                     (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
        }
    }
    DrawText("dBFS", (int)outer.x, (int)outer.y, 16,
              (Color){ 151, 174, 188, 255 });

    double first_frequency = ceil(lower_frequency / frequency_step) *
                             frequency_step;
    for (double frequency = first_frequency;
         frequency <= upper_frequency + frequency_step * 1e-6;
         frequency += frequency_step) {
        float x = plot.x + (float)((frequency - lower_frequency) /
                                   (upper_frequency - lower_frequency)) *
                                   plot.width;
        DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                 (Color){ 42, 61, 74, 255 });
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + 7,
                 (Color){ 126, 151, 166, 255 });
        snprintf(text, sizeof(text), "%.3f", frequency / 1000000.0);
        int label_width = MeasureText(text, 16);
        DrawText(text, (int)x - label_width / 2,
                 (int)(plot.y + plot.height) + 11, 16,
                 (Color){ 151, 174, 188, 255 });

        double minor_step = frequency_step / 5.0;
        for (int minor = 1; minor < 5; minor++) {
            double minor_frequency = frequency + minor * minor_step;
            if (minor_frequency >= upper_frequency)
                break;
            float minor_x = plot.x +
                            (float)((minor_frequency - lower_frequency) /
                                    (upper_frequency - lower_frequency)) *
                                plot.width;
            if (minor_x <= plot.x || minor_x >= plot.x + plot.width)
                continue;
            DrawLine((int)minor_x, (int)plot.y, (int)minor_x,
                     (int)(plot.y + plot.height),
                     (Color){ 24, 37, 48, 255 });
            DrawLine((int)minor_x, (int)(plot.y + plot.height),
                     (int)minor_x, (int)(plot.y + plot.height) + 4,
                     (Color){ 70, 91, 105, 255 });
        }
    }

    if (params->ready) {
        for (int i = 1; i < params->bins; i++) {
            float x0 = plot.x + plot.width * (float)(i - 1) /
                                  (params->bins - 1);
            float x1 = plot.x + plot.width * (float)i /
                                  (params->bins - 1);
            float average_y0 = sdrgui_plot_y(plot, params->average[i - 1],
                                      params->lower_dbfs,
                                      params->top_dbfs);
            float average_y1 = sdrgui_plot_y(plot, params->average[i],
                                      params->lower_dbfs,
                                      params->top_dbfs);
            float peak_y0 = sdrgui_plot_y(plot, params->peak[i - 1],
                                   params->lower_dbfs,
                                   params->top_dbfs);
            float peak_y1 = sdrgui_plot_y(plot, params->peak[i],
                                   params->lower_dbfs,
                                   params->top_dbfs);
            DrawLineEx((Vector2){ x0, peak_y0 }, (Vector2){ x1, peak_y1 },
                       1.0f, (Color){ 251, 176, 64, 210 });
            DrawLineEx((Vector2){ x0, average_y0 },
                       (Vector2){ x1, average_y1 }, 1.4f,
                       (Color){ 65, 202, 240, 255 });
        }
    }

    snprintf(text, sizeof(text),
              "frequency (MHz), major %.3f MHz   axis %.1f..%.1f dBFS   %d x %d-pair windows   bin %.3f Hz   average   peak hold",
              frequency_step / 1000000.0,
              params->lower_dbfs, params->top_dbfs,
              params->windows, params->bins,
             params->sample_rate / (double)params->bins);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
             (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (sdrgui_plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        double frequency = lower_frequency +
                           x_fraction * (upper_frequency - lower_frequency);
        float dbfs = params->top_dbfs -
                     y_fraction * (params->top_dbfs -
                                   params->lower_dbfs);
        char offset[40];
        sdrgui_format_frequency_offset(offset, sizeof(offset),
                                frequency - params->center_hz);
        snprintf(text, sizeof(text),
                 "frequency %.6f MHz   offset %s   power %.1f dBFS",
                 frequency / 1000000.0, offset, dbfs);
        sdrgui_cursor_readout(plot, mouse, text);
    }
}

void sdrgui_scatter(const struct sdrgui_scatter_params *params) {
    char text[256];
    Rectangle outer = params->plot;
    Rectangle plot = chart_plot_area(outer, (float)(MeasureText("+0.50", 16) + 11.0f),
                                    25.0f);
    Rectangle source = { 0.0f, 0.0f, (float)params->texture.width,
                          -(float)params->texture.height };
    float major_step = params->axis_limit /
                       (plot.width >= 700.0f && plot.height >= 350.0f
                            ? 4.0f
                            : 2.0f);
    float minor_step = major_step / 5.0f;

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawTexturePro(params->texture, source, plot, (Vector2){ 0.0f, 0.0f },
                   0.0f, WHITE);

    float limit = params->axis_limit;
    for (float value = -limit; value <= limit + minor_step * 0.01f;
         value += minor_step) {
        int major_index = (int)lroundf(value / major_step);
        int major = fabsf(value - major_index * major_step) < 0.0001f;
        float x = plot.x + (value / limit + 1.0f) * 0.5f * plot.width;
        float y = plot.y + (1.0f - (value / limit + 1.0f) * 0.5f) *
                           plot.height;
        Color grid = major ? (Color){ 42, 61, 74, 255 }
                           : (Color){ 24, 37, 48, 255 };
        Color tick = major ? (Color){ 126, 151, 166, 255 }
                           : (Color){ 70, 91, 105, 255 };
        int tick_size = major ? 7 : 4;

        DrawLine((int)x, (int)plot.y, (int)x,
                 (int)(plot.y + plot.height), grid);
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 grid);
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + tick_size, tick);
        DrawLine((int)plot.x - tick_size, (int)y, (int)plot.x, (int)y,
                 tick);

        if (major) {
            float label_value = fabsf(value) < 0.0001f ? 0.0f : value;
            int precision = limit < 0.1f ? 3 : 2;
            snprintf(text, sizeof(text), "%+.*f", precision, label_value);
            DrawText(text, (int)x - MeasureText(text, 16) / 2,
                     (int)(plot.y + plot.height) + 11, 16,
                     (Color){ 151, 174, 188, 255 });
            DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
                     (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
        }
    }

    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });
    DrawText("Q (normalized full scale)", (int)outer.x, (int)outer.y, 16,
             (Color){ 151, 174, 188, 255 });
    snprintf(text, sizeof(text),
              "I (normalized full scale)   manual range +/-%.3f   major %.3f   latest block: %zu points   Up/Down scale",
              limit, major_step,
              params->inserted);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
              (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (sdrgui_plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        float i_value = (x_fraction * 2.0f - 1.0f) * limit;
        float q_value = (1.0f - y_fraction * 2.0f) * limit;
        snprintf(text, sizeof(text), "I %+.4f   Q %+.4f full scale",
                 i_value, q_value);
        sdrgui_cursor_readout(plot, mouse, text);
    }
}

void sdrgui_waterfall(const struct sdrgui_waterfall_params *params) {
    char text[256];
    int channel_axis = params->channel_axis;
    Rectangle outer = params->plot;
    Rectangle plot = chart_plot_area(outer, (float)(MeasureText("-10.0 s", 16) + 11.0f),
                                    25.0f);
    Rectangle source = { 0.0f, 0.0f, (float)params->texture.width,
                         (float)params->texture.height };
    double full_lower = params->center_hz - params->sample_rate / 2.0;
    double full_upper = params->center_hz + params->sample_rate / 2.0;
    double lower_frequency = full_lower;
    double upper_frequency = full_upper;
    if (params->calibration_mode && params->zoom_center_hz > 0 &&
        full_upper > full_lower) {
        double zoom_lower = params->zoom_center_hz - params->zoom_half_width_hz;
        double zoom_upper = params->zoom_center_hz + params->zoom_half_width_hz;
        if (zoom_lower < full_lower)
            zoom_lower = full_lower;
        if (zoom_upper > full_upper)
            zoom_upper = full_upper;
        if (zoom_upper - zoom_lower > 1.0) {
            lower_frequency = zoom_lower;
            upper_frequency = zoom_upper;
            source.x = (float)((zoom_lower - full_lower) /
                               (full_upper - full_lower)) *
                       (float)params->texture.width;
            source.width = (float)((zoom_upper - zoom_lower) /
                                   (full_upper - full_lower)) *
                           (float)params->texture.width;
        }
    }
    const double frequency_steps[] = {
        1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0,
        100000.0, 200000.0, 500000.0, 1000000.0, 2000000.0,
        5000000.0, 10000000.0, 20000000.0, 50000000.0
    };
    double frequency_step = frequency_steps[
        sizeof(frequency_steps) / sizeof(frequency_steps[0]) - 1];
    double minimum_frequency_step = params->sample_rate * 105.0 /
                                    fmaxf(plot.width, 1.0f);
    for (size_t i = 0;
         i < sizeof(frequency_steps) / sizeof(frequency_steps[0]); i++) {
        if (frequency_steps[i] >= minimum_frequency_step) {
            frequency_step = frequency_steps[i];
            break;
        }
    }

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawTexturePro(params->texture, source, plot, (Vector2){ 0.0f, 0.0f },
                   0.0f, WHITE);

    double first_frequency = ceil(lower_frequency / frequency_step) *
                             frequency_step;
    if (!channel_axis) {
    for (double frequency = first_frequency;
         frequency <= upper_frequency + frequency_step * 1e-6;
         frequency += frequency_step) {
        float x = plot.x + (float)((frequency - lower_frequency) /
                                   (upper_frequency - lower_frequency)) *
                                   plot.width;
        DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                 (Color){ 170, 190, 200, 70 });
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                 (int)(plot.y + plot.height) + 7,
                 (Color){ 126, 151, 166, 255 });
        snprintf(text, sizeof(text), "%.3f", frequency / 1000000.0);
        DrawText(text, (int)x - MeasureText(text, 16) / 2,
                 (int)(plot.y + plot.height) + 11, 16,
                 (Color){ 151, 174, 188, 255 });

        double minor_step = frequency_step / 5.0;
        for (int minor = 1; minor < 5; minor++) {
            double minor_frequency = frequency + minor * minor_step;
            if (minor_frequency >= upper_frequency)
                break;
            float minor_x = plot.x +
                            (float)((minor_frequency - lower_frequency) /
                                    (upper_frequency - lower_frequency)) *
                                plot.width;
            if (minor_x <= plot.x || minor_x >= plot.x + plot.width)
                continue;
            DrawLine((int)minor_x, (int)plot.y, (int)minor_x,
                     (int)(plot.y + plot.height),
                     (Color){ 100, 125, 140, 32 });
            DrawLine((int)minor_x, (int)(plot.y + plot.height),
                     (int)minor_x, (int)(plot.y + plot.height) + 4,
                     (Color){ 70, 91, 105, 255 });
        }
    }
    } else {
        double channel_px = (double)plot.width * params->channel_spacing_hz /
                            (upper_frequency - lower_frequency);
        int label_stride = 1;
        while (channel_px * label_stride < 42.0)
            label_stride++;
        int first_chan = (int)ceil((lower_frequency - params->channel_base_hz) /
                                    params->channel_spacing_hz);
        if (first_chan < 1)
            first_chan = 1;
        for (int chan = first_chan; chan <= params->channel_max; chan++) {
            double frequency = params->channel_base_hz +
                               chan * params->channel_spacing_hz;
            if (frequency > upper_frequency)
                break;
            float x = plot.x + (float)((frequency - lower_frequency) /
                                       (upper_frequency - lower_frequency)) *
                                       plot.width;
            int labeled = ((chan - first_chan) % label_stride) == 0;
            DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                     labeled ? (Color){ 170, 190, 200, 70 }
                             : (Color){ 100, 125, 140, 32 });
            DrawLine((int)x, (int)(plot.y + plot.height), (int)x,
                     (int)(plot.y + plot.height) + (labeled ? 7 : 4),
                     labeled ? (Color){ 126, 151, 166, 255 }
                             : (Color){ 70, 91, 105, 255 });
            if (labeled) {
                snprintf(text, sizeof(text), "%d", chan);
                DrawText(text, (int)x - MeasureText(text, 16) / 2,
                         (int)(plot.y + plot.height) + 11, 16,
                         (Color){ 151, 174, 188, 255 });
            }
        }
    }

    double row_seconds = params->pair_count > 0
                             ? (double)params->pair_count /
                                   params->sample_rate
                             : (double)params->fallback_pairs /
                                   params->sample_rate;
    double visible_seconds = params->rows * row_seconds;
    int time_divisions = plot.height >= 400.0f ? 4 : 2;
    for (int division = 0; division <= time_divisions; division++) {
        float y = plot.y + plot.height * division / (float)time_divisions;
        double age = visible_seconds * division / (double)time_divisions;
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 (Color){ 170, 190, 200, division == 0 ? 100 : 45 });
        snprintf(text, sizeof(text), division == 0 ? "now" : "-%.1f s", age);
        DrawText(text, (int)plot.x - MeasureText(text, 16) - 11,
                 (int)y - 8, 16, (Color){ 151, 174, 188, 255 });
    }

    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });
    if (!params->calibration_mode)
        DrawText("time (newest at top)", (int)outer.x, (int)outer.y, 16,
                 (Color){ 151, 174, 188, 255 });
    if (channel_axis)
        snprintf(text, sizeof(text),
                 "%s   visible history %.1f s   color %.0f..%.0f dBFS",
                 params->channel_caption, visible_seconds,
                 params->lower_dbfs, params->top_dbfs);
    else
        snprintf(text, sizeof(text),
                 "frequency (MHz), major %.3f MHz   visible history %.1f s   color %.0f..%.0f dBFS",
                 frequency_step / 1000000.0, visible_seconds,
                 params->lower_dbfs, params->top_dbfs);
    DrawText(text, (int)plot.x, (int)(plot.y + plot.height + 36), 16,
             (Color){ 187, 205, 216, 255 });

    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (sdrgui_plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        double frequency = lower_frequency +
                           x_fraction * (upper_frequency - lower_frequency);
        double age = y_fraction * params->height * row_seconds;
        char offset[40];
        sdrgui_format_frequency_offset(offset, sizeof(offset),
                                frequency - params->center_hz);
        if (channel_axis) {
            double channel = (frequency - params->channel_base_hz) /
                             params->channel_spacing_hz;
            long chan = lround(channel);
            if (chan >= 1 && chan <= params->channel_max)
                snprintf(text, sizeof(text),
                         "%s %ld   frequency %.6f MHz   age %.2f s",
                         params->channel_label, chan, frequency / 1000000.0,
                         age);
            else
                snprintf(text, sizeof(text),
                         "%s   frequency %.6f MHz   age %.2f s",
                         params->channel_outside, frequency / 1000000.0, age);
        } else {
            snprintf(text, sizeof(text),
                     "frequency %.6f MHz   offset %s   age %.2f s",
                     frequency / 1000000.0, offset, age);
        }
        sdrgui_cursor_readout(plot, mouse, text);
    }
}

void sdrgui_scan_chart(const struct sdrgui_scan_chart_params *params) {
    char text[160];
    Rectangle outer = params->plot;
    Rectangle plot = chart_plot_area(outer, (float)(MeasureText("-100", 16) + 10.0f),
                                    25.0f);

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

    float bar_width = plot.width / (float)params->count;
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
        float x = plot.x + (float)(chan - 1) * bar_width;
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
        float x = plot.x + (float)(params->selected - 1) * bar_width;
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

void sdrgui_message_log(const struct sdrgui_message_log_params *params) {
    Rectangle plot = params->plot;
    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    const int pad = 12;
    const int row_height = 24;
    int x_time = (int)plot.x + pad;
    int x_icao = x_time + 96;
    int x_label = x_icao + 84;
    int x_detail = x_label + 64;
    int x_raw = x_detail + 430;
    int y = (int)plot.y + pad;

    if (params->caption && params->caption[0]) {
        DrawText(params->caption, x_time, y, 16, (Color){ 151, 174, 188, 255 });
        y += 24;
    }

    /* Column header. */
    DrawText("TIME", x_time, y, 16, (Color){ 126, 151, 166, 255 });
    DrawText("ICAO", x_icao, y, 16, (Color){ 126, 151, 166, 255 });
    DrawText("TYPE", x_label, y, 16, (Color){ 126, 151, 166, 255 });
    DrawText("DECODED MESSAGE", x_detail, y, 16, (Color){ 126, 151, 166, 255 });
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
        DrawText(row->detail, x_detail, row_y, 18,
                 (Color){ 213, 226, 234, 255 });
        DrawText(row->raw, x_raw, row_y, 18, (Color){ 130, 150, 162, 255 });
    }
}

void sdrgui_constellation(const struct sdrgui_constellation_params *params) {
    Rectangle outer = params->plot;
    Rectangle plot = chart_plot_area(outer, (float)(0.0f),
                                    25.0f);
    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });
    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    if (params->caption && params->caption[0])
        DrawText(params->caption, (int)outer.x, (int)outer.y, 16,
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

    if (params->title && params->title[0])
        DrawText(params->title, (int)outer.x, (int)outer.y, 16,
                 (Color){ 151, 174, 188, 255 });

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
        /* Draw as individual bars. */
        float bar_width = dx > 1.5f ? dx - 1.0f : 1.0f;
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
                DrawRectangle((int)x, (int)y, (int)bar_width, (int)(base_y - y), color);
            } else {
                DrawRectangle((int)x, (int)base_y, (int)bar_width, (int)(y - base_y), color);
            }
        }
    }
}

void sdrgui_text_field(Rectangle box, const char *text, int focused) {
    DrawRectangleRec(box, (Color){ 5, 10, 16, 255 });
    DrawRectangleLinesEx(box, 1.0f, focused ? (Color){ 255, 174, 62, 255 }
                                            : (Color){ 91, 117, 132, 255 });
    DrawText(text, (int)box.x + 10, (int)box.y + 9, 19,
             (Color){ 255, 225, 161, 255 });
}
