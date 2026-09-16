#ifndef VIEWER_COMMAND_H
#define VIEWER_COMMAND_H

#include <stddef.h>
#include <stdint.h>

/*
 * The one thing read from a Viewer besides a subscription (ticket 06):
 * a whitespace-delimited command line, parsed with sscanf on the same
 * principle `src/capture_sidecar.h` states outright -- this is not a
 * JSON parser and must not become one. `viewer_link.c` decides *that* a
 * line is a command rather than a subscription; parsing what the
 * command actually says lives here, decoupled from sockets and from
 * `struct app`, so it has a check of its own that needs neither.
 */

enum viewer_command_type {
    VIEWER_COMMAND_TUNE = 0
};

struct viewer_command {
    enum viewer_command_type type;
    uint32_t hz; /* VIEWER_COMMAND_TUNE */
};

/* Longest command line this parser will read at all -- past this, a line
   is refused rather than silently truncated. Generous for "tune <hz>"
   and its trailing whitespace; not sized for anything this ticket does
   not yet have a second command to justify widening for. */
#define VIEWER_COMMAND_LINE_MAX 128

/*
 * Parses one line with no trailing newline (the caller has already split
 * on the WebSocket frame boundary, not this). Returns 0 and fills `out`
 * on a recognized, well-formed command. Returns -1 and -- if `error` is
 * non-NULL -- a human-readable reason (truncated to fit `error_cap`,
 * always NUL-terminated) on: an empty or all-whitespace line, a line at
 * or past VIEWER_COMMAND_LINE_MAX, an unrecognized command word, a value
 * that does not parse as an integer or does not fit `uint32_t`, or a
 * line carrying anything after the value but whitespace.
 */
int viewer_command_parse(const char *line, size_t len, struct viewer_command *out,
                         char *error, size_t error_cap);

#endif
