#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdint.h>

/*
 * Command-line options, and the text parsers behind them.
 *
 * Kept apart from the application because none of it touches application
 * state: these are pure text-to-value functions. The settings and calibration
 * panels reuse parse_int and parse_frequency to read their input fields, so a
 * frequency typed at the prompt and one typed into the UI are parsed by the
 * same code and accept the same spellings.
 */

/* Defaults match dump1090's ADS-B acquisition. */
#define DEFAULT_FREQUENCY 1090000000U
#define DEFAULT_SAMPLE_RATE 2000000U

/* Tenths of a dB. Well below the tuner maximum: a moderate manual gain keeps
   strong nearby signals out of saturation, and the receiver snaps this to the
   nearest gain it supports rather than rejecting it. */
#define DEFAULT_GAIN_TENTHS 300

enum gain_request_kind {
    GAIN_REQUEST_DEFAULT,   /* nearest supported to DEFAULT_GAIN_TENTHS */
    GAIN_REQUEST_MAX,
    GAIN_REQUEST_AUTO,
    GAIN_REQUEST_NUMERIC
};

struct options {
    uint32_t frequency;
    uint32_t sample_rate;
    enum gain_request_kind gain_kind;
    int gain_tenths;
    const char *file_path;
    int gain_seen;
    int ppm;
    int ppm_seen;
};

void usage(const char *program);

/* Each returns 0 and writes *value on success, negative on malformed input. */
int parse_int(const char *text, int *value);
int parse_u32(const char *text, uint32_t *value);
/* Accepts a bare figure in Hz or a K/M/G suffix. */
int parse_frequency(const char *text, uint32_t *value);
/* Tenths of a dB, as librtlsdr wants them. */
int parse_numeric_gain(const char *text, int *tenths);

int parse_options(int argc, char **argv, struct options *options);

#endif
