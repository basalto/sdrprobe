#define _GNU_SOURCE

#include "acquisition.h"
#include "check.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * The handoff between the acquisition thread and the one that consumes blocks.
 *
 * This decides whether the program sees the signal at all, and it decides it
 * silently: a dropped block leaves no mark except a counter, and the counter
 * is the only thing telling an operator whether to believe what they are
 * looking at. It also has two modes now -- the overwriteable slot of ADR-0002,
 * and the lossless handoff scripted playback needs -- and the lossless one can
 * fail by hanging, which is the worst failure a check can have to catch.
 *
 * No receiver: publish_block does not know where its bytes came from. The file
 * worker is driven against a real capture, which is as close to the live path
 * as anything can get without hardware (ADR-0012, layer 1).
 */

static struct acquisition acq;
static unsigned char block[SAMPLE_BLOCK_BYTES];

static void fill(unsigned char value) {
    memset(block, value, sizeof(block));
}

static void setup(void) {
    memset(&acq, 0, sizeof(acq));
    check_int("acquisition_init succeeds", acquisition_init(&acq), 0);
}

static void teardown(void) {
    check_int("acquisition_destroy succeeds", acquisition_destroy(&acq), 0);
}

/* Take a block if there is one, and hand back the counters that came with
   it. */
static int take(struct slot_snapshot *snapshot) {
    return consume_latest(&acq, snapshot);
}

/*
 * The overwriteable slot: a consumer that falls behind loses blocks rather
 * than lagging, and always gets the newest one (ADR-0002).
 */
static void test_the_slot_overwrites(void) {
    struct slot_snapshot snap;

    setup();
    fill(0x11);
    publish_block(&acq, block, sizeof(block));
    fill(0x22);
    publish_block(&acq, block, sizeof(block));
    fill(0x33);
    publish_block(&acq, block, sizeof(block));

    check_int("three published, one waiting", take(&snap), 1);
    check_int("published counted", (long)snap.published_blocks, 3);
    check_int("processed counted", (long)snap.processed_blocks, 1);
    check_int("two were overwritten", (long)snap.overwritten_blocks, 2);
    /* Freshness over continuity: what survived is the newest block, not the
       oldest. A consumer that got 0x11 here would be showing stale data while
       claiming to be live. */
    check_int("and the survivor is the newest", acq.raw[0], 0x33);
    check_int("nothing left to take", take(&snap), 0);
    check_int("taking nothing does not count as processing",
              (long)snap.processed_blocks, 1);
    teardown();
}

/*
 * A block is consumed once. The generation is what enforces it, and without it
 * a slow producer would have the same block processed on every frame -- which
 * for a decoder means the same messages reported over and over.
 */
static void test_a_block_is_taken_once(void) {
    struct slot_snapshot snap;

    setup();
    fill(0x44);
    publish_block(&acq, block, sizeof(block));
    check_int("the first take gets it", take(&snap), 1);
    check_int("the second does not", take(&snap), 0);
    check_int("nor the third", take(&snap), 0);
    check_int("processed once", (long)snap.processed_blocks, 1);

    fill(0x55);
    publish_block(&acq, block, sizeof(block));
    check_int("a new block is a new generation", take(&snap), 1);
    check_int("with the new contents", acq.raw[0], 0x55);
    teardown();
}

/* Blocks that are not a whole number of I/Q pairs, or not a block at all. */
static void test_malformed_blocks(void) {
    struct slot_snapshot snap;

    setup();
    publish_block(&acq, block, 0);
    check_int("an empty block publishes nothing", take(&snap), 0);
    check_int("and is counted malformed", (long)snap.malformed_blocks, 1);

    publish_block(&acq, block, SAMPLE_BLOCK_BYTES + 2);
    check_int("an oversized block publishes nothing", take(&snap), 0);
    check_int("and is counted too", (long)snap.malformed_blocks, 2);

    /* An odd length is half an I/Q pair. The pair is dropped and the rest is
       kept: a truncated block is still signal, and refusing it entirely would
       throw away 65 ms because of one byte. */
    fill(0x66);
    publish_block(&acq, block, 1001);
    check_int("an odd length still publishes", take(&snap), 1);
    check_int("truncated to whole pairs", (long)acq.raw_len, 1000);
    check_int("and counted malformed", (long)snap.malformed_blocks, 3);

    /* A single byte is a pair once truncated -- of nothing. It must not
       publish an empty block. */
    publish_block(&acq, block, 1);
    check_int("one byte publishes nothing", take(&snap), 0);
    teardown();
}

/* Publishing after a stop is a no-op: the consumer is gone. */
static void test_stop_closes_the_slot(void) {
    struct slot_snapshot snap;

    setup();
    request_worker_stop(&acq);
    check_int("the stop is visible to a worker", worker_stop_requested(&acq),
              1);
    fill(0x77);
    publish_block(&acq, block, sizeof(block));
    check_int("nothing was published", take(&snap), 0);
    check_int("and nothing counted", (long)snap.published_blocks, 0);
    teardown();
}

/* ---- the lossless handoff ------------------------------------------- */

struct publisher {
    int count;
    int published;
    unsigned char first_value;
};

static void *publish_many(void *arg) {
    struct publisher *p = arg;
    unsigned char local[SAMPLE_BLOCK_BYTES];

    for (int i = 0; i < p->count; i++) {
        memset(local, (unsigned char)(p->first_value + i), sizeof(local));
        publish_block(&acq, local, sizeof(local));
        p->published++;
    }
    return NULL;
}

/*
 * With the lossless handoff the publisher waits instead of overwriting, so a
 * consumer sees every block in order however slowly it gets round to them.
 * This is what makes a scripted decode repeatable; without it the same capture
 * decoded 6 Mode S frames on an idle machine and 1 on a busy one.
 */
static void test_lossless_delivers_every_block(void) {
    struct publisher p = { 16, 0, 1 };
    struct slot_snapshot snap;
    pthread_t thread;
    int seen = 0;
    int out_of_order = 0;
    int guard = 0;

    setup();
    acquisition_set_lossless(&acq, 1);
    check_int("the publisher starts", pthread_create(&thread, NULL,
                                                     publish_many, &p),
              0);
    while (seen < p.count && guard++ < 100000) {
        if (!take(&snap)) {
            struct timespec pause = { 0, 200000L };
            nanosleep(&pause, NULL);
            continue;
        }
        if (acq.raw[0] != (unsigned char)(1 + seen))
            out_of_order++;
        seen++;
    }
    pthread_join(thread, NULL);
    take(&snap);

    check_int("every block published", p.published, p.count);
    check_int("every block consumed", seen, p.count);
    check_int("in the order they were published", out_of_order, 0);
    /* The whole point: nothing was dropped. */
    check_int("nothing overwritten", (long)snap.overwritten_blocks, 0);
    check_int("published equals processed",
              (long)snap.published_blocks, (long)snap.processed_blocks);
    teardown();
}

static void *publish_one(void *arg) {
    unsigned char local[SAMPLE_BLOCK_BYTES];

    (void)arg;
    memset(local, 0x99, sizeof(local));
    publish_block(&acq, local, sizeof(local));
    return NULL;
}

/*
 * A lossless publisher waiting for a consumer that never comes back must be
 * woken by the stop, or the join in stop_acquisition() never returns and the
 * program hangs on exit with no message.
 *
 * This check has to bound its own wait: a failure here is a hang, and a hung
 * check is worse than a failing one.
 */
static void test_stop_wakes_a_waiting_publisher(void) {
    struct slot_snapshot snap;
    pthread_t thread;
    struct timespec deadline;

    setup();
    acquisition_set_lossless(&acq, 1);
    /* Fill the slot so the publisher below has to wait. */
    fill(0x88);
    publish_block(&acq, block, sizeof(block));

    check_int("the publisher starts",
              pthread_create(&thread, NULL, publish_one, NULL), 0);
    /* Give it time to reach the wait rather than racing past it. */
    struct timespec settle = { 0, 50000000L };
    nanosleep(&settle, NULL);
    check_int("it is indeed waiting -- the slot is still full",
              (long)acq.latest.published_blocks, 1);

    request_worker_stop(&acq);
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 5;
    if (pthread_timedjoin_np(thread, NULL, &deadline) != 0) {
        check_msg(0, "the publisher did not wake: request_worker_stop must "
                     "broadcast, or shutdown hangs on the join\n");
        /* It is still blocked on the condition; joining it later is not
           possible and the process must not hang. */
        check_report("acquisition slot");
        fflush(stdout);
        fflush(stderr);
        _exit(1);
    }
    take(&snap);
    teardown();
}

/* ---- the file worker ------------------------------------------------- */

/*
 * The paced file worker, driven against a real capture with the lossless
 * handoff on: every byte of the file must arrive exactly once, in order, and
 * the worker must report a clean end rather than an error.
 *
 * This is the path `--file ... --headless --decode --once` takes, and the
 * reason a capture decodes the same messages every run.
 */
static void test_file_worker_reads_a_capture_whole(void) {
    const char *path = "testfiles/adsb_cpr_pair.bin";
    FILE *capture = fopen(path, "rb");
    struct slot_snapshot snap;
    pthread_t thread;
    uint64_t bytes = 0;
    long size;
    int guard = 0;

    if (!capture) {
        check_msg(0, "cannot open %s\n", path);
        return;
    }
    fseek(capture, 0, SEEK_END);
    size = ftell(capture);
    fseek(capture, 0, SEEK_SET);

    setup();
    acq.capture_bytes = (uint64_t)size;
    acquisition_set_lossless(&acq, 1);
    acquisition_attach_source(&acq, NULL, capture, 2000000U, path, 0);
    check_int("the file worker starts",
              pthread_create(&thread, NULL, file_worker, &acq), 0);

    memset(&snap, 0, sizeof(snap));
    while (!snap.worker_done && guard++ < 1000000) {
        if (take(&snap))
            bytes += acq.raw_len;
        else {
            struct timespec pause = { 0, 200000L };
            nanosleep(&pause, NULL);
        }
    }
    pthread_join(thread, NULL);
    while (take(&snap))
        bytes += acq.raw_len;

    check_int("the worker finished", snap.worker_done, 1);
    check_msg(!snap.worker_failed, "the worker failed: %s\n",
              snap.worker_error);
    /* Every byte, once. A short count means blocks were dropped; a long one
       means the capture was replayed. */
    check_int("every byte of the capture arrived", (long)bytes, size);
    check_int("nothing was overwritten", (long)snap.overwritten_blocks, 0);
    check_int("and nothing was malformed", (long)snap.malformed_blocks, 0);
    fclose(capture);
    teardown();
}

int main(void) {
    test_the_slot_overwrites();
    test_a_block_is_taken_once();
    test_malformed_blocks();
    test_stop_closes_the_slot();
    test_lossless_delivers_every_block();
    test_stop_wakes_a_waiting_publisher();
    test_file_worker_reads_a_capture_whole();

    return check_report("acquisition slot");
}
