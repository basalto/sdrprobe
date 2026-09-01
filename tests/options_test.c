#include "options.h"
#include "gsm_dsp.h"

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

static int failures;

static void fail(const char *what) {
    fprintf(stderr, "%s\n", what);
    failures++;
}

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
    char text[600];

    if (parse_line(line, &options) < 0) {
        snprintf(text, sizeof(text), "rejected a valid line: %s", line);
        fail(text);
    }
}

static void rejects(const char *line) {
    struct options options;
    char text[600];

    if (parse_line(line, &options) == 0) {
        snprintf(text, sizeof(text), "accepted a line it should not: %s", line);
        fail(text);
    }
}

static void test_defaults(void) {
    struct options options;

    if (parse_line("", &options) < 0) {
        fail("an empty command line was rejected");
        return;
    }
    if (options.frequency != DEFAULT_FREQUENCY)
        fail("default frequency is not 1090 MHz");
    if (options.sample_rate != DEFAULT_SAMPLE_RATE)
        fail("default sample rate is not 2 MS/s");
    if (options.gain_kind != GAIN_REQUEST_DEFAULT)
        fail("gain does not default to the nearest supported step");
    if (options.remove_dc != 1)
        fail("the DC-spike filter does not default on");
    if (options.file_path || options.headless || options.decode ||
        options.list_devices || options.play_once)
        fail("a flag defaulted to on");
    if (options.arfcn || options.survey_seen || options.record_seconds != 0.0 ||
        options.duration_seconds != 0.0)
        fail("an unset option came back set");
}

static void test_frequency_spellings(void) {
    uint32_t hz;

    if (parse_frequency("1090000000", &hz) < 0 || hz != 1090000000U)
        fail("plain Hz did not parse");
    if (parse_frequency("1090M", &hz) < 0 || hz != 1090000000U)
        fail("1090M did not parse");
    if (parse_frequency("1.09G", &hz) < 0 || hz != 1090000000U)
        fail("1.09G did not parse");
    if (parse_frequency("88.5M", &hz) < 0 || hz != 88500000U)
        fail("a decimal megahertz did not parse");
    if (parse_frequency("200k", &hz) < 0 || hz != 200000U)
        fail("a lower-case suffix did not parse");
    /* The rejections matter more: a mistyped frequency that parses tunes the
       receiver somewhere unintended and says nothing. */
    if (parse_frequency("", &hz) == 0)
        fail("an empty frequency parsed");
    if (parse_frequency("-100M", &hz) == 0)
        fail("a negative frequency parsed");
    if (parse_frequency("0", &hz) == 0)
        fail("zero parsed as a frequency");
    if (parse_frequency("100MM", &hz) == 0)
        fail("a double suffix parsed");
    if (parse_frequency("100X", &hz) == 0)
        fail("an unknown suffix parsed");
    if (parse_frequency("abc", &hz) == 0)
        fail("letters parsed as a frequency");
}

static void test_gain_and_numbers(void) {
    int tenths;
    int value;
    double seconds;

    if (parse_numeric_gain("29.7", &tenths) < 0 || tenths != 297)
        fail("29.7 dB did not become 297 tenths");
    if (parse_numeric_gain("0", &tenths) < 0 || tenths != 0)
        fail("zero gain did not parse");
    if (parse_numeric_gain("abc", &tenths) == 0)
        fail("letters parsed as a gain");

    if (parse_int("-42", &value) < 0 || value != -42)
        fail("a negative integer did not parse");
    if (parse_int("12x", &value) == 0)
        fail("trailing rubbish parsed as an integer");

    if (parse_seconds("2.5", &seconds) < 0 || fabs(seconds - 2.5) > 1e-9)
        fail("2.5 seconds did not parse");
    if (parse_seconds("0", &seconds) == 0)
        fail("zero seconds parsed");
    if (parse_seconds("-1", &seconds) == 0)
        fail("negative seconds parsed");
    if (parse_seconds("99999", &seconds) == 0)
        fail("a duration past the bound parsed");
}

static void test_named_values(void) {
    enum start_view view;
    int mask;
    int on;

    if (parse_view("survey", &view) < 0 || view != START_VIEW_SURVEY)
        fail("--view survey did not parse");
    if (parse_view("waterfall", &view) < 0 || view != START_VIEW_WATERFALL)
        fail("--view waterfall did not parse");
    if (parse_view("nope", &view) == 0)
        fail("an unknown view name parsed");

    if (parse_gsm_features("filter,trellis", GSM_OPT_FILTER, GSM_OPT_FINECFO,
                           GSM_OPT_TRELLIS, &mask) < 0 ||
        mask != (GSM_OPT_FILTER | GSM_OPT_TRELLIS))
        fail("a feature list did not parse");
    if (parse_gsm_features("none", GSM_OPT_FILTER, GSM_OPT_FINECFO,
                           GSM_OPT_TRELLIS, &mask) < 0 || mask != 0)
        fail("none did not clear the features");
    if (parse_gsm_features("filter,bogus", GSM_OPT_FILTER, GSM_OPT_FINECFO,
                           GSM_OPT_TRELLIS, &mask) == 0)
        fail("an unknown feature parsed");

    if (parse_switch("off", &on) < 0 || on != 0)
        fail("off did not parse");
    if (parse_switch("maybe", &on) == 0)
        fail("maybe parsed as a switch");
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

    /* And the combinations that must keep working. */
    accepts("--headless --record-seconds 3 --technology adsb");
    accepts("--file testfiles/adsb_cpr_pair.bin --headless --technology adsb"
            " --decode --once");
    accepts("--arfcn 73 --decode --headless --gsm-features none");
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
        if (!options.technology || strcmp(options.technology, "gsm") != 0)
            fail("--arfcn did not imply the GSM technology label");
        if (options.arfcn != 73)
            fail("--arfcn did not record the channel");
    }

    if (parse_line("--survey-range 88M:108M", &options) < 0) {
        fail("--survey-range was rejected");
    } else {
        if (options.view != START_VIEW_SURVEY)
            fail("--survey-range did not open the survey view");
        if (options.survey_from_hz != 88000000U ||
            options.survey_to_hz != 108000000U)
            fail("--survey-range did not record its edges");
    }

    if (parse_line("--dc-filter off", &options) < 0)
        fail("--dc-filter off was rejected");
    else if (options.remove_dc != 0)
        fail("--dc-filter off left the filter on");

    if (parse_line("--gain auto", &options) < 0)
        fail("--gain auto was rejected");
    else if (options.gain_kind != GAIN_REQUEST_AUTO)
        fail("--gain auto did not select automatic gain");

    if (parse_line("--gain 29.7", &options) < 0)
        fail("--gain 29.7 was rejected");
    else if (options.gain_kind != GAIN_REQUEST_NUMERIC ||
             options.gain_tenths != 297)
        fail("--gain 29.7 did not record 297 tenths");
}

int main(void) {
    test_defaults();
    test_frequency_spellings();
    test_gain_and_numbers();
    test_named_values();
    test_conflicting_flags();
    test_implications();

    if (failures) {
        fprintf(stderr, "%d options check(s) failed\n", failures);
        return 1;
    }
    puts("options checks passed");
    return 0;
}
