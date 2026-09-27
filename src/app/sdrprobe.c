#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <raylib.h>
#include <rlgl.h>        /* rlDrawRenderBatchActive, for --screenshot */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/stat.h>
#include <time.h>

#include "runtime/frame_advance.h"
#include "server/viewer_session.h"
#include "core/sdr_dsp.h"
#include "tech/gsm_dsp.h"
#include "tech/adsb_dsp.h"
#include "runtime/options.h"
#include "gui/gsm_layout.h"
#include "runtime/app.h"
#include "core/capture_sidecar.h"
#include "runtime/version.h"
#include "gui/chrome_layout.h"
#include "gui/scope_layout.h"
#include "gui/sdrgui.h"
#include "runtime/view_input.h"
#include "gui/view.h"
#include "tech/tetra_dsp.h"
#include "tech/tetra_sync.h"
#include "tech/lte_chain_analysis.h"
#include "tech/lte_confirm.h"
#include "tech/lte_stats.h"
#include "tech/lte_findings.h"
#include "runtime/debug_log.h"
#include "gui/overlay_signal_report.h"

/* input_route.h mirrors this so it can stay standalone; if that enum is
   reordered this stops the build rather than misrouting a key. */
typedef char input_route_adsb_matches[
    (DECODE_KIND_ADSB == (int)DECODE_ADSB) ? 1 : -1];
typedef char input_route_spectrum_matches[
    (VIEW_KIND_SPECTRUM == (int)VIEW_SPECTRUM) ? 1 : -1];
/* And view_input.h mirrors the Scope's "no field has focus" for the same
   reason: it decides whether the header is taking typed characters. */
typedef char view_input_scope_field_none_matches[
    (VIEW_INPUT_SCOPE_FIELD_NONE == SCOPE_FIELD_NONE) ? 1 : -1];
#include "raygui.h"














int clicked(Rectangle rectangle) {
    return CheckCollisionPointRec(GetMousePosition(), rectangle) &&
           IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}


static void configure_gui_style(void) {
    GuiSetStyle(DEFAULT, TEXT_SIZE, 17);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,
                ColorToInt((Color){ 10, 18, 28, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,
                ColorToInt((Color){ 29, 43, 54, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,
                ColorToInt((Color){ 44, 62, 75, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,
                ColorToInt((Color){ 44, 62, 75, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL,
                ColorToInt((Color){ 91, 117, 132, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED,
                ColorToInt((Color){ 255, 201, 103, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED,
                ColorToInt((Color){ 255, 201, 103, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,
                ColorToInt((Color){ 235, 242, 246, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED,
                ColorToInt((Color){ 255, 255, 255, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED,
                ColorToInt((Color){ 255, 255, 255, 255 }));
    /*
     * Disabled has to read as *quieter* than normal, and raygui's stock
     * disabled is a pale fill -- which on this dark theme made a dead button
     * the loudest thing on the row. These are the normal colours dimmed
     * rather than a separate grey, so a dim control still looks like the
     * control it is.
     */
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED,
                ColorToInt((Color){ 22, 32, 40, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED,
                ColorToInt((Color){ 55, 72, 84, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED,
                ColorToInt((Color){ 92, 111, 124, 255 }));
    GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
}

/* Render-only button: raygui draws it (themed); the click action is dispatched
   from the input phase via clicked(), so heavy actions never run mid-frame. */
/*
 * A button that says whether it can be pressed.
 *
 * Calibration's Back is dim at the top of its own stack, which is the only
 * honest way to draw a control that will do nothing: the alternative is a
 * button that looks live and swallows the click, and an operator who concludes
 * the screen is stuck.
 */
void draw_button_enabled(Rectangle rectangle, const char *label, int enabled) {
    if (!enabled)
        GuiSetState(STATE_DISABLED);
    GuiButton(rectangle, label);
    if (!enabled)
        GuiSetState(STATE_NORMAL);
}

void draw_button(Rectangle rectangle, const char *label, int primary) {
    if (primary) {
        GuiSetStyle(BUTTON, BASE_COLOR_NORMAL,
                    ColorToInt((Color){ 191, 111, 25, 255 }));
        GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED,
                    ColorToInt((Color){ 220, 142, 38, 255 }));
        GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL,
                    ColorToInt((Color){ 255, 201, 103, 255 }));
    }
    GuiButton(rectangle, label);
    if (primary) {
        GuiSetStyle(BUTTON, BASE_COLOR_NORMAL,
                    ColorToInt((Color){ 29, 43, 54, 255 }));
        GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED,
                    ColorToInt((Color){ 44, 62, 75, 255 }));
        GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL,
                    ColorToInt((Color){ 91, 117, 132, 255 }));
    }
}













/* --- Top-level tabs (Scope / Decode) --- */

static const char *tab_labels[TAB_COUNT] = { "Survey", "Scope",
                                             "Decode" };

static Rectangle tab_rect(int index) {
    struct chrome_layout chrome = chrome_layout_now();
    return chrome_tab_rect(&chrome, index);
}

/* A tab button with bright, prominent text and a clear active state. */
static void draw_tab(Rectangle rect, const char *label, int active) {
    Color fill = active ? (Color){ 191, 111, 25, 255 }
                        : (Color){ 27, 41, 52, 255 };
    Color border = active ? (Color){ 255, 201, 103, 255 }
                          : (Color){ 96, 124, 141, 255 };
    Color text = active ? (Color){ 255, 244, 224, 255 }
                        : (Color){ 214, 227, 236, 255 };
    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, active ? 2.0f : 1.0f, border);
    int font = 22;
    int tw = MeasureText(label, font);
    DrawText(label, (int)(rect.x + (rect.width - tw) / 2.0f),
             (int)(rect.y + (rect.height - font) / 2.0f), font, text);
}

static void draw_tab_bar(const struct app *app) {
    for (int i = 0; i < TAB_COUNT; i++)
        draw_tab(tab_rect(i), tab_labels[i], (int)app->tab == i);
}





/* One row of numbered mode options, the active one highlighted. */
/*
 * The numbered options, and the key hints after them.
 *
 * The hints are given twice, long and short, because the row shares its line
 * with the tab bar and there is no clipping between them: a suffix that does
 * not fit is simply drawn over the tabs. That was invisible while every
 * suffix was short, and arrived the moment the Scope's grew to name the zoom
 * keys. Long if it fits, short if only that fits, nothing if neither does --
 * the numbered options are what the row is for, and they are never the part
 * that gets dropped.
 */
static void draw_option_row(int active, const char **labels, int count,
                            const char *suffix, const char *short_suffix) {
    struct chrome_layout chrome = chrome_layout_now();
    int x = (int)chrome.option_row_left;
    const int y = (int)chrome.option_row_y;
    float limit = chrome_tab_rect(&chrome, 0).x - 12.0f;

    for (int i = 0; i < count; i++) {
        Color color = (i == active) ? (Color){ 255, 201, 103, 255 }
                                    : (Color){ 150, 172, 188, 255 };
        DrawText(labels[i], x, y, 18, color);
        x += MeasureText(labels[i], 18) + 26;
    }
    if (suffix && suffix[0] && (float)(x + MeasureText(suffix, 18)) > limit)
        suffix = short_suffix;
    if (suffix && suffix[0] && (float)(x + MeasureText(suffix, 18)) > limit)
        return;
    if (suffix && suffix[0])
        DrawText(suffix, x, y, 18, (Color){ 110, 132, 150, 255 });
}

/* The uniform application header, identical on every tab: application name
   (top-left), the current tab's numbered options (below it), and the buttons
   (tabs, Settings, Calibration, health) on the right. The mode display renders
   below it. */

static void draw_header(const struct app *app) {
    static const char *scope_opts[3] = {
        "1 magnitude", "2 spectrum + waterfall", "3 I/Q scatter"
    };
    static const char *decode_opts[6] = { "1 FM", "2 ADS-B", "3 GSM",
                                          "4 LTE", "5 TETRA", "6 SRD" };

    DrawText("sdrprobe signal visualizer", 22, 14, 24,
             (Color){ 225, 236, 245, 255 });
    if (app->tab == TAB_SURVEY) {
        /*
         * No numbered options. The survey has one screen, and the three the
         * Scope tab numbers are not alternatives to it -- listing them here
         * offered a reader keys that would take them somewhere else
         * entirely.
         */
        struct chrome_layout chrome = chrome_layout_now();
        DrawText("+/- zoom   Left/Right pan   Up/Down candidate   h help"
                 "   Esc quit", (int)chrome.option_row_left,
                 (int)chrome.option_row_y, 16, (Color){ 143, 167, 182, 255 });
    } else if (app->tab == TAB_SCOPE) {
        draw_option_row((int)app->view, scope_opts, 3,
                        app->view == VIEW_SPECTRUM
                            ? "drag/Up/Down zoom  Left/Right pan  +/- scale"
                              "  0 reset  h help  Esc quit"
                            : "+/- scale   h help   Esc quit",
                        "h help  Esc quit");
    } else {
        draw_option_row((int)app->decode, decode_opts, 6,
                        app->decode == DECODE_ADSB
                            ? "h help   Esc scope"
                            : "drag/Up/Down zoom  Left/Right pan  +/- scale"
                              "  0 reset  h help  Esc scope",
                        "h help  Esc scope");
    }

    /* The corner, on every screen: who to write to and which build this is.
       Dim enough to be ignorable and present enough to be quotable in a bug
       report, which is the whole job. */
    {
        struct chrome_layout chrome = chrome_layout_now();
        int w = MeasureText(SDRPROBE_SIGNATURE, 14);
        DrawText(SDRPROBE_SIGNATURE,
                 (int)(chrome.footer.x + chrome.footer.width - (float)w),
                 (int)chrome.footer.y, 14, (Color){ 88, 108, 122, 255 });
    }

    draw_tab_bar(app);
    draw_button(settings_button(), "Settings", 0);
    draw_button(calibration_button(), "Calibration", 0);
    draw_health_indicator(app);
}



/*
 * What is on screen, for the log. Read from the same fields the drawing reads
 * so the two cannot describe different screens.
 */
static struct debug_screen debug_screen_now(const struct app *app) {
    struct debug_screen s;

    memset(&s, 0, sizeof(s));
    s.tab = (int)app->tab;
    s.view = (int)app->view;
    s.decode = (int)app->decode;
    s.settings_open = app->set.open;
    s.calibration_open = app->cal.open;
    s.scan_open = app->bandscan.open;
    s.help_open = app->help.open;
    /* The same question the routing asks, from the same answer -- these were
       two lists and they already disagreed: the survey's band menu was in one
       and not the other, and the waterfall's was in neither. */
    {
        struct view_input v = view_input_now(app);

        s.menu_open = view_input_menu_open(&v);
    }
    s.analysis = app->adsb.analysis_mode || app->lte.analysis_mode ||
                 app->gsm.analysis_mode;
    return s;
}

/*
 * Every key the window received this frame, and where the router sent it.
 *
 * GetKeyPressed drains a queue raylib fills alongside the state IsKeyPressed
 * reads, so taking from it costs the handlers nothing. It is the only way to
 * see a key the program received and then ignored -- which is the difference
 * between "the key did not arrive" and "the key arrived and nothing was bound
 * to it", and those are two different bugs that look the same.
 */
static void debug_log_frame_keys(const struct app *app,
                                 const struct input_state *input) {
    int key;

    if (!debug_log_active())
        return;
    while ((key = GetKeyPressed()) != 0) {
        struct debug_screen screen = debug_screen_now(app);
        char where[64];

        debug_screen_describe(&screen, where, sizeof(where));
        debug_log_write("key", "%s -> %s on %s",
                        debug_key_name(key),
                        debug_target_name((int)input_route(input)), where);
    }
}

static int handle_tab_input(struct app *app) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (clicked(tab_rect(i))) {
            set_tab(app, i, GetTime());
            return 1;
        }
    }
    return 0;
}

/* --- Decode tab: numbered sub-views (1 FM, 2 ADS-B, 3 GSM, 4 LTE, 5 TETRA) --- */

/* --- GSM analysis view (band survey: channel scan + ARFCN waterfall) --- */



static void check_waterfall_right_click(struct app *app) {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        return;

    Rectangle rect = { 0, 0, 0, 0 };
    const struct chart_window *win = NULL;
    const char *tech = "raw";

    if (app->tab == TAB_SCOPE && app->view == VIEW_SPECTRUM) {
        rect = scope_plot_split(app->gui->plot).waterfall;
        win = &app->sv.window;
        tech = "scope";
    } else if (app->tab == TAB_DECODE) {
        if (app->decode == DECODE_SRD && !app->srd.analysis_mode) {
            rect = srd_waterfall_rect(app);
            win = &app->srd.window;
            tech = "srd";
        } else if (app->decode == DECODE_ADSB && !app->adsb.analysis_mode) {
            rect = adsb_waterfall_rect(app);
            win = &app->adsb.window;
            tech = "adsb";
        } else if (app->decode == DECODE_GSM && !app->gsm.analysis_mode) {
            rect = gsm_waterfall_rect();
            win = &app->gsm.window;
            tech = "gsm";
        } else if (app->decode == DECODE_LTE) {
            rect = lte_waterfall_rect(app);
            win = &app->lte.window;
            tech = "lte";
        } else if (app->decode == DECODE_FM) {
            rect = fm_waterfall_rect(app);
            win = &app->fm.window;
            tech = "fm";
        } else if (app->decode == DECODE_TETRA && !app->tetra.analysis_mode) {
            rect = tetra_waterfall_rect(app);
            win = &app->tetra.window;
            tech = "tetra";
        }
    }

    if (rect.width <= 0 || rect.height <= 0)
        return;

    Rectangle plot = sdrgui_waterfall_area(rect);
    Vector2 mouse = GetMousePosition();
    if (mouse.x < plot.x || mouse.x > plot.x + plot.width ||
        mouse.y < plot.y || mouse.y > plot.y + plot.height)
        return;

    float x_frac = (mouse.x - plot.x) / plot.width;
    float y_frac = (mouse.y - plot.y) / plot.height;

    double center_hz = (double)app->applied.frequency_hz;
    double span_hz = (double)app->applied.sample_rate_hz;
    double lower_hz = center_hz - span_hz / 2.0;
    double upper_hz = center_hz + span_hz / 2.0;

    if (win) {
        double zoom_c = 0.0, zoom_hw = 0.0;
        chart_window_zoom_of(win, &zoom_c, &zoom_hw);
        if (zoom_hw > 0.0) {
            lower_hz = zoom_c - zoom_hw;
            upper_hz = zoom_c + zoom_hw;
        }
    }

    double freq = lower_hz + (double)x_frac * (upper_hz - lower_hz);
    double row_seconds = app->frame.pair_count > 0
                             ? (double)app->frame.pair_count / (double)app->applied.sample_rate_hz
                             : (double)SAMPLE_BLOCK_PAIRS / (double)app->applied.sample_rate_hz;
    double visible_seconds = app->sv.waterfall_rows * row_seconds;
    double age = (double)y_frac * visible_seconds;

    waterfall_context_menu_open(app, mouse, freq, age, tech);
}

static int run_gui(struct app *app) {
    struct slot_snapshot snapshot;
    int result = 0;
    int break_requested = 0;
    /*
     * The window's own state, and the only place it is allocated. `headless`
     * and `server` leave `app->gui` NULL, which is what says they have no
     * window rather than a window with zeroed handles (`gui_state.h`).
     *
     * A file-scope static rather than a malloc for the reason every large
     * object in this program is: `struct gui_state` carries textures and a
     * 384-byte notice, and this file already keeps the acquisition and the
     * app itself off the stack.
     */
    static struct gui_state gui;

    memset(&gui, 0, sizeof(gui));
    app->gui = &gui;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1100, 720, "sdrprobe signal visualizer");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create raylib window.\n");
        return -1;
    }
    app->window_ready = 1;
    SetExitKey(KEY_NULL);
    SetWindowMinSize(1000, 540);
    SetTargetFPS(60);
    configure_gui_style();
    app->gui->plot = calculate_plot();
    if (recreate_scatter(app, app->gui->plot) < 0)
        return -1;
    if (recreate_waterfall(app, app->gui->plot, 1) < 0)
        return -1;

    sdr_dsp_init(&app->frame.dsp);
    /* The survey is where a session starts: "what is out there" comes before
       "what does this one look like", and the Scope views need somebody to
       have tuned the receiver first. TAB_SURVEY is 0, so this is also what
       zero-initialising gives -- said out loud rather than relied on. */
    app->tab = TAB_SURVEY;
    app->view = VIEW_MAGNITUDE;

    sigset_t worker_signals;
    sigset_t original_mask;
    sigemptyset(&worker_signals);
    sigaddset(&worker_signals, SIGINT);
    sigaddset(&worker_signals, SIGTERM);
    int mask_result = pthread_sigmask(SIG_BLOCK, &worker_signals,
                                      &original_mask);
    if (mask_result != 0) {
        fprintf(stderr, "Cannot block worker signals: %s\n",
                strerror(mask_result));
        return -1;
    }
    if (acquisition_attach_source(&app->acq, &app->source, app->capture,
                                  app->applied.sample_rate_hz,
                                  app->device.bytes_per_pair,
                                  app->options.file_path,
                                  !app->options.play_once) < 0) {
        /* Said rather than left blank: this was the one path here that
           returned -1 with no message, so the headless "Cannot start
           acquisition" fell through to "unknown" -- or to whatever an
           unrelated failure had left in the buffer. */
        snprintf(app->receiver_error, sizeof(app->receiver_error),
                 "Could not attach the source to the acquisition worker");
        return -1;
    }
    int thread_result = pthread_create(
        &app->acq.worker, NULL,
        app->receiver_mode ? receiver_worker : file_worker, &app->acq);
    if (thread_result == 0)
        app->acq.worker_started = 1;
    int restore_result = pthread_sigmask(SIG_SETMASK, &original_mask, NULL);
    if (restore_result != 0) {
        fprintf(stderr, "Cannot restore main-thread signal mask: %s\n",
                strerror(restore_result));
        request_worker_stop(&app->acq);
        return -1;
    }
    if (thread_result != 0) {
        fprintf(stderr, "Cannot start acquisition worker: %s\n",
                strerror(thread_result));
        return -1;
    }
    /* The starting screen is applied here, with the worker already running,
       because entering the GSM view is not a passive act: it retunes and can
       start a band scan, and retuning stops and restarts acquisition. Done
       before the worker existed, that left one worker started by the retune
       and a second started below, both reading the same device -- no blocks
       ever arrived and the shutdown join hung.

       The decode kind is set before the tab for a related reason: switching
       to the Decode tab first enters whichever kind is *already* recorded,
       and then leaves it again on the way to the one asked for -- retuning
       twice. This said "it defaults to GSM", which stopped being true when
       `enum decode_kind` put DECODE_FM first: zero-initialising gives FM,
       not GSM. The ordering argument never depended on which one it is. */
    switch (app->options.view) {
    case START_VIEW_MAGNITUDE: app->view = VIEW_MAGNITUDE;
                               set_tab(app, TAB_SCOPE, GetTime()); break;
    case START_VIEW_SPECTRUM:  app->view = VIEW_SPECTRUM;
                               set_tab(app, TAB_SCOPE, GetTime()); break;
    case START_VIEW_SCATTER:   app->view = VIEW_SCATTER;
                               set_tab(app, TAB_SCOPE, GetTime()); break;
    case START_VIEW_SURVEY:
        /*
         * Entered explicitly, not through set_tab.
         *
         * set_tab returns at once when the tab is already current, and since
         * the survey became the default tab it always is -- so --view survey
         * stopped calling view_survey_enter and --survey-range stopped
         * starting a sweep. Nothing failed: the window opened on the survey,
         * which is where it opens anyway, and the sweep simply never began.
         */
        set_tab(app, TAB_SURVEY, GetTime());
        view_survey_enter(app, GetTime());
        app->survey.band_menu_open = app->options.survey_bands;
        if (app->options.survey_band > 0)
            survey_choose_band(app, app->options.survey_band);
        break;
    case START_VIEW_GSM:       set_decode(app, DECODE_GSM, GetTime());
                               set_tab(app, TAB_DECODE, GetTime()); break;
    case START_VIEW_ADSB:      set_decode(app, DECODE_ADSB, GetTime());
                               set_tab(app, TAB_DECODE, GetTime()); break;
    case START_VIEW_FM:        set_decode(app, DECODE_FM, GetTime());
                               set_tab(app, TAB_DECODE, GetTime()); break;
    case START_VIEW_TETRA:     set_decode(app, DECODE_TETRA, GetTime());
                               set_tab(app, TAB_DECODE, GetTime()); break;
    case START_VIEW_SRD:       set_decode(app, DECODE_SRD, GetTime());
                               set_tab(app, TAB_DECODE, GetTime()); break;
    case START_VIEW_LTE:       set_decode(app, DECODE_LTE, GetTime());
                               set_tab(app, TAB_DECODE, GetTime()); break;
    case START_VIEW_CALIBRATION:
        open_calibration(app);
        /* --calibrate says which technology, so the overlay can be opened on
           the 4G arrangement and looked at. */
        if (app->options.calibrate == 2)
            calibration_select_technology(app, 1);
        break;
    case START_VIEW_SETTINGS:  open_settings(app); break;
    case START_VIEW_HELP:      open_help(app); break;
    case START_VIEW_STARTUP:   open_startup(app); break;
    default: break;
    }

    /*
     * And the form itself, where a run asked for it with `--startup`.
     *
     * `startup_form_wanted()` is the whole rule and it is in options.c, pure
     * and checked. It used to open on any plain windowed receiver launch and
     * keep out of scripted runs by a list of refusals; asking for it makes
     * that structural instead, which is what `check-pipelines`, every
     * screenshot recipe and every --duration check rest on
     * (ADR-0024, amended 2026-09-15).
     */
    if (startup_form_wanted(&app->options) && app->receiver_mode)
        open_startup(app);

    /*
     * The charts rather than the data, for whichever decode view was opened.
     * Every screen has to be reachable from the command line for the same
     * reason every decision does -- the LTE calibration panel shipped with
     * three overlapping regions because there was no way to look at it, and
     * three analysis screens had no way either.
     */
    if (app->config.fft_size > 0)
        app->sv.fft_size = app->config.fft_size;
    if (app->options.fft_size)
        app->sv.fft_size = app->options.fft_size;
    if (app->options.zoom_to_hz > app->options.zoom_from_hz) {
        scope_freq_sync(app);
        app->sv.window.freq.view_lower_hz = (double)app->options.zoom_from_hz;
        app->sv.window.freq.view_upper_hz = (double)app->options.zoom_to_hz;
        freq_window_clamp(&app->sv.window.freq, CHART_MIN_SPAN_HZ);
    }
    if (app->options.fm_play) {
        set_decode(app, DECODE_FM, GetTime());
        set_tab(app, TAB_DECODE, GetTime());
        fm_play(app);
    }
    if (app->options.fm_scan) {
        set_decode(app, DECODE_FM, GetTime());
        set_tab(app, TAB_DECODE, GetTime());
        fm_scan_begin(app, GetTime());
    }
    /*
     * Assigned, not merely set, and after set_decode has already run.
     *
     * enter_gsm() turns the analysis arrangement on whenever it enters with a
     * channel already chosen, which is what inspecting one from the scan or
     * the survey should do. It also happens on the way in from the command
     * line, where it made --analysis a no-op for GSM and left that view's
     * waterfall arrangement unreachable without a window -- and every screen
     * has to be reachable from the command line for the same reason every
     * decision does (ADR-0012). At startup nothing has been inspected yet, so
     * the flag is whatever was asked for.
     */
    app->fm.analysis_mode = app->options.analysis;
    app->adsb.analysis_mode = app->options.analysis;
    app->lte.analysis_mode = app->options.analysis;
    app->tetra.analysis_mode = app->options.analysis;
    app->gsm.analysis_mode = app->options.analysis;
    app->srd.analysis_mode = app->options.analysis;

    /* A recording asked for on the command line starts as soon as the worker
       is up, exactly as the button's does. */
    if (app->options.record_seconds > 0.0) {
        const char *basename;
        const char *technology;
        cli_record_labels(&app->options, &basename, &technology);
        start_capture_record(app, basename, technology, app->options.arfcn,
                             app->options.arfcn ? 400000.0 : 0.0,
                             app->options.record_seconds);
    }
    double run_started = GetTime();

    while (!stop_requested()) {
        /*
         * A run that is ending draws one more frame before it goes, so
         * --screenshot photographs a finished screen rather than whatever was
         * half-composed when the clock ran out.
         */
        int last_frame = 0;
        if (WindowShouldClose())
            last_frame = 1;
        if (app->options.duration_seconds > 0.0 &&
            GetTime() - run_started >= app->options.duration_seconds)
            last_frame = 1;
        if (last_frame && !app->options.screenshot_path)
            break;

        /* Who gets this frame's input. The precedence is in input_route.h,
           where it can be checked; what each target does with the keys is its
           own handler's business. */
        struct input_state input = input_state_now(app);

        /*
         * Once a frame: GetCharPressed() drains a queue, so a second caller
         * would intermittently see nothing.
         *
         * And **`input_takes_typing()` is the gate, not `text_focus`** --
         * chart_key_pressed() empties that queue in a `while` loop, so
         * reading it while anything on screen is taking typed characters
         * swallows every one of them before that handler runs. `text_focus`
         * names only three of the surfaces: the survey's fields, the FM
         * frequency and the Scope header. It does not name the settings panel
         * or the startup form, both of which read characters of their own, so
         * **typing into either was silently eaten here** -- reported against
         * the startup form's receiver label, which is the one field that
         * starts empty and so the one where nothing appearing is obvious.
         *
         * `input_takes_typing()` is the predicate that already means "some
         * surface is taking typed input", and using it is what stops the next
         * typing surface from having to remember to add itself.
         */
        enum chart_key chart_key = input_takes_typing(&input)
                                       ? CHART_KEY_NONE : chart_key_pressed();

        int shortcuts = input_shortcuts_live(&input);

        debug_log_frame_keys(app, &input);
        if (debug_log_active() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 at = GetMousePosition();
            struct debug_screen screen = debug_screen_now(app);
            char where[64];

            debug_screen_describe(&screen, where, sizeof(where));
            debug_log_write("click", "%.0f,%.0f on %s", at.x, at.y, where);
        }

        /* Quit is checked before the chain, so it means the same thing from
           every screen -- except while something is taking typed input, where
           losing a half-entered value to a stray letter is worse than having
           to click away first. */
        if (shortcuts && IsKeyPressed(KEY_Q))
            break;

        if (handle_waterfall_context_input(app)) {
            /* Handled by right-click menu or popup */
        } else if (input_help_opens(&input) && IsKeyPressed(KEY_H)) {
            open_help(app);
        } else {
            check_waterfall_right_click(app);
            switch (input_route(&input)) {
        case INPUT_TARGET_HELP:
            handle_help_input(app);
            break;
        case INPUT_TARGET_STARTUP:
            handle_startup_input(app);
            break;
        case INPUT_TARGET_SETTINGS:
            if (IsKeyPressed(KEY_ESCAPE))
                app->set.open = 0;
            else
                handle_settings_input(app);
            break;
        case INPUT_TARGET_SCAN:
            handle_scan_input(app);
            break;
        case INPUT_TARGET_CALIBRATION:
            handle_calibration_input(app);
            /* The overlay's waterfall takes the same gestures as every other
               one. Its axis is channels while calibrating against GSM, so the
               zoom floor is one ARFCN there and linear otherwise. */
            if (!input.text_focus)
                view_window_input(app, &app->cal.window,
                                  calibration_chart_rect(app), chart_key,
                                  app->cal.technology == 0
                                      ? GSM900_ARFCN_SPACING_HZ : 0.0,
                                  1);
            break;
        case INPUT_TARGET_SURVEY:
            if (handle_tab_input(app)) {
                /* Tab switched this frame. */
            } else if (clicked(settings_button()) ||
                       (shortcuts && IsKeyPressed(KEY_S))) {
                open_settings(app);
            } else if (clicked(calibration_button()) ||
                       (shortcuts && IsKeyPressed(KEY_C))) {
                open_calibration(app);
            } else if (IsKeyPressed(KEY_ESCAPE) &&
                       input_escape(&input) == INPUT_ESCAPE_QUIT) {
                /* The survey is the top of its own stack -- it is the tab the
                   program opens on -- so Escape leaves the program, the same
                   as it does from the Scope views. Anything nearer the
                   surface, a dropdown or a field, is handled below. */
                break_requested = 1;
            } else {
                handle_survey_input(app);
            }
            break;
        case INPUT_TARGET_DECODE:
        case INPUT_TARGET_SCOPE:
            /* A click on a button is unambiguous whatever has focus; only the
               letter that stands for it is suppressed while typing. */
            if (handle_tab_input(app)) {
                /* Tab switched this frame; skip per-tab input. */
            } else if (clicked(settings_button()) ||
                       (shortcuts && IsKeyPressed(KEY_S))) {
                open_settings(app);
            } else if (clicked(calibration_button()) ||
                       (shortcuts && IsKeyPressed(KEY_C))) {
                if (app->tab == TAB_DECODE && app->decode == DECODE_GSM)
                    leave_gsm(app);
                if (app->tab == TAB_DECODE && app->decode == DECODE_SRD)
                    leave_srd(app);
                if (app->tab == TAB_DECODE && app->decode == DECODE_ADSB)
                    leave_adsb(app);
                open_calibration(app);
            } else if (input.tab == TAB_DECODE) {
                if (input_decode_keys_live(&input)) {
                    if (IsKeyPressed(KEY_ONE))
                        set_decode(app, DECODE_FM, GetTime());
                    else if (IsKeyPressed(KEY_TWO))
                        set_decode(app, DECODE_ADSB, GetTime());
                    else if (IsKeyPressed(KEY_THREE))
                        set_decode(app, DECODE_GSM, GetTime());
                    else if (IsKeyPressed(KEY_FOUR))
                        set_decode(app, DECODE_LTE, GetTime());
                    else if (IsKeyPressed(KEY_FIVE))
                        set_decode(app, DECODE_TETRA, GetTime());
                    else if (IsKeyPressed(KEY_SIX))
                        set_decode(app, DECODE_SRD, GetTime());
                }
                if (app->decode == DECODE_GSM)
                    handle_gsm_input(app);
                else if (app->decode == DECODE_ADSB)
                    handle_adsb_input(app);
                else if (app->decode == DECODE_FM)
                    handle_fm_input(app);
                else if (app->decode == DECODE_TETRA)
                    handle_tetra_input(app);
                else if (app->decode == DECODE_SRD)
                    handle_srd_input(app);
                else
                    handle_lte_input(app);
                /*
                 * The window over each view's waterfall. After the view's own
                 * input, so a field that has the keyboard keeps the digits --
                 * and skipped entirely while one has focus, since Up and Down
                 * would otherwise zoom the chart while somebody types.
                 */
                if (!input.text_focus) {
                    struct chart_window *win = NULL;
                    Rectangle rect = { 0, 0, 0, 0 };
                    double spacing = 0.0;

                    if (app->decode == DECODE_GSM) {
                        win = &app->gsm.window;
                        rect = gsm_waterfall_rect();
                        spacing = GSM900_ARFCN_SPACING_HZ;
                    } else if (app->decode == DECODE_FM) {
                        win = &app->fm.window;
                        rect = fm_waterfall_rect(app);
                    } else if (app->decode == DECODE_LTE) {
                        win = &app->lte.window;
                        rect = lte_waterfall_rect(app);
                    } else if (app->decode == DECODE_SRD) {
                        win = &app->srd.window;
                        rect = srd_waterfall_rect(app);
                    } else if (app->decode == DECODE_ADSB) {
                        win = &app->adsb.window;
                        rect = adsb_waterfall_rect(app);
                    } else if (app->decode == DECODE_TETRA && !app->tetra.analysis_mode) {
                        win = &app->tetra.window;
                        rect = tetra_waterfall_rect(app);
                    }
                    if (win)
                        view_window_input(app, win, rect, chart_key, spacing,
                                          1);
                }
            } else if (IsKeyPressed(KEY_ESCAPE) && !input.text_focus) {
                /* A zoom is backed out of before the program is. 0 does it
                   explicitly, but a reader who has just dragged a region
                   reaches for Escape, and quitting instead is expensive. */
                if (input_escape(&input) == INPUT_ESCAPE_UNZOOM)
                    scope_freq_reset(app);
                else
                    break_requested = 1;
            }
            break;
            }
        }
        if (break_requested)
            break;

        /*
         * The scale keys, in one place for every screen that draws something
         * they mean anything for.
         *
         * They used to live in each view, and two views that draw a waterfall
         * never got them -- a key binding that is missing is invisible until
         * somebody presses the key, and nothing in a test can see a handler
         * that was not written. input_scale_keys() is the table now, and
         * check-input walks every screen against it.
         */
        {
            int scale = input_scale_keys(&input);
            int up = chart_key == CHART_KEY_SCALE_UP;
            int down = chart_key == CHART_KEY_SCALE_DOWN;

            /*
             * + and - are the scale keys, everywhere, with no exception.
             *
             * Up and Down were, on screens that had no frequency window to
             * zoom -- which was every screen but two until the decode views
             * and the calibration overlay got one. Keeping the exception
             * would have made the arrows mean "zoom" on five screens and
             * "scale" on two, and on the five they would have done both at
             * once. One rule is worth more than the arrows doing something
             * on the two charts that have no frequency axis at all.
             */
            if (scale == INPUT_SCALE_ACTIVE_CHART && (up || down))
                adjust_active_scale(app, up);
            else if (scale == INPUT_SCALE_WATERFALL && (up || down))
                adjust_waterfall_scale(app, up);
        }

        /*
         * The screen, when it changes. After the input phase rather than
         * before, so the line that follows a key is the screen that key
         * produced -- which is the whole question when a key is reported as
         * doing nothing.
         */
        if (debug_log_active()) {
            static struct debug_screen previous;
            static int seen;
            struct debug_screen screen = debug_screen_now(app);

            if (!seen || debug_screen_differs(&screen, &previous)) {
                char was[64], is[64];

                debug_screen_describe(&previous, was, sizeof(was));
                debug_screen_describe(&screen, is, sizeof(is));
                debug_log_write("screen", "%s%s%s", seen ? was : "",
                                seen ? " -> " : "", is);
                previous = screen;
                seen = 1;
            }
        }

        double now = GetTime();

        if (view_scope_resize_if_needed(app, calculate_plot()) < 0) {
            result = -1;
            break;
        }

        if (input_route(&input) == INPUT_TARGET_SCOPE) {
            /* The control row first: while one of its fields has focus the
               digits are a frequency being typed, not a view number. That is
               what input_view_keys_live() below is reading. */
            if (!app->cal.open && !app->set.open &&
                !app->help.open)
                scope_header_input(app);   /* retune_receiver logs its own */
            /* While a survey range field has focus the digits belong to it,
               not to the view switcher. */
            if (input_view_keys_live(&input)) {
                enum view_kind selected = app->view;
                if (IsKeyPressed(KEY_ONE))
                    selected = VIEW_MAGNITUDE;
                if (IsKeyPressed(KEY_TWO))
                    selected = VIEW_SPECTRUM;
                if (IsKeyPressed(KEY_THREE))
                    selected = VIEW_SCATTER;
                if (selected != app->view) {
                    app->view = selected;
                    if (selected == VIEW_SCATTER)
                        clear_scatter(app);
                }
                /*
                 * The frequency window the spectrum and the waterfall share:
                 * drag to zoom, Left and Right to pan, 0 to put it back. It
                 * retunes only when a pan has run out of received span.
                 */
                if (app->view == VIEW_SPECTRUM) {
                    scope_freq_input(app, app->gui->plot, chart_key);
                }
                /* The scale keys are applied once, below, for every screen
                   that has a scale -- not here, and not per view. */
            }
        }

        /*
         * The per-block work, extracted so a headless Viewer link (ADR-0027)
         * can drive the same sequence with no window: everything from block
         * consumption through the per-technology dispatch. `frame_advance()`
         * is the one place that decides what to compute; what follows here
         * is only the two GPU uploads it left for the draw phase, gated the
         * same way `update_waterfall()` and `update_scatter()` used to gate
         * them internally.
         */
        int spectrum_updated = frame_advance(app, &snapshot, now,
                                             scope_requested_fft_size(app));
        /*
         * The sound, which is the window's alone: `update_fm_audio()` feeds
         * a raylib `AudioStream` and `frame_advance()` used to call it, so
         * the step `headless` and `server` share reached for a device only a
         * window has. Every frame rather than every block, unchanged -- the
         * card asks on its own schedule and a block is several of its
         * buffers -- and it returns at once unless something is playing.
         */
        if (app->tab == TAB_DECODE && app->decode == DECODE_FM)
            update_fm_audio(app);
        /*
         * The magnitude chart's reduction, which `process_block()` used to
         * do. Gated on a new spectrum rather than on `have_samples`: the two
         * differ only when a block converts and then yields no spectrum --
         * `pair_count` below the transform size, which no shipping path
         * produces -- and there the chart shows the previous block for one
         * frame rather than being recomputed from magnitudes it already
         * drew.
         */
        if (spectrum_updated)
            recompute_magnitude_bins(app);
        if (spectrum_updated)
            render_waterfall(app);
        render_scatter(app, now);

        BeginDrawing();
        ClearBackground((Color){ 12, 19, 28, 255 });
        if (app->cal.open) {
            /* Calibration is a global full-screen overlay reached by a button,
               independent of the active tab. */
            if (app->bandscan.open)
                draw_scan(app);
            else
                draw_calibration(app);
        } else {
            if (app->tab == TAB_DECODE) {
                if (app->decode == DECODE_GSM)
                    draw_gsm(app);
                else if (app->decode == DECODE_ADSB)
                    draw_adsb(app);
                else if (app->decode == DECODE_FM)
                    draw_fm(app);
                else if (app->decode == DECODE_TETRA)
                    draw_tetra(app);
                else if (app->decode == DECODE_SRD)
                    draw_srd(app);
                else
                    draw_lte(app);
            } else if (app->tab == TAB_SURVEY) {
                draw_survey(app);
            } else {
                struct scope_view_model svm;

                {
            struct scope_view_model_input in;

            in.frame = &app->frame;
            in.sv = &app->sv;
            in.applied = &app->applied;
            in.device = &app->device;
            in.tab = (int)app->tab;
            in.decode = (int)app->decode;
            scope_view_model_build(&in, &svm);
        }
                draw_base_hud(app, &snapshot);
                draw_scope_header(app);
                if (app->view == VIEW_MAGNITUDE)
                    draw_magnitude(app, &svm);
                else if (app->view == VIEW_SPECTRUM) {
                    struct scope_plot_layout split =
                        scope_plot_split(app->gui->plot);
                    draw_spectrum(app, &svm, split.spectrum);
                    draw_waterfall(app, &svm, split.waterfall);
                } else
                    draw_scatter(app, &svm);
            }
            draw_header(app);
            if (app->set.open)
                draw_settings(app);
        }
        /*
         * The form is over everything but Help, which matches where the
         * router sends its keys. Drawn after the header so the tab bar is
         * visibly behind it rather than reachable through it.
         */
        if (app->startup.open)
            draw_startup(app);
        if (app->help.open)
            draw_help(app);
        draw_waterfall_context(app);

        if (last_frame) {
            /*
             * Flush the batch, then read, then swap.
             *
             * All three parts matter and the order is the whole of it. raylib
             * accumulates geometry in a render batch and does not touch the
             * framebuffer until something flushes it, which EndDrawing does on
             * its way to swapping the buffers. So a read placed after
             * EndDrawing sees a swapped back buffer -- undefined, in practice
             * usually the frame just presented, which is why it appeared to
             * work for months -- and a read placed before it sees a
             * framebuffer holding the cleared background and nothing else.
             *
             * Both failures look identical from the outside: a PNG of a screen
             * the compositor can be photographed drawing perfectly well. The
             * second was the more honest of the two, because it failed for
             * every view at once instead of only for the one whose overlay
             * happened to overrun the batch and flush it by accident.
             *
             * rlDrawRenderBatchActive is the flush on its own, without the
             * swap.
             *
             * Exported rather than handed to TakeScreenshot, which prefixes
             * the working directory to whatever it is given, so an absolute
             * path silently becomes nonsense and no file appears.
             */
            rlDrawRenderBatchActive();
            Image frame = LoadImageFromScreen();
            if (ExportImage(frame, app->options.screenshot_path))
                fprintf(stderr, "Wrote %s (%dx%d)\n",
                        app->options.screenshot_path, frame.width,
                        frame.height);
            else
                fprintf(stderr, "Could not write %s\n",
                        app->options.screenshot_path);
            UnloadImage(frame);
            EndDrawing();
            break;
        }
        EndDrawing();
        if (snapshot.worker_failed) {
            result = -1;
            break;
        }
    }
    return result;
}

/*
 * The window's `main`, and the whole of what makes this binary the windowed
 * one: it hands `run_gui` to the shared setup in `app_main.c`.
 * `sdrprobe` passes NULL there and is otherwise the same program
 * (`.scratch/layer-boundaries/issues/04-*`).
 */
static void release_window(struct app *app) {
    view_scope_release(app);
    if (app->window_ready) {
        CloseWindow();
        app->window_ready = 0;
    }
    fm_audio_close(app);
}

int main(int argc, char **argv) {
    static const struct app_window window = { run_gui, release_window };

    return sdrprobe_main(argc, argv, &window);
}
