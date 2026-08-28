/* tui.c: implementation of the terminal UI layer. See tui.h for the
 * interface contract and the rationale for the relative ramp scale.
 *
 * No curses: the whole UI is three lines redrawn in place with a handful
 * of ANSI escapes:
 *   \033[?1049h/l  enter/leave the alternate screen (like less(1)), so the
 *                  user's scrollback is untouched
 *   \033[?25l/h    hide/show the cursor
 *   \033[H         cursor home (start of frame redraw)
 *   \033[K         clear to end of line
 *   \033[2J        clear screen (once, at startup)
 *
 * stdin is put in non-canonical, no-echo mode with VMIN=VTIME=0 so
 * tui_quit_pressed() can poll for 'q' without blocking and without the
 * user having to press Enter. The original termios is restored on every
 * exit path via atexit().
 */
#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

static struct termios saved_termios;
static int termios_saved = 0;

static void term_restore(void) {
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
    /* Show cursor, leave alternate screen. */
    fprintf(stderr, "\033[?25h\033[?1049l");
    fflush(stderr);
}

int tui_setup(void) {
    struct termios t;
    if (!isatty(STDIN_FILENO))
        return -1;
    if (tcgetattr(STDIN_FILENO, &saved_termios) < 0)
        return -1;
    termios_saved = 1;
    t = saved_termios;
    t.c_lflag &= ~(ICANON | ECHO); /* raw-ish: keys arrive unbuffered,
                                    * unechoed */
    t.c_cc[VMIN] = 0;              /* read() returns immediately... */
    t.c_cc[VTIME] = 0;             /* ...with whatever is available (0/1) */
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) < 0)
        return -1;
    atexit(term_restore);
    /* Alternate screen, hide cursor, clear. */
    fprintf(stderr, "\033[?1049h\033[?25l\033[2J\033[H");
    fflush(stderr);
    return 0;
}

int tui_cols(void) {
    struct winsize ws;
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

int tui_quit_pressed(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n == 1 && (c == 'q' || c == 'Q'));
}

void tui_frame(const float *peaks, int nbins, const char *header,
               unsigned long long total_pairs, int frame) {
    /* Frame-wide stats over the per-bin peaks: min is our noise floor
     * estimate, max is the strongest pulse in the block. */
    float bmin = 1e30f, bmax = 0.0f, bsum = 0.0f;
    for (int i = 0; i < nbins; i++) {
        if (peaks[i] < bmin) bmin = peaks[i];
        if (peaks[i] > bmax) bmax = peaks[i];
        bsum += peaks[i];
    }
    if (nbins <= 0)
        return;

    /* Render one frame in-place: \033[H homes the cursor (the previous
     * plot line scrolls up by the newline we emit at the end), \033[K
     * clears to end of line so shorter footer text never leaves stale
     * characters. All output goes to stderr so the TUI works even when
     * stdout is piped. */
    char out[TUI_MAX_COLS + 256];
    char *p = out;
    p += sprintf(p, "\033[H\033[K%s  q quits\n", header);

    /* Map each bin's peak onto the 9-glyph ASCII ramp. See tui.h for why
     * the scale is relative to this frame's noise floor. */
    float span = bmax - bmin;
    for (int c = 0; c < nbins; c++) {
        int level = 0;
        if (span > 0.0f)
            level = (int)((peaks[c] - bmin) / span * 8.0f);
        if (level > 8) level = 8;
        if (level < 0) level = 0;
        /* level 0-8 -> ' ', '.', ':', '-', '=', '+', '*', '#', '@' */
        p += sprintf(p, "%c", " .:-=+*#@"[level]);
    }
    /* Footer: absolute peak stats for this block, cumulative sample
     * count (should advance at ~2M/s against wall clock), and a spinner
     * that proves the loop is alive even when the plot line is all
     * spaces. The trailing '\n' is what makes the previous frame scroll
     * up. */
    p += sprintf(p, "\n\033[Kpeak min %6.1f  mean %6.1f  max %6.1f"
                 "  %llu samples total  %c",
                 bmin, bsum / nbins, bmax, total_pairs,
                 "|/-\\"[frame % 4]);
    fwrite(out, 1, (size_t)(p - out), stderr);
    fflush(stderr);
}
