#include "debug_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *sink;
static double started;

static double now_seconds(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int debug_log_open(const char *path) {
    if (!path || !*path)
        return -1;
    if (strcmp(path, "-") == 0) {
        sink = stderr;
    } else {
        sink = fopen(path, "w");
        if (!sink) {
            fprintf(stderr, "Cannot write the debug log %s\n", path);
            return -1;
        }
    }
    started = now_seconds();
    return 0;
}

void debug_log_close(void) {
    if (sink && sink != stderr)
        fclose(sink);
    sink = NULL;
}

int debug_log_active(void) {
    return sink != NULL;
}

void debug_log_write(const char *keyword, const char *format, ...) {
    va_list args;

    if (!sink)
        return;
    fprintf(sink, "%9.3f  %-8s  ", now_seconds() - started,
            keyword ? keyword : "?");
    va_start(args, format);
    vfprintf(sink, format, args);
    va_end(args);
    fputc('\n', sink);
    /* Flushed every line on purpose: the log is most wanted when the program
       is about to do something that stops it, and a buffer that dies with it
       is the one thing this must not be. */
    fflush(sink);
}

int debug_screen_differs(const struct debug_screen *a,
                         const struct debug_screen *b) {
    if (!a || !b)
        return 1;
    return memcmp(a, b, sizeof(*a)) != 0;
}

static const char *SCOPE_VIEWS[] = {
    "magnitude", "spectrum", "scatter", "waterfall"
};
static const char *DECODES[] = { "fm", "adsb", "gsm", "lte" };

void debug_screen_describe(const struct debug_screen *screen, char *out,
                           size_t capacity) {
    const char *where;
    size_t used;

    if (!out || capacity == 0)
        return;
    out[0] = '\0';
    if (!screen)
        return;

    if (screen->tab == 2) {
        where = (screen->decode >= 0 &&
                 screen->decode < (int)(sizeof(DECODES) / sizeof(DECODES[0])))
                    ? DECODES[screen->decode] : "?";
        snprintf(out, capacity, "decode/%s", where);
    } else if (screen->tab == 0) {
        snprintf(out, capacity, "survey");
        where = NULL;
    } else {
        where = (screen->view >= 0 &&
                 screen->view <
                     (int)(sizeof(SCOPE_VIEWS) / sizeof(SCOPE_VIEWS[0])))
                    ? SCOPE_VIEWS[screen->view] : "?";
        snprintf(out, capacity, "scope/%s", where);
    }
    (void)where;

    /*
     * The overlays are appended rather than replacing the view, because they
     * are drawn over it and the view underneath is half the question when
     * something goes wrong -- "calibration over the survey" and "calibration
     * over the GSM view" leave the receiver in different places.
     */
    used = strlen(out);
    {
        struct { int on; const char *name; } flags[] = {
            { screen->help_open, " +help" },
            { screen->settings_open, " +settings" },
            { screen->calibration_open, " +calibration" },
            { screen->scan_open, " +scan" },
            { screen->menu_open, " +menu" },
            { screen->analysis, " +analysis" }
        };
        unsigned i;
        for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
            if (!flags[i].on)
                continue;
            if (used + strlen(flags[i].name) + 1 >= capacity)
                break;
            memcpy(out + used, flags[i].name, strlen(flags[i].name) + 1);
            used += strlen(flags[i].name);
        }
    }
}

/*
 * Key names.
 *
 * raylib's codes are ASCII for the printable keys and a scattered set above
 * 255 for the rest, so this is a table rather than arithmetic. It carries
 * every key this program binds, plus the ones an operator is likely to press
 * by mistake -- an unbound key that arrives is exactly as interesting as a
 * bound one when the complaint is "nothing happened".
 */
const char *debug_key_name(int key) {
    static char unknown[16];
    unsigned i;
    static const struct { int code; const char *name; } NAMES[] = {
        { 32, "SPACE" }, { 39, "APOSTROPHE" }, { 44, "COMMA" },
        { 45, "MINUS" }, { 46, "PERIOD" }, { 47, "SLASH" },
        { 48, "0" }, { 49, "1" }, { 50, "2" }, { 51, "3" }, { 52, "4" },
        { 53, "5" }, { 54, "6" }, { 55, "7" }, { 56, "8" }, { 57, "9" },
        { 59, "SEMICOLON" }, { 61, "EQUAL" },
        { 65, "A" }, { 66, "B" }, { 67, "C" }, { 68, "D" }, { 69, "E" },
        { 70, "F" }, { 71, "G" }, { 72, "H" }, { 73, "I" }, { 74, "J" },
        { 75, "K" }, { 76, "L" }, { 77, "M" }, { 78, "N" }, { 79, "O" },
        { 80, "P" }, { 81, "Q" }, { 82, "R" }, { 83, "S" }, { 84, "T" },
        { 85, "U" }, { 86, "V" }, { 87, "W" }, { 88, "X" }, { 89, "Y" },
        { 90, "Z" },
        { 256, "ESCAPE" }, { 257, "ENTER" }, { 258, "TAB" },
        { 259, "BACKSPACE" }, { 260, "INSERT" }, { 261, "DELETE" },
        { 262, "RIGHT" }, { 263, "LEFT" }, { 264, "DOWN" }, { 265, "UP" },
        { 266, "PAGE_UP" }, { 267, "PAGE_DOWN" }, { 268, "HOME" },
        { 269, "END" }, { 280, "CAPS_LOCK" }, { 290, "F1" }, { 291, "F2" },
        { 320, "KP_0" }, { 321, "KP_1" }, { 322, "KP_2" }, { 323, "KP_3" },
        { 324, "KP_4" }, { 325, "KP_5" }, { 326, "KP_6" }, { 327, "KP_7" },
        { 328, "KP_8" }, { 329, "KP_9" }, { 330, "KP_DECIMAL" },
        { 331, "KP_DIVIDE" }, { 332, "KP_MULTIPLY" }, { 333, "KP_SUBTRACT" },
        { 334, "KP_ADD" }, { 335, "KP_ENTER" },
        { 340, "LEFT_SHIFT" }, { 341, "LEFT_CONTROL" }, { 342, "LEFT_ALT" },
        { 344, "RIGHT_SHIFT" }, { 345, "RIGHT_CONTROL" }, { 346, "RIGHT_ALT" }
    };

    for (i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
        if (NAMES[i].code == key)
            return NAMES[i].name;
    snprintf(unknown, sizeof(unknown), "key %d", key);
    return unknown;
}

const char *debug_target_name(int target) {
    static const char *NAMES[] = {
        "help", "survey", "settings", "scan", "calibration", "decode",
        "scope"
    };

    if (target < 0 || target >= (int)(sizeof(NAMES) / sizeof(NAMES[0])))
        return "?";
    return NAMES[target];
}
