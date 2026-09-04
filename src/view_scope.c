#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chrome_layout.h"
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
    /* The chart components now reserve their own caption strip and label
       gutter inside this rectangle, so it covers the margins those used to
       overhang into: 66 px on the left for the widest axis label, 25 above for
       the caption, 8 below for the lowest label's overhang. The drawn plot
       lands where it always did. */
    Rectangle plot = { 16.0f, 185.0f, width - 46.0f, height - 245.0f };

    if (plot.width < 1.0f)
        plot.width = 1.0f;
    if (plot.height < 1.0f)
        plot.height = 1.0f;
    return plot;
}

void clear_scatter(struct app *app) {
    BeginTextureMode(app->sv.scatter);
    ClearBackground(BLANK);
    EndTextureMode();
    app->sv.scatter_inserted = 0;
    app->sv.scatter_history_head = 0;
    app->sv.scatter_history_count = 0;
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
    if (app->sv.scatter_ready)
        UnloadRenderTexture(app->sv.scatter);
    app->sv.scatter = replacement;
    app->sv.scatter_ready = 1;
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

    if (!app->sv.waterfall_dbfs || height > app->sv.waterfall_capacity) {
        float *history = realloc(
            app->sv.waterfall_dbfs,
            (size_t)height * SDR_DSP_FFT_MAX * sizeof(*history));
        if (!history) {
            fprintf(stderr, "Failed to allocate %d waterfall history rows.\n",
                    height);
            UnloadTexture(texture);
            free(pixels);
            return -1;
        }
        app->sv.waterfall_dbfs = history;
        app->sv.waterfall_capacity = height;
    }
    if (app->sv.waterfall_ready)
        UnloadTexture(app->sv.waterfall);
    free(app->sv.waterfall_pixels);
    app->sv.waterfall = texture;
    app->sv.waterfall_pixels = pixels;
    app->sv.waterfall_width = width;
    app->sv.waterfall_height = height;
    if (clear_history)
        app->sv.waterfall_rows = 0;
    else if (app->sv.waterfall_rows > height)
        app->sv.waterfall_rows = height;
    app->sv.waterfall_ready = 1;
    render_waterfall(app);
    app->sv.waterfall_tuned_hz = app->applied_frequency;
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
    if (!app->sv.waterfall_ready)
        return;
    size_t pixel_count = (size_t)app->sv.waterfall_width *
                         (size_t)app->sv.waterfall_height;
    for (size_t n = 0; n < pixel_count; n++)
        app->sv.waterfall_pixels[n] = (Color){ 6, 10, 17, 255 };

    int rows = app->sv.waterfall_rows < app->sv.waterfall_height
                   ? app->sv.waterfall_rows
                   : app->sv.waterfall_height;
    /* How many bins a row holds now. Rows are stored a maximum apart so the
       stride never changes, but only this many of each are filled. */
    int bins = app->spectrum_bins > 0 ? app->spectrum_bins
                                      : SDR_DSP_FFT_SIZE;
    for (int y = 0; y < rows; y++) {
        const float *row = app->sv.waterfall_dbfs +
                           (size_t)y * SDR_DSP_FFT_MAX;
        for (int x = 0; x < app->sv.waterfall_width; x++) {
            float position = app->sv.waterfall_width == 1
                                 ? 0.0f
                                 : (float)x * (bins - 1) /
                                       (float)(app->sv.waterfall_width - 1);
            int lower = (int)position;
            int upper = lower < bins - 1 ? lower + 1 : lower;
            float fraction = position - lower;
            float dbfs = row[lower] * (1.0f - fraction) +
                         row[upper] * fraction;
            app->sv.waterfall_pixels[(size_t)y * app->sv.waterfall_width + x] =
                waterfall_color(app, dbfs);
        }
    }
    UpdateTexture(app->sv.waterfall, app->sv.waterfall_pixels);
}


void update_waterfall(struct app *app) {
    if (!app->sv.waterfall_ready || !app->spectrum_ready)
        return;

    int retained = app->sv.waterfall_rows < app->sv.waterfall_capacity
                       ? app->sv.waterfall_rows
                       : app->sv.waterfall_capacity - 1;
    if (retained > 0)
        memmove(app->sv.waterfall_dbfs + SDR_DSP_FFT_MAX,
                app->sv.waterfall_dbfs,
                (size_t)retained * SDR_DSP_FFT_MAX *
                    sizeof(*app->sv.waterfall_dbfs));
    memcpy(app->sv.waterfall_dbfs, app->spectrum_average,
           (size_t)app->spectrum_bins *
           sizeof(*app->sv.waterfall_dbfs));
    if (app->sv.waterfall_rows < app->sv.waterfall_height)
        app->sv.waterfall_rows++;
    render_waterfall(app);
}

void draw_waterfall_rect(const struct app *app, int calibration_mode,
                                Rectangle rect, double zoom_center_hz) {
    struct sdrgui_waterfall_params params = {
        rect, app->sv.waterfall, (double)app->applied_frequency,
        (double)app->applied_sample_rate, calibration_mode,
        calibration_mode && app->calibration_technology == 0,
        zoom_center_hz, CALIBRATION_VIEW_HALF_WIDTH_HZ,
        app->sv.waterfall_rows, app->sv.waterfall_height, app->pair_count,
        SAMPLE_BLOCK_PAIRS, app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS,
        GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ, 124,
        "ARFCN", "GSM 900 ARFCN (200 kHz spacing)", "outside GSM 900"
    };
    sdrgui_waterfall(&params);
}

/*
 * The Scope's waterfall, into the Scope's plot.
 *
 * It used to take a calibration flag, and the calibration overlay used it --
 * which drew that overlay's waterfall into app->plot, a rectangle 52 px above
 * the one the overlay's own layout had set aside. The result was a waterfall
 * over the status line, and expected/measured markers placed against a chart
 * that was somewhere else. The flag is gone rather than fixed: every other
 * view already passes its own rectangle, so with no flag left there is no way
 * to draw a waterfall anywhere but where a layout put it.
 */
void draw_waterfall(const struct app *app) {
    const struct freq_window *w = &app->sv.freq;
    double span = w->view_upper_hz - w->view_lower_hz;
    double data = w->data_upper_hz - w->data_lower_hz;
    struct sdrgui_waterfall_params params = {
        app->plot, app->sv.waterfall, (double)app->applied_frequency,
        (double)app->applied_sample_rate, 0, 0,
        0.0, 0.0,
        app->sv.waterfall_rows, app->sv.waterfall_height, app->pair_count,
        SAMPLE_BLOCK_PAIRS, app->waterfall_lower_dbfs, SPECTRUM_TOP_DBFS,
        GSM900_BASE_HZ, GSM900_ARFCN_SPACING_HZ, 124,
        "ARFCN", "GSM 900 ARFCN (200 kHz spacing)", "outside GSM 900"
    };

    /* The component already knows how to draw part of its span -- the
       calibration overlay has always used it to hold one channel on screen.
       A zoomed window is the same request with different numbers. */
    if (span > 0.0 && data > 0.0 && span < data - 1.0) {
        params.zoom_center_hz = (w->view_lower_hz + w->view_upper_hz) / 2.0;
        params.zoom_half_width_hz = span / 2.0;
    }
    sdrgui_waterfall(&params);
}

void update_scatter(struct app *app, double now, int insert) {
    if (insert && app->pair_count > 0) {
        struct scatter_block *block =
            &app->sv.scatter_history[app->sv.scatter_history_head];
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
        app->sv.scatter_inserted = block->count;
        app->sv.scatter_history_head =
            (app->sv.scatter_history_head + 1) % SCATTER_HISTORY_BLOCKS;
        if (app->sv.scatter_history_count < SCATTER_HISTORY_BLOCKS)
            app->sv.scatter_history_count++;
    }

    while (app->sv.scatter_history_count > 0) {
        size_t oldest = (app->sv.scatter_history_head + SCATTER_HISTORY_BLOCKS -
                         app->sv.scatter_history_count) %
                        SCATTER_HISTORY_BLOCKS;
        if (now - app->sv.scatter_history[oldest].time <=
            SCATTER_HISTORY_SECONDS)
            break;
        app->sv.scatter_history_count--;
    }

    size_t oldest = (app->sv.scatter_history_head + SCATTER_HISTORY_BLOCKS -
                     app->sv.scatter_history_count) %
                    SCATTER_HISTORY_BLOCKS;

    BeginTextureMode(app->sv.scatter);
    ClearBackground(BLANK);
    for (size_t b = 0; b < app->sv.scatter_history_count; b++) {
        const struct scatter_block *block =
            &app->sv.scatter_history[(oldest + b) % SCATTER_HISTORY_BLOCKS];
        float age = (float)(now - block->time);
        float age_alpha = 1.0f - age / (float)SCATTER_HISTORY_SECONDS;
        if (age_alpha < 0.0f)
            age_alpha = 0.0f;
        for (size_t n = 0; n < block->count; n++) {
            float radial = hypotf(block->i[n], block->q[n]) /
                           app->sv.scatter_axis_limit;
            if (radial > 1.0f)
                radial = 1.0f;
            float emphasis = sqrtf(radial);
            float x = (block->i[n] / app->sv.scatter_axis_limit + 1.0f) *
                      0.5f * (float)(app->sv.scatter.texture.width - 1);
            float y = (1.0f - block->q[n] / app->sv.scatter_axis_limit) *
                      0.5f * (float)(app->sv.scatter.texture.height - 1);
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

/* Room for the HUD, which shares its rows with the buttons at the right edge. */
static float hud_width(void) {
    float limit = chrome_layout_now().status_left - 22.0f;
    return limit > 0.0f ? limit : 0.0f;
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
    sdrgui_text_fit(text, 22, 78, 17, hud_width(), (Color){ 187, 205, 216, 255 });
    snprintf(text, sizeof(text),
              "center: %.6f MHz   rate: %u S/s   gain: %s   PPM: %+d   DC filter: %s   view: %s   FPS: %d",
              app->applied_frequency / 1000000.0, app->applied_sample_rate,
              gain, app->applied_ppm, app->remove_dc ? "on" : "off",
              view_name(app->view), GetFPS());
    sdrgui_text_fit(text, 22, 103, 17, hud_width(), (Color){ 187, 205, 216, 255 });
    snprintf(text, sizeof(text),
             "blocks published: %llu   processed: %llu   overwritten: %llu   malformed: %llu   worker: %s",
             (unsigned long long)snapshot->published_blocks,
             (unsigned long long)snapshot->processed_blocks,
             (unsigned long long)snapshot->overwritten_blocks,
             (unsigned long long)snapshot->malformed_blocks, state);
    sdrgui_text_fit(text, 22, 128, 17, hud_width(), (Color){ 187, 205, 216, 255 });

    if (snapshot->worker_failed) {
        snprintf(text, sizeof(text), "acquisition error: %s",
                 snapshot->worker_error);
        sdrgui_text_fit(text, 22, 154, 17, hud_width(), (Color){ 255, 104, 104, 255 });
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
        sdrgui_text_fit(text, 22, 154, 17, hud_width(), quality_color);
    }
}

void draw_magnitude(const struct app *app) {
    double duration_ms = app->have_samples
                             ? (double)app->pair_count * 1000.0 /
                                   app->applied_sample_rate
                             : 0.0;
    struct sdrgui_magnitude_params params = {
        app->plot, app->have_samples, app->sv.magnitude_peaks,
        app->sv.magnitude_bin_count, app->sv.magnitude_lower, app->sv.magnitude_upper,
        app->magnitude_min, app->magnitude_mean, app->magnitude_max,
        duration_ms, PHYSICAL_MAGNITUDE_MAX
    };
    sdrgui_magnitude(&params);
}

void draw_spectrum(const struct app *app) {
    const struct freq_window *w = &app->sv.freq;
    struct sdrgui_spectrum_params params;
    double span = w->view_upper_hz - w->view_lower_hz;
    double data = w->data_upper_hz - w->data_lower_hz;

    memset(&params, 0, sizeof(params));
    params.plot = app->plot;
    params.center_hz = (double)app->applied_frequency;
    params.sample_rate = (double)app->applied_sample_rate;
    params.ready = app->spectrum_ready;
    params.average = app->spectrum_average;
    params.peak = app->spectrum_peak;
    params.bins = app->spectrum_bins;
    params.lower_dbfs = app->sv.spectrum_lower_dbfs;
    params.top_dbfs = SPECTRUM_TOP_DBFS;
    params.windows = app->spectrum_windows;
    if (span > 0.0 && data > 0.0 && span < data - 1.0) {
        params.view_lower_hz = w->view_lower_hz;
        params.view_upper_hz = w->view_upper_hz;
    }
    /* The region being dragged out, drawn over the trace so a reader can see
       what they are about to select rather than what they selected. */
    if (app->sv.freq_dragging) {
        double a = freq_window_hz_at(w, app->plot.x, app->plot.width,
                                     app->sv.freq_drag_from_x);
        double b = freq_window_hz_at(w, app->plot.x, app->plot.width,
                                     app->sv.freq_drag_to_x);
        params.drag_active = 1;
        params.drag_lower_hz = a < b ? a : b;
        params.drag_upper_hz = a < b ? b : a;
    }
    sdrgui_spectrum(&params);
}

void draw_scatter(const struct app *app) {
    struct sdrgui_scatter_params params = {
        app->plot, app->sv.scatter.texture, app->sv.scatter_axis_limit,
        app->sv.scatter_inserted
    };
    sdrgui_scatter(&params);
}
void recompute_magnitude_bins(struct app *app) {
    size_t capacity;

    if (!app->have_samples || app->pair_count == 0) {
        app->sv.magnitude_bin_count = 0;
        return;
    }
    capacity = app->plot.width > 1.0f ? (size_t)app->plot.width : 1;
    if (capacity > SAMPLE_BLOCK_PAIRS)
        capacity = SAMPLE_BLOCK_PAIRS;
    app->sv.magnitude_bin_count = sdr_dsp_peak_bins(
        app->magnitudes, app->pair_count, app->sv.magnitude_peaks, capacity);
}

void decay_spectrum_peak(struct app *app, double now) {
    if (!app->spectrum_peak_ready) {
        app->spectrum_peak_time = now;
        return;
    }
    double elapsed = now - app->spectrum_peak_time;
    if (elapsed <= 0.0)
        return;
    float decay = (float)elapsed * PEAK_DECAY_DB_PER_SECOND;
    for (int i = 0; i < app->spectrum_bins; i++)
        app->spectrum_peak[i] = fmaxf(SDR_DSP_DBFS_FLOOR,
                                     app->spectrum_peak[i] - decay);
    app->spectrum_peak_time = now;
}

void adjust_active_scale(struct app *app, int zoom_in) {
    if (app->view == VIEW_MAGNITUDE) {
        app->sv.magnitude_upper *= zoom_in ? SCALE_FACTOR : 1.0f / SCALE_FACTOR;
        app->sv.magnitude_upper = fmaxf(1.0f,
                                     fminf(app->sv.magnitude_upper,
                                           PHYSICAL_MAGNITUDE_MAX));
    } else if (app->view == VIEW_SPECTRUM) {
        app->sv.spectrum_lower_dbfs += zoom_in ? DB_SCALE_STEP : -DB_SCALE_STEP;
        app->sv.spectrum_lower_dbfs = fmaxf(
            SDR_DSP_DBFS_FLOOR,
            fminf(app->sv.spectrum_lower_dbfs, SPECTRUM_TOP_DBFS - 20.0f));
    } else if (app->view == VIEW_SCATTER) {
        app->sv.scatter_axis_limit *= zoom_in ? SCALE_FACTOR : 1.0f / SCALE_FACTOR;
        app->sv.scatter_axis_limit = fmaxf(0.01f,
                                        fminf(app->sv.scatter_axis_limit, 1.0f));
    } else {
        adjust_waterfall_scale(app, zoom_in);
    }
}


/* The scales each view starts at. */
void view_scope_defaults(struct app *app) {
    app->sv.fft_size = SDR_DSP_FFT_SIZE;
    app->sv.magnitude_lower = 0.0f;
    app->sv.magnitude_upper = 64.0f;
    app->sv.spectrum_lower_dbfs = SDR_DSP_DBFS_FLOOR;
    app->sv.scatter_axis_limit = 0.5f;
    app->waterfall_lower_dbfs = SDR_DSP_DBFS_FLOOR;
}

/* The scatter render texture and the waterfall texture track the plot
   rectangle, so they are rebuilt when it changes. The frame loop used to
   compare their dimensions itself, which meant it had to know that the
   scatter's size lives on a RenderTexture and the waterfall's in two ints.
   Returns negative if a texture could not be created. */
int view_scope_resize_if_needed(struct app *app, Rectangle plot) {
    int resized = IsWindowResized() ||
                  (int)plot.width != app->sv.scatter.texture.width ||
                  (int)plot.height != app->sv.scatter.texture.height ||
                  (int)plot.width != app->sv.waterfall_width ||
                  (int)plot.height != app->sv.waterfall_height;
    if (!resized) {
        app->plot = plot;
        /* Retuning does not rebuild the waterfall itself -- tuning a receiver
           is not a drawing operation, and it happens on paths that have no
           window at all. What it leaves behind is a history gathered at
           another frequency, which the view throws away here. */
        if (app->sv.waterfall_tuned_hz != app->applied_frequency)
            return recreate_waterfall(app, plot, 1);
        return 0;
    }
    if (recreate_scatter(app, plot) < 0)
        return -1;
    if (recreate_waterfall(app, plot, 0) < 0)
        return -1;
    recompute_magnitude_bins(app);
    return 0;
}

/* Release the GPU textures and history buffers. Safe before they exist. */
void view_scope_release(struct app *app) {
    if (app->sv.scatter_ready) {
        UnloadRenderTexture(app->sv.scatter);
        app->sv.scatter_ready = 0;
    }
    if (app->sv.waterfall_ready) {
        UnloadTexture(app->sv.waterfall);
        app->sv.waterfall_ready = 0;
    }
    free(app->sv.waterfall_pixels);
    app->sv.waterfall_pixels = NULL;
    free(app->sv.waterfall_dbfs);
    app->sv.waterfall_dbfs = NULL;
}

/*
 * The frequency window the spectrum and the waterfall share.
 *
 * `data` is what is arriving -- the tuning plus or minus half the sample rate
 * -- and it moves whenever the receiver does, so it is re-anchored here
 * rather than remembered. `view` is the part of it drawn.
 */
void scope_freq_sync(struct app *app) {
    struct scope_view *sv = &app->sv;
    double half = (double)app->applied_sample_rate / 2.0;
    double lower = (double)app->applied_frequency - half;
    double upper = (double)app->applied_frequency + half;

    if (sv->freq_anchor_hz != app->applied_frequency ||
        sv->freq_anchor_rate != app->applied_sample_rate) {
        /*
         * The receiver moved. Keep the *width* the reader chose and carry it
         * to the new tuning rather than throwing the zoom away: retuning is
         * how panning continues past the edge of the span, and a zoom that
         * reset itself every time would make that unusable.
         */
        double span = sv->freq.view_upper_hz - sv->freq.view_lower_hz;
        double centre = (sv->freq.view_upper_hz + sv->freq.view_lower_hz) / 2.0;
        double shift = (double)app->applied_frequency -
                       (double)sv->freq_anchor_hz;

        sv->freq.data_lower_hz = lower;
        sv->freq.data_upper_hz = upper;
        if (sv->freq_anchor_rate == 0 || span <= 0.0 ||
            span >= (double)sv->freq_anchor_rate) {
            freq_window_reset(&sv->freq);
        } else {
            centre += shift;
            sv->freq.view_lower_hz = centre - span / 2.0;
            sv->freq.view_upper_hz = centre + span / 2.0;
            freq_window_clamp(&sv->freq, SCOPE_FREQ_MIN_SPAN_HZ);
        }
        sv->freq_anchor_hz = app->applied_frequency;
        sv->freq_anchor_rate = app->applied_sample_rate;
        return;
    }
    sv->freq.data_lower_hz = lower;
    sv->freq.data_upper_hz = upper;
    if (sv->freq.view_upper_hz <= sv->freq.view_lower_hz)
        freq_window_reset(&sv->freq);
}

/* What the two charts are showing, for the header to say and for anything
   that draws against it. */
void scope_freq_range(const struct app *app, double *lower, double *upper) {
    if (lower)
        *lower = app->sv.freq.view_lower_hz;
    if (upper)
        *upper = app->sv.freq.view_upper_hz;
}

/*
 * Drag to zoom, Left and Right to pan, 0 to put it back.
 *
 * Returns non-zero when the receiver has to move -- which happens only when a
 * pan has run out of received span, never from a drag. Looking closer at
 * something already arriving cannot cost you the samples either side of it.
 */
int scope_freq_input(struct app *app, Rectangle plot) {
    struct scope_view *sv = &app->sv;
    Vector2 mouse = GetMousePosition();
    int retune = 0;

    scope_freq_sync(app);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, plot)) {
        sv->freq_dragging = 1;
        sv->freq_drag_from_x = mouse.x;
        sv->freq_drag_to_x = mouse.x;
    } else if (sv->freq_dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        sv->freq_drag_to_x = mouse.x;
    } else if (sv->freq_dragging) {
        double lower = 0.0, upper = 0.0;

        sv->freq_dragging = 0;
        if (freq_window_drag(&sv->freq, plot.x, plot.width,
                             sv->freq_drag_from_x, sv->freq_drag_to_x,
                             SCOPE_FREQ_MIN_SPAN_HZ, &lower, &upper)) {
            sv->freq.view_lower_hz = lower;
            sv->freq.view_upper_hz = upper;
            freq_window_clamp(&sv->freq, SCOPE_FREQ_MIN_SPAN_HZ);
        }
    }

    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT) ||
        IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        double direction = (IsKeyPressed(KEY_RIGHT) ||
                            IsKeyPressedRepeat(KEY_RIGHT)) ? 1.0 : -1.0;
        double over = freq_window_pan_overflow(&sv->freq,
                                               direction * SCOPE_FREQ_PAN,
                                               SCOPE_FREQ_MIN_SPAN_HZ);
        if (over != 0.0 && app->receiver_mode) {
            double want = (double)app->applied_frequency + over;
            if (want > 0.0 &&
                retune_receiver(app, (uint32_t)llround(want),
                                app->applied_ppm) == 0)
                retune = 1;
        }
    }
    return retune;
}

/* Back to the whole of what is arriving. */
void scope_freq_reset(struct app *app) {
    scope_freq_sync(app);
    freq_window_reset(&app->sv.freq);
}
