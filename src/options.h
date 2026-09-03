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

/* Which screen the application opens on. The four Scope views and the two
   Decode views are one list here because a caller picking a starting screen
   does not care which tab it lives under. */
enum start_view {
    START_VIEW_DEFAULT = 0,
    START_VIEW_MAGNITUDE,
    START_VIEW_SPECTRUM,
    START_VIEW_SCATTER,
    START_VIEW_WATERFALL,
    START_VIEW_SURVEY,
    START_VIEW_GSM,
    START_VIEW_ADSB,
    START_VIEW_LTE
};

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

    /* Scripted runs: acquire, record and quit without anyone at the window. */
    int device_index;         /* receiver to open */
    int list_devices;         /* print the receivers and exit */
    int headless;             /* acquire with no window */
    double record_seconds;    /* 0 = do not record at startup */
    double duration_seconds;  /* 0 = run until quit */
    enum start_view view;
    /* What a recording is labelled in its sidecar. NULL means take it from
       --view, which a windowed run usually gives; a headless run has no view,
       so it says so here instead. */
    const char *technology;

    /*
     * What describes the installation rather than this run. Both persist:
     * given here they are written to the configuration file and stand until
     * changed again, because an antenna and a location are things somebody
     * altered physically, not things they should retype every sweep.
     * NULL means "whatever the configuration already says".
     */
    const char *antenna;
    const char *site;

    /* Decode-side controls, so a capture can be decoded from a script the way
       the views decode it on screen. */
    int arfcn;                /* 0 = none; tunes 400 kHz below the channel */
    /* An LTE channel number. Unlike an ARFCN it tunes to the carrier centre
       and not beside it: the synchronisation signals sit on the middle
       subcarriers, and LTE leaves the very centre unused, so the receiver's
       own DC spike lands where the standard already transmits nothing. */
    int earfcn;
    int decode;               /* headless: print decoded messages to stdout */
    /* headless: sweep and print the candidates found, so an agent can read a
       survey without a window or a person to click one. */
    int survey_report;
    /* Run the confirmation pass as soon as a survey started from the command
       line finishes. The pass is otherwise a button, and a decision reachable
       only by clicking is one no check can reach (ADR-0012). */
    int survey_confirm;
    /* Sweep this many times over, folding each into what the site knows and
       printing what changed. A watch is otherwise a button, and a button is
       not something that can run overnight from a script (ADR-0012). */
    int survey_watch;
    /*
     * Run a calibration with no window and print what it measures.
     *
     * 0 none, 1 GSM, 2 LTE. The lock gate is a decision -- it decides whether
     * a correction may be applied -- and until this it was reachable only by
     * somebody clicking Start and watching a status line, which is exactly
     * what ADR-0012 says a decision must never be.
     */
    int calibrate;
    /* Scan this LTE band and calibrate against the strongest cell, instead of
       being told an EARFCN. 0 = not asked for. */
    int calibrate_band;
    /* Give up after this long even if the gate never opens; a calibration
       that cannot lock is a result worth reporting, not a hang. */
    double calibrate_seconds;
    /* headless: walk an LTE band's channels and print the cells found, for
       the same reason -- the scan is otherwise a button. 0 = not asked for,
       otherwise the band number. */
    int lte_scan_band;
    /* Where to write a picture of the last frame, so an agent working on a
       view can see what it drew. A windowed run only: there is no frame
       without a window. */
    const char *screenshot_path;
    int play_once;            /* stop at the end of a capture, do not loop */
    int gsm_features;         /* GSM_OPT_* bitmask for the SCH decoder */
    int gsm_features_seen;
    int remove_dc;            /* the DC-spike filter, on unless told otherwise */
    /* Band survey: the range to load into the view, and whether to start
       sweeping it without waiting to be asked. */
    uint32_t survey_from_hz;
    uint32_t survey_to_hz;
    int survey_seen;
    double survey_dwell_seconds;  /* 0 = leave the view's own default */
};

/* An upper bound on the two time flags. Not a limit anyone should meet: it
   exists so a typo cannot ask for a recording measured in days, and so the
   byte count a duration implies stays inside what the arithmetic can hold. */
#define MAX_RUN_SECONDS 3600.0

void usage(const char *program);

/* Each returns 0 and writes *value on success, negative on malformed input. */
int parse_int(const char *text, int *value);
int parse_u32(const char *text, uint32_t *value);
/* Accepts a bare figure in Hz or a K/M/G suffix. */
int parse_frequency(const char *text, uint32_t *value);
/* Tenths of a dB, as librtlsdr wants them. */
int parse_numeric_gain(const char *text, int *tenths);
/* A positive count of seconds, at most MAX_RUN_SECONDS. */
int parse_seconds(const char *text, double *value);
/* A starting-screen name: magnitude, spectrum, scatter, waterfall, gsm, adsb,
   lte. */
int parse_view(const char *text, enum start_view *view);
/* A comma-separated GSM feature list -- filter, finecfo, trellis -- or "none".
   Returns the GSM_OPT_* bitmask; the caller supplies the flag values so this
   file stays free of the DSP headers. */
int parse_gsm_features(const char *text, int filter_flag, int finecfo_flag,
                       int trellis_flag, int *mask);
/* "on" or "off". */
int parse_switch(const char *text, int *value);

int parse_options(int argc, char **argv, struct options *options);

#endif
