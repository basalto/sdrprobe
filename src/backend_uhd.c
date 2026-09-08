/*
 * UHD, when there is a UHD to talk to.
 *
 * `.scratch/device-model/issues/07-a-second-backend.md`. This file is always
 * compiled; what it contains depends on `HAVE_UHD`, which the Makefile
 * defaults to 0 until the adapter below is written against real hardware.
 *
 * **The adapter is deliberately not written yet, and that is a decision
 * rather than an omission.** The board is on order. Writing C++ against an
 * API that cannot be compiled here would put untested code in the tree and
 * call it progress, which is the failure this whole spec has spent its time
 * correcting -- an antenna-gain explanation that was wrong, a full scale that
 * agreed with nothing, a block size that was right only by luck. What is here
 * instead is the seam, proven by two other implementations, and a refusal
 * that says so out loud.
 *
 * `docs/absolute-power-reference.md` and ticket 07 carry what the adapter has
 * to do. The short version, so it is beside the code that needs it:
 *
 *   - `open` takes `type=b200` plus, very likely, `fpga=<vendor image>`: at
 *     `b200_impl.cpp:423` an EEPROM product ID UHD does not recognise makes
 *     the constructor rethrow *unless* `fpga` was given. A clone may need it
 *     to enumerate at all.
 *   - the profile it fills in is **not** the RTL's: `SAMPLE_FORMAT_S16` with
 *     a full scale that has to come from somewhere real, 70 MHz to 6 GHz,
 *     `GAIN_MODEL_RANGE` -- but the unit is an index, not dB, because
 *     `ad9361_device.cpp` clips the value to 0..76 and pokes it into a
 *     gain-table register whose meaning changes at 1300 and 4000 MHz.
 *   - `flush` has no analogue and returns 0, the same truthful no-op the
 *     capture backend gives.
 *   - `ppm_drifts` is **unverified** for this board: the GPSDO slot is gone
 *     and no TCXO figure is published, so measure it rather than assume it.
 */

#include "device_backend.h"

#ifdef HAVE_UHD

#error "backend_uhd.c: the UHD adapter is not written yet. Build with " \
       "HAVE_UHD=0, or write it -- see .scratch/device-model/issues/07-*."

#else

/*
 * Without UHD there is no backend, and saying so with NULL is what lets a
 * caller ask instead of testing a macro. A binary built without UHD can then
 * refuse a UHD device with a sentence rather than failing to link, and the
 * refusal lives in one place.
 */
const struct device_backend *device_backend_uhd(void) {
    return NULL;
}

#endif /* HAVE_UHD */
