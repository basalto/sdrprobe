#define _POSIX_C_SOURCE 200809L

#include "viewer_command.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_cap, const char *msg) {
    if (!error || error_cap == 0)
        return;
    snprintf(error, error_cap, "%s", msg);
}

int viewer_command_parse(const char *line, size_t len, struct viewer_command *out,
                         char *error, size_t error_cap) {
    char buf[VIEWER_COMMAND_LINE_MAX];
    char word[32];
    int word_consumed = 0;
    size_t trimmed_len;
    size_t i;
    char *value_start;
    char *value_end;
    unsigned long value;

    /* Trailing whitespace or control bytes only -- a Viewer typing into a
       text box can leave a trailing \r or \n even though the WebSocket
       frame boundary already ended the message; leading whitespace is
       sscanf's problem below, not this parser's to strip twice. */
    trimmed_len = len;
    while (trimmed_len > 0 && (unsigned char)line[trimmed_len - 1] <= ' ')
        trimmed_len--;
    if (trimmed_len == 0) {
        set_error(error, error_cap, "empty command");
        return -1;
    }
    if (trimmed_len >= sizeof(buf)) {
        set_error(error, error_cap, "command line too long");
        return -1;
    }
    memcpy(buf, line, trimmed_len);
    buf[trimmed_len] = '\0';

    if (sscanf(buf, "%31s%n", word, &word_consumed) != 1) {
        set_error(error, error_cap, "malformed command");
        return -1;
    }
    if (strcmp(word, "view") == 0) {
        char screen[32];
        int screen_consumed = 0;
        size_t i;

        value_start = buf + word_consumed;
        while (*value_start == ' ' || *value_start == '\t')
            value_start++;
        if (sscanf(value_start, "%31s%n", screen, &screen_consumed) != 1) {
            set_error(error, error_cap, "view requires a screen name");
            return -1;
        }
        for (i = 0; value_start[screen_consumed + i] != '\0'; i++) {
            if (!isspace((unsigned char)value_start[screen_consumed + i])) {
                set_error(error, error_cap, "unexpected trailing field");
                return -1;
            }
        }
        if (strcmp(screen, "scope") == 0) {
            out->type = VIEWER_COMMAND_VIEW;
            out->screen = VIEWER_SCREEN_SCOPE;
            return 0;
        }
        if (strcmp(screen, "survey") == 0) {
            out->type = VIEWER_COMMAND_VIEW;
            out->screen = VIEWER_SCREEN_SURVEY;
            return 0;
        }
        set_error(error, error_cap, "unrecognized screen");
        return -1;
    }
    if (strcmp(word, "tune") != 0) {
        set_error(error, error_cap, "unrecognized command");
        return -1;
    }

    value_start = buf + word_consumed;
    while (*value_start == ' ' || *value_start == '\t')
        value_start++;
    if (*value_start == '\0') {
        set_error(error, error_cap, "tune requires one frequency in Hz");
        return -1;
    }
    if (!isdigit((unsigned char)*value_start)) {
        set_error(error, error_cap, "frequency must be a positive integer");
        return -1;
    }

    /*
     * strtoul() rather than sscanf's own %lu: a numeric conversion that
     * overflows what it converts to is undefined behaviour in sscanf
     * (C11 7.21.6.2p10), and this is parsing bytes an attacker-controlled
     * Viewer sent over a socket -- the same posture CLAUDE.md states for
     * every parser reading bytes from outside the process. strtoul()'s
     * overflow behaviour is defined instead: it clamps to ULONG_MAX and
     * sets errno.
     */
    errno = 0;
    value = strtoul(value_start, &value_end, 10);
    if (value_end == value_start) {
        set_error(error, error_cap, "frequency must be a positive integer");
        return -1;
    }
    for (i = 0; value_end[i] != '\0'; i++) {
        if (!isspace((unsigned char)value_end[i])) {
            set_error(error, error_cap, "unexpected trailing field");
            return -1;
        }
    }
    if (errno == ERANGE || value > UINT32_MAX) {
        set_error(error, error_cap, "frequency out of range");
        return -1;
    }

    out->type = VIEWER_COMMAND_TUNE;
    out->hz = (uint32_t)value;
    return 0;
}
