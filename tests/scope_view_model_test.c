#include "check.h"

#include "app.h"
#include "device_profile.h"
#include "scope_view_model.h"

#include <string.h>

/*
 * `struct app` is nearly 9 MB, so it is never a stack local or a by-value
 * return here -- a single `static struct app app;` is zeroed explicitly at
 * the top of each test.
 */
static void zero_app(struct app *app) {
    memset(app, 0, sizeof(*app));
}

static void test_measurements_pass_through(void) {
    static struct app app;

    zero_app(&app);
    app.frame.have_samples = 1;
    app.frame.pair_count = 131072;
    app.frame.magnitude_min = 1.0f;
    app.frame.magnitude_mean = 20.0f;
    app.frame.magnitude_max = 90.0f;
    app.frame.magnitudes[0] = 5.5f;
    app.frame.signal_stats_ready = 1;
    app.frame.signal_stats.snr_db = 12.5f;
    app.frame.signal_stats.clipping_percent = 0.25f;
    app.frame.spectrum_ready = 1;
    app.frame.spectrum_bins = 2048;
    app.frame.spectrum_windows = 4;
    app.frame.spectrum_average[3] = -42.0f;
    app.frame.spectrum_peak[3] = -20.0f;
    app.applied.frequency_hz = 948400000;
    app.applied.sample_rate_hz = 2000000;
    app.applied.ppm = 7;
    app.applied.generation = 3;
    app.device.full_scale = 127.5f;

    struct scope_view_model svm;
    scope_view_model_build(&app, &svm);

    check_int("have_samples passes through", svm.have_samples, 1);
    check_int("center_hz is the applied frequency", (long)svm.center_hz,
              948400000);
    check_int("sample_rate_hz is the applied rate", (long)svm.sample_rate_hz,
              2000000);
    check_int("ppm passes through", svm.ppm, 7);
    check_int("tuning generation passes through",
              (long)svm.tuning_generation, 3);
    check_close("full_scale passes through", svm.full_scale, 127.5, 1e-6);
    check_close("physical_magnitude_max matches device_magnitude_max()",
                svm.physical_magnitude_max,
                device_magnitude_max(&app.device), 1e-6);

    check_int("spectrum_ready passes through", svm.spectrum_ready, 1);
    check_int("spectrum_bins passes through", svm.spectrum_bins, 2048);
    check_int("spectrum_windows passes through", svm.spectrum_windows, 4);
    check_true("spectrum_average aliases the frame's array",
              svm.spectrum_average == app.frame.spectrum_average);
    check_true("spectrum_peak aliases the frame's array",
              svm.spectrum_peak == app.frame.spectrum_peak);
    check_close("a spectrum_average sample reads through the alias",
                svm.spectrum_average[3], -42.0, 1e-6);
    check_close("a spectrum_peak sample reads through the alias",
                svm.spectrum_peak[3], -20.0, 1e-6);

    check_true("magnitudes aliases the frame's array",
              svm.magnitudes == app.frame.magnitudes);
    check_close("a magnitudes sample reads through the alias",
                svm.magnitudes[0], 5.5, 1e-6);
    check_size("pair_count passes through", svm.pair_count, 131072);
    check_close("magnitude_min passes through", svm.magnitude_min, 1.0, 1e-6);
    check_close("magnitude_mean passes through", svm.magnitude_mean, 20.0, 1e-6);
    check_close("magnitude_max passes through", svm.magnitude_max, 90.0, 1e-6);
    check_close("duration_ms is pair_count over the applied rate",
                svm.duration_ms, 131072.0 * 1000.0 / 2000000.0, 1e-6);

    check_int("signal_stats_ready passes through", svm.signal_stats_ready, 1);
    check_close("signal_stats is a copy, not a pointer",
                svm.signal_stats.snr_db, 12.5, 1e-6);
    check_close("every signal_stats field copies",
                svm.signal_stats.clipping_percent, 0.25, 1e-6);
}

/* A block with nothing measured yet: duration is zero rather than a
   division against a rate nothing was measured at. */
static void test_no_samples_yet(void) {
    static struct app app;

    zero_app(&app);
    app.applied.sample_rate_hz = 2000000;
    app.frame.pair_count = 131072; /* stale from a previous block */

    struct scope_view_model svm;
    scope_view_model_build(&app, &svm);

    check_int("have_samples is false", svm.have_samples, 0);
    check_close("duration_ms is zero with no samples", svm.duration_ms, 0.0,
                1e-9);
}

/* The waterfall row is the ring's front slot, and only trustworthy when the
   ring itself is ready -- advance_waterfall_row() (view_scope.c) always
   writes the newest row at index 0. */
static void test_waterfall_row_is_the_rings_front(void) {
    static struct app app;
    static float ring[SDR_DSP_FFT_MAX * 4];

    zero_app(&app);
    app.sv.waterfall_ready = 1;
    app.sv.waterfall_dbfs = ring;
    ring[0] = -30.5f;

    struct scope_view_model svm;
    scope_view_model_build(&app, &svm);

    check_int("waterfall_ready passes through", svm.waterfall_ready, 1);
    check_true("waterfall_row aliases the ring's front",
              svm.waterfall_row == app.sv.waterfall_dbfs);
    check_close("the front slot reads through the alias", svm.waterfall_row[0],
                -30.5, 1e-6);
}

static void test_waterfall_not_ready(void) {
    static struct app app;

    zero_app(&app);

    struct scope_view_model svm;
    scope_view_model_build(&app, &svm);

    check_int("waterfall_ready is false with no ring", svm.waterfall_ready, 0);
}

/* An empty scatter history is not a wraparound case: no block has ever been
   inserted, so there is nothing to point at. */
static void test_scatter_empty_history(void) {
    static struct app app;

    zero_app(&app);

    struct scope_view_model svm;
    scope_view_model_build(&app, &svm);

    check_true("scatter_i is null with no history", svm.scatter_i == NULL);
    check_true("scatter_q is null with no history", svm.scatter_q == NULL);
    check_size("scatter_count is zero with no history", svm.scatter_count, 0);
}

/* advance_scatter_history() (view_scope.c) writes the newest block at
   scatter_history_head and then advances it, so the block just inserted is
   one slot behind the head, with wraparound at either end of the ring. */
static void test_scatter_newest_block_no_wrap(void) {
    static struct app app;

    zero_app(&app);
    app.sv.scatter_history_count = 3;
    app.sv.scatter_history_head = 3; /* three inserted, none wrapped yet */
    app.sv.scatter_history[2].count = 5;
    app.sv.scatter_history[2].i[0] = 0.25f;
    app.sv.scatter_history[2].q[0] = -0.5f;

    struct scope_view_model svm;
    scope_view_model_build(&app, &svm);

    check_size("scatter_count is the newest block's count",
              svm.scatter_count, 5);
    check_true("scatter_i aliases the newest block",
              svm.scatter_i == app.sv.scatter_history[2].i);
    check_close("a scatter_i sample reads through the alias", svm.scatter_i[0],
                0.25, 1e-6);
    check_close("a scatter_q sample reads through the alias", svm.scatter_q[0],
                -0.5, 1e-6);
}

static void test_scatter_newest_block_wraps(void) {
    static struct app app;

    zero_app(&app);
    app.sv.scatter_history_count = SCATTER_HISTORY_BLOCKS;
    app.sv.scatter_history_head = 0; /* head wrapped back to the start */
    app.sv.scatter_history[SCATTER_HISTORY_BLOCKS - 1].count = 9;

    struct scope_view_model svm;
    scope_view_model_build(&app, &svm);

    check_size("scatter_count follows the block at the ring's far end",
              svm.scatter_count, 9);
    check_true("scatter_i aliases the ring's last slot",
              svm.scatter_i == app.sv.scatter_history[SCATTER_HISTORY_BLOCKS - 1].i);
}

int main(void) {
    test_measurements_pass_through();
    test_no_samples_yet();
    test_waterfall_row_is_the_rings_front();
    test_waterfall_not_ready();
    test_scatter_empty_history();
    test_scatter_newest_block_no_wrap();
    test_scatter_newest_block_wraps();
    return check_report("the Scope's view model, built from known inputs");
}
