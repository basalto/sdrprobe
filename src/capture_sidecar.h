#ifndef CAPTURE_SIDECAR_H
#define CAPTURE_SIDECAR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_profile.h"

/*
 * What a capture says about itself.
 *
 * A raw .bin is bytes. Reading one correctly needs to know the container --
 * `testfiles/` is 8-bit and `build/testfiles16/` is the same signal at four
 * bytes a pair -- and that fact lived in the sidecar's prose, where nothing
 * could read it. `.scratch/device-model/` makes it a field.
 *
 * **This is not a JSON parser and must not become one.** It looks for three
 * keys by name and ignores everything else, including nesting, because the
 * three it wants are top-level scalars in every sidecar this program writes.
 * A capture whose sidecar is missing, unreadable or silent about its format is
 * the house 8-bit convention, which is what every capture was until now.
 */

struct capture_sidecar {
    enum sample_format format;
    float full_scale;
    /* What was actually found, as opposed to what was assumed. A caller that
       wants to warn needs to tell those apart. */
    int format_stated;
    int full_scale_stated;
};

/* The number that follows `"key":`, or `fallback` when the key is absent or
   its value is not a number. Deliberately tolerant: a null, as several
   reconstructed sidecars carry, reads as absent rather than as zero. */
static inline double capture_sidecar_number(const char *text, const char *key,
                                            double fallback, int *found) {
    char pattern[64];
    if (found)
        *found = 0;
    if (!text || !key)
        return fallback;
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *at = strstr(text, pattern);
    if (!at)
        return fallback;
    at = strchr(at + strlen(pattern), ':');
    if (!at)
        return fallback;
    at++;
    while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r')
        at++;
    if (*at != '-' && *at != '+' && *at != '.' && (*at < '0' || *at > '9'))
        return fallback; /* null, a string, anything that is not a number */
    char *end = NULL;
    double value = strtod(at, &end);
    if (end == at)
        return fallback;
    if (found)
        *found = 1;
    return value;
}

/*
 * The quoted string that follows `"key":`, copied into `out`. Returns 1 when
 * the key was found with a string value, 0 otherwise.
 *
 * Bounded to the value itself, which is the whole point: a key's meaning comes
 * from its own value and never from text that happens to sit near it.
 */
static inline int capture_sidecar_string(const char *text, const char *key,
                                         char *out, size_t out_size) {
    char pattern[64];
    if (!text || !key || !out || out_size == 0)
        return 0;
    out[0] = '\0';
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *at = strstr(text, pattern);
    if (!at)
        return 0;
    at = strchr(at + strlen(pattern), ':');
    if (!at)
        return 0;
    at++;
    while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r')
        at++;
    if (*at != '"')
        return 0; /* null, a number, an array -- not a string */
    at++;
    size_t n = 0;
    while (*at && *at != '"' && n + 1 < out_size) {
        if (*at == '\\' && at[1])
            at++;
        out[n++] = *at++;
    }
    out[n] = '\0';
    return 1;
}

/*
 * Read the container out of a sidecar's text.
 *
 * `bytes_per_pair` decides it when present, because it is a number and cannot
 * be misread. Otherwise the `format` string is searched for "16-bit", which is
 * what every sidecar this program has ever written says in prose. Anything
 * else is the house convention.
 *
 * Returns 0 on success, negative when the sidecar describes a container whose
 * full scale it does not also state. That refusal is the whole point: two
 * different 16-bit sources rail at 2040.0 and 2047.5, `device_default_full_scale`
 * returns 0 for S16 rather than guessing, and a capture that cannot say which
 * it is cannot be read in dBFS at all.
 */
static inline int capture_sidecar_parse(const char *text,
                                        struct capture_sidecar *out) {
    if (!out)
        return -1;

    out->format = SAMPLE_FORMAT_U8;
    out->full_scale = device_default_full_scale(SAMPLE_FORMAT_U8);
    out->format_stated = 0;
    out->full_scale_stated = 0;
    if (!text)
        return 0;

    int found = 0;
    double width = capture_sidecar_number(text, "bytes_per_pair", 0.0, &found);
    if (found && width > 0.0) {
        out->format_stated = 1;
        if (width == 4.0)
            out->format = SAMPLE_FORMAT_S16;
        else if (width == 8.0)
            out->format = SAMPLE_FORMAT_CF32;
        else
            out->format = SAMPLE_FORMAT_U8;
    } else {
        /*
         * The *value* of the format key, not merely text near it. An earlier
         * version searched within eighty characters and a note reading "the
         * Fire code is a 16-bit shortened cyclic code" sat inside that window,
         * which would have read an 8-bit capture as four bytes a pair -- a
         * sentence about error correction changing how samples are decoded.
         */
        char value[160];
        if (capture_sidecar_string(text, "format", value, sizeof(value)) &&
            strstr(value, "16-bit")) {
            out->format = SAMPLE_FORMAT_S16;
            out->format_stated = 1;
        }
    }

    double scale = capture_sidecar_number(text, "full_scale", 0.0, &found);
    if (found && scale > 0.0) {
        out->full_scale = (float)scale;
        out->full_scale_stated = 1;
    } else {
        out->full_scale = device_default_full_scale(out->format);
        if (!(out->full_scale > 0.0f))
            return -1; /* S16 with nothing to say what its rail is */
    }
    return 0;
}

/* `<path>.bin` -> `<path>.json`, the rule acquisition.c writes by. */
static inline void capture_sidecar_path(const char *bin_path, char *out,
                                        size_t out_size) {
    size_t n = bin_path ? strlen(bin_path) : 0;
    if (n > 4 && strcmp(bin_path + n - 4, ".bin") == 0)
        snprintf(out, out_size, "%.*s.json", (int)(n - 4), bin_path);
    else
        snprintf(out, out_size, "%s.json", bin_path ? bin_path : "");
}

/*
 * The sidecar beside a capture, if there is one. A missing sidecar is not an
 * error -- most captures predate them -- and gives the house convention.
 * Returns 0 when `out` is usable, negative only when a sidecar exists and
 * describes a container it cannot give a full scale for.
 */
static inline int capture_sidecar_read(const char *bin_path,
                                       struct capture_sidecar *out) {
    char path[1024];
    char text[8192];

    if (!out)
        return -1;
    capture_sidecar_parse(NULL, out); /* the defaults */
    if (!bin_path)
        return 0;

    capture_sidecar_path(bin_path, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    size_t got = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[got] = '\0';
    return capture_sidecar_parse(text, out);
}

#endif /* CAPTURE_SIDECAR_H */
