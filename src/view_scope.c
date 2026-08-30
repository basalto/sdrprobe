#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "view.h"
#include "sdrgui.h"

/*
 * The Scope tab: magnitude, spectrum, I/Q scatter and waterfall, plus the GPU
 * resources two of them keep between frames. The waterfall texture and the
 * scatter's fading history are rebuilt when the window resizes, so their
 * lifecycle sits beside the drawing rather than in the frame loop.
 */

Rectangle calculate_plot(void) {
    float width = (float)GetScreenWidth();
    float height = (float)GetScreenHeight();
    Rectangle plot = { 82.0f, 210.0f, width - 112.0f, height - 278.0f };

    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
    return plot;
}

void clear_scatter(struct app *app) {
    BeginTextureMode(app->scatter);
    ClearBackground(BLANK);
    EndTextureMode();
    app->scatter_inserted = 0;
    app->scatter_history_head = 0;
    app->scatter_history_count = 0;
}

int recreate_scatter(struct app *app, Rectangle plot) {
    int width = (int)plot.width;
    int height = (int)plot.height;
    RenderTexture2D replacement = LoadRenderTexture(width, height);

    if (replacement.id == 0 || replacement.texture.id == 0) {
        fprintf(stderr, "Failed to create %dx%d I/Q render texture.\n",
                width, height);
        if (replacement.id != 0 || replacement.texture.id != 0)
            UnloadRenderTexture(replacement);
        return -1;
    }
    if (app->scatter_ready)
        UnloadRenderTexture(app->scatter);
    app->scatter = replacement;
    app->scatter_ready = 1;
    app->plot = plot;
    clear_scatter(app);
    return 0;
}

int recreate_waterfall(struct app *app, Rectangle plot,
                              int clear_history) {
    int width = (int)plot.width;
    int height = (int)plot.height;
    Color *pixels = calloc((size_t)width * (size_t)height, sizeof(*pixels));
    if (!pixels) {
        fprintf(stderr, "Failed to allocate %dx%d waterfall pixels.\n",
                width, height);
        return -1;
    }

    Image image = GenImageColor(width, height, (Color){ 6, 10, 17, 255 });
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0) {
        fprintf(stderr, "Failed to create %dx%d waterfall texture.\n",
                width, height);
        free(pixels);
        return -1;
    }

    if (!app->waterfall_dbfs || height > app->waterfall_capacity) {
        float *history = realloc(
            app->waterfall_dbfs,
            (size_t)height * SDR_DSP_FFT_SIZE * sizeof(*history));
        if (!history) {
            fprintf(stderr, "Failed to allocate %d waterfall history rows.\n",
                    height);
            UnloadTexture(texture);
            free(pixels);
            return -1;
        }
        app->waterfall_dbfs = history;
        app->waterfall_capacity = height;
    }
    if (app->waterfall_ready)
        UnloadTexture(app->waterfall);
    free(app->waterfall_pixels);
    app->waterfall = texture;
    app->waterfall_pixels = pixels;
    app->waterfall_width = width;
    app->waterfall_height = height;
    if (clear_history)
        app->waterfall_rows = 0;
    else if (app->waterfall_rows > height)
        app->waterfall_rows = height;
    app->waterfall_ready = 1;
    render_waterfall(app);
    return 0;
}

static Color waterfall_color(const struct app *app, float dbfs);



static Color waterfall_color(const struct app *app, float dbfs) {
    float level = (dbfs - app->waterfall_lower_dbfs) /
                  (SPECTRUM_TOP_DBFS - app->waterfall_lower_dbfs);
    if (level < 0.0f)
        level = 0.0f;
    if (level > 1.0f)
        level = 1.0f;

    if (level < 0.35f) {
        float t = level / 0.35f;
        return (Color){ (unsigned char)(5 + 20 * t),
                        (unsigned char)(8 + 45 * t),
                        (unsigned char)(20 + 95 * t), 255 };
    }
    if (level < 0.65f) {
        float t = (level - 0.35f) / 0.30f;
        return (Color){ (unsigned char)(25 + 210 * t),
                        (unsigned char)(53 + 37 * t),
                        (unsigned char)(115 - 90 * t), 255 };
    }
    float t = (level - 0.65f) / 0.35f;
    return (Color){ (unsigned char)(235 + 20 * t),
                    (unsigned char)(90 + 165 * t),
                    (unsigned char)(25 + 205 * t), 255 };
}

void render_waterfall(struct app *app) {
    if (!app->waterfall_ready)
        return;
    size_t pixel_count = (size_t)app->waterfall_width *
                         (size_t)app->waterfall_height;
    for (size_t n = 0; n < pixel_count; n++)
        app->waterfall_pixels[n] = (Color){ 6, 10, 17, 255 };

    int rows = app->waterfall_rows < app->waterfall_height
                   ? app->waterfall_rows
                   : app->waterfall_height;
    for (int y = 0; y < rows; y++) {
        const float *row = app->waterfall_dbfs +
                           (size_t)y * SDR_DSP_FFT_SIZE;
        for (int x = 0; x < app->waterfall_width; x++) {
            float position = app->waterfall_width == 1
                                 ? 0.0f
                                 : (float)x * (SDR_DSP_FFT_SIZE - 1) /
                                       (float)(app->waterfall_width - 1);
            int lower = (int)position;
            int upper = lower < SDR_DSP_FFT_SIZE - 1 ? lower + 1 : lower;
            float fraction = position - lower;
            float dbfs = row[lower] * (1.0f - fraction) +
                         row[upper] * fraction;
            app->waterfall_pixels[(size_t)y * app->waterfall_width + x] =
                waterfall_color(app, dbfs);
        }
    }
    UpdateTexture(app->waterfall, app->waterfall_pixels);
}


void update_waterfall(struct app *app) {
    if (!app->waterfall_ready || !app->spectrum_ready)
        return;

    int retained = app->waterfall_rows < app->waterfall_capacity
                       ? app->waterfall_rows
                       : app->waterfall_capacity - 1;
    if (retained > 0)
        memmove(app->waterfall_dbfs + SDR_DSP_FFT_SIZE,
                app->waterfall_dbfs,
                (size_t)retained * SDR_DSP_FFT_SIZE *
                    sizeof(*app->waterfall_dbfs));
    memcpy(app->waterfall_dbfs, app->spectrum_average,
           SDR_DSP_FFT_SIZE * sizeof(*app->waterfall_dbfs));
    if (app->waterfall_rows < app->waterfall_height)
        app->waterfall_rows++;
    render_waterfall(app);
}

void draw_waterfall_rect(const struct app *app, int calibration_mode,
                                Rectangle rect, double zoom_center_hz) {
    struct sdrgui_waterfall_params params = {
        rect, app->waterfall, (double)app->applied_frequency,
        (double)app->applied_sample_rate, calibration_mode,
        calibration_mode && app->calibration_technology == 0,
        zoom_center_hz, CALIBRATION_VIEW_HALF_WIDTH_HZ,
        app->waterfall_rows, app->waterfall_height, app->pair_count,
        SAMPLE_BLOCK_PAIRS, app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS,
        GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ, 124,
        "ARFCN", "GSM 900 ARFCN (200 kHz spacing)", "outside GSM 900"
    };
    sdrgui_waterfall(&params);
}

void draw_waterfall(const struct app *app, int calibration_mode) {
    draw_waterfall_rect(app, calibration_mode, app->plot,
                        calibration_mode ? (double)app->calibration_expected_hz
                                         : 0.0);
}

void update_scatter(struct app *app, double now, int insert) {
    if (insert && app->pair_count > 0) {
        struct scatter_block *block =
            &app->scatter_history[app->scatter_history_head];
        block->count = app->pair_count < SCATTER_SAMPLES
                           ? app->pair_count
                           : SCATTER_SAMPLES;
        block->time = now;
        for (size_t n = 0; n < block->count; n++) {
            size_t index = block->count == 1
                               ? 0
                               : n * (app->pair_count - 1) /
                                     (block->count - 1);
            block->i[n] = app->i_samples[index] / 127.5f;
            block->q[n] = app->q_samples[index] / 127.5f;
        }
        app->scatter_inserted = block->count;
        app->scatter_history_head =
            (app->scatter_history_head + 1) % SCATTER_HISTORY_BLOCKS;
        if (app->scatter_history_count < SCATTER_HISTORY_BLOCKS)
            app->scatter_history_count++;
    }

    while (app->scatter_history_count > 0) {
        size_t oldest = (app->scatter_history_head + SCATTER_HISTORY_BLOCKS -
                         app->scatter_history_count) %
                        SCATTER_HISTORY_BLOCKS;
        if (now - app->scatter_history[oldest].time <=
            SCATTER_HISTORY_SECONDS)
            break;
        app->scatter_history_count--;
    }

    size_t oldest = (app->scatter_history_head + SCATTER_HISTORY_BLOCKS -
                     app->scatter_history_count) %
                    SCATTER_HISTORY_BLOCKS;

    BeginTextureMode(app->scatter);
    ClearBackground(BLANK);
    for (size_t b = 0; b < app->scatter_history_count; b++) {
        const struct scatter_block *block =
            &app->scatter_history[(oldest + b) % SCATTER_HISTORY_BLOCKS];
        float age = (float)(now - block->time);
        float age_alpha = 1.0f - age / (float)SCATTER_HISTORY_SECONDS;
        if (age_alpha < 0.0f)
            age_alpha = 0.0f;
        for (size_t n = 0; n < block->count; n++) {
            float radial = hypotf(block->i[n], block->q[n]) /
                           app->scatter_axis_limit;
            if (radial > 1.0f)
                radial = 1.0f;
            float emphasis = sqrtf(radial);
            float x = (block->i[n] / app->scatter_axis_limit + 1.0f) *
                      0.5f * (float)(app->scatter.texture.width - 1);
            float y = (1.0f - block->q[n] / app->scatter_axis_limit) *
                      0.5f * (float)(app->scatter.texture.height - 1);
            float persistence = 0.30f + 0.70f * age_alpha;
            int alpha = (int)((135.0f + 120.0f * emphasis) * persistence);
            if (alpha < 1)
                continue;
            DrawRectangle((int)x - 1, (int)y - 1, 3, 3,
                          (Color){ 255,
                                   (unsigned char)(145 + 100 * emphasis),
                                   (unsigned char)(35 + 45 * emphasis),
                                   (unsigned char)alpha });
        }
    }
    EndTextureMode();
}

static const char *view_name(enum view_kind view) {
    if (view == VIEW_SPECTRUM)
        return "spectrum";
    if (view == VIEW_SCATTER)
        return "I/Q scatter";
    if (view == VIEW_WATERFALL)
        return "waterfall";
    return "magnitude";
}

void draw_base_hud(const struct app *app,
                           const struct slot_snapshot *snapshot) {
    char text[640];
    const char *state = snapshot->worker_failed
                            ? "failed"
                            : snapshot->worker_done ? "done" : "running";
    char gain[64];

    if (!app->receiver_mode) {
        snprintf(gain, sizeof(gain), "capture");
    } else if (!app->applied_manual_gain) {
        snprintf(gain, sizeof(gain), "auto");
    } else {
        snprintf(gain, sizeof(gain), "%.1f dB",
                 app->applied_gain_tenths / 10.0);
    }

    snprintf(text, sizeof(text), "source: %s   state: %s", app->source_label,
             state);
    DrawText(text, 22, 78, 17, (Color){ 187, 205, 216, 255 });
    snprintf(text, sizeof(text),
              "center: %.6f MHz   rate: %u S/s   gain: %s   PPM: %+d   DC filter: %s   view: %s   FPS: %d",
              app->applied_frequency / 1000000.0, app->applied_sample_rate,
              gain, app->applied_ppm, app->remove_dc ? "on" : "off",
              view_name(app->view), GetFPS());
    DrawText(text, 22, 103, 17, (Color){ 187, 205, 216, 255 });
    snprintf(text, sizeof(text),
             "blocks published: %llu   processed: %llu   overwritten: %llu   malformed: %llu   worker: %s",
             (unsigned long long)snapshot->published_blocks,
             (unsigned long long)snapshot->processed_blocks,
             (unsigned long long)snapshot->overwritten_blocks,
             (unsigned long long)snapshot->malformed_blocks, state);
    DrawText(text, 22, 128, 17, (Color){ 187, 205, 216, 255 });

    if (snapshot->worker_failed) {
        snprintf(text, sizeof(text), "acquisition error: %s",
                 snapshot->worker_error);
        DrawText(text, 22, 154, 17, (Color){ 255, 104, 104, 255 });
    } else if (!app->have_samples) {
        DrawText("waiting for samples", 22, 154, 17,
                  (Color){ 250, 190, 74, 255 });
    } else if (app->signal_stats_ready) {
        const struct sdr_signal_stats *stats = &app->signal_stats;
        const char *quality = "healthy";
        Color quality_color = (Color){ 90, 220, 164, 255 };
        if (stats->clipping_percent >= 0.1f || stats->headroom_db < 1.0f) {
            quality = "clipping";
            quality_color = (Color){ 255, 102, 94, 255 };
        } else if (stats->clipping_percent > 0.0f ||
                   stats->headroom_db < 3.0f) {
            quality = "low headroom";
            quality_color = (Color){ 250, 190, 74, 255 };
        }
        snprintf(text, sizeof(text),
                 "noise (p10) %.2f   signal (p99.5) %.2f   estimated SNR %.1f dB   clipping %.4f%%   headroom %.1f dB   %s",
                 stats->noise_magnitude, stats->signal_magnitude,
                 stats->snr_db, stats->clipping_percent,
                 stats->headroom_db, quality);
        DrawText(text, 22, 154, 17, quality_color);
    }
}

void draw_magnitude(const struct app *app) {
    double duration_ms = app->have_samples
                             ? (double)app->pair_count * 1000.0 /
                                   app->applied_sample_rate
                             : 0.0;
    struct sdrgui_magnitude_params params = {
        app->plot, app->have_samples, app->magnitude_peaks,
        app->magnitude_bin_count, app->magnitude_lower, app->magnitude_upper,
        app->magnitude_min, app->magnitude_mean, app->magnitude_max,
        duration_ms, PHYSICAL_MAGNITUDE_MAX
    };
    sdrgui_magnitude(&params);
}

void draw_spectrum(const struct app *app) {
    struct sdrgui_spectrum_params params = {
        app->plot, (double)app->applied_frequency,
        (double)app->applied_sample_rate, app->spectrum_ready,
        app->spectrum_average, app->spectrum_peak, SDR_DSP_FFT_SIZE,
        app->spectrum_lower_dbfs, SPECTRUM_TOP_DBFS, app->spectrum_windows
    };
    sdrgui_spectrum(&params);
}

void draw_scatter(const struct app *app) {
    struct sdrgui_scatter_params params = {
        app->plot, app->scatter.texture, app->scatter_axis_limit,
        app->scatter_inserted
    };
    sdrgui_scatter(&params);
}
