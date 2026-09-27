#ifndef VIEWER_COMMAND_H
#define VIEWER_COMMAND_H

#include <stddef.h>
#include <stdint.h>

/*
 * The one thing read from a Viewer besides a subscription (ticket 06):
 * a whitespace-delimited command line, parsed with sscanf on the same
 * principle `src/core/capture_sidecar.h` states outright -- this is not a
 * JSON parser and must not become one. `viewer_link.c` decides *that* a
 * line is a command rather than a subscription; parsing what the
 * command actually says lives here, decoupled from sockets and from
 * `struct app`, so it has a check of its own that needs neither.
 */

enum viewer_command_type {
    VIEWER_COMMAND_TUNE = 0,
    VIEWER_COMMAND_VIEW
};

/*
 * The screens `view <name>` can ask for -- `view` names the screen, which is
 * the whole of what a command is for.
 *
 * This said "a third is one more name and one more `set_tab()` branch", and
 * the third turned out not to be. FM is not a tab: it is the Decode tab with
 * `DECODE_FM` chosen, so reaching it is a `set_decode()` *and* a `set_tab()`,
 * in that order -- the order sdrprobe.c's own startup sequence already
 * explains, since switching the tab first enters whichever decode kind is
 * already recorded and leaves it again on the way past. A screen name here is
 * therefore a name for a *destination*, not for a member of `enum active_tab`,
 * and the four decode views still to come (ticket 07) are each one more name
 * and one more line of that mapping.
 */
enum viewer_screen {
    VIEWER_SCREEN_SCOPE = 0,
    VIEWER_SCREEN_SURVEY,
    VIEWER_SCREEN_FM,
    VIEWER_SCREEN_GSM,
    VIEWER_SCREEN_ADSB,
    VIEWER_SCREEN_TETRA,
    VIEWER_SCREEN_SRD
};

struct viewer_command {
    enum viewer_command_type type;
    uint32_t hz;              /* VIEWER_COMMAND_TUNE */
    enum viewer_screen screen; /* VIEWER_COMMAND_VIEW */
};

/* Longest command line this parser will read at all -- past this, a line
   is refused rather than silently truncated. Generous for "tune <hz>"
   and its trailing whitespace; generous for "view survey" too, ticket
   07's own second command, and not sized for a third this file does not
   yet have. */
#define VIEWER_COMMAND_LINE_MAX 128

/*
 * Parses one line with no trailing newline (the caller has already split
 * on the WebSocket frame boundary, not this). Returns 0 and fills `out` on
 * a recognized, well-formed command -- "tune <hz>" or "view
 * scope|survey|fm". Returns -1 and -- if `error` is non-NULL -- a
 * human-readable reason (truncated to fit `error_cap`, always
 * NUL-terminated) on: an empty or all-whitespace line, a line at or past
 * VIEWER_COMMAND_LINE_MAX, an unrecognized command word, a `tune` value
 * that does not parse as an integer or does not fit `uint32_t`, a `view`
 * naming a screen this build does not have, or a line carrying anything
 * after the value or the screen name but whitespace.
 */
int viewer_command_parse(const char *line, size_t len, struct viewer_command *out,
                         char *error, size_t error_cap);

#endif
