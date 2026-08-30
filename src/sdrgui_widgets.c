#include "sdrgui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Small standalone pieces that are not charts: the calibration-health dot,
 * the text input the settings and calibration panels use, and the wrapped
 * paragraph text the help overlay is written in.
 */

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

void sdrgui_text_field(Rectangle box, const char *text, int focused) {
    DrawRectangleRec(box, (Color){ 5, 10, 16, 255 });
    DrawRectangleLinesEx(box, 1.0f, focused ? (Color){ 255, 174, 62, 255 }
                                            : (Color){ 91, 117, 132, 255 });
    DrawText(text, (int)box.x + 10, (int)box.y + 9, 19,
             (Color){ 255, 225, 161, 255 });
}

/* One line is accumulated in `line`, and a word is added to it only if the
   result still fits; otherwise the line is emitted and the word starts the
   next one. Measuring and drawing walk identical paths, so a measure pass and
   the draw that follows it always agree on the height. */
float sdrgui_text_block(Rectangle box, const char *text, int size,
                        int line_gap, Color color, int draw) {
    char line[512];
    char candidate[512];
    int line_length = 0;
    float y = box.y;
    const float step = (float)size + (float)line_gap;
    const char *cursor = text;

    if (!text)
        return 0.0f;
    line[0] = '\0';
    while (*cursor) {
        const char *word_end = cursor;
        while (*word_end && *word_end != ' ' && *word_end != '\n')
            word_end++;

        int candidate_length = 0;
        if (line_length > 0) {
            memcpy(candidate, line, (size_t)line_length);
            candidate_length = line_length;
            candidate[candidate_length++] = ' ';
        }
        size_t word_length = (size_t)(word_end - cursor);
        if (word_length > sizeof(candidate) - 1 - (size_t)candidate_length)
            word_length = sizeof(candidate) - 1 - (size_t)candidate_length;
        memcpy(candidate + candidate_length, cursor, word_length);
        candidate_length += (int)word_length;
        candidate[candidate_length] = '\0';

        if (line_length > 0 &&
            (float)MeasureText(candidate, size) > box.width) {
            if (draw)
                DrawText(line, (int)box.x, (int)y, size, color);
            y += step;
            memcpy(line, cursor, word_length);
            line_length = (int)word_length;
            line[line_length] = '\0';
        } else {
            memcpy(line, candidate, (size_t)candidate_length + 1);
            line_length = candidate_length;
        }

        cursor = word_end;
        if (*cursor == '\n') {
            if (draw)
                DrawText(line, (int)box.x, (int)y, size, color);
            y += step;
            line_length = 0;
            line[0] = '\0';
            cursor++;
        } else if (*cursor == ' ') {
            cursor++;
        }
    }
    if (line_length > 0) {
        if (draw)
            DrawText(line, (int)box.x, (int)y, size, color);
        y += step;
    }
    return y - box.y;
}
