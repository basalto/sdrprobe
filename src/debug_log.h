#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stddef.h>

/*
 * A record of what the program was told and what it showed.
 *
 * Off unless --debug-log names a file, and costs nothing when off.
 *
 * The reason it exists: a window's behaviour is the one part of this program
 * that no check can reach (ADR-0012 exempts drawing, and by extension the
 * keystroke that reached the drawing). Keys cannot be injected here at all --
 * wtype synthesises a keysym on a scratch keycode while raylib reads physical
 * ones, so an hour went into discovering that an `h` had arrived as Escape,
 * which one line of this would have said outright. And when an operator
 * reports that a key did nothing, the question is always the same: did the
 * program not receive it, receive it and route it elsewhere, or route it
 * correctly to a handler that ignored it. Those are three different bugs and
 * they look identical from the outside.
 *
 * So: one line per event, keyword first, the same shape the survey records
 * use. What it must never become is a frame-rate trace -- a log nobody can
 * read is a log nobody reads, so the screen line is written when the screen
 * changes and not otherwise.
 */

/* Open a log. "-" writes to stderr. Returns 0, or negative and prints why. */
int debug_log_open(const char *path);
void debug_log_close(void);
int debug_log_active(void);

/* One line: a keyword, then whatever describes it. The timestamp is added. */
void debug_log_write(const char *keyword, const char *format, ...);

/*
 * What is on screen, and whether it changed.
 *
 * Kept as a struct so the comparison is a pure function rather than a pile of
 * remembered fields next to the drawing code -- which is where the last
 * version of this idea would have rotted.
 */
struct debug_screen {
    int tab;
    int view;               /* Scope tab: which of the five */
    int decode;             /* Decode tab: which technology */
    int settings_open;
    int calibration_open;
    int scan_open;
    int help_open;
    int menu_open;          /* a combo list is down over the view */
    int analysis;           /* a view showing its charts rather than its data */
};

/* Whether two snapshots describe different screens. */
int debug_screen_differs(const struct debug_screen *a,
                         const struct debug_screen *b);
/* "decode/fm", "scope/survey +menu", "scope/spectrum +settings" -- what a
   reader needs to picture the screen, in one field. */
void debug_screen_describe(const struct debug_screen *screen, char *out,
                           size_t capacity);

/* The name of a raylib key code: "ESCAPE", "3", "H", or "key 321" for one
   that is not named here. The point of the log is to say what actually
   arrived, so an unknown code prints its number rather than nothing. */
const char *debug_key_name(int key);

/* The input target's name, from input_route.h's enum. */
const char *debug_target_name(int target);

#endif
