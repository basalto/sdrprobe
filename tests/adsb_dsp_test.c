#include "adsb_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check_int(const char *name, long actual, long expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %ld, expected %ld\n", name, actual, expected);
        failures++;
    }
}

static void check_close(const char *name, double actual, double expected,
                        double tolerance) {
    if (!isfinite(actual) || fabs(actual - expected) > tolerance) {
        fprintf(stderr, "%s: got %.6f, expected %.6f (+/- %.6f)\n", name, actual,
                expected, tolerance);
        failures++;
    }
}

static void check_str(const char *name, const char *actual,
                      const char *expected) {
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "%s: got \"%s\", expected \"%s\"\n", name, actual,
                expected);
        failures++;
    }
}

/* Parse a hex string like "8D4840D6..." into bytes; returns byte count. */
static int parse_hex(const char *hex, uint8_t *out, int capacity) {
    int len = (int)strlen(hex);
    int count = len / 2;
    if (count > capacity)
        return -1;
    for (int i = 0; i < count; i++) {
        unsigned int byte = 0;
        sscanf(hex + 2 * i, "%2x", &byte);
        out[i] = (uint8_t)byte;
    }
    return count;
}

/* Published mode-s.org identification squitter (callsign "KLM1023"). */
static void test_identification(void) {
    uint8_t bytes[ADSB_LONG_BYTES];
    int n = parse_hex("8D4840D6202CC371C32CE0576098", bytes, ADSB_LONG_BYTES);
    check_int("identification byte count", n, ADSB_LONG_BYTES);
    check_int("identification CRC valid", (long)adsb_crc(bytes, n), 0);

    struct adsb_message msg;
    check_int("identification decoded", adsb_decode_frame(bytes, n, &msg), 1);
    check_int("identification DF", msg.downlink_format, 17);
    check_int("identification ICAO", (long)msg.icao, 0x4840D6);
    check_int("identification kind", msg.kind, ADSB_KIND_IDENTIFICATION);
    check_str("identification callsign", msg.callsign, "KLM1023");
}

/* Published mode-s.org velocity squitter: ~159 kt ground speed, 182.88 deg. */
static void test_velocity(void) {
    uint8_t bytes[ADSB_LONG_BYTES];
    int n = parse_hex("8D485020994409940838175B284F", bytes, ADSB_LONG_BYTES);
    struct adsb_message msg;
    check_int("velocity decoded", adsb_decode_frame(bytes, n, &msg), 1);
    check_int("velocity ICAO", (long)msg.icao, 0x485020);
    check_int("velocity kind", msg.kind, ADSB_KIND_VELOCITY);
    check_int("velocity available", msg.has_velocity, 1);
    check_close("velocity ground speed", msg.ground_speed_kt, 159.2, 1.0);
    check_close("velocity heading", msg.heading_deg, 182.88, 0.5);
}

/* Published mode-s.org airborne-position even/odd pair over the Netherlands. */
static void test_position(void) {
    uint8_t even[ADSB_LONG_BYTES];
    uint8_t odd[ADSB_LONG_BYTES];
    parse_hex("8D40621D58C382D690C8AC2863A7", even, ADSB_LONG_BYTES);
    parse_hex("8D40621D58C386435CC412692AD6", odd, ADSB_LONG_BYTES);

    struct adsb_message em, om;
    check_int("even decoded", adsb_decode_frame(even, ADSB_LONG_BYTES, &em), 1);
    check_int("odd decoded", adsb_decode_frame(odd, ADSB_LONG_BYTES, &om), 1);
    check_int("even kind", em.kind, ADSB_KIND_AIRBORNE_POSITION);
    check_int("even parity", em.cpr_odd, 0);
    check_int("odd parity", om.cpr_odd, 1);
    check_int("even has altitude", em.has_altitude, 1);
    check_close("even altitude", em.altitude_ft, 38000.0, 25.0);

    double lat, lon;
    check_int("global position decoded",
              adsb_cpr_global(em.cpr_lat, em.cpr_lon, om.cpr_lat, om.cpr_lon, 0,
                              &lat, &lon),
              1);
    check_close("position latitude", lat, 52.2572, 0.001);
    check_close("position longitude", lon, 3.91937, 0.001);
}

static void test_crc_rejects_corruption(void) {
    uint8_t bytes[ADSB_LONG_BYTES];
    int n = parse_hex("8D4840D6202CC371C32CE0576098", bytes, ADSB_LONG_BYTES);
    bytes[5] ^= 0x01; /* flip one bit */
    if (adsb_crc(bytes, n) == 0) {
        fprintf(stderr, "corrupted frame: CRC unexpectedly zero\n");
        failures++;
    }
    struct adsb_message msg;
    check_int("corrupted frame dropped", adsb_decode_frame(bytes, n, &msg), 0);
}

static void test_frame_length(void) {
    check_int("DF17 long", adsb_frame_length_bits(17), ADSB_LONG_BITS);
    check_int("DF11 short", adsb_frame_length_bits(11), ADSB_SHORT_BITS);
    check_int("DF20 long", adsb_frame_length_bits(20), ADSB_LONG_BITS);
}

/* The frame every waveform check below is built from: a real DF17
   identification squitter with a valid CRC. */
static const char *const sample_frame_hex = "8D4840D6202CC371C32CE0576098";

#define WAVE_LEAD 20
#define WAVE_TOTAL (WAVE_LEAD + ADSB_PREAMBLE_SAMPLES + \
                    ADSB_LONG_BITS * ADSB_SAMPLES_PER_BIT + 20)

static const float wave_high = 100.0f;
static const float wave_low = 2.0f;

/* A buffer holding one modulated frame at WAVE_LEAD over a quiet floor. */
static float *build_wave(const uint8_t *frame) {
    float *mag = calloc(WAVE_TOTAL, sizeof(*mag));
    if (!mag) {
        fprintf(stderr, "wave allocation failed\n");
        exit(2);
    }
    for (size_t k = 0; k < WAVE_TOTAL; k++)
        mag[k] = wave_low;
    size_t written = adsb_modulate_frame(frame, ADSB_LONG_BYTES, wave_high,
                                         wave_low, mag, WAVE_LEAD, WAVE_TOTAL);
    if (written != ADSB_PREAMBLE_SAMPLES +
                   ADSB_LONG_BITS * ADSB_SAMPLES_PER_BIT) {
        fprintf(stderr, "modulator wrote %zu samples\n", written);
        failures++;
    }
    return mag;
}

static int compare_float(const void *left, const void *right) {
    float a = *(const float *)left;
    float b = *(const float *)right;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* End-to-end: modulate a real frame and recover it through the preamble
   detector and pulse-position demodulator. */
static void test_demod_from_magnitude(void) {
    uint8_t frame[ADSB_LONG_BYTES];
    parse_hex(sample_frame_hex, frame, ADSB_LONG_BYTES);
    float *mag = build_wave(frame);

    struct adsb_decoder dec;
    adsb_decoder_init(&dec);
    struct adsb_message out[4];
    struct adsb_demod_stats stats;
    size_t count = adsb_demod(&dec, mag, WAVE_TOTAL, 0.0, out, 4, NULL,
                              &stats);
    if (count < 1) {
        fprintf(stderr, "demod recovered no frames\n");
        failures++;
    } else {
        check_int("demod ICAO", (long)out[0].icao, 0x4840D6);
        check_str("demod callsign", out[0].callsign, "KLM1023");
    }
    check_int("stats attempts", (long)stats.attempts, 1);
    check_int("stats crc failures", (long)stats.crc_failed, 0);
    check_int("stats decoded", (long)stats.decoded, 1);
    if (stats.preambles < 1) {
        fprintf(stderr, "stats counted no preambles\n");
        failures++;
    }
    free(mag);
}

/* The preamble score peaks where the frame actually starts, and stands clear
   of the field around it -- which is the whole claim the landscape chart makes
   to a reader deciding whether a lock is real. */
static void test_trace_landscape(void) {
    uint8_t frame[ADSB_LONG_BYTES];
    parse_hex(sample_frame_hex, frame, ADSB_LONG_BYTES);
    float *mag = build_wave(frame);

    struct adsb_decoder dec;
    adsb_decoder_init(&dec);
    struct adsb_message out[4];
    struct adsb_frame_trace trace;
    memset(&trace, 0, sizeof(trace));
    adsb_demod(&dec, mag, WAVE_TOTAL, 1.5, out, 4, &trace, NULL);

    check_int("trace valid", trace.valid, 1);
    check_int("trace crc ok", trace.crc_ok, 1);
    check_int("trace DF", trace.downlink_format, 17);
    check_int("trace bit count", trace.bit_count, ADSB_LONG_BITS);
    check_int("trace ICAO", (long)trace.icao, 0x4840D6);
    check_close("trace time", trace.time_seconds, 1.5, 1e-9);
    check_int("landscape centre", trace.landscape_center,
              ADSB_TRACE_HALF_WIDTH);

    float peak = trace.landscape[trace.landscape_center];
    for (int i = 0; i < ADSB_TRACE_LANDSCAPE; i++) {
        if (trace.landscape[i] > peak) {
            fprintf(stderr,
                    "landscape peak is at %d (%.2f), not the centre (%.2f)\n",
                    i, trace.landscape[i], peak);
            failures++;
            break;
        }
    }
    float sorted[ADSB_TRACE_LANDSCAPE];
    memcpy(sorted, trace.landscape, sizeof(sorted));
    qsort(sorted, ADSB_TRACE_LANDSCAPE, sizeof(*sorted), compare_float);
    float median = sorted[ADSB_TRACE_LANDSCAPE / 2];
    if (peak < 2.0f * median) {
        fprintf(stderr, "landscape peak %.2f does not clear 2x median %.2f\n",
                peak, median);
        failures++;
    }
    free(mag);
}

/* The envelope is the frame as the receiver saw it, normalised: the four
   preamble pulses reach 1.0 and the quiet samples after them stay near the
   noise floor. */
static void test_trace_envelope(void) {
    uint8_t frame[ADSB_LONG_BYTES];
    parse_hex(sample_frame_hex, frame, ADSB_LONG_BYTES);
    float *mag = build_wave(frame);

    struct adsb_decoder dec;
    adsb_decoder_init(&dec);
    struct adsb_message out[4];
    struct adsb_frame_trace trace;
    memset(&trace, 0, sizeof(trace));
    adsb_demod(&dec, mag, WAVE_TOTAL, 0.0, out, 4, &trace, NULL);

    check_close("preamble high", trace.preamble_high, wave_high, 0.01);
    static const int pulses[4] = { 0, 2, 7, 9 };
    for (int i = 0; i < 4; i++) {
        char name[48];
        snprintf(name, sizeof(name), "envelope pulse %d", pulses[i]);
        check_close(name, trace.envelope[pulses[i]], 1.0, 0.01);
    }
    for (int k = 10; k < ADSB_PREAMBLE_SAMPLES; k++) {
        char name[48];
        snprintf(name, sizeof(name), "envelope quiet %d", k);
        check_close(name, trace.envelope[k], wave_low / wave_high, 0.01);
    }
    /* An extended squitter is always a long frame, so the envelope covers the
       whole buffer: 16 preamble samples then two per bit, ending on the last
       bit's second half. */
    check_int("envelope length", ADSB_TRACE_SAMPLES,
              ADSB_PREAMBLE_SAMPLES + trace.bit_count * ADSB_SAMPLES_PER_BIT);
    int last = (frame[ADSB_LONG_BYTES - 1]) & 1;
    check_close("envelope last sample",
                trace.envelope[ADSB_TRACE_SAMPLES - 1],
                last ? (double)(wave_low / wave_high) : 1.0, 0.01);
    free(mag);
}

/* Confidence is high on a clean frame and collapses when the two halves of
   each bit interval move together -- the marginal frame the bar chart exists
   to make visible, which still decodes and so is invisible in the log. */
static void test_trace_confidence(void) {
    uint8_t frame[ADSB_LONG_BYTES];
    parse_hex(sample_frame_hex, frame, ADSB_LONG_BYTES);

    float *clean = build_wave(frame);
    struct adsb_decoder dec;
    adsb_decoder_init(&dec);
    struct adsb_message out[4];
    struct adsb_frame_trace trace;
    memset(&trace, 0, sizeof(trace));
    size_t count = adsb_demod(&dec, clean, WAVE_TOTAL, 0.0, out, 4, &trace,
                              NULL);
    check_int("clean frame decoded", (long)count, 1);
    float lowest = 1.0f;
    for (int bit = 0; bit < trace.bit_count; bit++)
        if (trace.confidence[bit] < lowest)
            lowest = trace.confidence[bit];
    if (lowest < 0.8f) {
        fprintf(stderr, "clean frame confidence dips to %.3f\n", lowest);
        failures++;
    }
    free(clean);

    /* Raise the low half of every data bit to 0.85 of the high one. The hard
       decisions are unchanged, so this still decodes; only the margin behind
       them shrinks. */
    float *marginal = build_wave(frame);
    float *data = marginal + WAVE_LEAD + ADSB_PREAMBLE_SAMPLES;
    for (int bit = 0; bit < ADSB_LONG_BITS; bit++) {
        float *pair = data + bit * ADSB_SAMPLES_PER_BIT;
        if (pair[0] > pair[1])
            pair[1] = 0.85f * pair[0];
        else
            pair[0] = 0.85f * pair[1];
    }
    adsb_decoder_init(&dec);
    memset(&trace, 0, sizeof(trace));
    count = adsb_demod(&dec, marginal, WAVE_TOTAL, 0.0, out, 4, &trace, NULL);
    check_int("marginal frame still decodes", (long)count, 1);
    float highest = 0.0f;
    for (int bit = 0; bit < trace.bit_count; bit++)
        if (trace.confidence[bit] > highest)
            highest = trace.confidence[bit];
    if (highest > 0.2f) {
        fprintf(stderr, "marginal frame confidence reaches %.3f\n", highest);
        failures++;
    }
    /* The sign still says which way each decision went. */
    for (int bit = 0; bit < trace.bit_count; bit++) {
        int expected = (frame[bit >> 3] >> (7 - (bit & 7))) & 1;
        if ((trace.margin[bit] > 0.0f) != (expected != 0)) {
            fprintf(stderr, "margin sign disagrees with bit %d\n", bit);
            failures++;
            break;
        }
    }
    free(marginal);
}

/* A frame that fails its CRC is dropped from the log but kept in the trace:
   it is the frame a user most needs to see, and the counters have to say it
   happened. */
static void test_trace_latches_failure(void) {
    uint8_t frame[ADSB_LONG_BYTES];
    parse_hex(sample_frame_hex, frame, ADSB_LONG_BYTES);
    frame[5] ^= 0x01; /* leaves DF17 intact, breaks the parity */
    float *mag = build_wave(frame);

    struct adsb_decoder dec;
    adsb_decoder_init(&dec);
    struct adsb_message out[4];
    struct adsb_frame_trace trace;
    struct adsb_demod_stats stats;
    memset(&trace, 0, sizeof(trace));
    size_t count = adsb_demod(&dec, mag, WAVE_TOTAL, 0.0, out, 4, &trace,
                              &stats);

    check_int("failed frame not decoded", (long)count, 0);
    check_int("failed frame traced", trace.valid, 1);
    check_int("failed frame marked", trace.crc_ok, 0);
    check_int("failed frame DF", trace.downlink_format, 17);
    check_int("failed frame has no address", (long)trace.icao, 0);
    check_int("stats decoded none", (long)stats.decoded, 0);
    if (stats.crc_failed < 1) {
        fprintf(stderr, "stats did not count the CRC failure\n");
        failures++;
    }
    free(mag);
}

/* The score is the evidence preamble_at() weighs, so it must be far higher on
   a preamble than on the noise beside it. */
static void test_preamble_score(void) {
    uint8_t frame[ADSB_LONG_BYTES];
    parse_hex(sample_frame_hex, frame, ADSB_LONG_BYTES);
    float *mag = build_wave(frame);

    float at_frame = adsb_preamble_score(mag, WAVE_LEAD, WAVE_TOTAL);
    float at_noise = adsb_preamble_score(mag, 0, WAVE_TOTAL);
    check_close("score on a flat floor", at_noise, 1.0, 0.01);
    if (at_frame < 10.0f * at_noise) {
        fprintf(stderr, "preamble score %.2f barely clears noise %.2f\n",
                at_frame, at_noise);
        failures++;
    }
    check_close("score past the buffer",
                adsb_preamble_score(mag, WAVE_TOTAL - 4, WAVE_TOTAL), 0.0,
                1e-9);
    free(mag);
}

int main(void) {
    test_frame_length();
    test_identification();
    test_velocity();
    test_position();
    test_crc_rejects_corruption();
    test_demod_from_magnitude();
    test_preamble_score();
    test_trace_landscape();
    test_trace_envelope();
    test_trace_confidence();
    test_trace_latches_failure();

    if (failures) {
        fprintf(stderr, "%d adsb_dsp check(s) failed\n", failures);
        return 1;
    }
    puts("adsb_dsp checks passed");
    return 0;
}
