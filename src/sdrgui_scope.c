#include "sdrgui.h"

#include <math.h>
#include <stdio.h>

/*
 * The Scope tab's four views: magnitude over time, the dBFS spectrum, the I/Q
 * scatter, and the frequency-over-time waterfall.
 */

void sdrgui_magnitude(const struct sdrgui_magnitude_params *params) {
    char text[256];
    Rectangle outer = params->plot;
    Rectangle plot = sdrgui_chart_area(outer, (float)(MeasureText("-000.00", 16) + 11.0f),
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
    DrawText("magnitude (sample units)", (int)plot.x, (int)outer.y, 16,
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
    Rectangle plot = sdrgui_chart_area(outer, (float)(MeasureText("-120", 16) + 11.0f),
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
    DrawText("dBFS", (int)plot.x, (int)outer.y, 16,
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
    Rectangle plot = sdrgui_chart_area(outer, (float)(MeasureText("+0.50", 16) + 11.0f),
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
    DrawText("Q (normalized full scale)", (int)plot.x, (int)outer.y, 16,
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
    /*
     * The footer goes 36 px under the plot and chart_area reserves 8, so the
     * plot has to give back the difference or the caption lands outside the
     * rectangle the caller handed over -- which it did, under the FM view's
     * panel row, where it read as a stray "dB" on top of a panel. Every other
     * waterfall in the program had empty space beneath it and never noticed.
     *
     * Taking it off the plot rather than asking callers to leave room is what
     * the rule in CLAUDE.md requires: a chart draws inside its rectangle, and
     * a caller cannot compute this clearance because it depends on a font.
     */
    Rectangle plot = sdrgui_chart_area(outer,
                                       (float)(MeasureText("-10.0 s", 16) +
                                               11.0f),
                                       25.0f);
    plot.height -= SDRGUI_WATERFALL_FOOTER_H;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
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
        DrawText("time (newest at top)", (int)plot.x, (int)outer.y, 16,
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

/* --- Band survey --- */

/* Where the trace goes inside the rectangle the chart was handed: one
   derivation for the drawing and the hit test both, so they cannot disagree
   about where a candidate is. */
static Rectangle survey_chart_area(Rectangle outer) {
    Rectangle plot = sdrgui_chart_area(outer,
                                       (float)(MeasureText("-100", 16) + 10.0f),
                                       25.0f);
    /* This chart carries two rows under the trace -- the frequency labels and
       the caption counting the candidates -- so it reserves them here rather
       than drawing them past its own rectangle and onto the panel below,
       which is what it did first. */
    plot.height -= 40.0f;
    if (plot.height < 40.0f)
        plot.height = 40.0f;
    return plot;
}

static float survey_x_for_hz(Rectangle plot, double lower_hz, double upper_hz,
                             double hz) {
    double span = upper_hz - lower_hz;
    if (span <= 0.0)
        return plot.x;
    return plot.x + (float)((hz - lower_hz) / span) * plot.width;
}

/* Bins are laid out across the swept range, which is not necessarily what is
   on screen: zooming narrows the window without resampling the array. */
static double survey_hz_for_bin(const struct sdrgui_survey_params *params,
                                int bin) {
    if (params->count <= 1)
        return params->data_lower_hz;
    return params->data_lower_hz +
           (params->data_upper_hz - params->data_lower_hz) *
               ((double)bin + 0.5) / (double)params->count;
}

int sdrgui_survey_chart_peak_at(Rectangle outer,
                                const struct sdrgui_survey_params *params,
                                Vector2 point) {
    Rectangle plot = survey_chart_area(outer);
    int nearest = -1;
    float nearest_distance = 0.0f;

    if (!params || params->peak_count <= 0 ||
        !CheckCollisionPointRec(point, plot))
        return -1;
    /* Nearest candidate within a finger's width, rather than the one exactly
       under the pixel: a carrier can be a fraction of a pixel wide here, and
       an exact hit test would be unusable. */
    for (int i = 0; i < params->peak_count; i++) {
        double hz = survey_hz_for_bin(params, params->peaks[i].index);
        float x;
        if (hz < params->lower_hz || hz > params->upper_hz)
            continue;   /* zoomed past it */
        x = survey_x_for_hz(plot, params->lower_hz, params->upper_hz, hz);
        float distance = fabsf(point.x - x);
        if (distance > 8.0f)
            continue;
        if (nearest < 0 || distance < nearest_distance) {
            nearest = i;
            nearest_distance = distance;
        }
    }
    return nearest;
}

double sdrgui_survey_chart_hz_at(Rectangle outer,
                                 const struct sdrgui_survey_params *params,
                                 Vector2 point) {
    Rectangle plot = survey_chart_area(outer);
    double span;

    if (!params || !CheckCollisionPointRec(point, plot))
        return NAN;
    span = params->upper_hz - params->lower_hz;
    if (span <= 0.0)
        return NAN;
    return params->lower_hz +
           span * (double)((point.x - plot.x) / plot.width);
}

float sdrgui_survey_chart_x_at(Rectangle outer,
                               const struct sdrgui_survey_params *params,
                               double hz) {
    Rectangle plot = survey_chart_area(outer);
    double span;

    if (!params)
        return NAN;
    span = params->upper_hz - params->lower_hz;
    if (span <= 0.0)
        return NAN;
    if (hz < params->lower_hz || hz > params->upper_hz)
        return NAN;               /* off screen; the caller draws nothing */
    return plot.x +
           (float)((hz - params->lower_hz) / span) * plot.width;
}

void sdrgui_survey_chart(const struct sdrgui_survey_params *params) {
    Rectangle outer = params->plot;
    Rectangle plot = survey_chart_area(outer);
    char text[160];

    DrawRectangleRec(plot, (Color){ 6, 10, 17, 255 });

    float minimum = -110.0f;
    float maximum = -20.0f;
    int measured = 0;
    for (int i = 0; i < params->count; i++) {
        float power = params->power_dbfs[i];
        double hz = survey_hz_for_bin(params, i);
        if (power <= params->sentinel)
            continue;
        /* Only what is on screen sets the scale, so zooming into a quiet band
           beside a loud one does not leave the quiet band flat on the floor. */
        if (hz < params->lower_hz || hz > params->upper_hz)
            continue;
        if (!measured || power < minimum)
            minimum = power;
        if (!measured || power > maximum)
            maximum = power;
        measured = 1;
    }
    if (!measured) {
        minimum = -110.0f;
        maximum = -20.0f;
    }
    minimum -= 3.0f;
    maximum += 6.0f;   /* room above the trace for the candidate ticks */
    if (maximum - minimum < 20.0f)
        maximum = minimum + 20.0f;

    for (int division = 0; division <= 4; division++) {
        float y = plot.y + plot.height * division / 4.0f;
        float value = maximum - (maximum - minimum) * division / 4.0f;
        DrawLine((int)plot.x, (int)y, (int)(plot.x + plot.width), (int)y,
                 (Color){ 170, 190, 200, division == 4 ? 100 : 40 });
        snprintf(text, sizeof(text), "%.0f", value);
        DrawText(text, (int)plot.x - MeasureText(text, 16) - 10, (int)y - 8, 16,
                 (Color){ 151, 174, 188, 255 });
    }
    DrawText("dBFS", (int)plot.x, (int)outer.y, 16,
             (Color){ 151, 174, 188, 255 });

    /* The allocations, shaded behind everything: what the spectrum here is
       set aside for, so a peak can be read against what was expected of the
       region it sits in. Alternating tints keep neighbours apart without a
       colour meaning anything. */
    for (int i = 0; i < params->band_count; i++) {
        const struct sdrgui_survey_band *band = &params->bands[i];
        double from = band->lower_hz < params->lower_hz ? params->lower_hz
                                                        : band->lower_hz;
        double to = band->upper_hz > params->upper_hz ? params->upper_hz
                                                      : band->upper_hz;
        if (to <= from)
            continue;
        float x0 = survey_x_for_hz(plot, params->lower_hz, params->upper_hz,
                                   from);
        float x1 = survey_x_for_hz(plot, params->lower_hz, params->upper_hz, to);
        float width = x1 - x0;
        if (width < 1.0f)
            width = 1.0f;
        DrawRectangle((int)x0, (int)plot.y, (int)width, (int)plot.height,
                      (i % 2) ? (Color){ 90, 130, 170, 26 }
                              : (Color){ 120, 100, 170, 26 });
        DrawLine((int)x0, (int)plot.y, (int)x0, (int)(plot.y + plot.height),
                 (Color){ 130, 160, 190, 60 });
        /* A name only where there is room for it; the cursor readout carries
           the rest. */
        int text_width = MeasureText(band->name, 15);
        if (width > (float)text_width + 10.0f)
            DrawText(band->name, (int)x0 + 5, (int)plot.y + 4, 15,
                     (Color){ 156, 178, 196, 190 });
    }

    /* The trace: one vertical mark per bin, because a survey array is usually
       wider than the panel and a line would hide the narrow signals that are
       the point of it. Bins outside the window are skipped rather than
       resampled, so zooming shows the same measurements enlarged. */
    for (int i = 0; i < params->count; i++) {
        float power = params->power_dbfs[i];
        if (power <= params->sentinel)
            continue;
        double hz = survey_hz_for_bin(params, i);
        if (hz < params->lower_hz || hz > params->upper_hz)
            continue;
        float x = survey_x_for_hz(plot, params->lower_hz, params->upper_hz, hz);
        float y = sdrgui_plot_y(plot, power, minimum, maximum);
        DrawLine((int)x, (int)(plot.y + plot.height), (int)x, (int)y,
                 (Color){ 70, 120, 180, 200 });
    }

    /* Candidates, ticked above the trace so a 200 kHz signal in a 1.7 GHz
       sweep is still findable. */
    for (int i = 0; i < params->peak_count; i++) {
        double hz = survey_hz_for_bin(params, params->peaks[i].index);
        float x;
        float y;
        if (hz < params->lower_hz || hz > params->upper_hz)
            continue;
        x = survey_x_for_hz(plot, params->lower_hz, params->upper_hz, hz);
        y = sdrgui_plot_y(plot, params->peaks[i].power_dbfs, minimum, maximum);
        Color color = (Color){ 99, 228, 170, 255 };
        if (i == params->selected)
            color = (Color){ 255, 174, 62, 255 };
        else if (i == params->hover)
            color = (Color){ 235, 242, 246, 255 };
        DrawLine((int)x, (int)y - 12, (int)x, (int)y - 2, color);
        DrawCircle((int)x, (int)y - 14, 3.0f, color);
        if (i == params->selected)
            DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                     (Color){ 255, 174, 62, 90 });
    }

    /* How far the sweep has got. */
    if (params->sweeping && params->count > 0) {
        double hz = survey_hz_for_bin(params, params->swept_bins);
        if (hz >= params->lower_hz && hz <= params->upper_hz) {
            float x = survey_x_for_hz(plot, params->lower_hz, params->upper_hz,
                                      hz);
            DrawLine((int)x, (int)plot.y, (int)x, (int)(plot.y + plot.height),
                     (Color){ 250, 190, 74, 200 });
        }
    }

    /* The rectangle being dragged to zoom, drawn over the trace so the reader
       can see what the release will select. */
    if (params->drag_active) {
        double from = params->drag_lower_hz;
        double to = params->drag_upper_hz;
        if (to < from) {
            double swap = from;
            from = to;
            to = swap;
        }
        float x0 = survey_x_for_hz(plot, params->lower_hz, params->upper_hz,
                                   from);
        float x1 = survey_x_for_hz(plot, params->lower_hz, params->upper_hz, to);
        if (x1 < x0) {
            float swap = x0;
            x0 = x1;
            x1 = swap;
        }
        DrawRectangle((int)x0, (int)plot.y, (int)(x1 - x0), (int)plot.height,
                      (Color){ 255, 174, 62, 40 });
        DrawLine((int)x0, (int)plot.y, (int)x0, (int)(plot.y + plot.height),
                 (Color){ 255, 174, 62, 220 });
        DrawLine((int)x1, (int)plot.y, (int)x1, (int)(plot.y + plot.height),
                 (Color){ 255, 174, 62, 220 });
        char span[64];
        snprintf(span, sizeof(span), "%.3f MHz", (to - from) / 1e6);
        DrawText(span, (int)x0 + 6, (int)plot.y + 6, 16,
                 (Color){ 255, 202, 105, 255 });
    }

    DrawRectangleLinesEx(plot, 1.0f, (Color){ 82, 109, 126, 255 });

    /* Frequency axis, in whatever unit keeps the labels readable. A chart with
       no span yet gets no labels: five zeroes would be a reading, and there is
       nothing to read. */
    double span_hz = params->upper_hz - params->lower_hz;
    for (int division = 0; division <= 4 && span_hz > 0.0; division++) {
        double hz = params->lower_hz + span_hz * division / 4.0;
        float x = plot.x + plot.width * division / 4.0f;
        if (span_hz >= 100e6)
            snprintf(text, sizeof(text), "%.0f", hz / 1e6);
        else if (span_hz >= 2e6)
            snprintf(text, sizeof(text), "%.1f", hz / 1e6);
        else
            snprintf(text, sizeof(text), "%.3f", hz / 1e6);
        int width = MeasureText(text, 16);
        int at = (int)x - width / 2;
        if (division == 0)
            at = (int)plot.x;
        if (division == 4)
            at = (int)(plot.x + plot.width) - width;
        DrawText(text, at, (int)(plot.y + plot.height + 8), 16,
                 (Color){ 151, 174, 188, 255 });
    }
    if (params->lower_hz > params->data_lower_hz + 1.0 ||
        params->upper_hz < params->data_upper_hz - 1.0)
        snprintf(text, sizeof(text),
                 "frequency (MHz)   %d candidates   zoomed to %.3f-%.3f of "
                 "%.3f-%.3f MHz   +/- zoom, Left/Right pan, 0 reset",
                 params->peak_count, params->lower_hz / 1e6,
                 params->upper_hz / 1e6, params->data_lower_hz / 1e6,
                 params->data_upper_hz / 1e6);
    else if (params->suspicious_count > 0)
        /* The zoom hint gives way to the warning, which is the more urgent
           of the two and only appears when there is something to warn about.
           What the mark means is in the help overlay, with the rest of the
           explanations. */
        snprintf(text, sizeof(text),
                 "frequency (MHz)   %d candidates, %d marked * look like "
                 "the receiver",
                 params->peak_count, params->suspicious_count);
    else
        snprintf(text, sizeof(text),
                 "frequency (MHz)   %d candidates above the local floor"
                 "   +/- zoom, Left/Right pan",
                 params->peak_count);
    sdrgui_text_fit(text, (int)plot.x, (int)(plot.y + plot.height + 30), 16,
                    plot.width, (Color){ 187, 205, 216, 255 });

    if (!measured && params->empty_notice && params->empty_notice[0])
        DrawText(params->empty_notice, (int)plot.x + 12,
                 (int)(plot.y + plot.height / 2.0f) - 8, 17,
                 (Color){ 187, 205, 216, 255 });

    /* Cursor readout: the frequency under the pointer, and the candidate it
       would select. */
    float x_fraction;
    float y_fraction;
    Vector2 mouse;
    if (sdrgui_plot_cursor(plot, &x_fraction, &y_fraction, &mouse)) {
        double hz = params->lower_hz + span_hz * (double)x_fraction;
        int at = sdrgui_survey_chart_peak_at(outer, params, mouse);
        const char *band = NULL;
        for (int i = 0; i < params->band_count; i++)
            if (hz >= params->bands[i].lower_hz &&
                hz < params->bands[i].upper_hz)
                band = params->bands[i].name;
        if (at >= 0)
            snprintf(text, sizeof(text),
                     "%.4f MHz   %.1f dBFS   %s   click to inspect",
                     survey_hz_for_bin(params, params->peaks[at].index) / 1e6,
                     (double)params->peaks[at].power_dbfs,
                     band ? band : "no allocation listed");
        else
            snprintf(text, sizeof(text), "%.4f MHz   %s", hz / 1e6,
                     band ? band : "no allocation listed");
        sdrgui_cursor_readout(plot, mouse, text);
    }
}
