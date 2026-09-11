#ifndef TESTS_FAKE_BACKEND_H
#define TESTS_FAKE_BACKEND_H

#include "device_backend.h"

/*
 * A device that does what it is told to do, including failing.
 *
 * `check-device-backend` had one of these as a `static` inside its own
 * translation unit, which was right while it was the only check that needed
 * a receiver it did not have. A second one -- `check-receiver-runtime` --
 * would have meant two definitions of what a device does, drifting apart, and
 * that is the fixture-copy fault `probe-two-cell` cost a session to learn.
 *
 * So it is shared, and it records what it was asked as well as answering:
 * a transaction that rolls back is only correct if it put things *back*, and
 * the only way to see that is to have counted.
 */

struct fake_device {
    uint32_t frequency_hz;
    uint32_t sample_rate_hz;
    int ppm;
    int manual_gain;
    int gain;
    /* What it was asked to do, so a rollback can be seen rather than assumed. */
    int flushes;
    int stops;
    int blocks_sent;
    int frequency_writes;
    int rate_writes;
    /*
     * And what to refuse. Each is a countdown: 1 fails the next call, 2 fails
     * the one after. Zero never fails.
     *
     * Asking for something impossible is **not** a way to make a real device
     * fail -- phase 1a of `.scratch/deepening/issues/10-*` measured an
     * RTL-SDR accepting 10 Hz and a sample rate inside librtlsdr's own hole,
     * returning success and reading the value back. So a fake has to be told.
     */
    int fail_frequency_in;
    int fail_rate_in;
    int fail_flush_in;
    int fail_frequency_read_in;   /* reads back 0: "cannot say where I am" */
};

/* The backend, and the state it writes. One device at a time, which is what
   every caller here wants. */
const struct device_backend *fake_backend(void);
struct fake_device *fake_backend_state(void);

#endif
