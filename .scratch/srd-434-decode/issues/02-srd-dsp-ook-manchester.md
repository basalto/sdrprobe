# `srd_dsp`: on-off keying and Manchester in the 433 ISM allocation

Status: done

## What

A technology DSP module for short-range devices at 433-435 MHz: find the
transmissions, find the carrier, demodulate the envelope to chips, recover the
chip period, and Manchester-decode to bits. Prefix `srd_`, for the reason in
`spec.md`.

`-lm` only, no GUI, no receiver -- the rule every `*_dsp.c` here keeps.

## Why it is worth building

The measurements are already done and they are unambiguous:
`docs/srd-remote-control-ook-capture-and-decode.md`. OOK, a 500 us chip, Manchester at
1000 bit/s, with FSK and PWM each explicitly refuted rather than merely not
chosen. The signal stands 47-59 dB over its local floor, so nothing about the
demodulation is marginal.

## Shape

Most of the module already exists as `scripts/ook_report.c`, which was written
against the shipping primitives deliberately. The work is largely promoting
its measurement stages into a module with an interface, plus the chip-period
recovery that the tool currently leaves to a human reading a histogram.

Reuse rather than reimplement:

- `signal_find_carrier()` for the carrier, once given the right window;
- `signal_envelope_stats()` for the envelope;
- `sdr_dsp_spectrum()` for the time-frequency scan.

New:

- `srd_find_transmissions()` -- the whole-capture scan and grouping. This is
  the part with no equivalent anywhere in the program, and it is what ticket
  01 is really about.
- `srd_chip_period()` -- from run lengths, not from a blind symbol-rate
  search. The blind search returned 730 Bd on this signal against a measured
  1000 bit/s, and `signal_probe.h` already documents why it cannot tell a
  symbol rate from a frame rate on a bursty signal.
- `srd_manchester_decode()` -- chips to bits, with both polarities tried,
  since nothing observed distinguishes them.

## The carrier must be searched per transmission, and this is measured

**Do not give the module a fixed carrier offset.** Measured over the seven
presses in the two fixtures:

| | spread |
| --- | --- |
| within `434a` (4 presses) | **46 Hz** |
| within `434b` (3 presses) | **944 Hz** |
| between the two capture means | **784 Hz** |

`434a` alone would have suggested a transmitter stable to 46 Hz and made a
fixed offset look reasonable. It is not: the remote control's own crystal moves nearly a
kilohertz between presses in one 5 s recording, which is unremarkable for the
parts these use and is fatal to any decoder that assumes otherwise. The search
window has to be at least a few kilohertz wide, and `signal_find_carrier()`
already takes one.

This is the ordinary shape of a fixture agreeing with the assumption built
against it, which is why the real-capture check remains alongside the
synthetic fixtures.

## Captures

`testfiles/srd_remote_control_ook_a.bin` is the external OOK fixture.
`check-pipelines` asserts that the survey finds the SRD remote control near
434.417 MHz and that the assembled decoder recovers full frames. The carrier
offset and chip period remain pinned in the protocol document and are
reproducible with `make probe-ook`. A missing fixture is reported as a skip.

What `srd_dsp`'s own unit layer gets is still **synthetic** fixtures — an OOK Manchester
burst laid into noise at a known carrier, chip period and bit pattern — which
is what every other module's unit layer uses anyway. `AGENTS.md`'s standing
warning applies and should be written into the check: a synthetic round trip
agrees with whatever assumption built it, so the real captures remain the only
thing that establishes the decode, and they establish it by being run by hand.

## Blocked by

Not blocked, but ticket 01 makes `srd_find_transmissions()` either a new
function or a fix to an existing one, which changes where this code lives.
Worth settling 01 first.

## What was built

- `src/srd_dsp.{c,h}`: technology DSP module for 433-435 MHz ISM / SRD devices,
  linking `-lm` only:
  - `srd_find_transmissions()`: scans full capture/buffers with FFT chunks
    (`SRD_SCAN_SIZE` 1024), detects busy chunks (>25 dB over median bin), groups
    bursts separated by <= 50 ms gap, and refines the carrier frequency using
    `signal_find_carrier()`.
  - `srd_demodulate_envelope()`: mixes to `carrier_hz` and boxcar-decimates to
    `SRD_WORK_RATE_HZ` (~200 kS/s) computing `sqrt(re^2 + im^2)`.
  - `srd_envelope_threshold()` and `srd_extract_runs()`: slices envelope to
    HIGH/LOW runs.
  - `srd_chip_period()`: recovers the fundamental chip period directly from the
    distribution of run lengths (shortest recurrent peak / mode).
  - `srd_runs_to_chips()`: converts runs to binary chips.
  - `srd_manchester_decode()` and `srd_manchester_decode_both()`: auto-detects
    chip alignment phase (0 vs 1) by minimising Manchester violations (00/11)
    and decodes bits according to G.E. Thomas or IEEE 802.3 polarities.
  - `srd_pack_bits()`: packs bits MSB-first into bytes.
- `tests/srd_dsp_test.c`: synthetic test suite testing all operations,
  plus mandatory real-capture verification against
  `testfiles/srd_remote_control_ook_a.bin`.
- Wired into `Makefile`: `check-srd-dsp` in `check-dsp`, `CHECK_UNITS`,
  `DSP_SRC`/`DSP_HDR`, and `.PHONY`.
- Verified with `make check`: 20,383 checks in 63 suites, 0 failures.
