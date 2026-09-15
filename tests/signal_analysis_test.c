/*
 * Deterministic checks for retrospective signal analysis.
 *
 *   make check-signal-analysis
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device_profile.h"
#include "sdr_dsp.h"
#include "signal_analysis.h"

static int load_capture(const char *path, float **i_out, float **q_out, size_t *pairs_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    struct device_profile dev = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);
    size_t pairs = (size_t)sz / dev.bytes_per_pair;
    if (pairs > 131072 * 2) pairs = 131072 * 2; /* 2 blocks */

    unsigned char *raw = malloc(pairs * dev.bytes_per_pair);
    float *i_buf = malloc(pairs * sizeof(float));
    float *q_buf = malloc(pairs * sizeof(float));
    float *mag_buf = malloc(pairs * sizeof(float));

    if (!raw || !i_buf || !q_buf || !mag_buf || fread(raw, dev.bytes_per_pair, pairs, f) != pairs) {
        fclose(f);
        free(raw); free(i_buf); free(q_buf); free(mag_buf);
        return 0;
    }
    fclose(f);

    sdr_dsp_convert_iq(&dev, raw, pairs * dev.bytes_per_pair, i_buf, q_buf, mag_buf, pairs);
    free(raw);
    free(mag_buf);

    *i_out = i_buf;
    *q_out = q_buf;
    *pairs_out = pairs;
    return 1;
}

static void test_bare_carrier_analysis(void) {
    float *i_s, *q_s;
    size_t pairs;
    if (!load_capture("testfiles/carrier_75000_bare.bin", &i_s, &q_s, &pairs)) {
        check_skip("testfiles/carrier_75000_bare.bin is missing");
        return;
    }

    struct signal_analysis_result res;
    /* The 75 MHz harmonic was recorded tuned 300 kHz below carrier (carrier is at +300 kHz) */
    int rc = signal_analysis_run(i_s, q_s, pairs, 2000000.0, 300000.0, 127.5f, NULL, &res);
    check_int("bare carrier analysis returns 0", rc, 0);
    check_int("carrier found", res.carrier_found, 1);
    check_str("verdict summary is Bare carrier", res.verdict_summary, "Bare carrier");
    check_true("standing fraction is >= 0.80", res.carrier_power_fraction >= 0.80);
    check_int("no technology evidence claimed when hint is NULL", res.has_technology_evidence, 0);

    free(i_s);
    free(q_s);
}

static void test_modulated_carrier_analysis(void) {
    float *i_s, *q_s;
    size_t pairs;
    if (!load_capture("testfiles/gsm_arfcn_69.bin", &i_s, &q_s, &pairs)) {
        check_skip("testfiles/gsm_arfcn_69.bin is missing");
        return;
    }

    struct signal_analysis_result res;
    /* GSM capture is tuned 400 kHz below carrier */
    int rc = signal_analysis_run(i_s, q_s, pairs, 2000000.0, 400000.0, 127.5f, "gsm", &res);
    check_int("modulated carrier analysis returns 0", rc, 0);
    check_int("carrier found", res.carrier_found, 1);
    check_str("verdict summary is Modulated carrier", res.verdict_summary, "Modulated carrier");
    check_true("standing fraction is < 0.80", res.carrier_power_fraction < 0.80);

    free(i_s);
    free(q_s);
}

static void test_pulsed_signal_analysis(void) {
    float *i_s, *q_s;
    size_t pairs;
    if (!load_capture("testfiles/adsb_cpr_pair.bin", &i_s, &q_s, &pairs)) {
        check_skip("testfiles/adsb_cpr_pair.bin is missing");
        return;
    }

    struct signal_analysis_result res;
    int rc = signal_analysis_run(i_s, q_s, pairs, 2000000.0, 0.0, 127.5f, NULL, &res);
    check_int("pulsed signal analysis returns 0", rc, 0);
    check_str("verdict summary is No standing carrier",
              res.verdict_summary, "No standing carrier");
    check_true("standing fraction is < 0.35", res.carrier_power_fraction < 0.35);

    free(i_s);
    free(q_s);
}

static void test_refusals_and_invalid_inputs(void) {
    struct signal_analysis_result res;
    check_int("NULL samples refused", signal_analysis_run(NULL, NULL, 10000, 2e6, 0.0, 127.5f, NULL, &res), -1);
    float dummy[100];
    check_int("short buffer refused", signal_analysis_run(dummy, dummy, 500, 2e6, 0.0, 127.5f, NULL, &res), -1);
    check_int("NULL result refused", signal_analysis_run(dummy, dummy, 10000, 2e6, 0.0, 127.5f, NULL, NULL), -1);
}

int main(void) {
    test_bare_carrier_analysis();
    test_modulated_carrier_analysis();
    test_pulsed_signal_analysis();
    test_refusals_and_invalid_inputs();
    return check_report("retrospective signal analysis");
}
