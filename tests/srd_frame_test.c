/*
 * Deterministic checks for the SRD frame extraction and parsing.
 *
 *   make check-srd-frame
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device_profile.h"
#include "sdr_dsp.h"
#include "srd_dsp.h"
#include "srd_frame.h"

static void unpack_bytes_to_bits(const uint8_t *bytes, size_t byte_count,
                                 uint8_t *bits_out) {
    for (size_t i = 0; i < byte_count; i++) {
        for (int b = 7; b >= 0; b--) {
            *bits_out++ = (bytes[i] >> b) & 1;
        }
    }
}

static size_t encode_manchester(const uint8_t *bits, size_t bit_count,
                                enum srd_manchester_polarity polarity,
                                uint8_t *chips_out) {
    size_t chips = 0;
    for (size_t i = 0; i < bit_count; i++) {
        uint8_t b = bits[i];
        if (polarity == SRD_MANCHESTER_THOMAS) {
            chips_out[chips++] = b ? 0 : 1;
            chips_out[chips++] = b ? 1 : 0;
        } else {
            chips_out[chips++] = b ? 1 : 0;
            chips_out[chips++] = b ? 0 : 1;
        }
    }
    return chips;
}

static size_t chips_to_runs(const uint8_t *chips, size_t chip_count,
                            double chip_s, struct srd_run *runs_out,
                            size_t max_runs) {
    if (!chips || chip_count == 0 || max_runs == 0)
        return 0;

    int state = chips[0];
    size_t run_len = 1;
    size_t runs_written = 0;

    for (size_t i = 1; i < chip_count; i++) {
        int now = chips[i];
        if (now == state) {
            run_len++;
            continue;
        }
        if (runs_written < max_runs) {
            runs_out[runs_written].state = state;
            runs_out[runs_written].length_samples = run_len * 100;
            runs_out[runs_written].duration_seconds = (double)run_len * chip_s;
            runs_written++;
        }
        state = now;
        run_len = 1;
    }

    if (runs_written < max_runs) {
        runs_out[runs_written].state = state;
        runs_out[runs_written].length_samples = run_len * 100;
        runs_out[runs_written].duration_seconds = (double)run_len * chip_s;
        runs_written++;
    }

    return runs_written;
}

static void test_synthetic_full_and_repeat_frames(void) {
    const double chip_s = 0.000500;
    struct srd_run runs[512];
    size_t run_count = 0;

    /* 1. Preamble: 10 runs of 500 us */
    int state = 1;
    for (int i = 0; i < 10; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = chip_s;
        runs[run_count].length_samples = 100;
        state = !state;
        run_count++;
    }

    /* 2. Delimiter 1: 6 runs of 750 us */
    for (int i = 0; i < 6; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = 0.000750;
        runs[run_count].length_samples = 150;
        state = !state;
        run_count++;
    }

    /* 3. Full Frame: 10 bytes (0x3F, 8 payload bytes, 0xD4) */
    const uint8_t full_bytes[10] = { 0x3F, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0xD4 };
    uint8_t full_bits[80];
    uint8_t full_chips[160];
    unpack_bytes_to_bits(full_bytes, 10, full_bits);
    encode_manchester(full_bits, 80, SRD_MANCHESTER_THOMAS, full_chips);
    run_count += chips_to_runs(full_chips, 160, chip_s, runs + run_count, 512 - run_count);

    /* 4. Inter-frame preamble */
    state = runs[run_count - 1].state ? 0 : 1;
    for (int i = 0; i < 10; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = chip_s;
        runs[run_count].length_samples = 100;
        state = !state;
        run_count++;
    }

    /* 5. Delimiter 2: 6 runs of 750 us */
    for (int i = 0; i < 6; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = 0.000750;
        runs[run_count].length_samples = 150;
        state = !state;
        run_count++;
    }

    /* 6. Repeat Frame: 3 bytes (0x1F, 0x12, 0xD4) */
    const uint8_t repeat_bytes[3] = { 0x1F, 0x12, 0xD4 };
    uint8_t repeat_bits[24];
    uint8_t repeat_chips[48];
    unpack_bytes_to_bits(repeat_bytes, 3, repeat_bits);
    encode_manchester(repeat_bits, 24, SRD_MANCHESTER_THOMAS, repeat_chips);
    run_count += chips_to_runs(repeat_chips, 48, chip_s, runs + run_count, 512 - run_count);

    struct srd_frame frames[8];
    size_t count = srd_extract_frames(runs, run_count, chip_s, SRD_MANCHESTER_THOMAS, frames, 8);
    check_size("extracted exactly 2 frames", count, 2);

    if (count >= 2) {
        check_int("first frame is SRD_FRAME_FULL", frames[0].kind, SRD_FRAME_FULL);
        check_size("first frame has 10 bytes", frames[0].byte_count, 10);
        check_size("first frame has 80 bits", frames[0].bit_count, 80);
        check_size("first frame zero errors", frames[0].error_count, 0);
        check_int("first frame bytes match",
                  memcmp(frames[0].bytes, full_bytes, 10), 0);

        check_int("second frame is SRD_FRAME_REPEAT", frames[1].kind, SRD_FRAME_REPEAT);
        check_size("second frame has 3 bytes", frames[1].byte_count, 3);
        check_size("second frame has 24 bits", frames[1].bit_count, 24);
        check_size("second frame zero errors", frames[1].error_count, 0);
        check_int("second frame bytes match",
                  memcmp(frames[1].bytes, repeat_bytes, 3), 0);
    }
}

static void test_real_capture_invariants(void) {
    const char *path_a = "testfiles/srd_remote_control_ook_a.bin";
    FILE *fa = fopen(path_a, "rb");
    if (!fa) {
        check_skip(path_a);
        return;
    }

    struct device_profile dev = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);
    fseek(fa, 0, SEEK_END);
    long sz = ftell(fa);
    rewind(fa);
    size_t pairs = (size_t)sz / dev.bytes_per_pair;
    if (pairs > 10000000) pairs = 10000000;

    unsigned char *raw = malloc(pairs * dev.bytes_per_pair);
    float *i_buf = malloc(pairs * sizeof(float));
    float *q_buf = malloc(pairs * sizeof(float));
    float *mag_buf = malloc(pairs * sizeof(float));
    fread(raw, dev.bytes_per_pair, pairs, fa);
    fclose(fa);
    sdr_dsp_convert_iq(&dev, raw, pairs * dev.bytes_per_pair, i_buf, q_buf, mag_buf, pairs);

    struct srd_transmission txs[SRD_MAX_TRANSMISSIONS];
    int count = srd_find_transmissions(i_buf, q_buf, pairs, 2000000.0, dev.full_scale, 25.0, 0.050, txs, SRD_MAX_TRANSMISSIONS);
    check_int("capture 434a has 4 presses", count, 4);

    if (count >= 1) {
        size_t env_cap = txs[0].pair_count / 10 + 2000;
        float *env = malloc(env_cap * sizeof(float));
        struct srd_run runs[8192];
        double work_rate;
        size_t env_n = srd_demodulate_envelope(i_buf + txs[0].offset_pairs, q_buf + txs[0].offset_pairs,
                                               txs[0].pair_count, 2000000.0, txs[0].carrier_hz, env, env_cap, &work_rate);
        double thresh = srd_envelope_threshold(env, env_n);
        size_t r_n = srd_extract_runs(env, env_n, work_rate, thresh, runs, 8192);
        double chip_s = srd_chip_period(runs, r_n);

        struct srd_frame frames[16];
        size_t n_frames = srd_extract_frames(runs, r_n, chip_s, SRD_MANCHESTER_THOMAS, frames, 16);
        check_size("press 1 has at least 4 frames", n_frames >= 4, 1);
        if (n_frames >= 4) {
            for (size_t f = 0; f < 4; f++) {
                check_int("frame is SRD_FRAME_FULL", frames[f].kind, SRD_FRAME_FULL);
                check_size("frame has 10 bytes", frames[f].byte_count, 10);
                check_size("frame has 0 errors", frames[f].error_count, 0);
                check_int("frame has the full-frame header", frames[f].bytes[0],
                          SRD_HEADER_FULL_THOMAS);
                check_int("frame has the protocol trailer", frames[f].bytes[9],
                          SRD_TRAILER_THOMAS);
                check_int("repeated frame bytes agree",
                          memcmp(frames[f].bytes, frames[0].bytes, 10), 0);
            }
        }
        free(env);
    }

    free(raw);
    free(i_buf);
    free(q_buf);
    free(mag_buf);
}

static void test_synthetic_generic_manchester_frames(void) {
    const double chip_s = 0.000064; /* 64 us chips */
    struct srd_run runs[512];
    size_t run_count = 0;

    /* 1. Leading carrier / preamble (16 chips) */
    int state = 1;
    for (int i = 0; i < 16; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = chip_s;
        runs[run_count].length_samples = 128;
        state = !state;
        run_count++;
    }

    /* 2. Generic Manchester payload: 6 bytes */
    const uint8_t payload_bytes[6] = { 0xA5, 0x5A, 0x12, 0x34, 0x56, 0x78 };
    uint8_t bits[48];
    uint8_t chips[96];
    unpack_bytes_to_bits(payload_bytes, 6, bits);
    encode_manchester(bits, 48, SRD_MANCHESTER_THOMAS, chips);
    run_count += chips_to_runs(chips, 96, chip_s, runs + run_count, 512 - run_count);

    struct srd_frame frames[4];
    size_t n = srd_extract_frames(runs, run_count, chip_s, SRD_MANCHESTER_THOMAS, frames, 4);
    check_size("generic Manchester frame extracted", n, 1);
    if (n > 0) {
        check_int("frame is SRD_FRAME_GENERIC", frames[0].kind, SRD_FRAME_GENERIC);
        check_size("frame has 6 bytes", frames[0].byte_count, 6);
        check_int("payload matches exactly",
                  memcmp(frames[0].bytes, payload_bytes, 6), 0);
    }
}

/*
 * Three frames in one run stream, and all three reported.
 *
 * The generic path used to keep only the longest unbroken Manchester
 * stretch, which on testfiles/srd_remote_control_fsk.bin threw away most
 * of a capture: `make probe-srd` finds eight transmissions each carrying a
 * 14-byte frame, and the extractor returned one. A transmitter with no
 * delimiter offers no frame boundary but the code itself, so every maximal
 * legal stretch is a candidate frame and the caller decides what to do with
 * them.
 *
 * The fixture is three identical shapes -- a quiet run, a preamble, a
 * payload -- because that is what a repeating remote sends and what a block
 * of samples holds. The quiet run is what separates them: four chips of one
 * state is a Manchester violation whichever alignment reads it, which is the
 * only boundary an unknown transmitter gives.
 *
 * The payloads all start with a 0 bit deliberately. A preamble decodes to a
 * run of one value and srd_extract_frames() strips a leading run of eight or
 * more as preamble, so a payload whose first bit continues that run loses a
 * bit to the strip and every byte after it shifts. That is a property of
 * having no sync word rather than a fault, and it is why this fixture pins
 * the bytes rather than trusting the length.
 */
static void test_generic_path_emits_every_frame(void) {
    const double chip_s = 0.000064;
    const uint8_t payloads[3][6] = {
        { 0x5A, 0xA5, 0x12, 0x34, 0x56, 0x78 },
        { 0x12, 0x9C, 0x44, 0x01, 0xEE, 0x03 },
        { 0x3C, 0xC3, 0x77, 0x88, 0x99, 0xAA },
    };
    struct srd_run runs[1024];
    size_t run_count = 0;
    struct srd_frame frames[8];
    size_t n, f;

    for (int k = 0; k < 3; k++) {
        uint8_t bits[48];
        uint8_t chips[96];
        int state;

        /* The gap: four chips of one state, a violation at either phase. */
        runs[run_count].state = 0;
        runs[run_count].duration_seconds = 4.0 * chip_s;
        runs[run_count].length_samples = 512;
        run_count++;

        /*
         * Fifteen chips of preamble, not sixteen. The scan resumes on the
         * last chip of the gap, so it spends one pair crossing into the
         * preamble; an odd preamble leaves the payload starting on a pair
         * boundary, which is what keeps the bytes readable.
         */
        state = 1;
        for (int i = 0; i < 15; i++) {
            runs[run_count].state = state;
            runs[run_count].duration_seconds = chip_s;
            runs[run_count].length_samples = 128;
            state = !state;
            run_count++;
        }

        unpack_bytes_to_bits(payloads[k], 6, bits);
        encode_manchester(bits, 48, SRD_MANCHESTER_THOMAS, chips);
        run_count += chips_to_runs(chips, 96, chip_s, runs + run_count,
                                   1024 - run_count);
    }

    n = srd_extract_frames(runs, run_count, chip_s, SRD_MANCHESTER_THOMAS,
                           frames, 8);
    check_size("all three generic frames are reported", n, 3);

    for (f = 0; f < n && f < 3; f++) {
        char label[64];

        snprintf(label, sizeof(label), "frame %zu is generic", f);
        check_int(label, frames[f].kind, SRD_FRAME_GENERIC);
        snprintf(label, sizeof(label), "frame %zu has 6 bytes", f);
        check_size(label, frames[f].byte_count, 6);
        snprintf(label, sizeof(label), "frame %zu carries its own payload", f);
        check_int(label, memcmp(frames[f].bytes, payloads[f], 6), 0);
    }

    /*
     * Each frame knows when it was, which it did not before: a generic frame
     * reported time 0.0 whatever it was, so a caller had nothing but the
     * arrival of the block to stamp it with and two frames 100 ms apart were
     * indistinguishable in a log.
     */
    if (n == 3) {
        check_true("frame times increase across the stream",
                   frames[1].time_seconds > frames[0].time_seconds &&
                   frames[2].time_seconds > frames[1].time_seconds);
        check_close("the second frame is one shape later",
                    frames[1].time_seconds - frames[0].time_seconds,
                    115.0 * chip_s, 4.0 * chip_s);
    }

    /*
     * The caller's array is the limit, and it is respected rather than
     * overrun -- the same stream asked for two frames returns two.
     */
    n = srd_extract_frames(runs, run_count, chip_s, SRD_MANCHESTER_THOMAS,
                           frames, 2);
    check_size("a two-frame array gets two frames", n, 2);
}

/*
 * A stream carrying nothing but preamble is not a frame.
 *
 * An unmodulated carrier and a pure preamble both pack to 0x00 or 0xFF
 * whatever chip period was used to read them, so reporting one as a decode
 * would turn every wakeup burst into a frame. The 2-FSK SRD remote control capture sends one
 * before each data burst, so this is half of what that capture holds.
 */
static void test_preamble_alone_is_not_a_frame(void) {
    const double chip_s = 0.000064;
    struct srd_run runs[512];
    size_t run_count = 0;
    struct srd_frame frames[4];
    int state = 1;

    for (int i = 0; i < 400; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = chip_s;
        runs[run_count].length_samples = 128;
        state = !state;
        run_count++;
    }

    check_size("pure preamble decodes to no frame",
               srd_extract_frames(runs, run_count, chip_s,
                                  SRD_MANCHESTER_THOMAS, frames, 4), 0);
}

/*
 * A device type is named only where the frame proves it.
 *
 * This is a verdict, and signal_findings.h refuses to make one from a
 * measurement because "probably TETRA" is a claim nothing here can stand
 * behind. The difference is the evidence: a full or repeat frame has matched
 * a header and a trailer at known offsets in a Manchester stream decoded
 * without a violation, which repetition cannot manufacture. A burst with no
 * decode has proved nothing, whatever its modulation or chip period, so it is
 * named nothing.
 */
static void test_device_type_is_named_only_from_evidence(void) {
    uint8_t known_fsk[14] = { 0x27, 0xE5, 0x7B, 0x11, 0x22, 0x33, 0x44,
                              0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB };
    uint8_t other[14] = { 0x12, 0x34, 0x56, 0x11, 0x22, 0x33, 0x44,
                          0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB };
    uint8_t short_frame[6] = { 0x27, 0xE5, 0x7B, 0x11, 0x22, 0x33 };

    check_int("a full frame is the delimited OOK remote",
              srd_device_type_of(SRD_FRAME_FULL, NULL, 0),
              SRD_DEVICE_REMOTE_CONTROL);
    check_int("so is a repeat frame",
              srd_device_type_of(SRD_FRAME_REPEAT, NULL, 0),
              SRD_DEVICE_REMOTE_CONTROL);
    check_int("a 14-byte generic frame with the known prefix is the 2-FSK remote",
              srd_device_type_of(SRD_FRAME_GENERIC, known_fsk, 14),
              SRD_DEVICE_REMOTE_FSK);

    /* Everything a decode does not prove. */
    check_int("a generic frame with an unknown prefix is unknown",
              srd_device_type_of(SRD_FRAME_GENERIC, other, 14),
              SRD_DEVICE_UNKNOWN);
    check_int("the right prefix in too short a frame is unknown",
              srd_device_type_of(SRD_FRAME_GENERIC, short_frame, 6),
              SRD_DEVICE_UNKNOWN);
    check_int("a detected burst with no frame is unknown",
              srd_device_type_of(SRD_FRAME_UNDECODED, NULL, 0),
              SRD_DEVICE_UNKNOWN);
    check_int("and so is a wakeup, whatever its modulation",
              srd_device_type_of(SRD_FRAME_FSK_DETECTED, NULL, 0),
              SRD_DEVICE_UNKNOWN);
    check_int("null bytes cannot name anything",
              srd_device_type_of(SRD_FRAME_GENERIC, NULL, 14),
              SRD_DEVICE_UNKNOWN);

    check_str("the unknown type has a name to draw",
              srd_device_type_name(SRD_DEVICE_UNKNOWN), "unknown");
    check_str("and so does the SRD remote control",
              srd_device_type_name(SRD_DEVICE_REMOTE_CONTROL),
              "SRD remote control");
    check_str("the 2-FSK remote is named after its protocol",
              srd_device_type_name(SRD_DEVICE_REMOTE_FSK),
              "SRD remote control (2-FSK)");
    check_true("a type out of range still has a name rather than a crash",
               srd_device_type_name((enum srd_device_type)99) != NULL);
}

static void test_real_2fsk_capture(void) {
    const char *path = "testfiles/srd_remote_control_fsk.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        check_skip(path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *raw = malloc(size);
    check_true("allocated raw capture buffer", raw != NULL);
    check_size("read entire capture", fread(raw, 1, size, f), size);
    fclose(f);

    size_t pairs = size / 2;
    if (pairs > 3000000)
        pairs = 3000000;
    float *i_buf = malloc(pairs * sizeof(float));
    float *q_buf = malloc(pairs * sizeof(float));
    float *mag_buf = malloc(pairs * sizeof(float));
    struct sdr_dsp dsp;
    sdr_dsp_init(&dsp);
    struct device_profile dev = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);
    sdr_dsp_convert_iq(&dev, raw, pairs * dev.bytes_per_pair, i_buf, q_buf, mag_buf, pairs);

    struct srd_transmission txs[SRD_MAX_TRANSMISSIONS];
    int count = srd_find_transmissions(i_buf, q_buf, pairs, 2000000.0, dev.full_scale,
                                       15.0, 0.010, txs, SRD_MAX_TRANSMISSIONS);
    check_true("transmissions found in real 2-FSK capture", count > 0);

    int decoded_any = 0;
    for (int t = 0; t < count; t++) {
        if (txs[t].modulation != SRD_MOD_FSK2 ||
            txs[t].duration_seconds < 0.020 || txs[t].duration_seconds > 0.030)
            continue;

        size_t cap = txs[t].pair_count / 10 + 2000;
        float *disc = malloc(cap * sizeof(float));
        double work_rate = 0.0;
        size_t n = srd_demodulate_fsk(i_buf + txs[t].offset_pairs,
                                      q_buf + txs[t].offset_pairs,
                                      txs[t].pair_count, 2000000.0,
                                      txs[t].carrier_hz, disc, cap,
                                      &work_rate);
        double th = srd_discriminator_threshold(disc, n);
        struct srd_run runs[2048];
        size_t r = srd_extract_runs(disc, n, work_rate, th, runs, 2048);
        double chip_s = srd_chip_period(runs, r);
        if (chip_s > 0.0) {
            struct srd_frame frames[4];
            size_t n_frames = srd_extract_frames(runs, r, chip_s, SRD_MANCHESTER_THOMAS, frames, 4);
            if (n_frames >= 1 && frames[0].byte_count >= 10) {
                check_close("2-FSK chip period is ~64 us", chip_s * 1e6,
                            64.0, 2.0);
                check_int("frame is generic", frames[0].kind, SRD_FRAME_GENERIC);
                check_true("frame has >= 10 bytes", frames[0].byte_count >= 10);
                decoded_any = 1;
                free(disc);
                break;
            }
        }
        free(disc);
    }
    check_true("decoded at least 1 frame from 2-FSK data bursts", decoded_any);

    free(raw);
    free(i_buf);
    free(q_buf);
    free(mag_buf);
}

int main(void) {
    test_synthetic_full_and_repeat_frames();
    test_synthetic_generic_manchester_frames();
    test_generic_path_emits_every_frame();
    test_preamble_alone_is_not_a_frame();
    test_device_type_is_named_only_from_evidence();
    test_real_capture_invariants();
    test_real_2fsk_capture();
    return check_report("SRD 430-440 MHz frame extraction");
}
