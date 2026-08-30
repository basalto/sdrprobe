#include "options.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void usage(const char *program) {
    fprintf(stderr,
            "Usage: %s [--frequency Hz|K|M|G] [--sample-rate samples_per_second]\n"
            "          [--gain max|auto|dB] [--ppm signed_integer]\n"
            "          [--file capture.bin]\n",
            program);
}

int parse_int(const char *text, int *value) {
    char *end = NULL;
    long parsed;
    if (!text || !text[0])
        return -1;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || !end || *end || parsed < INT_MIN || parsed > INT_MAX)
        return -1;
    *value = (int)parsed;
    return 0;
}

int parse_u32(const char *text, uint32_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    if (!text || !text[0] || text[0] == '-' || text[0] == '+')
        return -1;
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno || !end || *end || parsed == 0 || parsed > UINT32_MAX)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

int parse_frequency(const char *text, uint32_t *value) {
    char *end = NULL;
    double parsed;
    double multiplier = 1.0;
    double hz;

    if (!text || !text[0] || text[0] == '-' || text[0] == '+')
        return -1;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno || !end || end == text || !isfinite(parsed) || parsed <= 0.0)
        return -1;
    if (*end) {
        if (end[1])
            return -1;
        switch (*end) {
        case 'k':
        case 'K':
            multiplier = 1000.0;
            break;
        case 'm':
        case 'M':
            multiplier = 1000000.0;
            break;
        case 'g':
        case 'G':
            multiplier = 1000000000.0;
            break;
        default:
            return -1;
        }
    }
    hz = parsed * multiplier;
    if (!isfinite(hz) || hz < 1.0 || hz > UINT32_MAX ||
        fabs(hz - round(hz)) > 1e-6)
        return -1;
    *value = (uint32_t)llround(hz);
    return 0;
}

int parse_numeric_gain(const char *text, int *tenths) {
    const char *p = text;
    char *end = NULL;
    double value;
    double scaled;
    long long rounded;
    int digits = 0;
    int dots = 0;

    if (!p || !*p)
        return -1;
    if (*p == '+' || *p == '-')
        p++;
    for (; *p; p++) {
        if (*p >= '0' && *p <= '9') {
            digits++;
        } else if (*p == '.' && !dots) {
            dots++;
        } else {
            return -1;
        }
    }
    if (!digits)
        return -1;

    errno = 0;
    value = strtod(text, &end);
    if (errno || !end || *end || !isfinite(value))
        return -1;
    scaled = value * 10.0;
    if (scaled < (double)INT_MIN || scaled > (double)INT_MAX)
        return -1;
    rounded = llround(scaled);
    if (fabs(scaled - (double)rounded) > 1e-7)
        return -1;
    *tenths = (int)rounded;
    return 0;
}

int parse_options(int argc, char **argv, struct options *options) {
    int frequency_seen = 0;
    int sample_rate_seen = 0;
    int file_seen = 0;

    memset(options, 0, sizeof(*options));
    options->frequency = DEFAULT_FREQUENCY;
    options->sample_rate = DEFAULT_SAMPLE_RATE;
    options->gain_kind = GAIN_REQUEST_DEFAULT;

    for (int i = 1; i < argc; i++) {
        const char *option = argv[i];
        const char *argument;

        if (strcmp(option, "--frequency") == 0) {
            if (frequency_seen || i + 1 >= argc)
                return -1;
            argument = argv[++i];
            if (parse_frequency(argument, &options->frequency) < 0)
                return -1;
            frequency_seen = 1;
        } else if (strcmp(option, "--sample-rate") == 0) {
            if (sample_rate_seen || i + 1 >= argc)
                return -1;
            argument = argv[++i];
            if (parse_u32(argument, &options->sample_rate) < 0)
                return -1;
            sample_rate_seen = 1;
        } else if (strcmp(option, "--gain") == 0) {
            if (options->gain_seen || i + 1 >= argc)
                return -1;
            argument = argv[++i];
            if (strcmp(argument, "max") == 0) {
                options->gain_kind = GAIN_REQUEST_MAX;
            } else if (strcmp(argument, "auto") == 0) {
                options->gain_kind = GAIN_REQUEST_AUTO;
            } else {
                if (parse_numeric_gain(argument, &options->gain_tenths) < 0)
                    return -1;
                options->gain_kind = GAIN_REQUEST_NUMERIC;
            }
            options->gain_seen = 1;
        } else if (strcmp(option, "--file") == 0) {
            if (file_seen || i + 1 >= argc)
                return -1;
            options->file_path = argv[++i];
            if (!options->file_path[0])
                return -1;
            file_seen = 1;
        } else if (strcmp(option, "--ppm") == 0) {
            if (options->ppm_seen || i + 1 >= argc ||
                parse_int(argv[++i], &options->ppm) < 0 ||
                options->ppm < -1000 || options->ppm > 1000)
                return -1;
            options->ppm_seen = 1;
        } else {
            return -1;
        }
    }

    if (options->file_path && options->gain_seen)
        return -1;
    return 0;
}
