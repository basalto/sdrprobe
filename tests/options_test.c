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
    expect(parse_view("waterfall", &view) >= 0 && view == START_VIEW_WATERFALL,
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

int main(void) {
    test_defaults();
    test_frequency_spellings();
    test_gain_and_numbers();
    test_named_values();
    test_conflicting_flags();
    test_implications();

    test_installation_flags();

    return check_report("command line");
}
