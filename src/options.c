#include "options.h"

#include "gsm_dsp.h"

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
            "          [--file capture.bin] [--device index]\n"
            "          [--view magnitude|spectrum|scatter|waterfall|survey|gsm|adsb]\n"
            "          [--record-seconds n] [--technology gsm|adsb|raw]\n"
            "          [--arfcn 1-124] [--gsm-features list] [--dc-filter on|off]\n"
            "          [--survey-range low:high] [--survey-dwell seconds]\n"
            "          [--duration n] [--once] [--headless] [--decode]\n"
            "          [--list-devices]\n"
            "\n"
            "  --view            screen to open on\n"
            "  --record-seconds  record raw I/Q to captures/ from startup,\n"
            "                    with a .json sidecar describing the tuning\n"
            "  --technology      what that recording is labelled; defaults to\n"
            "                    --view, or raw when there is no view\n"
            "  --duration        quit after n seconds\n"
            "  --headless        acquire with no window; pair with\n"
            "                    --record-seconds to capture from a script\n"
            "  --arfcn           tune to a GSM 900 downlink channel (its\n"
            "                    carrier sits 400 kHz above the tuned centre)\n"
            "  --gsm-features    SCH decoder refinements to enable: any of\n"
            "                    filter,finecfo,trellis, or none (default all)\n"
            "  --dc-filter       spectrum/waterfall DC-spike removal\n"
            "  --survey-range    open the band survey on this range and sweep\n"
            "                    it, for example 88M:108M\n"
            "  --survey-dwell    seconds to sit on each step; longer catches\n"
            "                    transmitters that are not always on\n"
            "  --once            play a capture through once instead of looping\n"
            "  --decode          headless: print decoded messages to stdout\n"
            "  --list-devices    print the receivers found, and exit\n",
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

int parse_seconds(const char *text, double *value) {
    char *end = NULL;
    double parsed;

    if (!text || !text[0])
        return -1;
    errno = 0;
    parsed = strtod(text, &end);
    if (errno || !end || *end || !isfinite(parsed) || parsed <= 0.0 ||
        parsed > MAX_RUN_SECONDS)
        return -1;
    *value = parsed;
    return 0;
}

int parse_view(const char *text, enum start_view *view) {
    static const struct {
        const char *name;
        enum start_view view;
    } names[] = {
        { "magnitude", START_VIEW_MAGNITUDE },
        { "spectrum", START_VIEW_SPECTRUM },
        { "scatter", START_VIEW_SCATTER },
        { "waterfall", START_VIEW_WATERFALL },
        { "survey", START_VIEW_SURVEY },
        { "gsm", START_VIEW_GSM },
        { "adsb", START_VIEW_ADSB }
    };

    if (!text)
        return -1;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (strcmp(text, names[i].name) == 0) {
            *view = names[i].view;
            return 0;
        }
    }
    return -1;
}

int parse_gsm_features(const char *text, int filter_flag, int finecfo_flag,
                       int trellis_flag, int *mask) {
    char buffer[64];
    char *token;
    char *save = NULL;

    if (!text || !text[0] || strlen(text) >= sizeof(buffer))
        return -1;
    snprintf(buffer, sizeof(buffer), "%s", text);
    *mask = 0;
    if (strcmp(buffer, "none") == 0)
        return 0;
    for (token = strtok_r(buffer, ",", &save); token;
         token = strtok_r(NULL, ",", &save)) {
        if (strcmp(token, "filter") == 0)
            *mask |= filter_flag;
        else if (strcmp(token, "finecfo") == 0)
            *mask |= finecfo_flag;
        else if (strcmp(token, "trellis") == 0)
            *mask |= trellis_flag;
        else
            return -1;
    }
    return 0;
}

int parse_switch(const char *text, int *value) {
    if (!text)
        return -1;
    if (strcmp(text, "on") == 0) {
        *value = 1;
        return 0;
    }
    if (strcmp(text, "off") == 0) {
        *value = 0;
        return 0;
    }
    return -1;
}

int parse_options(int argc, char **argv, struct options *options) {
    int frequency_seen = 0;
    int sample_rate_seen = 0;
    int file_seen = 0;
    int device_seen = 0;
    int view_seen = 0;
    int record_seen = 0;
    int duration_seen = 0;

    memset(options, 0, sizeof(*options));
    options->remove_dc = 1;
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
        } else if (strcmp(option, "--device") == 0) {
            if (device_seen || i + 1 >= argc ||
                parse_int(argv[++i], &options->device_index) < 0 ||
                options->device_index < 0 || options->device_index > 31)
                return -1;
            device_seen = 1;
        } else if (strcmp(option, "--view") == 0) {
            if (view_seen || i + 1 >= argc ||
                parse_view(argv[++i], &options->view) < 0)
                return -1;
            view_seen = 1;
        } else if (strcmp(option, "--survey-range") == 0) {
            /* LOW:HIGH, each in the same spellings --frequency takes. */
            if (options->survey_seen || i + 1 >= argc)
                return -1;
            const char *text = argv[++i];
            const char *colon = strchr(text, ':');
            char low[32];
            if (!colon || (size_t)(colon - text) >= sizeof(low))
                return -1;
            memcpy(low, text, (size_t)(colon - text));
            low[colon - text] = '\0';
            if (parse_frequency(low, &options->survey_from_hz) < 0 ||
                parse_frequency(colon + 1, &options->survey_to_hz) < 0 ||
                options->survey_to_hz <= options->survey_from_hz)
                return -1;
            options->survey_seen = 1;
            options->view = START_VIEW_SURVEY;
        } else if (strcmp(option, "--survey-dwell") == 0) {
            if (options->survey_dwell_seconds > 0.0 || i + 1 >= argc ||
                parse_seconds(argv[++i], &options->survey_dwell_seconds) < 0)
                return -1;
        } else if (strcmp(option, "--arfcn") == 0) {
            if (options->arfcn || i + 1 >= argc ||
                parse_int(argv[++i], &options->arfcn) < 0 ||
                options->arfcn < 1 || options->arfcn > 124)
                return -1;
        } else if (strcmp(option, "--gsm-features") == 0) {
            if (options->gsm_features_seen || i + 1 >= argc ||
                parse_gsm_features(argv[++i], GSM_OPT_FILTER, GSM_OPT_FINECFO,
                                   GSM_OPT_TRELLIS,
                                   &options->gsm_features) < 0)
                return -1;
            options->gsm_features_seen = 1;
        } else if (strcmp(option, "--dc-filter") == 0) {
            if (i + 1 >= argc || parse_switch(argv[++i], &options->remove_dc) < 0)
                return -1;
        } else if (strcmp(option, "--decode") == 0) {
            if (options->decode)
                return -1;
            options->decode = 1;
        } else if (strcmp(option, "--once") == 0) {
            if (options->play_once)
                return -1;
            options->play_once = 1;
        } else if (strcmp(option, "--technology") == 0) {
            if (options->technology || i + 1 >= argc)
                return -1;
            options->technology = argv[++i];
            if (strcmp(options->technology, "gsm") != 0 &&
                strcmp(options->technology, "adsb") != 0 &&
                strcmp(options->technology, "raw") != 0)
                return -1;
        } else if (strcmp(option, "--record-seconds") == 0) {
            if (record_seen || i + 1 >= argc ||
                parse_seconds(argv[++i], &options->record_seconds) < 0)
                return -1;
            record_seen = 1;
        } else if (strcmp(option, "--duration") == 0) {
            if (duration_seen || i + 1 >= argc ||
                parse_seconds(argv[++i], &options->duration_seconds) < 0)
                return -1;
            duration_seen = 1;
        } else if (strcmp(option, "--headless") == 0) {
            if (options->headless)
                return -1;
            options->headless = 1;
        } else if (strcmp(option, "--list-devices") == 0) {
            if (options->list_devices)
                return -1;
            options->list_devices = 1;
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
    /* A capture cannot come from a receiver that is not being opened, and a
       window that is not being opened has no screen to start on. */
    if (options->file_path && device_seen)
        return -1;
    if (options->headless && (view_seen || options->survey_seen))
        return -1;
    if (options->survey_seen && view_seen &&
        options->view != START_VIEW_SURVEY)
        return -1;
    /* An ARFCN is a frequency; naming both leaves no way to say which wins. */
    if (options->arfcn && frequency_seen)
        return -1;
    /* Looping is a property of playback, and decoding needs to know which
       decoder to run: --technology names it, and --arfcn implies GSM. */
    if (options->play_once && !options->file_path)
        return -1;
    if (options->decode && !options->headless)
        return -1;
    if (options->decode && !options->technology && !options->arfcn)
        return -1;
    if (options->arfcn) {
        if (options->technology && strcmp(options->technology, "gsm") != 0)
            return -1;
        options->technology = "gsm";
    }
    return 0;
}
