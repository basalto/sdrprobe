#include "options.h"
#include "sdr_dsp.h"

#include "gsm_dsp.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The LTE sample grid, spelled here as an integer so this file stays free of
   the DSP headers -- the same reason parse_gsm_features takes its flags as
   arguments. lte_dsp.h derives it from 128 subcarriers of 15 kHz. */
#define LTE_SAMPLE_RATE_HZ_U32 1920000U

void usage(const char *program) {
    fprintf(stderr,
            "Usage: %s [--frequency Hz|K|M|G] [--sample-rate samples_per_second]\n"
            "          [--gain max|auto|dB] [--ppm signed_integer]\n"
            "          [--file capture.bin] [--device index]\n"
            "          [--view magnitude|spectrum|scatter|waterfall|survey|gsm|adsb|lte|fm|\n"
            "                  calibration|settings|help]\n"
            "          [--record-seconds n] [--technology gsm|adsb|lte|fm|raw]\n"
            "          [--debug-log FILE|-] [--analysis] [--fm-scan] [--fm-play]\n"
            "          [--survey-select n] [--survey-bands] [--survey-band n]\n"
            "          [--zoom from:to] [--fft points]\n"
            "          [--antenna name] [--site name]\n"
            "          [--arfcn 1-124] [--earfcn n] [--lte-scan band]\n"
            "          [--gsm-features list]\n"
            "          [--dc-filter on|off]\n"
            "          [--survey-range low:high] [--survey-dwell seconds]\n"
            "          [--duration n] [--once] [--headless] [--decode]\n"
            "          [--screenshot file.png]\n"
            "          [--list-devices]\n"
            "\n"
            "  --view            screen to open on\n"
            "  --record-seconds  record raw I/Q to captures/ from startup,\n"
            "                    with a .json sidecar describing the tuning\n"
            "  --lte-chain       walk the LTE decode chain over a live cell,\n"
            "                    printing what every stage produced per block\n"
            "  --lte-chain-band  scan this band and walk its strongest cell\n"
            "  --lte-chain-seconds  how long to walk it for\n"
            "  --calibrate       gsm|lte: measure the receiver's frequency\n"
            "                    error with no window and print every step\n"
            "  --calibrate-band  scan this LTE band and use its strongest cell\n"
            "  --calibrate-seconds  give up after this long if it never locks\n"
            "  --survey-watch    keep sweeping this many times, folding each\n"
            "                    into the site's history and saying what changed\n"
            "  --survey-save     write the sweep to surveys/ and fold it into\n"
            "                    what the site has heard, as Save does\n"
            "  --survey-confirm  after a --survey-range sweep, ask again about\n"
            "                    every signal it found, and say which held up\n"
            "  --antenna         name the antenna in use; saved, and reported\n"
            "                    by a survey so two sweeps can be compared\n"
            "  --site            name where the receiver is; saved likewise. A\n"
            "                    survey taken somewhere else is not a baseline\n"
            "  --technology      what that recording is labelled; defaults to\n"
            "                    --view, or raw when there is no view\n"
            "  --duration        quit after n seconds\n"
            "  --headless        acquire with no window; pair with\n"
            "                    --record-seconds to capture from a script\n"
            "  --arfcn           tune to a GSM 900 downlink channel (its\n"
            "                    carrier sits 400 kHz above the tuned centre)\n"
            "  --earfcn          tune to an LTE downlink carrier, centred on\n"
            "                    it, and set the 1.92 MS/s LTE sample grid\n"
            "  --lte-scan        headless: walk an LTE band's channels and\n"
            "                    print the cells found (band 8, 20 or 28)\n"
            "  --gsm-features    SCH decoder refinements to enable: any of\n"
            "                    filter,finecfo,trellis, or none (default all)\n"
            "  --dc-filter       spectrum/waterfall DC-spike removal\n"
            "  --survey-range    open the band survey on this range and sweep\n"
            "                    it, for example 88M:108M\n"
            "  --survey-dwell    seconds to sit on each step; longer catches\n"
            "                    transmitters that are not always on\n"
            "  --once            play a capture through once instead of looping\n"
            "  --decode          headless: print decoded messages to stdout\n"
            "  --screenshot      write the last frame to a PNG before quitting,\n"
            "                    so a view can be looked at without a person;\n"
            "                    pair with --duration\n"
            "  --list-devices    print the receivers found, and exit\n"
            "  --version         print the version, and exit\n",
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
        { "adsb", START_VIEW_ADSB },
        { "lte", START_VIEW_LTE },
        { "fm", START_VIEW_FM },
        { "calibration", START_VIEW_CALIBRATION },
        { "settings", START_VIEW_SETTINGS },
        { "help", START_VIEW_HELP }
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
        } else if (strcmp(option, "--screenshot") == 0) {
            if (options->screenshot_path || i + 1 >= argc)
                return -1;
            options->screenshot_path = argv[++i];
            if (!options->screenshot_path[0])
                return -1;
        } else if (strcmp(option, "--lte-scan") == 0) {
            if (options->lte_scan_band || i + 1 >= argc ||
                parse_int(argv[++i], &options->lte_scan_band) < 0)
                return -1;
            /* Only the bands a dongle can reach. The channel map knows more,
               but a scan of a band the tuner cannot hear finds nothing and
               takes a minute to say so. */
            if (options->lte_scan_band != 8 && options->lte_scan_band != 20 &&
                options->lte_scan_band != 28)
                return -1;
        } else if (strcmp(option, "--earfcn") == 0) {
            /* The range is every band the plugin knows, checked properly
               against the table in main(); this only refuses what could not
               be a channel number at all. */
            if (options->earfcn || i + 1 >= argc ||
                parse_int(argv[++i], &options->earfcn) < 0 ||
                options->earfcn < 0 || options->earfcn > 65535)
                return -1;
            if (options->earfcn == 0)
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
        } else if (strcmp(option, "--survey") == 0) {
            if (options->survey_report)
                return -1;
            options->survey_report = 1;
        } else if (strcmp(option, "--once") == 0) {
            if (options->play_once)
                return -1;
            options->play_once = 1;
        } else if (strcmp(option, "--fm-play") == 0) {
            options->fm_play = 1;
        } else if (strcmp(option, "--fm-scan") == 0) {
            options->fm_scan = 1;
        } else if (strcmp(option, "--fft") == 0) {
            if (options->fft_size || i + 1 >= argc)
                return -1;
            if (parse_int(argv[++i], &options->fft_size) < 0 ||
                !sdr_dsp_fft_size_valid(options->fft_size))
                return -1;
        } else if (strcmp(option, "--zoom") == 0) {
            /* "from:to", split into a buffer of our own -- argv belongs to
               the caller and writing a terminator into it is not ours to do. */
            char range[64];
            char *colon;

            if (options->zoom_to_hz || i + 1 >= argc)
                return -1;
            snprintf(range, sizeof(range), "%s", argv[++i]);
            colon = strchr(range, ':');
            if (!colon)
                return -1;
            *colon = '\0';
            if (parse_frequency(range, &options->zoom_from_hz) < 0 ||
                parse_frequency(colon + 1, &options->zoom_to_hz) < 0 ||
                options->zoom_to_hz <= options->zoom_from_hz)
                return -1;
        } else if (strcmp(option, "--survey-band") == 0) {
            if (options->survey_band || i + 1 >= argc)
                return -1;
            if (parse_int(argv[++i], &options->survey_band) < 0 ||
                options->survey_band < 1)
                return -1;
        } else if (strcmp(option, "--survey-bands") == 0) {
            options->survey_bands = 1;
        } else if (strcmp(option, "--survey-select") == 0) {
            if (options->survey_select || i + 1 >= argc)
                return -1;
            if (parse_int(argv[++i], &options->survey_select) < 0 ||
                options->survey_select < 1)
                return -1;
        } else if (strcmp(option, "--analysis") == 0) {
            options->analysis = 1;
        } else if (strcmp(option, "--debug-log") == 0) {
            if (options->debug_log || i + 1 >= argc)
                return -1;
            options->debug_log = argv[++i];
            if (!*options->debug_log)
                return -1;
        } else if (strcmp(option, "--technology") == 0) {
            if (options->technology || i + 1 >= argc)
                return -1;
            options->technology = argv[++i];
            if (strcmp(options->technology, "gsm") != 0 &&
                strcmp(options->technology, "adsb") != 0 &&
                strcmp(options->technology, "lte") != 0 &&
                strcmp(options->technology, "fm") != 0 &&
                strcmp(options->technology, "raw") != 0)
                return -1;
        } else if (strcmp(option, "--survey-save") == 0) {
            options->survey_save = 1;
        } else if (strcmp(option, "--survey-confirm") == 0) {
            options->survey_confirm = 1;
        } else if (strcmp(option, "--lte-chain") == 0) {
            options->lte_chain = 1;
        } else if (strcmp(option, "--lte-chain-band") == 0) {
            long band;
            char *end;
            if (options->lte_chain_band || i + 1 >= argc)
                return -1;
            band = strtol(argv[++i], &end, 10);
            if (*end || band < 1 || band > 100)
                return -1;
            options->lte_chain_band = (int)band;
        } else if (strcmp(option, "--lte-chain-seconds") == 0) {
            if (options->lte_chain_seconds > 0.0 || i + 1 >= argc ||
                parse_seconds(argv[++i], &options->lte_chain_seconds) < 0 ||
                options->lte_chain_seconds <= 0.0)
                return -1;
        } else if (strcmp(option, "--calibrate") == 0) {
            if (options->calibrate || i + 1 >= argc)
                return -1;
            if (!strcmp(argv[i + 1], "gsm"))
                options->calibrate = 1;
            else if (!strcmp(argv[i + 1], "lte"))
                options->calibrate = 2;
            else
                return -1;
            i++;
        } else if (strcmp(option, "--calibrate-band") == 0) {
            long band;
            char *end;
            if (options->calibrate_band || i + 1 >= argc)
                return -1;
            band = strtol(argv[++i], &end, 10);
            if (*end || band < 1 || band > 100)
                return -1;
            options->calibrate_band = (int)band;
        } else if (strcmp(option, "--calibrate-seconds") == 0) {
            if (options->calibrate_seconds > 0.0 || i + 1 >= argc ||
                parse_seconds(argv[++i], &options->calibrate_seconds) < 0 ||
                options->calibrate_seconds <= 0.0)
                return -1;
        } else if (strcmp(option, "--survey-watch") == 0) {
            long sweeps;
            char *end;
            if (options->survey_watch || i + 1 >= argc)
                return -1;
            sweeps = strtol(argv[++i], &end, 10);
            if (*end || sweeps < 1 || sweeps > 100000)
                return -1;
            options->survey_watch = (int)sweeps;
        } else if (strcmp(option, "--antenna") == 0) {
            if (options->antenna || i + 1 >= argc || !*argv[i + 1])
                return -1;
            options->antenna = argv[++i];
        } else if (strcmp(option, "--site") == 0) {
            if (options->site || i + 1 >= argc || !*argv[i + 1])
                return -1;
            options->site = argv[++i];
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
        } else if (strcmp(option, "--version") == 0) {
            /* Reachable without a window, like every other answer this
               program gives (ADR-0012): a version only in a corner of a
               screenshot is not something a script can report. */
            if (options->show_version)
                return -1;
            options->show_version = 1;
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
    /* --survey-range names a range to sweep; on its own it also opens the
       survey view, which needs a window. With --survey it is a range for the
       headless sweep instead, and no view is implied. */
    if (options->headless && (view_seen ||
                              (options->survey_seen && !options->survey_report)))
        return -1;
    if (options->survey_seen && view_seen &&
        options->view != START_VIEW_SURVEY)
        return -1;
    /* Asking again is about what a sweep found, so there has to be one. */
    if (options->survey_confirm && !options->survey_seen)
        return -1;
    /* The chain needs a cell to walk: an EARFCN, or a band to find one in. */
    if (options->lte_chain && !options->earfcn && !options->lte_chain_band)
        return -1;
    if (options->lte_chain && options->earfcn && options->lte_chain_band)
        return -1;
    if ((options->lte_chain_band || options->lte_chain_seconds > 0.0) &&
        !options->lte_chain)
        return -1;
    /* Its own run, like the calibration and the survey. */
    if (options->lte_chain && (options->calibrate || options->survey_seen ||
                               options->decode || options->lte_scan_band))
        return -1;

    /*
     * A calibration needs to know what to point at: an ARFCN for GSM, and for
     * LTE either an EARFCN or a band to find one in.
     */
    if (options->calibrate == 1 && !options->arfcn &&
        options->view != START_VIEW_CALIBRATION)
        return -1;
    if (options->calibrate == 2 && !options->earfcn &&
        !options->calibrate_band && options->view != START_VIEW_CALIBRATION)
        return -1;
    if (options->calibrate == 2 && options->earfcn && options->calibrate_band)
        return -1;   /* two answers to one question */
    if (options->calibrate_band && options->calibrate != 2)
        return -1;
    if (options->calibrate_seconds > 0.0 && !options->calibrate)
        return -1;
    /* And it is a headless run of its own, not something to bolt onto a
       sweep or a decode -- unless it is naming the technology for
       `--view calibration`, which opens the overlay to be looked at. */
    if (options->calibrate && options->view != START_VIEW_CALIBRATION &&
        (options->survey_seen || options->decode || options->lte_scan_band))
        return -1;
    if (options->calibrate && options->view == START_VIEW_CALIBRATION &&
        options->headless)
        return -1;

    /* Saving is saving a sweep, so there has to be one. */
    if (options->survey_save && !options->survey_report)
        return -1;

    /* And a watch is a sweep repeated, so likewise. */
    if (options->survey_watch && !options->survey_seen)
        return -1;
    /* Both at once would ask again in the middle of a watch, retuning the
       receiver away from a sweep that is about to restart. */
    if (options->survey_watch && options->survey_confirm)
        return -1;
    /* An ARFCN is a frequency; naming both leaves no way to say which wins. */
    if (options->arfcn && frequency_seen)
        return -1;
    if (options->earfcn && frequency_seen)
        return -1;
    if (options->earfcn && options->arfcn)
        return -1;
    /* Looping is a property of playback, and decoding needs to know which
       decoder to run: --technology names it, and --arfcn implies GSM. */
    if (options->play_once && !options->file_path)
        return -1;
    if (options->decode && !options->headless)
        return -1;
    /* A picture of a frame needs a frame. */
    if (options->screenshot_path && options->headless)
        return -1;
    /* And a run that never ends never takes one, which reads as a hang. */
    if (options->screenshot_path && options->duration_seconds <= 0.0)
        return -1;
    /* A headless survey prints candidates; a headless decode prints messages.
       Asking for both leaves two things interleaved on one stdout. */
    if (options->survey_report && !options->headless)
        return -1;
    if (options->survey_report && options->decode)
        return -1;
    /* A scan walks the band and prints what it found; a decode prints
       messages from one tuning. Asking for both interleaves two things on one
       stdout, and the scan is retuning under the decode's feet besides. */
    if (options->lte_scan_band && !options->headless)
        return -1;
    if (options->lte_scan_band && (options->decode || options->survey_report))
        return -1;
    /* And it needs a receiver: a capture holds one tuning. */
    if (options->lte_scan_band && options->file_path)
        return -1;
    if (options->lte_scan_band && (options->earfcn || frequency_seen))
        return -1;
    /* Sweeping a receiver needs to be told what to sweep -- the whole tuner
       takes four minutes and is nobody's intended default. A capture holds one
       tuning, so its own span is the only range there is. */
    if (options->survey_report && !options->file_path && !options->survey_seen)
        return -1;
    if (options->survey_report && options->file_path && options->survey_seen)
        return -1;
    /* A decode has to know which decoder to run. --technology names it; a
       channel number implies it. */
    if (options->decode && !options->technology && !options->arfcn &&
        !options->earfcn)
        return -1;
    if (options->arfcn) {
        if (options->technology && strcmp(options->technology, "gsm") != 0)
            return -1;
        options->technology = "gsm";
    }
    /* An EARFCN implies LTE, and LTE implies its own sample rate: 1.92 MS/s
       is 128 subcarriers of 15 kHz, and the plugin refuses anything else
       rather than resampling (ADR-0014). Setting it here means a capture
       recorded with --earfcn is on the right grid without being asked, and an
       explicit --sample-rate that disagrees is a contradiction rather than
       something to silently override. */
    if (options->earfcn) {
        if (options->technology && strcmp(options->technology, "lte") != 0)
            return -1;
        options->technology = "lte";
    }
    if (options->lte_scan_band) {
        if (options->technology && strcmp(options->technology, "lte") != 0)
            return -1;
        options->technology = "lte";
    }
    if (options->technology && strcmp(options->technology, "lte") == 0) {
        if (sample_rate_seen && options->sample_rate != LTE_SAMPLE_RATE_HZ_U32)
            return -1;
        options->sample_rate = LTE_SAMPLE_RATE_HZ_U32;
    }
    return 0;
}
