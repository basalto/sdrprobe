/* tui.h: minimal terminal UI layer for rtl_tui.
 *
 * Owns everything that knows about ANSI escape codes, termios raw mode,
 * and the ASCII ramp: the rest of the program passes in "array of per-bin
 * peak magnitudes + stats" and never touches a terminal API. No SDR types
 * cross this interface, so a different backend (e.g. the planned raylib
 * visualizer from docs/) could implement the same seam later.
 *
 * All drawing goes to stderr so the UI works even when stdout is piped.
 */
#ifndef TUI_H
#define TUI_H

/* Cap on rendered columns; one peak per column. Terminals wider than this
 * just don't get finer time resolution (each column would otherwise cover
 * fewer than ~256 samples and the per-frame work would grow for no visual
 * benefit). */
#define TUI_MAX_COLS 512

/* Put stdin in raw (non-canonical, no-echo, non-blocking) mode, switch to
 * the alternate screen, hide the cursor, and clear. Returns 0 on success,
 * -1 if stdin is not a tty or termios fails. Registers an atexit handler
 * that restores the terminal on every normal exit path. */
int tui_setup(void);

/* Terminal width in columns; one bin per column. Queried once at startup
 * (resizing mid-run just means the plot no longer matches the window). */
int tui_cols(void);

/* Return 1 if the user pressed 'q' (non-blocking poll). */
int tui_quit_pressed(void);

/* Draw one frame in place and scroll the previous one up.
 *
 * peaks/nbins: per-bin peak magnitudes on the 127.5-centered scale
 *              (0 = silence, ~180.4 = full-scale).
 * header:      text for the header line (caller owns app-specific labels
 *              like frequency, sample rate, and source name).
 * total_pairs: cumulative I/Q pairs processed (for the footer).
 * frame:       frame counter, used only to animate the spinner.
 *
 * The ramp is scaled RELATIVE to this frame's own noise floor:
 * (peak - min) / (max - min). An absolute 0..180 scale was tried first
 * and rendered a working ADS-B signal as a blank screen, because both
 * noise (~3-20) and diluted pulses sat in the first ramp step (~20 units
 * each). Caveat: on a perfectly quiet frame this stretches pure noise
 * across the whole ramp -- the footer's absolute min/mean/max numbers
 * are the way to tell "loud frame with pulses" from "quiet frame, just
 * stretched noise". */
void tui_frame(const float *peaks, int nbins, const char *header,
               unsigned long long total_pairs, int frame);

#endif /* TUI_H */
