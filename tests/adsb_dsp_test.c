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

/* End-to-end: synthesize the magnitude waveform of a real frame and recover it
   through the preamble detector and pulse-position demodulator. */
static void test_demod_from_magnitude(void) {
    uint8_t frame[ADSB_LONG_BYTES];
    parse_hex("8D4840D6202CC371C32CE0576098", frame, ADSB_LONG_BYTES);

    const float high = 100.0f;
    const float low = 2.0f;
    const size_t lead = 20;
    const size_t total =
        lead + ADSB_PREAMBLE_SAMPLES + ADSB_LONG_BITS * ADSB_SAMPLES_PER_BIT + 20;
    float *mag = calloc(total, sizeof(*mag));
    if (!mag) {
        fprintf(stderr, "demod allocation failed\n");
        exit(2);
    }
    for (size_t k = 0; k < total; k++)
        mag[k] = low;
    /* Preamble pulses at samples 0, 2, 7, 9. */
    float *pre = mag + lead;
    pre[0] = pre[2] = pre[7] = pre[9] = high;
    /* Pulse-position data bits: energy-first is a one, energy-second a zero. */
    float *data = mag + lead + ADSB_PREAMBLE_SAMPLES;
    for (int bit = 0; bit < ADSB_LONG_BITS; bit++) {
        int set = (frame[bit >> 3] >> (7 - (bit & 7))) & 1;
        data[bit * ADSB_SAMPLES_PER_BIT] = set ? high : low;
        data[bit * ADSB_SAMPLES_PER_BIT + 1] = set ? low : high;
    }

    struct adsb_decoder dec;
    adsb_decoder_init(&dec);
    struct adsb_message out[4];
    size_t count = adsb_demod(&dec, mag, total, 0.0, out, 4);
    if (count < 1) {
        fprintf(stderr, "demod recovered no frames\n");
        failures++;
    } else {
        check_int("demod ICAO", (long)out[0].icao, 0x4840D6);
        check_str("demod callsign", out[0].callsign, "KLM1023");
    }
    free(mag);
}

int main(void) {
    test_frame_length();
    test_identification();
    test_velocity();
    test_position();
    test_crc_rejects_corruption();
    test_demod_from_magnitude();

    if (failures) {
        fprintf(stderr, "%d adsb_dsp check(s) failed\n", failures);
        return 1;
    }
    puts("adsb_dsp checks passed");
    return 0;
}
