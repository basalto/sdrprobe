#ifndef GUI_STATE_H
#define GUI_STATE_H

#include <raylib.h>

/*
 * What only a window owns: textures, an audio stream, the plot rectangle,
 * and the right-click menu's context.
 *
 * `app.h` used to hold these six fields itself and so included `<raylib.h>`,
 * which meant every file that touched `struct app` compiled against raylib --
 * the view models, `frame_advance`, and the Viewer link among them, none of
 * which draws anything. `.scratch/layer-boundaries/issues/01-*` is the "why".
 *
 * `struct app` now carries a `struct gui_state *` and nothing else about
 * raylib, and this header is included only by files that draw. **The pointer
 * is NULL under `headless` and `server`**, which is the truth about a session
 * with no window and is the point of the indirection: a runtime path that
 * reaches for a texture crashes at once instead of reading a zeroed handle
 * that happens to look plausible.
 *
 * `run_gui()` is the one allocator, and nothing outside the GUI ever
 * dereferences the pointer.
 */

/*
 * Right-clicking a waterfall opens a menu, and the menu can open a
 * retrospective signal report over it. Modal and it takes no typing, which is
 * why it is its own state rather than a typing surface -- it reached
 * `struct app` at all only in 2026-09-15, before which `q` was live behind
 * it and quit the program out from under a reader
 * (`.scratch/iq-ring-buffer/issues/03-*`).
 *
 * Moved here whole rather than leaving the struct in `app.h` with its
 * `Vector2` alone extracted: every field of it belongs to a menu, both its
 * readers draw, and half a struct in each header is a worse seam than none.
 */
struct waterfall_signal_context {
    /* `menu_open` and `popup_open` were here and are `app->sv`'s now: they
       route input, and routing has to be decidable without a window. See
       `struct scope_view` in app.h. */
    Vector2 mouse_pos;
    double clicked_freq_hz;
    double clicked_age_seconds;
    char technology[16];

    double report_freq_hz;
    double report_offset_hz;
    double report_age_seconds;
    double report_duration_seconds;
    double report_prominence_db;
    double report_standing_fraction;
    double report_envelope_variation;
    double report_peak_mean_db;
    char report_modulation[64];
    char report_technology[96];
    char notice[384];
    double notice_time;
};

struct gui_state {
    /*
     * The FM view's output stream. The sound is the window's alone: a Viewer
     * cannot hear it, and `fm_state` carries whether this machine is playing
     * rather than any means of doing so.
     */
    AudioStream fm_audio_stream;

    /*
     * The Scope's two GPU resources and the CPU-side pixels behind one of
     * them. `struct scope_view` keeps the float data -- the waterfall's dBFS
     * history, the scatter history -- because that is what the view model
     * reads and what a browser is sent; only the handles are here.
     */
    RenderTexture2D scatter;
    Texture2D waterfall;
    Color *waterfall_pixels;

    /*
     * Where the main chart sits. `recreate_scatter()` sets it as a side
     * effect (it always has), and the overlays read it to place their own
     * charts.
     */
    Rectangle plot;

    struct waterfall_signal_context wf_menu;
};

#endif
