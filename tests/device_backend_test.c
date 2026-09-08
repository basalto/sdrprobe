/*
 * check-device-backend -- the contract a receiver has to satisfy.
 *
 * `.scratch/device-model/issues/07-a-second-backend.md`. The vtable in
 * `device_backend.h` exists because a second backend is coming; this suite is
 * what keeps it honest before the hardware lands, and it does that with two
 * implementations that need none:
 *
 *   - the **capture** backend, which is real and shipping, and whose content
 *     is mostly refusals -- a recording holds one tuning at one rate with one
 *     gain baked in;
 *   - a **fake**, which records what it was asked and can be made to fail on
 *     demand, so the refusal paths and the NULL-entry paths are reachable.
 *
 * The librtlsdr backend is deliberately absent: linking it would drag
 * `<rtl-sdr.h>` into a check, and this suite links `-lm` and nothing else.
 * What it establishes is the contract; `check-pipelines` establishes that the
 * real one is wired in.
 */

#include <fcntl.h>
#include <unistd.h>

#include "check.h"

#include "device_backend.h"

#include <stdlib.h>
#include <string.h>

/* ---- a fake device, so the contract is reachable without hardware ---- */

struct fake {
    uint32_t frequency_hz;
    uint32_t sample_rate_hz;
    int ppm;
    int manual_gain;
    int gain;
    int flushes;
    int stops;
    int blocks_sent;
    int fail_next_tune;
};

static struct fake fake_state;

static int fake_open(struct device_session *s, int index,
                     struct device_profile *out) {
    if (index != 0)
        return -1;
    memset(&fake_state, 0, sizeof(fake_state));
    fake_state.frequency_hz = 100000000;
    fake_state.sample_rate_hz = 2000000;
    *out = device_profile_rtlsdr("fake", NULL, 0);
    s->handle = &fake_state;
    return 0;
}

static void fake_close(struct device_session *s) { s->handle = NULL; }

static int fake_set_frequency(struct device_session *s, uint32_t hz) {
    struct fake *f = s->handle;
    if (f->fail_next_tune) {
        f->fail_next_tune = 0;
        return -1; /* and the frequency must not have moved */
    }
    f->frequency_hz = hz;
    return 0;
}
static int fake_frequency(struct device_session *s, uint32_t *out) {
    *out = ((struct fake *)s->handle)->frequency_hz;
    return 0;
}
static int fake_set_rate(struct device_session *s, uint32_t hz) {
    ((struct fake *)s->handle)->sample_rate_hz = hz;
    return 0;
}
static int fake_rate(struct device_session *s, uint32_t *out) {
    *out = ((struct fake *)s->handle)->sample_rate_hz;
    return 0;
}
static int fake_set_ppm(struct device_session *s, int ppm) {
    ((struct fake *)s->handle)->ppm = ppm;
    return 0;
}
static int fake_ppm(struct device_session *s, int *out) {
    *out = ((struct fake *)s->handle)->ppm;
    return 0;
}
static int fake_set_gain(struct device_session *s, int manual, int value) {
    struct fake *f = s->handle;
    f->manual_gain = manual;
    if (manual)
        f->gain = value;
    return 0;
}
static int fake_gain(struct device_session *s, int *out) {
    *out = ((struct fake *)s->handle)->gain;
    return 0;
}
static int fake_flush(struct device_session *s) {
    ((struct fake *)s->handle)->flushes++;
    return 0;
}
static int fake_stream(struct device_session *s, device_block_fn cb, void *ctx,
                       uint32_t block_bytes) {
    struct fake *f = s->handle;
    uint8_t *block = calloc(1, block_bytes);
    if (!block)
        return -1;
    cb(ctx, block, block_bytes);
    f->blocks_sent++;
    free(block);
    return 0;
}
static int fake_stop(struct device_session *s) {
    ((struct fake *)s->handle)->stops++;
    return 0;
}

static const struct device_backend fake_backend = {
    "fake",       fake_open,    fake_close,   fake_set_frequency,
    fake_frequency, fake_set_rate, fake_rate,  fake_set_ppm,
    fake_ppm,     fake_set_gain, fake_gain,   fake_flush,
    fake_stream,  fake_stop,
};

/* A backend that implements almost nothing, which is the case the wrappers
   exist to survive: a NULL entry must be a refusal, not a crash. */
static const struct device_backend sparse_backend = {
    "sparse", NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

static void test_the_contract_round_trips(void) {
    struct device_session s = { &fake_backend, NULL };
    struct device_profile p;
    uint32_t hz = 0, rate = 0;
    int ppm = 0, gain = 0;

    check_int("it opens", fake_backend.open(&s, 0, &p), 0);
    check_true("and is then open", device_session_open(&s));

    check_int("frequency sets", device_set_frequency_hz(&s, 948400000), 0);
    check_int("and reads back", device_frequency_hz(&s, &hz), 0);
    check_int("the value it was given", (long)hz, 948400000);

    check_int("rate sets", device_set_sample_rate_hz(&s, 1920000), 0);
    check_int("and reads back", device_sample_rate_hz(&s, &rate), 0);
    check_int("the value it was given", (long)rate, 1920000);

    check_int("ppm sets", device_set_ppm(&s, -31), 0);
    check_int("and reads back", device_ppm(&s, &ppm), 0);
    check_int("the value it was given", ppm, -31);

    check_int("gain sets", device_set_gain(&s, 1, 297), 0);
    check_int("and reads back", device_gain(&s, &gain), 0);
    check_int("the value it was given", gain, 297);

    check_int("flush works", device_flush(&s), 0);
    check_int("and was counted", fake_state.flushes, 1);
    check_int("stop works", device_stop(&s), 0);
    check_int("and was counted", fake_state.stops, 1);

    device_close(&s);
    check_true("closing leaves it closed", !device_session_open(&s));
}

/*
 * A refused retune must not move the tuning. This is the property the
 * receiver lease depends on -- `borrow and tune` cancels its token when the
 * retune fails, and that is only correct if the failure changed nothing.
 */
static void test_a_refused_tune_changes_nothing(void) {
    struct device_session s = { &fake_backend, NULL };
    struct device_profile p;
    uint32_t before = 0, after = 0;

    fake_backend.open(&s, 0, &p);
    device_set_frequency_hz(&s, 800000000);
    device_frequency_hz(&s, &before);

    fake_state.fail_next_tune = 1;
    check_int("the retune is refused", device_set_frequency_hz(&s, 900000000),
              -1);
    check_int("and reads back", device_frequency_hz(&s, &after), 0);
    check_int("the frequency it had", (long)after, (long)before);

    device_close(&s);
}

/* A closed session, and a backend with holes in it, refuse rather than
   crash. Ninety call sites depend on that being true in one place. */
static void test_a_closed_or_sparse_session_refuses(void) {
    struct device_session closed = { NULL, NULL };
    struct device_session sparse = { &sparse_backend, (void *)1 };
    uint32_t hz;
    int n;

    check_true("a closed session is not open", !device_session_open(&closed));
    check_int("and refuses to tune", device_set_frequency_hz(&closed, 1), -1);
    check_int("and to report", device_frequency_hz(&closed, &hz), -1);
    check_int("and to flush", device_flush(&closed), -1);
    check_int("and to stop", device_stop(&closed), -1);
    check_int("and to stream", device_stream(&closed, NULL, NULL, 4), -1);
    check_int("a NULL session too", device_set_ppm(NULL, 0), -1);

    check_true("a sparse backend counts as open",
               device_session_open(&sparse));
    check_int("but every missing entry refuses",
              device_set_frequency_hz(&sparse, 1), -1);
    check_int("including gain", device_gain(&sparse, &n), -1);
    check_int("including flush", device_flush(&sparse), -1);

    /* Closing a session that was never opened must be safe. */
    device_close(&closed);
    check_true("closing a closed session is safe",
               !device_session_open(&closed));
    device_close(NULL);
}

static int captured_blocks;
static uint32_t captured_bytes;
static void count_block(void *ctx, const uint8_t *data, uint32_t bytes) {
    (void)ctx;
    (void)data;
    captured_blocks++;
    captured_bytes = bytes;
}

/*
 * The capture backend, which is real and whose content is mostly refusals.
 * `testfiles/gsm_arfcn_69.bin` is 8-bit at 2 MS/s tuned 400 kHz below the
 * channel; none of that can be changed by asking.
 */
static void test_a_capture_refuses_what_a_recording_cannot_do(void) {
    struct device_session s = { NULL, NULL };
    struct device_profile p;
    uint32_t hz = 0, rate = 0;
    int ppm = 0, gain = 0;

    if (device_backend_capture_open(&s, "testfiles/gsm_arfcn_69.bin",
                                    948400000, 2000000, 0, 0, &p) < 0) {
        check_true("testfiles/gsm_arfcn_69.bin opens", 0);
        return;
    }
    check_true("it is open", device_session_open(&s));
    check_str("and says what it is", s.backend->name, "capture");
    check_int("8-bit, from the sidecar", (int)p.format, SAMPLE_FORMAT_U8);
    check_close("at 127.5", p.full_scale, 127.5, 1e-6);
    check_true("and it cannot retune", p.can_retune == 0);

    check_int("the frequency reads back", device_frequency_hz(&s, &hz), 0);
    check_int("as the tuning it was recorded at", (long)hz, 948400000);
    check_int("but setting it is refused",
              device_set_frequency_hz(&s, 900000000), -1);
    check_int("the rate reads back", device_sample_rate_hz(&s, &rate), 0);
    check_int("as 2 MS/s", (long)rate, 2000000);
    check_int("but setting it is refused",
              device_set_sample_rate_hz(&s, 1920000), -1);
    check_int("ppm reads back", device_ppm(&s, &ppm), 0);
    check_int("but correcting it is refused", device_set_ppm(&s, -31), -1);
    check_int("gain cannot be read", device_gain(&s, &gain), -1);
    check_int("nor set", device_set_gain(&s, 1, 297), -1);

    /* Flush returns 0 and that is truthful, not a stub: nothing is in flight
       on a file, so a caller keeps one retune-then-flush code path. */
    check_int("flush succeeds with nothing to do", device_flush(&s), 0);

    /* The length is whole pairs of this container. 8126464 is 31 blocks of
       262144, so it divides exactly. */
    check_true("its length is whole pairs",
               device_backend_capture_bytes(&s) % p.bytes_per_pair == 0);
    check_msg(device_backend_capture_bytes(&s) == 8126464,
              "capture bytes: got %llu, expected 8126464\n",
              (unsigned long long)device_backend_capture_bytes(&s));

    captured_blocks = 0;
    captured_bytes = 0;
    check_int("it streams", device_stream(&s, count_block, NULL, 262144), 0);
    check_int("31 blocks of 262144", captured_blocks, 31);
    check_int("the last one whole", (long)captured_bytes, 262144);

    device_close(&s);
    check_true("and closes", !device_session_open(&s));
}

/* The 16-bit corpus goes through the same backend and comes back as a
   different container, which is the whole point of the sidecar. */
static void test_a_capture_reads_its_own_container(void) {
    struct device_session s = { NULL, NULL };
    struct device_profile p;

    if (device_backend_capture_open(&s, "build/testfiles16/gsm_arfcn_69.bin",
                                    948400000, 2000000, 0, 0, &p) < 0) {
        check_true("build/testfiles16 present (make check-sample-format)", 0);
        return;
    }
    check_int("16-bit, from its sidecar", (int)p.format, SAMPLE_FORMAT_S16);
    check_close("full scale 2040", p.full_scale, 2040.0, 1e-6);
    check_size("four bytes a pair", p.bytes_per_pair, 4);
    check_msg(device_backend_capture_bytes(&s) == 16252928,
              "16-bit capture bytes: got %llu, expected 16252928\n",
              (unsigned long long)device_backend_capture_bytes(&s));

    /* Same signal, same pairs a block, twice the bytes -- ticket 09. */
    captured_blocks = 0;
    check_int("it streams", device_stream(&s, count_block, NULL, 524288), 0);
    check_int("31 blocks again, at twice the bytes", captured_blocks, 31);

    device_close(&s);
}

static void test_a_missing_capture_is_refused(void) {
    struct device_session s = { NULL, NULL };
    struct device_profile p;
    int refused;

    /* The refusal explains itself on stderr, which is right for a user and
       noise in a report, so it is muted for the call that provokes it. The
       return value is what is under test, not the wording. */
    {
        int saved = dup(STDERR_FILENO);
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        refused = device_backend_capture_open(&s, "no/such/capture.bin", 1, 1,
                                              0, 0, &p);
        fflush(stderr);
        if (saved >= 0) {
            dup2(saved, STDERR_FILENO);
            close(saved);
        }
    }
    check_int("no such file", refused, -1);
    check_true("and nothing was opened", !device_session_open(&s));
    check_int("a NULL path too",
              device_backend_capture_open(&s, NULL, 1, 1, 0, 0, &p), -1);

    /* There is no such thing as "capture number 2", so the index opener
       refuses rather than inventing one. */
    check_int("and opening a capture by index is refused",
              device_backend_capture()->open(&s, 0, &p), -1);
}

int main(void) {
    test_the_contract_round_trips();
    test_a_refused_tune_changes_nothing();
    test_a_closed_or_sparse_session_refuses();
    test_a_capture_refuses_what_a_recording_cannot_do();
    test_a_capture_reads_its_own_container();
    test_a_missing_capture_is_refused();
    return check_report("what a receiver has to satisfy");
}
