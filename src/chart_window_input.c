#include "view.h"

/*
 * The reading half of a chart's gestures, and nothing else.
 *
 * `chart_window.c` was the only file outside the GUI set that called raylib,
 * and it called it for six things: `GetMousePosition`,
 * `IsMouseButtonPressed`, `IsMouseButtonDown`, `CheckCollisionPointRec`,
 * `IsKeyPressed` and `IsKeyPressedRepeat`. Every one of them is a *reading*;
 * what a drag or a zoom then means is arithmetic over a
 * `struct chart_window`, and it is in `chart_window.c` where a check can
 * reach it (ADR-0012, `.scratch/layer-boundaries/issues/04-*`).
 *
 * So this file is the six calls, a struct, and a call. It is in `GUI_SRC`;
 * `chart_window.c` links into `./sdrprobe` with no raylib at all.
 */

double chart_window_input(struct chart_window *w, Rectangle plot,
                          enum chart_key key, double min_span) {
    Vector2 mouse = GetMousePosition();
    struct chart_gesture_input in;
    int right;

    in.pointer_x = mouse.x;
    in.pointer_over = CheckCollisionPointRec(mouse, plot);
    in.press = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    in.held = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.zoom_in = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP);
    in.zoom_out = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN);

    /* Right wins when both arrows are down, which is what the `? :` this
       replaced did -- there is no third direction to have an opinion about. */
    right = IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT);
    if (right)
        in.pan = 1;
    else if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))
        in.pan = -1;
    else
        in.pan = 0;

    return chart_window_gesture(w, plot.x, plot.width, &in,
                                key == CHART_KEY_RESET_ZOOM, min_span);
}
