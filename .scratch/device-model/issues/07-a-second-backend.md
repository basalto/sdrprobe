# 07 - A second backend behind the acquisition worker

Status: needs-info
Blocked by: 02, 03, 04

The driver, at last. Everything above is device-shaped arithmetic; this is the
device.

## What is waiting on information

**Which device.** The spec does not depend on it and this ticket does:

- a real Ettus B210 means UHD, whose C API (`uhd.h`) is usable from C but
  whose library is C++;
- a LimeSDR means LimeSuite; a bladeRF means libbladeRF;
- an ADALM-Pluto is USB 2.0 and caps near 20 MHz, which reopens SIB1 but not
  comfortably, and would not reopen NR at 30 kHz spacing with margin;
- SoapySDR covers all of them behind one C API and is the hedge, at the cost
  of a layer that normalises away the facts ticket 02 exists to keep.

## Shape, whichever it is

`acquisition.h` currently `#include`s `<rtl-sdr.h>`, so the seam is in the
header rather than behind it. A worker gains a third variant beside
`receiver_worker` and `file_worker`. The pull model matters: librtlsdr pushes
fixed buffers through `rtlsdr_read_async`; UHD's `recv()` is a pull with
metadata, including **sample timestamps and overflow flags**, neither of which
librtlsdr can provide. The slot stays a single overwriteable slot (ADR-0002);
an overflow flag is a thing to *report*, not a reason to queue.

An ADR is owed here, because "device I/O is not a DSP library" is a reading of
ADR-0003 that should be written down rather than assumed.

## Not in scope

- Two channels. The contract must not preclude MIMO; this does not build it.
- Transmitting.
