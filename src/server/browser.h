#ifndef BROWSER_H
#define BROWSER_H

/*
 * Pointing a browser at a URL, without this process becoming responsible
 * for it afterwards.
 *
 * One caller: `viewer_session_run()`, once, after the Viewer link is already
 * listening -- see its own comment for why that order matters (a browser
 * started ahead of the bind races the listener and the first load reads as
 * the program being broken, not as a browser that opened a beat early).
 *
 * **What this proves, and what it does not.** Whether to call this at all is
 * `browser_wanted()`'s decision, pure and checked with no process and no
 * display. Starting a real subprocess is not something a unit check can
 * watch happen correctly, so nothing here is checked by one --
 * `.scratch/cli-subcommands/issues/02-*` has the one thing that has to be
 * verified by hand instead: that a long-running `web` leaves no zombie.
 */

/*
 * Starts `xdg-open <url>` in the background and returns at once -- this
 * process never learns whether the browser itself started, which is what
 * "returns at once" requires: finding out means waiting for it.
 *
 * Returns 0 once the attempt has been handed off, -1 if it could not even be
 * started (the double fork below failed, which needs no more than the
 * process table being full). The caller reports which on stderr; this
 * function writes nothing anywhere; a browser xdg-open cannot itself start
 * fails silently by construction; see the file comment.
 */
int browser_open(const char *url);

#endif
