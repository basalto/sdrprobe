#define _POSIX_C_SOURCE 200809L

#include "server/viewer_command.h"

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

/*
 * The screen names, as a table rather than a chain of `strcmp`s.
 *
 * Two names were two `if`s, which was unremarkable; ticket 07 already
 * recorded what the same shape costs one file over, where
 * `handle_subscribe_line()`'s hand-matched list "had no branch for
 * survey_spectrum or survey_state, so a client subscribing to them received
 * nothing, silently". A table is what a check can walk, and adding a screen
 * is then a row rather than a branch somebody has to remember to write.
 */
static const struct {
    const char *name;
    enum viewer_screen screen;
} viewer_screens[] = {
    { "scope",  VIEWER_SCREEN_SCOPE },
    { "survey", VIEWER_SCREEN_SURVEY },
    { "fm",     VIEWER_SCREEN_FM },
    { "gsm",    VIEWER_SCREEN_GSM },
    { "adsb",   VIEWER_SCREEN_ADSB },
    { "tetra",  VIEWER_SCREEN_TETRA },
    { "srd",    VIEWER_SCREEN_SRD },
    { "lte",    VIEWER_SCREEN_LTE },
    { "settings", VIEWER_SCREEN_SETTINGS },
    { "calibration", VIEWER_SCREEN_CALIBRATION }
};

/*
 * The Settings fields `set` can name, and what each accepts.
 *
 * A table for the reason the screen names are one: a hand-written branch per
 * name is what left the subscribe parser two names short, silently
 * (`web-visualization/12`). `lo`/`hi` bound the value, and `boolean` says
 * the field takes `on`/`off` as well as 1/0 -- typing `set dc on` is what a
 * person does, and refusing it to save a line of parsing would be a rule
 * nobody wants.
 */
static const struct {
    const char *name;
    enum viewer_setting setting;
    int lo, hi;
    int boolean;
} viewer_settings[] = {
    /* The same bound `apply_settings()` enforces, stated here so a command
       is refused where it was typed rather than two layers later. */
    { "ppm",   VIEWER_SETTING_PPM,   -1000, 1000, 0 },
    { "fft",   VIEWER_SETTING_FFT,     256, 16384, 0 },
    /* An index into the device's own gain list, where 0 is automatic. The
       upper bound is the device's and cannot be stated here, so it is left
       wide and refused by the applier, which has the profile. */
    { "gain",  VIEWER_SETTING_GAIN,      0, 1000, 0 },
    { "dc",    VIEWER_SETTING_DC,        0, 1, 1 },
    { "drift", VIEWER_SETTING_DRIFT,     0, 1, 1 }
};

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
        for (i = 0; i < sizeof(viewer_screens) / sizeof(viewer_screens[0]);
             i++) {
            if (strcmp(screen, viewer_screens[i].name) == 0) {
                out->type = VIEWER_COMMAND_VIEW;
                out->screen = viewer_screens[i].screen;
                return 0;
            }
        }
        set_error(error, error_cap, "unrecognized screen");
        return -1;
    }
    if (strcmp(word, "calibrate") == 0) {
        static const struct {
            const char *name;
            enum viewer_calibrate reference;
        } references[] = {
            { "gsm",  VIEWER_CALIBRATE_GSM },
            { "lte",  VIEWER_CALIBRATE_LTE },
            { "stop", VIEWER_CALIBRATE_STOP }
        };
        char which[32];
        int which_consumed = 0;
        size_t k;

        value_start = buf + word_consumed;
        while (*value_start == ' ' || *value_start == '\t')
            value_start++;
        if (sscanf(value_start, "%31s%n", which, &which_consumed) != 1) {
            set_error(error, error_cap,
                      "calibrate requires gsm, lte or stop");
            return -1;
        }
        for (i = 0; value_start[which_consumed + i] != '\0'; i++) {
            if (!isspace((unsigned char)value_start[which_consumed + i])) {
                set_error(error, error_cap, "unexpected trailing field");
                return -1;
            }
        }
        for (k = 0; k < sizeof(references) / sizeof(references[0]); k++)
            if (strcmp(which, references[k].name) == 0) {
                out->type = VIEWER_COMMAND_CALIBRATE;
                out->reference = references[k].reference;
                return 0;
            }
        set_error(error, error_cap, "unrecognized reference");
        return -1;
    }
    if (strcmp(word, "apply") == 0) {
        for (i = (size_t)word_consumed; buf[i] != '\0'; i++) {
            if (!isspace((unsigned char)buf[i])) {
                set_error(error, error_cap, "unexpected trailing field");
                return -1;
            }
        }
        out->type = VIEWER_COMMAND_APPLY;
        return 0;
    }
    if (strcmp(word, "set") == 0) {
        char field[32], text[32];
        int field_consumed = 0, text_consumed = 0;
        size_t k;

        value_start = buf + word_consumed;
        while (*value_start == ' ' || *value_start == '\t')
            value_start++;
        if (sscanf(value_start, "%31s%n", field, &field_consumed) != 1) {
            set_error(error, error_cap,
                      "set requires a field and a value");
            return -1;
        }
        value_start += field_consumed;
        while (*value_start == ' ' || *value_start == '\t')
            value_start++;
        if (sscanf(value_start, "%31s%n", text, &text_consumed) != 1) {
            set_error(error, error_cap, "set requires a value");
            return -1;
        }
        for (i = 0; value_start[text_consumed + i] != '\0'; i++) {
            if (!isspace((unsigned char)value_start[text_consumed + i])) {
                set_error(error, error_cap, "unexpected trailing field");
                return -1;
            }
        }
        for (k = 0; k < sizeof(viewer_settings) / sizeof(viewer_settings[0]);
             k++) {
            long v;
            char *end;

            if (strcmp(field, viewer_settings[k].name) != 0)
                continue;
            if (viewer_settings[k].boolean) {
                if (strcmp(text, "on") == 0 || strcmp(text, "1") == 0)
                    v = 1;
                else if (strcmp(text, "off") == 0 || strcmp(text, "0") == 0)
                    v = 0;
                else {
                    set_error(error, error_cap, "value must be on or off");
                    return -1;
                }
            } else {
                errno = 0;
                v = strtol(text, &end, 10);
                if (end == text || *end != '\0' || errno == ERANGE) {
                    set_error(error, error_cap, "value must be an integer");
                    return -1;
                }
                if (v < viewer_settings[k].lo || v > viewer_settings[k].hi) {
                    set_error(error, error_cap, "value out of range");
                    return -1;
                }
            }
            out->type = VIEWER_COMMAND_SET;
            out->setting = viewer_settings[k].setting;
            out->value = (int)v;
            return 0;
        }
        set_error(error, error_cap, "unrecognized setting");
        return -1;
    }
    if (strcmp(word, "scan") == 0) {
        static const struct {
            const char *name;
            enum viewer_scan scan;
        } targets[] = {
            { "fm",   VIEWER_SCAN_FM },
            { "lte",  VIEWER_SCAN_LTE },
            { "stop", VIEWER_SCAN_STOP }
        };
        char which[32];
        int which_consumed = 0;
        size_t k;

        value_start = buf + word_consumed;
        while (*value_start == ' ' || *value_start == '\t')
            value_start++;
        if (sscanf(value_start, "%31s%n", which, &which_consumed) != 1) {
            set_error(error, error_cap, "scan requires fm, lte or stop");
            return -1;
        }
        for (i = 0; value_start[which_consumed + i] != '\0'; i++) {
            if (!isspace((unsigned char)value_start[which_consumed + i])) {
                set_error(error, error_cap, "unexpected trailing field");
                return -1;
            }
        }
        for (k = 0; k < sizeof(targets) / sizeof(targets[0]); k++)
            if (strcmp(which, targets[k].name) == 0) {
                out->type = VIEWER_COMMAND_SCAN;
                out->scan = targets[k].scan;
                return 0;
            }
        set_error(error, error_cap, "unrecognized scan target");
        return -1;
    }
    if (strcmp(word, "select") == 0) {
        static const struct {
            const char *name;
            enum viewer_select select;
        } kinds[] = {
            { "arfcn",     VIEWER_SELECT_ARFCN },
            { "cell",      VIEWER_SELECT_CELL },
            { "candidate", VIEWER_SELECT_CANDIDATE }
        };
        char kind[32], text[32];
        int kind_consumed = 0, text_consumed = 0;
        size_t k;
        long v;
        char *end;

        value_start = buf + word_consumed;
        while (*value_start == ' ' || *value_start == '\t')
            value_start++;
        if (sscanf(value_start, "%31s%n", kind, &kind_consumed) != 1) {
            set_error(error, error_cap,
                      "select requires a kind and an index");
            return -1;
        }
        value_start += kind_consumed;
        while (*value_start == ' ' || *value_start == '\t')
            value_start++;
        if (sscanf(value_start, "%31s%n", text, &text_consumed) != 1) {
            set_error(error, error_cap, "select requires an index");
            return -1;
        }
        for (i = 0; value_start[text_consumed + i] != '\0'; i++) {
            if (!isspace((unsigned char)value_start[text_consumed + i])) {
                set_error(error, error_cap, "unexpected trailing field");
                return -1;
            }
        }
        errno = 0;
        v = strtol(text, &end, 10);
        if (end == text || *end != '\0' || errno == ERANGE || v < 0 ||
            v > 100000) {
            set_error(error, error_cap, "index must be a non-negative integer");
            return -1;
        }
        for (k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
            if (strcmp(kind, kinds[k].name) == 0) {
                out->type = VIEWER_COMMAND_SELECT;
                out->select = kinds[k].select;
                out->value = (int)v;
                return 0;
            }
        set_error(error, error_cap, "unrecognized select kind");
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
