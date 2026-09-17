#include "check.h"
#include "gsm_dsp.h"
#include "options.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * The command line, checked properly rather than by typing flags at a shell
 * and watching the exit status.
 *
 * This is the program's other user interface and its main one for anything
 * scripted, so a rejection that should happen and does not is a wrong sweep, a
 * wrong tuning, or a capture labelled as something it is not. It is also pure
 * text in and a struct out, which makes it the cheapest thing in the codebase
 * to check exhaustively (ADR-0012, layer 1).
 */

/* A check whose name is a sentence: these assertions read better as "this
   must not happen" than as an expected value. */
static void expect(int ok, const char *what) { check_msg(ok, "%s\n", what); }

/* A failure with no condition left to state -- the guard above it already
   decided. Counted like any other check. */
static void fail(const char *what) { check_msg(0, "%s\n", what); }

/* Parse a command line written as a single string, split on spaces. */
static int parse_line(const char *line, struct options *options) {
    static char buffer[512];
    char *argv[32];
    int argc = 1;

    argv[0] = (char *)"sdrprobe";
    snprintf(buffer, sizeof(buffer), "%s", line);
    for (char *token = strtok(buffer, " "); token && argc < 32;
         token = strtok(NULL, " "))
        argv[argc++] = token;
    return parse_options(argc, argv, options);
}

static void accepts(const char *line) {
    struct options options;

    check_msg(parse_line(line, &options) >= 0, "rejected a valid line: %s\n",
              line);
}

static void rejects(const char *line) {
    struct options options;

    check_msg(parse_line(line, &options) != 0,
              "accepted a line it should not: %s\n", line);
}

static void test_defaults(void) {
    struct options options;

    if (parse_line("", &options) < 0) {
        fail("an empty command line was rejected");
        return;
    }
    expect(options.frequency == DEFAULT_FREQUENCY,
           "default frequency is not 1090 MHz");
    expect(options.sample_rate == DEFAULT_SAMPLE_RATE,
           "default sample rate is not 2 MS/s");
    expect(options.gain_kind == GAIN_REQUEST_DEFAULT,
           "gain does not default to the nearest supported step");
    expect(options.remove_dc == 1, "the DC-spike filter does not default on");
    expect(!options.file_path && !options.headless && !options.decode &&
               !options.list_devices && !options.play_once,
           "a flag defaulted to on");
    expect(!options.arfcn && !options.survey_seen &&
               options.record_seconds == 0.0 && options.duration_seconds == 0.0,
           "an unset option came back set");
}

static void test_frequency_spellings(void) {
    uint32_t hz;

    expect(parse_frequency("1090000000", &hz) >= 0 && hz == 1090000000U,
           "plain Hz did not parse");
    expect(parse_frequency("1090M", &hz) >= 0 && hz == 1090000000U,
           "1090M did not parse");
    expect(parse_frequency("1.09G", &hz) >= 0 && hz == 1090000000U,
           "1.09G did not parse");
    expect(parse_frequency("88.5M", &hz) >= 0 && hz == 88500000U,
           "a decimal megahertz did not parse");
    expect(parse_frequency("200k", &hz) >= 0 && hz == 200000U,
           "a lower-case suffix did not parse");
    /* The rejections matter more: a mistyped frequency that parses tunes the
       receiver somewhere unintended and says nothing. */
    expect(parse_frequency("", &hz) != 0, "an empty frequency parsed");
    expect(parse_frequency("-100M", &hz) != 0, "a negative frequency parsed");
    expect(parse_frequency("0", &hz) != 0, "zero parsed as a frequency");
    expect(parse_frequency("100MM", &hz) != 0, "a double suffix parsed");
    expect(parse_frequency("100X", &hz) != 0, "an unknown suffix parsed");
    expect(parse_frequency("abc", &hz) != 0, "letters parsed as a frequency");
}

static void test_gain_and_numbers(void) {
    int tenths;
    int value;
    double seconds;

    expect(parse_numeric_gain("29.7", &tenths) >= 0 && tenths == 297,
           "29.7 dB did not become 297 tenths");
    expect(parse_numeric_gain("0", &tenths) >= 0 && tenths == 0,
           "zero gain did not parse");
    expect(parse_numeric_gain("abc", &tenths) != 0, "letters parsed as a gain");

    expect(parse_int("-42", &value) >= 0 && value == -42,
           "a negative integer did not parse");
    expect(parse_int("12x", &value) != 0,
           "trailing rubbish parsed as an integer");

    expect(parse_seconds("2.5", &seconds) >= 0 && fabs(seconds - 2.5) <= 1e-9,
           "2.5 seconds did not parse");
    expect(parse_seconds("0", &seconds) != 0, "zero seconds parsed");
    expect(parse_seconds("-1", &seconds) != 0, "negative seconds parsed");
    expect(parse_seconds("99999", &seconds) != 0,
           "a duration past the bound parsed");
}

static void test_named_values(void) {
    enum start_view view;
    int mask;
    int on;

    expect(parse_view("survey", &view) >= 0 && view == START_VIEW_SURVEY,
           "--view survey did not parse");
    expect(parse_view("waterfall", &view) >= 0 && view == START_VIEW_SPECTRUM,
           "--view waterfall did not parse");
    expect(parse_view("nope", &view) != 0, "an unknown view name parsed");

    expect(parse_gsm_features("filter,trellis", GSM_OPT_FILTER, GSM_OPT_FINECFO,
                              GSM_OPT_TRELLIS, &mask) >= 0 &&
               mask == (GSM_OPT_FILTER | GSM_OPT_TRELLIS),
           "a feature list did not parse");
    expect(parse_gsm_features("none", GSM_OPT_FILTER, GSM_OPT_FINECFO,
                              GSM_OPT_TRELLIS, &mask) >= 0 &&
               mask == 0,
           "none did not clear the features");
    expect(parse_gsm_features("filter,bogus", GSM_OPT_FILTER, GSM_OPT_FINECFO,
                              GSM_OPT_TRELLIS, &mask) != 0,
           "an unknown feature parsed");

    expect(parse_switch("off", &on) >= 0 && on == 0, "off did not parse");
    expect(parse_switch("maybe", &on) != 0, "maybe parsed as a switch");
}

/* The combinations the parser exists to refuse. Each one is a way to ask for
   something the program cannot honestly do. */
static void test_conflicting_flags(void) {
    /* Gain belongs to a receiver; a capture already has whatever gain it was
       recorded at. */
    rejects("--file testfiles/adsb_modes1.bin --gain 30");
    rejects("--file testfiles/adsb_modes1.bin --device 1");
    /* A screen has no meaning without a window. */
    rejects("--headless --view adsb");
    rejects("--headless --survey-range 88M:108M");
    /* Decoding needs a window-free run and something to decode. */
    rejects("--decode");
    rejects("--decode --technology adsb");
    rejects("--headless --decode");
    /* An ARFCN is a frequency; naming both leaves no way to say which wins. */
    rejects("--arfcn 73 --frequency 900M");
    rejects("--earfcn 6200 --frequency 796M");
    rejects("--earfcn 6200 --arfcn 73");
    rejects("--earfcn 6200 --technology gsm");
    rejects("--earfcn 0");
    /* LTE runs on its own sample grid, so a rate that is not it is a
       contradiction rather than something to quietly override (ADR-0014). */
    rejects("--earfcn 6200 --sample-rate 2M");
    rejects("--technology lte --sample-rate 2400000");
    /* A band scan walks the band; a decode reads one tuning; a survey prints
       its own list. No two of them share a stdout or a receiver. */
    rejects("--lte-scan 20");                       /* needs --headless */
    rejects("--lte-scan 20 --headless --decode");
    rejects("--lte-scan 20 --headless --survey --survey-range 791M:821M");
    rejects("--lte-scan 20 --headless --earfcn 6200");
    rejects("--lte-scan 20 --headless --file testfiles/lte_b20_pci28.bin");
    rejects("--lte-scan 3 --headless");             /* out of a dongle's reach */
    rejects("--lte-scan 0 --headless");
    rejects("--lte-scan 20 --headless --technology gsm");
    /* A picture of a frame needs a frame, and a run that never ends never
       takes one -- which would read as a hang rather than a refusal. */
    rejects("--screenshot shot.png");
    rejects("--screenshot shot.png --headless --duration 5");
    rejects("--view lte --screenshot");
    rejects("--arfcn 73 --technology adsb");
    rejects("--arfcn 0");
    rejects("--arfcn 125");
    /* Looping is a property of playback. */
    rejects("--once");
    /* Ranges must ascend, and both halves must parse. */
    rejects("--survey-range 108M:88M");
    rejects("--survey-range 88M");
    rejects("--survey-range 88M:");
    /* Repeats are a typo, not an override. */
    rejects("--frequency 100M --frequency 200M");
    rejects("--headless --headless");
    /* Values that are missing entirely. */
    rejects("--frequency");
    rejects("--survey-dwell");
    rejects("--not-a-flag");

    /* A headless survey prints candidates; the rules around it. */
    rejects("--survey");                       /* needs a window-free run */
    rejects("--headless --survey");            /* nothing said about what */
    rejects("--headless --survey --decode --technology adsb");
    rejects("--headless --survey --survey --survey-range 88M:108M");
    /* A capture holds one tuning, so its own span is the only range there
       is; naming another would be asking it for samples it does not hold. */
    rejects("--file testfiles/gsm_arfcn_69.bin --headless --survey"
            " --survey-range 88M:108M");
    /* Without --survey, a range still means the survey view, which needs a
       window. */
    rejects("--headless --survey-range 88M:108M");

    /* And the combinations that must keep working. */
    accepts("--headless --survey --survey-range 88M:108M");
    accepts("--headless --survey --survey-range 470M:690M --survey-dwell 0.5");
    accepts("--file testfiles/gsm_arfcn_69.bin --frequency 948.4M --headless"
            " --survey --once");
    accepts("--headless --record-seconds 3 --technology adsb");
    accepts("--file testfiles/adsb_cpr_pair.bin --headless --technology adsb"
            " --decode --once");
    accepts("--arfcn 73 --decode --headless --gsm-features none");
    accepts("--earfcn 6200 --decode --headless --once "
            "--file testfiles/lte_b20_pci28.bin");
    accepts("--earfcn 6200 --sample-rate 1920000");
    accepts("--lte-scan 20 --headless");
    accepts("--view spectrum --duration 5 --screenshot shot.png");
    accepts("--lte-scan 28 --headless --gain max");
    accepts("--technology lte --headless --record-seconds 2");
    accepts("--survey-range 88M:108M --survey-dwell 0.5");
    accepts("--view survey --duration 20 --dc-filter off");
    accepts("--list-devices");
    accepts("--device 3 --gain max --ppm -12");
}

/* Flags that imply other flags have to actually imply them. */
static void test_implications(void) {
    struct options options;

    if (parse_line("--arfcn 73", &options) < 0) {
        fail("--arfcn 73 was rejected");
    } else {
        expect(options.technology && strcmp(options.technology, "gsm") == 0,
               "--arfcn did not imply the GSM technology label");
        expect(options.arfcn == 73, "--arfcn did not record the channel");
    }

    if (parse_line("--earfcn 6200", &options) < 0) {
        fail("--earfcn 6200 was rejected");
    } else {
        expect(options.technology && strcmp(options.technology, "lte") == 0,
               "--earfcn did not imply the LTE technology label");
        expect(options.earfcn == 6200, "--earfcn did not record the channel");
        /* The rate is not a default here but a consequence: 128 subcarriers
           of 15 kHz, and the plugin refuses anything else. */
        expect(options.sample_rate == 1920000U,
               "--earfcn did not set LTE's sample grid");
    }

    if (parse_line("--lte-scan 8 --headless", &options) < 0) {
        fail("--lte-scan 8 was rejected");
    } else {
        expect(options.lte_scan_band == 8, "--lte-scan did not record a band");
        expect(options.technology && strcmp(options.technology, "lte") == 0,
               "--lte-scan did not imply the LTE technology label");
        expect(options.sample_rate == 1920000U,
               "--lte-scan did not set LTE's sample grid");
    }

    if (parse_line("--technology lte", &options) < 0) {
        fail("--technology lte was rejected");
    } else {
        expect(options.sample_rate == 1920000U,
               "--technology lte did not set LTE's sample grid");
    }

    if (parse_line("--survey-range 88M:108M", &options) < 0) {
        fail("--survey-range was rejected");
    } else {
        expect(options.view == START_VIEW_SURVEY,
               "--survey-range did not open the survey view");
        expect(options.survey_from_hz == 88000000U &&
                   options.survey_to_hz == 108000000U,
               "--survey-range did not record its edges");
    }

    if (parse_line("--dc-filter off", &options) < 0)
        fail("--dc-filter off was rejected");
    else
        expect(options.remove_dc == 0, "--dc-filter off left the filter on");

    if (parse_line("--gain auto", &options) < 0)
        fail("--gain auto was rejected");
    else
        expect(options.gain_kind == GAIN_REQUEST_AUTO,
               "--gain auto did not select automatic gain");

    if (parse_line("--gain 29.7", &options) < 0)
        fail("--gain 29.7 was rejected");
    else
        expect(options.gain_kind == GAIN_REQUEST_NUMERIC &&
                   options.gain_tenths == 297,
               "--gain 29.7 did not record 297 tenths");
}

/*
 * --antenna and --site describe the installation rather than the run, and
 * persist. Parsing is all that is checked here: writing the file is
 * check-config's, and where it goes depends on a home directory this cannot
 * assume.
 */
static void test_installation_flags(void) {
    struct options options;
    const char *ok[] = { "sdrprobe", "--antenna", "discone, roof",
                         "--site", "lisbon-office" };
    const char *no_value[] = { "sdrprobe", "--antenna" };
    const char *empty[] = { "sdrprobe", "--site", "" };
    const char *twice[] = { "sdrprobe", "--antenna", "a", "--antenna", "b" };

    check_int("an antenna and a site are accepted",
              parse_options(5, (char **)ok, &options), 0);
    check_str("the antenna is kept whole, commas and all", options.antenna,
              "discone, roof");
    check_str("and the site", options.site, "lisbon-office");

    check_true("a flag with nothing after it is refused",
               parse_options(2, (char **)no_value, &options) < 0);
    /* An empty name would write an empty setting, and an empty site compares
       equal to another empty site -- the mistake the site exists to catch. */
    check_true("so is an empty name",
               parse_options(3, (char **)empty, &options) < 0);
    check_true("and so is saying it twice",
               parse_options(5, (char **)twice, &options) < 0);

    check_int("neither is required",
              parse_options(1, (char **)ok, &options), 0);
    check_true("and unset means whatever the file already says",
               options.antenna == NULL && options.site == NULL);
}

static void test_asking_again_needs_a_sweep(void) {
    struct options options;
    const char *alone[] = { "sdrprobe", "--survey-confirm" };
    const char *withSweep[] = { "sdrprobe", "--survey-range", "88M:108M",
                                "--survey-confirm" };

    /* There is nothing to ask again about without a sweep to have found it. */
    check_true("asking again alone is refused",
               parse_options(2, (char **)alone, &options) < 0);
    check_int("with a range it is accepted",
              parse_options(4, (char **)withSweep, &options), 0);
    check_true("and is off unless asked for", options.survey_confirm == 1);
    parse_options(3, (char **)withSweep, &options);
    check_int("a sweep alone does not ask again", options.survey_confirm, 0);
}

static void test_watching_needs_a_sweep(void) {
    struct options options;
    const char *alone[] = { "sdrprobe", "--survey-watch", "5" };
    const char *ok[] = { "sdrprobe", "--survey-range", "88M:108M",
                         "--survey-watch", "5" };
    const char *both[] = { "sdrprobe", "--survey-range", "88M:108M",
                           "--survey-watch", "5", "--survey-confirm" };
    const char *zero[] = { "sdrprobe", "--survey-range", "88M:108M",
                           "--survey-watch", "0" };
    const char *words[] = { "sdrprobe", "--survey-range", "88M:108M",
                            "--survey-watch", "lots" };

    check_true("a watch with nothing to repeat is refused",
               parse_options(3, (char **)alone, &options) < 0);
    check_int("with a range it is accepted",
              parse_options(5, (char **)ok, &options), 0);
    check_int("and remembers how many", options.survey_watch, 5);
    /* Asking again retunes the receiver away from a sweep that is about to
       restart, so the two cannot both run. */
    check_true("watching and asking again together are refused",
               parse_options(6, (char **)both, &options) < 0);
    check_true("no sweeps is not a watch",
               parse_options(5, (char **)zero, &options) < 0);
    check_true("nor is a word",
               parse_options(5, (char **)words, &options) < 0);
    check_int("and it is off unless asked for",
              parse_options(3, (char **)ok, &options) == 0
                  ? options.survey_watch : -1, 0);
}

/*
 * Running a calibration with no window.
 *
 * The lock gate decides whether a correction may be applied, and until this it
 * could only be reached by somebody clicking Start and reading a status line
 * -- which is what ADR-0012 says a decision must never be.
 */
static void test_headless_calibration(void) {
    struct options options;
    const char *gsm[] = { "sdrprobe", "--calibrate", "gsm", "--arfcn", "113" };
    const char *gsm_bare[] = { "sdrprobe", "--calibrate", "gsm" };
    const char *lte[] = { "sdrprobe", "--calibrate", "lte", "--earfcn", "6200" };
    const char *band[] = { "sdrprobe", "--calibrate", "lte",
                           "--calibrate-band", "20" };
    const char *both[] = { "sdrprobe", "--calibrate", "lte", "--earfcn",
                           "6200", "--calibrate-band", "20" };
    const char *nonsense[] = { "sdrprobe", "--calibrate", "5g" };
    const char *stray[] = { "sdrprobe", "--calibrate-band", "20" };
    const char *clash[] = { "sdrprobe", "--calibrate", "gsm", "--arfcn", "113",
                            "--survey-range", "88M:108M" };

    check_int("GSM with a channel", parse_options(5, (char **)gsm, &options), 0);
    check_int("names the technology", options.calibrate, 1);
    /* Without one there is nothing to point at, and guessing a channel would
       calibrate against whatever happened to be there. */
    check_true("GSM without one is refused",
               parse_options(3, (char **)gsm_bare, &options) < 0);

    check_int("LTE with an EARFCN", parse_options(5, (char **)lte, &options), 0);
    check_int("LTE with a band to search",
              parse_options(5, (char **)band, &options), 0);
    check_int("which it remembers", options.calibrate_band, 20);
    check_true("but not both, which is two answers to one question",
               parse_options(7, (char **)both, &options) < 0);

    check_true("an unknown technology is refused",
               parse_options(3, (char **)nonsense, &options) < 0);
    check_true("a band with nothing to calibrate is refused",
               parse_options(3, (char **)stray, &options) < 0);
    /* It is a run of its own: sharing the block loop with a sweep would have
       the receiver in two places at once. */
    check_true("calibrating and sweeping together are refused",
               parse_options(7, (char **)clash, &options) < 0);
    check_int("and it is off unless asked for",
              parse_options(1, (char **)gsm, &options) == 0
                  ? options.calibrate : -1, 0);
}

/*
 * Walking the LTE chain over a live cell, with no window.
 *
 * probe-lte-chain does this for a capture. A capture is two seconds of one
 * afternoon; a cell that decodes in half its blocks and one that never decodes
 * are indistinguishable in a single block and obvious in sixty.
 */
static void test_headless_lte_chain(void) {
    struct options options;
    const char *earfcn[] = { "sdrprobe", "--lte-chain", "--earfcn", "6200" };
    const char *band[] = { "sdrprobe", "--lte-chain", "--lte-chain-band", "20" };
    const char *bare[] = { "sdrprobe", "--lte-chain" };
    const char *both[] = { "sdrprobe", "--lte-chain", "--earfcn", "6200",
                           "--lte-chain-band", "20" };
    const char *stray[] = { "sdrprobe", "--lte-chain-band", "20" };
    const char *clash[] = { "sdrprobe", "--lte-chain", "--earfcn", "6200",
                            "--calibrate", "lte" };

    check_int("with an EARFCN", parse_options(4, (char **)earfcn, &options), 0);
    check_int("or with a band to find one in",
              parse_options(4, (char **)band, &options), 0);
    check_int("which it remembers", options.lte_chain_band, 20);
    /* Without either there is no cell to walk, and picking one at random
       would report a chain over whatever happened to be tuned. */
    check_true("with neither it is refused",
               parse_options(2, (char **)bare, &options) < 0);
    check_true("and with both, which is two answers to one question",
               parse_options(6, (char **)both, &options) < 0);
    check_true("a band with no chain to walk is refused",
               parse_options(3, (char **)stray, &options) < 0);
    /* One receiver: a chain and a calibration together would have it in two
       places at once. */
    check_true("walking and calibrating together are refused",
               parse_options(6, (char **)clash, &options) < 0);
}

static void test_saving_a_scripted_sweep(void) {
    struct options options;
    /* A survey already needs a range or a file to sweep; saving needs a
       survey on top of that. */
    const char *ok[] = { "sdrprobe", "--headless", "--survey",
                         "--survey-range", "88M:108M", "--survey-save" };
    const char *bare[] = { "sdrprobe", "--survey-save" };

    check_int("saving a sweep that happens",
              parse_options(6, (char **)ok, &options), 0);
    check_int("is remembered", options.survey_save, 1);
    check_true("and saving nothing is refused",
               parse_options(2, (char **)bare, &options) < 0);
    check_int("off unless asked for",
              parse_options(5, (char **)ok, &options) == 0
                  ? options.survey_save : -1, 0);
}

/*
 * Every screen is reachable from the command line.
 *
 * This is the enforceable half of the screenshot rule: a screen nobody can
 * open without clicking is a screen nobody looks at, and the LTE calibration
 * panel shipped with three overlapping regions for exactly that reason. The
 * looking itself needs a display and cannot live in `make check`; that a
 * screen can be *asked for* does not.
 */
static void test_every_screen_is_reachable(void) {
    static const char *screens[] = {
        "magnitude", "spectrum", "scatter", "waterfall", "survey",
        "gsm", "adsb", "lte", "calibration", "settings", "help"
    };
    unsigned i;

    for (i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
        enum start_view view = START_VIEW_DEFAULT;
        check_msg(parse_view(screens[i], &view) == 0,
                  "--view %s is not accepted\n", screens[i]);
        check_msg(view != START_VIEW_DEFAULT,
                  "--view %s parses to the default\n", screens[i]);
    }
    /* And the overlays specifically, which are the ones that were only ever
       reachable by a key or a button. */
    {
        enum start_view view = START_VIEW_DEFAULT;
        check_int("the calibration overlay",
                  parse_view("calibration", &view), 0);
        check_int("names itself", (int)view, (int)START_VIEW_CALIBRATION);
        check_int("settings", parse_view("settings", &view), 0);
        check_int("names itself", (int)view, (int)START_VIEW_SETTINGS);
        check_int("help", parse_view("help", &view), 0);
        check_int("names itself", (int)view, (int)START_VIEW_HELP);
    }
    check_true("and a screen that does not exist is refused",
               parse_view("nonsense", &(enum start_view){ 0 }) < 0);

    /*
     * Both arrangements of every decode view, which is a screen count of its
     * own: four views that each draw their data or their charts, reached by
     * --view with and without --analysis.
     *
     * GSM was the one that could not be. Entering that view with a channel
     * already chosen turns the analysis arrangement on -- right when a reader
     * inspects a channel from the scan, and wrong on the way in from the
     * command line, where it made --analysis a no-op and left the ARFCN
     * waterfall unreachable without a window. The option is assigned at
     * startup now rather than only set, so a flag that is absent means
     * something.
     */
    {
        static const char *decoders[] = { "gsm", "adsb", "lte", "fm" };
        unsigned d;

        for (d = 0; d < sizeof(decoders) / sizeof(decoders[0]); d++) {
            struct options plain, charts;
            const char *a[] = { "sdrprobe", "--view", decoders[d] };
            const char *b[] = { "sdrprobe", "--view", decoders[d],
                                "--analysis" };

            check_msg(parse_options(3, (char **)a, &plain) == 0,
                      "--view %s is refused\n", decoders[d]);
            check_msg(parse_options(4, (char **)b, &charts) == 0,
                      "--view %s --analysis is refused\n", decoders[d]);
            check_msg(plain.analysis == 0,
                      "--view %s asks for charts without being told to\n",
                      decoders[d]);
            check_msg(charts.analysis == 1,
                      "--view %s --analysis does not ask for charts\n",
                      decoders[d]);
            check_msg(plain.view == charts.view,
                      "--analysis changes which view %s opens\n",
                      decoders[d]);
        }
    }

    /*
     * Every one of them names a different screen, but one pair is now an
     * intentional exception: "waterfall" is kept as an alias for "spectrum"
     * rather than its own screen, since the Scope's spectrum and waterfall
     * are one combined view (spectrum on top, waterfall below) and not two.
     *
     * The other collisions this guards against are real bugs. It stopped
     * catching them when the survey became a tab: the four Scope views set
     * app->view and nothing set the tab, so with the survey as the default
     * tab all four of them opened the survey. Nothing failed -- the flags
     * parsed, the program ran, and four screenshots came out identical,
     * which is how it was noticed.
     */
    {
        enum start_view seen[16];
        unsigned a, b, count = 0;

        for (i = 0; i < (int)(sizeof(screens) / sizeof(screens[0])); i++) {
            enum start_view view = START_VIEW_DEFAULT;
            if (parse_view(screens[i], &view) == 0)
                seen[count++] = view;
        }
        for (a = 0; a < count; a++)
            for (b = a + 1; b < count; b++) {
                int is_the_known_alias =
                    (strcmp(screens[a], "spectrum") == 0 &&
                     strcmp(screens[b], "waterfall") == 0) ||
                    (strcmp(screens[a], "waterfall") == 0 &&
                     strcmp(screens[b], "spectrum") == 0);
                if (is_the_known_alias)
                    continue;
                check_msg(seen[a] != seen[b],
                          "--view %s and --view %s name the same screen\n",
                          screens[a], screens[b]);
            }
    }
}

/* --version, which is how a script asks which build it is looking at. */
static void test_version_flag(void) {
    struct options o;
    const char *argv[] = { "sdrprobe", "--version" };

    check_int("--version parses", parse_options(2, (char **)argv, &o), 0);
    check_true("and asks for the version", o.show_version);
    {
        const char *twice[] = { "sdrprobe", "--version", "--version" };
        check_true("but not twice",
                   parse_options(3, (char **)twice, &o) < 0);
    }
    {
        const char *plain[] = { "sdrprobe" };
        check_int("nothing asks for it by default",
                  parse_options(1, (char **)plain, &o), 0);
        check_true("and it stays off", !o.show_version);
    }
}


/* ADR-0018's two flags: a stable receiver name, and claiming a legacy
   correction. Both are the operator saying something the program may not
   guess. */
static void test_receiver_identity_flags(void) {
    struct options o;

    check_int("a label is taken",
              parse_line("--receiver-label rooftop-dongle", &o), 0);
    check_str("and kept", o.receiver_label, "rooftop-dongle");
    check_int("claiming is off unless asked", o.claim_calibration, 0);

    check_int("claiming is taken", parse_line("--claim-calibration", &o), 0);
    check_int("and set", o.claim_calibration, 1);

    check_int("a label twice is a contradiction",
              parse_line("--receiver-label a --receiver-label b", &o), -1);
    check_int("claiming twice too",
              parse_line("--claim-calibration --claim-calibration", &o), -1);
    check_int("a label with no value",
              parse_line("--receiver-label", &o), -1);
}

/* ------------------------------------------------------------------ */

/*
 * The startup form's bypass: who sees it, who does not, and how the
 * environment says so.
 *
 * The negative half is what matters most. `check-pipelines`, every screenshot
 * recipe and every --duration check run the program with a window or without
 * one and expect it to get on with the job; a form in front of any of them
 * would break all of them at once, so the skip conditions are a list here
 * rather than a condition spread over main().
 */
static const char *g_env_site;
static const char *g_env_antenna;
static const char *g_env_label;
static const char *g_env_no_startup;
static const char *g_env_startup;
static const char *g_env_no_browser;
/* Not SDRPROBE_* -- browser_wanted() reads these two directly, the way the
   real environment names them, since they say whether there is anywhere to
   draw rather than answering one of this program's own questions. */
static const char *g_env_display;
static const char *g_env_wayland_display;

static const char *fake_env(const char *name) {
    if (strcmp(name, "SDRPROBE_SITE") == 0)
        return g_env_site;
    if (strcmp(name, "SDRPROBE_ANTENNA") == 0)
        return g_env_antenna;
    if (strcmp(name, "SDRPROBE_RECEIVER_LABEL") == 0)
        return g_env_label;
    if (strcmp(name, "SDRPROBE_NO_STARTUP") == 0)
        return g_env_no_startup;
    if (strcmp(name, "SDRPROBE_STARTUP") == 0)
        return g_env_startup;
    if (strcmp(name, "SDRPROBE_NO_BROWSER") == 0)
        return g_env_no_browser;
    if (strcmp(name, "DISPLAY") == 0)
        return g_env_display;
    if (strcmp(name, "WAYLAND_DISPLAY") == 0)
        return g_env_wayland_display;
    return NULL;
}

static void clear_env(void) {
    g_env_site = NULL;
    g_env_antenna = NULL;
    g_env_label = NULL;
    g_env_no_startup = NULL;
    g_env_startup = NULL;
    g_env_no_browser = NULL;
    g_env_display = NULL;
    g_env_wayland_display = NULL;
}

/*
 * Who sees the startup form, now that it is asked for rather than assumed.
 *
 * The old rule opened it on any plain windowed launch and kept it out of
 * scripted runs with seven refusals -- so a scripted run was safe only while
 * every one of them was right. Opt-in makes that safety structural: nothing
 * asked, so nothing opens, and the refusals that remain are the ones that are
 * about something other than inference.
 */
static void test_who_sees_the_startup_form(void) {
    struct options options;

    check_int("a plain windowed receiver launch parses",
              parse_line("", &options), 0);
    check_int("and is no longer asked -- a cold launch reaches a view",
              startup_form_wanted(&options), 0);

    check_int("--startup parses", parse_line("--startup", &options), 0);
    check_int("and is what opens the form", startup_form_wanted(&options), 1);
    check_int("twice is a mistake",
              parse_line("--startup --startup", &options), -1);

    /* The two impossibility refusals, which outrank the request: there is no
       window to draw the form in, and a capture has no crystal to measure. */
    check_int("--startup --headless parses",
              parse_line("--startup --headless", &options), 0);
    check_int("and is never asked -- no window, and nobody to answer",
              startup_form_wanted(&options), 0);

    check_int("--startup over a capture parses",
              parse_line("--startup --file testfiles/gsm_arfcn_69.bin",
                         &options), 0);
    check_int("and is never asked -- its correction is in its samples",
              startup_form_wanted(&options), 0);

    /*
     * And the provenance guard, which is not an inference: a measured
     * correction offering to overwrite a stated one is the confusion that
     * cost a measured +32 twice in one afternoon.
     */
    check_int("--startup --ppm 32 parses",
              parse_line("--startup --ppm 32", &options), 0);
    check_int("and is not asked -- measuring one would offer to overwrite it",
              startup_form_wanted(&options), 0);
    check_int("--startup --ppm 0 parses",
              parse_line("--startup --ppm 0", &options), 0);
    check_int("and is not asked either", startup_form_wanted(&options), 0);

    /*
     * The three that no longer refuse. Each existed to keep the form away
     * from a run that had not asked for it; a run that says `--startup` has.
     */
    check_int("--startup --duration 20 parses",
              parse_line("--startup --duration 20", &options), 0);
    check_int("and is asked -- the timer no longer implies a refusal",
              startup_form_wanted(&options), 1);

    check_int("--startup --view gsm parses",
              parse_line("--startup --view gsm", &options), 0);
    check_int("and is asked", startup_form_wanted(&options), 1);

    check_int("--startup --site roof parses",
              parse_line("--startup --site roof", &options), 0);
    check_int("and is asked -- the site pre-fills rather than refusing",
              startup_form_wanted(&options), 1);

    /*
     * An explicit refusal beats an explicit request, in either order, so the
     * pair never resolves by argument position.
     */
    check_int("--no-startup still parses", parse_line("--no-startup", &options),
              0);
    check_int("and still refuses", startup_form_wanted(&options), 0);
    check_int("twice is still a mistake",
              parse_line("--no-startup --no-startup", &options), -1);
    check_int("--no-startup --startup parses",
              parse_line("--no-startup --startup", &options), 0);
    check_int("and the refusal wins", startup_form_wanted(&options), 0);
    check_int("--startup --no-startup parses",
              parse_line("--startup --no-startup", &options), 0);
    check_int("and the refusal wins whichever order they came in",
              startup_form_wanted(&options), 0);

    check_int("--view startup names the form itself",
              parse_line("--view startup", &options), 0);
    check_int("and it is reachable from the command line",
              (int)options.view, (int)START_VIEW_STARTUP);
    /* That route is START_VIEW_STARTUP's and not this rule's, which is what
       keeps the form screenshottable without any flag at all. */
    check_int("without needing --startup to say so",
              startup_form_wanted(&options), 0);
}

static void test_the_environment_answers_the_same_questions(void) {
    struct options options;

    clear_env();
    parse_line("", &options);
    check_int("an empty environment changes nothing",
              options_apply_environment(&options, fake_env), 0);
    check_int("and the form stays shut", startup_form_wanted(&options), 0);

    /* The request, for a launcher that cannot reach the command line. */
    clear_env();
    g_env_startup = "1";
    parse_line("", &options);
    check_int("the request is applied",
              options_apply_environment(&options, fake_env), 1);
    check_int("and the form opens", startup_form_wanted(&options), 1);

    clear_env();
    g_env_startup = "";
    parse_line("", &options);
    check_int("an empty request is no request",
              options_apply_environment(&options, fake_env), 0);
    check_int("and the form stays shut", startup_form_wanted(&options), 0);

    /* A refusal from either route beats a request from either route. */
    clear_env();
    g_env_startup = "1";
    parse_line("--no-startup", &options);
    options_apply_environment(&options, fake_env);
    check_int("the environment cannot ask past a refusing flag",
              startup_form_wanted(&options), 0);

    clear_env();
    g_env_no_startup = "1";
    parse_line("--startup", &options);
    options_apply_environment(&options, fake_env);
    check_int("nor can a flag ask past a refusing variable",
              startup_form_wanted(&options), 0);

    clear_env();
    g_env_site = "field-hut";
    g_env_antenna = "discone, roof";
    g_env_label = "dongle-a";
    parse_line("", &options);
    check_int("three values are applied",
              options_apply_environment(&options, fake_env), 3);
    check_str("the site", options.site, "field-hut");
    check_str("the antenna, commas and all", options.antenna, "discone, roof");
    check_str("the label", options.receiver_label, "dongle-a");
    check_int("and nothing asked for the form", startup_form_wanted(&options),
              0);

    /* A flag beats a variable. One rule, in one direction. */
    clear_env();
    g_env_site = "from-the-environment";
    parse_line("--site from-the-flag", &options);
    options_apply_environment(&options, fake_env);
    check_str("a flag outranks the environment", options.site,
              "from-the-flag");

    /*
     * An empty variable is not a value. A unit file that forgot to fill one
     * in must leave the config alone and must **not** suppress the form --
     * the alternative files every sweep under whatever site was last used,
     * which is silent and permanent.
     */
    clear_env();
    g_env_site = "";
    parse_line("", &options);
    check_int("an empty variable applies nothing",
              options_apply_environment(&options, fake_env), 0);
    check_true("and leaves the site unset", options.site == NULL);

    clear_env();
    g_env_no_startup = "1";
    parse_line("--startup", &options);
    check_int("the refusal is applied",
              options_apply_environment(&options, fake_env), 1);
    check_int("and the form does not open", startup_form_wanted(&options), 0);

    clear_env();
    g_env_no_startup = "";
    parse_line("--startup", &options);
    check_int("an empty refusal is no refusal",
              options_apply_environment(&options, fake_env), 0);
    check_int("so the form still opens", startup_form_wanted(&options), 1);

    check_int("a null lookup is survivable",
              options_apply_environment(&options, NULL), 0);
    clear_env();
}

/*
 * The command word: `sdrprobe`, `sdrprobe server`, `sdrprobe web`.
 *
 * A command names the frontend and nothing else -- window, browser, socket
 * -- so this checks that it sets exactly the flags a caller could have set
 * directly, that it is recognised only at argv[1], and that everything the
 * Viewer link cannot honour (a different screen, a different headless mode)
 * is refused rather than silently dropped.
 */
/*
 * Whether `web`'s browser opens -- browser_wanted(), pure over `struct
 * options` and one environment lookup, reaching every row with no process
 * and no display. What is not checked here, and cannot be, is starting the
 * real browser: `browser.h`'s own comment says why, and
 * `.scratch/cli-subcommands/issues/02-*` has the one thing that was
 * verified by hand instead -- a minute of a real `web` run leaving no
 * zombie behind.
 */
static void test_the_browser(void) {
    struct options options;

    clear_env();
    g_env_display = ":0";
    check_int("web parses", parse_line("web", &options), 0);
    check_int("and wants a browser, with somewhere to draw",
              browser_wanted(&options, fake_env), 1);

    clear_env();
    g_env_wayland_display = "wayland-1";
    parse_line("web", &options);
    check_int("Wayland alone is also somewhere to draw",
              browser_wanted(&options, fake_env), 1);

    /* The default: neither the window nor server ever wants one, whatever
       the display says -- server has no browser to open at all, and the
       plain window is not a serving command in the first place. */
    clear_env();
    g_env_display = ":0";
    parse_line("", &options);
    check_int("the window never wants one",
              browser_wanted(&options, fake_env), 0);
    parse_line("server", &options);
    check_int("and neither does server", browser_wanted(&options, fake_env),
              0);

    /* No display, either variable: skipped, not attempted. */
    clear_env();
    parse_line("web", &options);
    check_int("with nowhere to draw, web does not want one",
              browser_wanted(&options, fake_env), 0);

    /* By request, the flag. */
    clear_env();
    g_env_display = ":0";
    check_int("web --no-browser parses",
              parse_line("web --no-browser", &options), 0);
    check_int("and does not want one", browser_wanted(&options, fake_env),
              0);

    /* By request, the variable -- for a launcher that runs web and cannot
       reach the command line either. */
    clear_env();
    g_env_display = ":0";
    g_env_no_browser = "1";
    parse_line("web", &options);
    check_int("the variable is applied",
              options_apply_environment(&options, fake_env), 1);
    check_int("and it refuses same as the flag",
              browser_wanted(&options, fake_env), 0);

    clear_env();
    g_env_display = ":0";
    g_env_no_browser = "";
    parse_line("web", &options);
    check_int("an empty variable is not a request",
              options_apply_environment(&options, fake_env), 0);
    check_int("so the browser is still wanted",
              browser_wanted(&options, fake_env), 1);

    /*
     * The equivalence the whole flag exists for: two spellings of one
     * behaviour that must not be free to drift apart. Same environment,
     * same display, so the only thing that can account for a difference is
     * the command word itself.
     */
    clear_env();
    g_env_display = ":0";
    {
        struct options web_suppressed, plain_server;

        parse_line("web --no-browser", &web_suppressed);
        parse_line("server", &plain_server);
        check_int("web --no-browser wants no browser",
                  browser_wanted(&web_suppressed, fake_env), 0);
        check_int("neither does server",
                  browser_wanted(&plain_server, fake_env), 0);
        check_int("and they agree on headless too", web_suppressed.headless,
                  plain_server.headless);
        check_int("and on serve", web_suppressed.serve, plain_server.serve);
    }
}

static void test_the_command_word(void) {
    struct options options;
    const char *window[] = { "sdrprobe" };
    const char *window_flag[] = { "sdrprobe", "--frequency", "100M" };
    const char *server[] = { "sdrprobe", "server" };
    const char *web[] = { "sdrprobe", "web" };
    const char *equivalent[] = { "sdrprobe", "--headless", "--serve" };
    const char *bad[] = { "sdrprobe", "serv" };
    const char *second[] = { "sdrprobe", "--duration", "1", "web" };
    const char *with_port[] = { "sdrprobe", "server", "--serve-port", "9000" };
    const char *serve_alone[] = { "sdrprobe", "--serve" };
    const char *serve_view[] = { "sdrprobe", "server", "--view", "lte" };
    const char *serve_shot[] = { "sdrprobe", "server", "--screenshot",
                                 "x.png", "--duration", "1" };
    const char *serve_analysis[] = { "sdrprobe", "server", "--analysis" };
    const char *serve_calibrate[] = { "sdrprobe", "server", "--calibrate",
                                      "gsm", "--arfcn", "113" };
    const char *serve_survey[] = { "sdrprobe", "server", "--survey" };
    const char *serve_decode[] = { "sdrprobe", "server", "--decode",
                                   "--technology", "gsm" };
    const char *serve_lte_scan[] = { "sdrprobe", "server", "--lte-scan",
                                     "20" };
    const char *serve_lte_chain[] = { "sdrprobe", "server", "--lte-chain",
                                      "--earfcn", "6200" };
    /* Ticket 07: a range no longer refuses alongside a serving command --
       it seeds the Survey tab's own sweep the moment `view survey` selects
       it, through the same view_survey_enter() every windowed launch
       already goes through. */
    const char *serve_survey_range[] = { "sdrprobe", "server",
                                         "--survey-range", "88M:108M" };

    check_int("no command reaches the window",
              parse_options(1, (char **)window, &options), 0);
    check_int("COMMAND_WINDOW is the default", options.command,
              COMMAND_WINDOW);
    check_int("and headless is off", options.headless, 0);
    check_int("and serve is off", options.serve, 0);

    check_int("a flag with no command is unaffected",
              parse_options(3, (char **)window_flag, &options), 0);
    check_int("still the window", options.command, COMMAND_WINDOW);

    check_int("server parses", parse_options(2, (char **)server, &options),
              0);
    check_int("as COMMAND_SERVER", options.command, COMMAND_SERVER);
    check_int("headless follows", options.headless, 1);
    check_int("and serve follows", options.serve, 1);

    check_int("web parses", parse_options(2, (char **)web, &options), 0);
    check_int("as COMMAND_WEB", options.command, COMMAND_WEB);
    check_int("headless follows", options.headless, 1);
    check_int("and serve follows", options.serve, 1);

    /*
     * The command is sugar, not a second code path: server sets exactly
     * what the two flags together already set, on the fields every other
     * check in this file reads.
     */
    check_int("the flag pair parses", parse_options(3, (char **)equivalent,
                                                    &options), 0);
    check_int("server and --headless --serve agree on headless",
              options.headless, 1);
    check_int("and on serve", options.serve, 1);
    check_int("though the flag pair names no command",
              options.command, COMMAND_WINDOW);

    /* An unrecognised word at argv[1] is refused, and names itself so a
       reader is not left with only the usage dump to go on. */
    options.unknown_command = NULL;
    check_true("an unknown command refuses",
               parse_options(2, (char **)bad, &options) < 0);
    check_true("and says what it saw",
               options.unknown_command &&
                   strcmp(options.unknown_command, "serv") == 0);

    /* Recognised only at argv[1] -- anywhere else it is what it always was,
       an unknown argument, refused with no command recorded. */
    options.unknown_command = NULL;
    check_true("web after another argument is not a command",
               parse_options(4, (char **)second, &options) < 0);
    check_true("and is not reported as one",
               options.unknown_command == NULL);

    check_int("a serving command still takes its own flags",
              parse_options(4, (char **)with_port, &options), 0);
    check_int("the port", options.serve_port, 9000);

    /*
     * --serve alone used to open a window, bind no socket and serve
     * nothing -- reachable live, twice, before this ticket. It has to do
     * one of the two honest things now.
     */
    check_int("--serve alone is headless too", parse_options(
                  2, (char **)serve_alone, &options), 0);
    check_int("because serve implies it", options.headless, 1);

    /* What the Viewer link cannot honour is refused, not silently kept. */
    check_true("a serving command refuses a chosen view",
               parse_options(4, (char **)serve_view, &options) < 0);
    check_true("and a screenshot",
               parse_options(6, (char **)serve_shot, &options) < 0);
    check_true("and the analysis arrangement",
               parse_options(3, (char **)serve_analysis, &options) < 0);

    /* Serving is its own run, like the calibration and the survey already
       are of each other. */
    check_true("a serving command refuses a calibration",
               parse_options(6, (char **)serve_calibrate, &options) < 0);
    check_true("a headless survey",
               parse_options(3, (char **)serve_survey, &options) < 0);
    check_int("but a survey range is not that survey -- it seeds the tab",
              parse_options(4, (char **)serve_survey_range, &options), 0);
    check_true("a headless decode",
               parse_options(5, (char **)serve_decode, &options) < 0);
    check_true("an LTE band scan",
               parse_options(4, (char **)serve_lte_scan, &options) < 0);
    check_true("and an LTE chain walk",
               parse_options(5, (char **)serve_lte_chain, &options) < 0);
}

int main(void) {
    test_receiver_identity_flags();
    test_defaults();
    test_frequency_spellings();
    test_gain_and_numbers();
    test_named_values();
    test_conflicting_flags();
    test_implications();

    test_installation_flags();

    test_asking_again_needs_a_sweep();

    test_watching_needs_a_sweep();

    test_headless_calibration();

    test_headless_lte_chain();

    test_saving_a_scripted_sweep();

    test_every_screen_is_reachable();

    test_version_flag();

    test_who_sees_the_startup_form();
    test_the_environment_answers_the_same_questions();

    test_the_command_word();
    test_the_browser();

    return check_report("command line");
}
