/*
 * Unit checks for the transversal I/Q history ring buffer.
 *
 *   make check-iq-ring
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "iq_ring.h"

static void test_init_and_configure(void) {
    struct iq_ring ring;
    check_int("init returns 0", iq_ring_init(&ring, 10.0), 0);
    check_true("mutex ready", ring.mutex_ready);

    check_int("configure with 2 MS/s u8",
              iq_ring_configure(&ring, 2000000, SAMPLE_FORMAT_U8, 127.5f,
                                433800000, 496, 1, 44, "RTL2832U", "R820T"),
              0);

    /* 10.0s at 2 MS/s * 2 bytes/pair = 40,000,000 bytes */
    check_size("capacity is 40M bytes", ring.capacity_bytes, 40000000);
    check_int("bytes per pair is 2", (int)ring.bytes_per_pair, 2);
    check_int("initial filled bytes is 0", (int)ring.filled_bytes, 0);

    iq_ring_free(&ring);
}

static void test_push_and_extract(void) {
    struct iq_ring ring;
    iq_ring_init(&ring, 1.0); /* 1.0 second buffer */
    /* 1000 S/s, 2 bytes/pair = 2000 bytes capacity */
    iq_ring_configure(&ring, 1000, SAMPLE_FORMAT_U8, 127.5f,
                      100000000, 300, 1, 0, "TestDev", "TestTuner");

    check_size("capacity is 2000 bytes", ring.capacity_bytes, 2000);

    /* Push 500 bytes (0.25s) with known values */
    unsigned char block1[500];
    for (int i = 0; i < 500; i++) block1[i] = (unsigned char)(i & 0xFF);
    iq_ring_push(&ring, block1, 500, 1.0);

    check_size("filled 500 bytes", ring.filled_bytes, 500);
    check_close("available 0.25s", iq_ring_available_seconds(&ring), 0.25, 0.01);

    /* Push 1000 bytes (0.5s) */
    unsigned char block2[1000];
    memset(block2, 0xAA, 1000);
    iq_ring_push(&ring, block2, 1000, 1.5);
    check_size("filled 1500 bytes", ring.filled_bytes, 1500);

    /* Push another 1000 bytes to cause a wrap-around (total 2500 pushed, cap 2000) */
    unsigned char block3[1000];
    memset(block3, 0x55, 1000);
    iq_ring_push(&ring, block3, 1000, 2.0);

    check_size("filled capped at 2000 bytes", ring.filled_bytes, 2000);
    check_close("available 1.0s", iq_ring_available_seconds(&ring), 1.0, 0.01);

    /* Extract 0.5s slice centered at age 0.25s ago */
    unsigned char extracted[1024];
    size_t actual = 0;
    uint32_t rate = 0, freq = 0;
    enum sample_format fmt;
    float fs = 0;

    int rc = iq_ring_extract_slice(&ring, 0.25, 0.5, extracted, sizeof(extracted),
                                   &actual, &rate, &freq, &fmt, &fs);
    check_int("extract slice returns 0", rc, 0);
    check_size("extracted 1000 bytes (0.5s)", actual, 1000);
    check_int("sample rate 1000", (int)rate, 1000);
    check_int("frequency 100M", (int)freq, 100000000);

    iq_ring_free(&ring);
}

static void test_save_slice(void) {
    struct iq_ring ring;
    iq_ring_init(&ring, 2.0);
    iq_ring_configure(&ring, 10000, SAMPLE_FORMAT_U8, 127.5f,
                      433800000, 496, 1, 44, "RTL2832U", "R820T");

    /* Push 20,000 bytes (1.0s) of test samples */
    unsigned char buf[4000];
    for (int i = 0; i < 4000; i++) buf[i] = (unsigned char)(i % 256);
    for (int k = 0; k < 5; k++) {
        iq_ring_push(&ring, buf, 4000, (double)k * 0.2);
    }

    char tmp_bin[] = "/tmp/opencode/test_ring_slice_XXXXXX";
    int fd = mkstemp(tmp_bin);
    check_true("mkstemp created temp file", fd >= 0);
    close(fd);

    int rc = iq_ring_save_slice(&ring, 0.5, 0.5, tmp_bin, "test_srd");
    check_int("save slice returns 0", rc, 0);

    /* Verify binary file was written */
    FILE *f = fopen(tmp_bin, "rb");
    check_true("slice bin file exists", f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        /* 0.5s at 10,000 S/s * 2 bytes = 10,000 bytes */
        check_int("slice file size is 10000 bytes", (int)sz, 10000);
    }

    /* Verify sidecar was written */
    char tmp_json[512];
    snprintf(tmp_json, sizeof(tmp_json), "%s.json", tmp_bin);
    FILE *fj = fopen(tmp_json, "r");
    check_true("slice sidecar json exists", fj != NULL);
    if (fj) {
        char content[1024] = {0};
        fread(content, 1, sizeof(content) - 1, fj);
        fclose(fj);
        check_true("sidecar has technology test_srd", strstr(content, "\"technology\": \"test_srd\"") != NULL);
        check_true("sidecar has center frequency", strstr(content, "\"center_frequency_hz\": 433800000") != NULL);
        unlink(tmp_json);
    }
    unlink(tmp_bin);

    iq_ring_free(&ring);
}

static void test_snapshot_extraction_and_reconfiguration(void) {
    struct iq_ring ring;
    iq_ring_init(&ring, 2.0);
    /* 2.4 MS/s 16-bit container: 4 bytes per pair */
    iq_ring_configure(&ring, 2400000, SAMPLE_FORMAT_S16, 32767.0f,
                      800000000, 350, 1, 10, "Airspy", "R820T");

    check_size("capacity accommodates 2.4 MS/s S16", ring.capacity_bytes,
               (size_t)(2.0 * 2400000.0 * 4.0));

    /* Push 2 seconds of synthetic wrapped data */
    size_t block_bytes = 480000;
    unsigned char *block = malloc(block_bytes);
    for (size_t i = 0; i < block_bytes; i++) block[i] = (unsigned char)(i & 0xFF);
    for (int k = 0; k < 50; k++) {
        iq_ring_push(&ring, block, block_bytes, (double)k * 0.05);
    }
    free(block);

    /* Extract 1.0s snapshot without caller capacity */
    struct iq_snapshot *snap = iq_ring_extract_snapshot(&ring, 0.5, 1.0);
    check_true("snapshot extraction succeeded", snap != NULL);
    if (snap) {
        check_size("snapshot has 1.0s of S16 bytes", snap->byte_count, 2400000 * 4);
        check_size("snapshot has 2.4M pairs", snap->pair_count, 2400000);
        check_int("sample rate preserved", (int)snap->sample_rate, 2400000);
        check_int("format is S16", snap->format, SAMPLE_FORMAT_S16);
        check_int("frequency preserved", (int)snap->frequency_hz, 800000000);
        check_close("duration is 1.0s", snap->duration_seconds, 1.0, 0.001);
        check_str("tuner is R820T", snap->tuner, "R820T");
        iq_snapshot_free(snap);
    }

    /* Reconfiguration resets history */
    iq_ring_configure(&ring, 2000000, SAMPLE_FORMAT_U8, 127.5f,
                      900000000, 400, 1, 0, "RTL2832U", "R820T");
    check_size("history cleared on reconfiguration", ring.filled_bytes, 0);
    struct iq_snapshot *empty_snap = iq_ring_extract_snapshot(&ring, 0.0, 1.0);
    check_true("old bytes cannot pair with new metadata", empty_snap == NULL);

    iq_ring_free(&ring);
}

int main(void) {
    test_init_and_configure();
    test_push_and_extract();
    test_snapshot_extraction_and_reconfiguration();
    test_save_slice();
    return check_report("transversal IQ ring buffer");
}
