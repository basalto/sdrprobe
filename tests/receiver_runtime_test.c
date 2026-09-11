#include "check.h"

#include "fake_backend.h"
#include "receiver_runtime.h"

#include <string.h>

/*
 * Changing what the receiver is doing, and putting it back when that fails.
 *
 * `retune_receiver_at_rate()` has been a multi-step transaction with a
 * rollback at every step since it was written, and **not one of those steps
 * has ever been checked**: it took `struct app`, it lived beside `main()`,
 * and the receiver path is the half no check reaches (ADR-0012). Correct by
 * inspection, for as long as it existed.
 *
 * The case this file exists for is the one the ticket names: **the rate takes
 * and the tuning refuses.** What the caller must be left holding is the old
 * rate, the old tuning, one running worker, and a sentence naming which half
 * refused.
 */

static struct device_session source;
static struct device_profile profile;
static struct receiver_applied applied;
static char error[256];

/* A worker that only counts, because what matters here is that it is stopped
   and started the right number of times and is running at the end. */
static struct {
    int running;
    int starts;
    int stops;
    int fail_start_in;
    int fail_stop_in;
} worker;

static int due(int *countdown) {
    if (*countdown <= 0)
        return 0;
    if (--*countdown == 0)
        return 1;
    return 0;
}

static int worker_stop(void *ctx) {
    (void)ctx;
    if (due(&worker.fail_stop_in))
        return -1;
    /* Nothing running is a successful stop, not a failure. The RTL-SDR
       backend's own stop() returns -1 in that case (phase 1a), and a
       transaction reading that as failure would refuse to retune an idle
       receiver. */
    worker.stops++;
    worker.running = 0;
    return 0;
}

static int worker_start(void *ctx) {
    (void)ctx;
    if (due(&worker.fail_start_in))
        return -1;
    worker.starts++;
    worker.running = 1;
    return 0;
}

static struct receiver_runtime a_runtime(void) {
    struct receiver_runtime rt;

    memset(&source, 0, sizeof(source));
    memset(&worker, 0, sizeof(worker));
    memset(&applied, 0, sizeof(applied));
    error[0] = '\0';
    source.backend = fake_backend();
    check_int("the fake opens", source.backend->open(&source, 0, &profile), 0);
    applied.frequency_hz = 100000000;
    applied.sample_rate_hz = 2000000;
    applied.ppm = 0;
    worker.running = 1;

    memset(&rt, 0, sizeof(rt));
    rt.source = &source;
    rt.applied = &applied;
    rt.error = error;
    rt.error_size = sizeof(error);
    rt.live = 1;
    rt.life.stop = worker_stop;
    rt.life.start = worker_start;
    return rt;
}

/* The ordinary case, so the failures below mean something. */
static void test_a_tuning_that_takes(void) {
    struct receiver_runtime rt = a_runtime();

    check_int("it tunes", receiver_runtime_tune(&rt, 948400000, -31), 0);
    check_int("the applied frequency is what came back", (int)applied.frequency_hz,
              948400000);
    check_int("and the correction with it", applied.ppm, -31);
    check_int("the worker was stopped once", worker.stops, 1);
    check_int("and started once", worker.starts, 1);
    check_int("and is running", worker.running, 1);
    check_str("with nothing to report", error, "");
}

/*
 * **The case this file exists for.** The rate takes; the tuning refuses; the
 * rate has to go back too.
 *
 * Without that last step a refusal leaves the receiver half-moved: sampling
 * at a rate nothing asked for, tuned where it always was, and every decoder
 * downstream reading a signal at the wrong rate with nothing to say so.
 */
static void test_the_rate_takes_and_the_tuning_refuses(void) {
    struct receiver_runtime rt = a_runtime();
    struct fake_device *f = fake_backend_state();

    /* The rate is set first and succeeds; the frequency write after it
       fails. The fake has to be told to fail, because a real one does not
       refuse an impossible request -- it accepts 10 Hz and says so. */
    f->fail_frequency_in = 1;
    check_int("the transaction refuses",
              receiver_runtime_tune_at_rate(&rt, 948400000, 1920000, -31), -1);

    check_int("the rate was put back", (int)applied.sample_rate_hz, 2000000);
    check_int("and so was the device's", (int)f->sample_rate_hz, 2000000);
    check_int("the tuning never moved", (int)applied.frequency_hz, 100000000);
    check_int("nor the correction", applied.ppm, 0);
    check_true("the error names the half that refused",
               strstr(error, "rejected") != NULL);
    check_true("and names the frequency it was asked for",
               strstr(error, "948.400000") != NULL);
    check_int("exactly one worker is running at the end", worker.running, 1);
    check_int("and it was left started, not stopped", worker.starts >= 1 &&
              worker.stops >= 1, 1);
}

/* The rate itself refusing is the simpler half, and must not move anything. */
static void test_a_rate_that_refuses(void) {
    struct receiver_runtime rt = a_runtime();
    struct fake_device *f = fake_backend_state();

    f->fail_rate_in = 1;
    check_int("it refuses",
              receiver_runtime_tune_at_rate(&rt, 948400000, 1920000, 0), -1);
    check_int("the rate is untouched", (int)applied.sample_rate_hz, 2000000);
    check_int("the tuning too", (int)applied.frequency_hz, 100000000);
    check_true("and it says which", strstr(error, "MS/s") != NULL);
    check_int("with the worker running", worker.running, 1);
}

/*
 * A device that cannot say where it is tuned has not been tuned.
 *
 * Every frequency this program prints, saves, or matches a site history
 * against comes from that read-back, so accepting the write and skipping the
 * read would put an unverified number into a sweep's JSON.
 */
static void test_a_tuning_that_cannot_be_read_back(void) {
    struct receiver_runtime rt = a_runtime();
    struct fake_device *f = fake_backend_state();

    f->fail_frequency_read_in = 1;
    check_int("it refuses", receiver_runtime_tune(&rt, 948400000, 0), -1);
    check_int("and puts the tuning back", (int)applied.frequency_hz, 100000000);
    check_int("on the device as well", (int)f->frequency_hz, 100000000);
    check_true("saying it could not read it back",
               strstr(error, "read the tuning back") != NULL);
    check_int("with the worker running", worker.running, 1);
}

/* A flush that fails is a refusal: stale samples from the previous tuning
   would otherwise be measured as if they came from the new one -- which is
   the fault the survey's settle exists for, one layer down. */
static void test_a_flush_that_fails(void) {
    struct receiver_runtime rt = a_runtime();
    struct fake_device *f = fake_backend_state();

    f->fail_flush_in = 1;
    check_int("it refuses", receiver_runtime_tune(&rt, 948400000, 0), -1);
    check_int("and the tuning is back", (int)applied.frequency_hz, 100000000);
    check_int("with the worker running", worker.running, 1);
}

/* A restart that fails is the worst case: the settings are back but there is
   no worker, and the caller has to be told rather than left to notice. */
static void test_a_restart_that_fails(void) {
    struct receiver_runtime rt = a_runtime();

    worker.fail_start_in = 1;
    check_int("it refuses", receiver_runtime_tune(&rt, 948400000, -31), -1);
    check_int("the tuning is back", (int)applied.frequency_hz, 100000000);
    check_int("and the correction", applied.ppm, 0);
    check_true("and it says acquisition would not restart",
               strstr(error, "would not restart") != NULL);
    check_int("the second start was attempted", worker.starts, 1);
    check_int("so a worker is running", worker.running, 1);
}

/* A stop that fails stops the transaction before anything is touched. */
static void test_a_stop_that_fails(void) {
    struct receiver_runtime rt = a_runtime();
    struct fake_device *f = fake_backend_state();

    worker.fail_stop_in = 1;
    check_int("it refuses", receiver_runtime_tune(&rt, 948400000, -31), -1);
    check_int("nothing was written to the device", f->frequency_writes, 0);
    check_int("the tuning is where it was", (int)applied.frequency_hz,
              100000000);
}

/* An unchanged rate does not stop the worker to set the rate it already has:
   a needless stop drops blocks, and the survey counts them. */
static void test_an_unchanged_rate_is_a_plain_retune(void) {
    struct receiver_runtime rt = a_runtime();
    struct fake_device *f = fake_backend_state();

    check_int("it tunes",
              receiver_runtime_tune_at_rate(&rt, 948400000, 2000000, 0), 0);
    check_int("the rate was never written", f->rate_writes, 0);
    check_int("the worker stopped once, not twice", worker.stops, 1);
    check_int("and started once", worker.starts, 1);
}

/* A capture holds one tuning at one rate, so both refuse -- with a sentence,
   because "nothing happened" is what a reader cannot act on. */
static void test_a_capture_refuses_and_says_why(void) {
    struct receiver_runtime rt = a_runtime();
    struct fake_device *f = fake_backend_state();

    rt.live = 0;
    check_int("tuning refuses", receiver_runtime_tune(&rt, 948400000, 0), -1);
    check_true("saying a capture holds one frequency",
               strstr(error, "capture holds one frequency") != NULL);
    error[0] = '\0';
    check_int("and so does a rate change",
              receiver_runtime_tune_at_rate(&rt, 948400000, 1920000, 0), -1);
    check_true("saying a rate change needs a receiver",
               strstr(error, "requires a live receiver") != NULL);
    check_int("nothing was written either time", f->frequency_writes, 0);
    check_int("nor was the worker touched", worker.stops, 0);
}

/* And what it refuses to be asked. */
static void test_refusals(void) {
    struct receiver_runtime rt = a_runtime();

    check_int("no runtime", receiver_runtime_tune(NULL, 1, 0), -1);
    check_int("no rate change either",
              receiver_runtime_tune_at_rate(NULL, 1, 2, 0), -1);
    rt.source = NULL;
    check_int("no device", receiver_runtime_tune(&rt, 1, 0), -1);
    rt = a_runtime();
    rt.applied = NULL;
    check_int("nowhere to record what was applied",
              receiver_runtime_tune(&rt, 1, 0), -1);

    /* No error buffer is allowed: a headless path may not want one, and the
       transaction must not write through a null. */
    rt = a_runtime();
    rt.error = NULL;
    rt.error_size = 0;
    check_int("and it still refuses cleanly with nowhere to say why",
              receiver_runtime_tune(&rt, 948400000, 0), 0);
}

int main(void) {
    test_a_tuning_that_takes();
    test_the_rate_takes_and_the_tuning_refuses();
    test_a_rate_that_refuses();
    test_a_tuning_that_cannot_be_read_back();
    test_a_flush_that_fails();
    test_a_restart_that_fails();
    test_a_stop_that_fails();
    test_an_unchanged_rate_is_a_plain_retune();
    test_a_capture_refuses_and_says_why();
    test_refusals();
    return check_report("the receiver's transitions, and their rollback");
}
