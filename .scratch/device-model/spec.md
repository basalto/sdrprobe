# One data contract for the device, so a second one is not a rewrite

An RTL-SDR is the only receiver this program has ever seen, and a B210-class
device (AD9361, 12-bit, 70 MHz - 6 GHz, up to 61.44 MS/s, two coherent
channels) is being bought. This is what has to exist before that device can be
plugged in without either rewriting the program or, far worse, leaving it
running and quietly reporting numbers that are no longer true.

## The finding that sets the shape

The coupling to librtlsdr is already narrow. Six files, nineteen distinct API
calls:

| file | `rtlsdr_` calls |
| --- | --- |
| `sdrprobe.c` | 48 |
| `overlay_settings.c` | 18, all gain |
| `acquisition.{c,h}` | 4, plus `#include <rtl-sdr.h>` in the header |
| `app.h`, `view.h` | 2, the `rtlsdr_dev_t *` handle |

And the sample format has exactly one seam already: `sdr_dsp_convert_iq()`.
Precisely one DSP entry point downstream still takes raw bytes
(`fm_discriminate`); everything else takes floats. **Swapping the driver is
the easy half and it is nearly done by accident.**

The measurement that matters is the other one. An early reading of this
question claimed every threshold in the program is calibrated against 8-bit
full scale. That is false, and the correction is the reason this spec is
shaped the way it is: almost every threshold here is **relative** --
`SURVEY_CONFIRM_PROMINENCE_DB`, `SDR_DSP_FLOOR_MARGIN_DB`,
`SIGNAL_BURST_OVER_FLOOR_DB`, `SIGNAL_BURST_FLOOR_PCT`, Rayleigh's 0.5227,
every percentile and every fraction. A dB over a local floor is a dB over a
local floor on any device. Those transfer untouched.

What does **not** transfer is a much shorter and much more specific list, and
it is worth having found it, because it is the difference between a rewrite
and five tickets.

## What actually does not transfer

| fact | where it is compiled in | why it moves |
| --- | --- | --- |
| full scale = 127.5 | `sdr_dsp.c:82` convert, `:229` clipping, `:262` headroom, `:707` FFT scale | 12-bit in a 16-bit container is not 8-bit |
| display ceiling | `SPECTRUM_TOP_DBFS 6.0f` (`acquisition.h:69`) | anchored to the same full scale |
| two bytes per pair | `SAMPLE_BLOCK_PAIRS (SAMPLE_BLOCK_BYTES / 2)`, `sdr_dsp.c:78` | `sc16` is four |
| 65.5 ms of signal per block | every timing budget in `CLAUDE.md` | follows from the line above |
| 24 - 1766 MHz | `SURVEY_TUNER_LOWER_HZ`, `SURVEY_TUNER_UPPER_HZ` | and `check-survey-bands` asserts reach in **both** directions, so it fails by design |
| a discrete gain list in tenths of a dB | `overlay_settings.c`, `app.h:950-951` | an AD9361 has a continuous range in dB |
| a 28.8 MHz crystal | the 14.4 MHz comb, `docs/receiver-artifacts.md`, `RECEIVER_COMB_MAX_FRACTION` | a different device has different spurs, or none there |
| ppm drifts and is per-site | `config_site_ppm()`, the whole calibration overlay | a TCXO barely drifts; a GPSDO does not drift at all |

The block-time row is the dangerous one. Nothing errors. `SAMPLE_BLOCK_BYTES`
stays 262144, the block silently holds 65536 pairs instead of 131072, and
every budget in `CLAUDE.md` -- the GSM SCH decode at 22 ms of 65.5, the LTE
cell search at 14 of 68.3 -- is wrong by a factor of two with nothing on
screen to say so. ADR-0002 has a slow renderer *drop* blocks, so the symptom
is lost decodes on a faster device.

## Why this is not speculative generality

The repository's own deletion test says an adapter with one implementation is
a pass-through. This one has three the day the hardware arrives, and the
argument is arithmetic rather than taste:

`testfiles/` is 8-bit and must keep decoding forever. `CLAUDE.md` pins BSIC
59, 56 and 38; PCI 32 under the normal cyclic prefix in every block;
identification 0x8343 and the name `TSF`; six ADS-B frames with one global CPR
position. A B210 is 12-bit. So **one binary has to hold two sample formats at
once** -- the live device and the regression corpus -- from the first day the
device is plugged in. `file_worker` is already a second source; it simply
happens to share a format with the first.

## What it unlocks, which is the reason to want it

Four tickets are `wontfix` purely on limits this device does not have:

- `.scratch/nr-cell-search/issues/01` concludes verbatim: **"It is NR, it runs
  at 30 kHz, and it does not fit. 02 and 03 are closed."** The arithmetic is
  127 x 30 kHz = 3.81 MHz, "and no rate this dongle has would". A B210 covers
  3.81 MHz without noticing. 774.2 MHz becomes decodable and `nr_dsp` becomes
  worth writing.
- `.scratch/lte-sib1/issues/03` and `04` are closed because the cell is 50
  resource blocks and 1.92 MS/s sees six. At 20 MS/s it sees fifty, which
  finally gives `lte_turbo.c` and `lte_transport.c` -- today "experimental
  groundwork with no consumer" -- their consumer.

Beyond that: sample timestamps, which librtlsdr cannot provide at all; a
calibrated front-end gain, which is not dBm while the antenna is unknown but
is closer than dBFS; and two coherent channels.

## The contract

A `struct device_profile` that carries the **measurement** facts, not only the
verbs. This is the part no general-purpose layer will write, and it is why
SoapySDR is proposed below as a backend rather than as the contract: Soapy
models formats, gain stages, rate ranges, antennas and clock sources well, and
deliberately normalises away exactly the facts above. It will never say that
the comb is at 14.4 MHz, that full scale is 127.5, or that this device's noise
floor sits 24 dB below the last one's.

The profile holds, at minimum:

- sample format and **full scale**, the number dBFS is relative to;
- bytes per pair, and therefore pairs per block and seconds per block;
- tuning range, rate range, and whether tuning is possible at all
  (file playback says no, and already refuses);
- the gain model: a discrete list, or a continuous range, with its unit;
- frequency correction: whether it exists, its unit, and whether it drifts;
- the reference clock, because artifact detection is derived from it;
- retune settling time, today `SURVEY_SETTLE_SECONDS` for every device.

## Decisions

- **Model it here; put SoapySDR behind it if breadth is wanted later.** ADR-0003
  bans DSP libraries, not device I/O, so Soapy does not contradict it. But the
  profile above is ours either way, and a Soapy backend fills it in rather than
  replacing it.
- **The profile is data, not a vtable.** Behaviour that genuinely differs
  (start streaming, retune, set gain) gets function pointers *later*, when
  there is a second backend to satisfy them. The data contract can and should
  land first, because it is what the checks need.
- **A capture carries its own profile.** The `.json` sidecar already records
  the format in prose (`acquisition.c:35`). It becomes a field that is read.
- **No answer in `testfiles/` may move.** `check-pipelines` is the gate on
  every ticket here, exactly as it is for `.scratch/deepening/`.
- **The exact device is not yet chosen** and this spec does not depend on it.
  A real Ettus B210 means UHD; a LimeSDR means LimeSuite; an ADALM-Pluto is
  USB 2.0 and caps near 20 MHz, which reopens SIB1 but not comfortably. Ticket
  07 is `needs-info` until that is settled; 01 to 06 are not blocked by it.

## Order

1. **Prove a format change moves no answer** (`01`) -- needs no hardware, and
   is the gate every later ticket is measured against.
   **Done, 2026-09-08**: `make rescale-capture` and `check-sample-format`, 64
   checks. The hypothesis holds -- and two of the ticket's own claims did not.
   Full scale of the rescaled corpus is **2040.0**, not the 2047.5 named in
   this spec and in ticket 02, which agrees with none of the 256 byte values
   and misses by 366 times the tolerance the ticket set; the check asserts
   **exact** equality instead, since nothing in the scaling rounds. And the
   expensive half -- the built program over both corpora -- moves to 03,
   because the program cannot read a 16-bit file until 02 and 03 exist. It is
   not needed: there is exactly one byte-to-float seam in the program, so
   identical floats mean identical answers by construction rather than by
   measurement. **Ticket 02 must use 2040.0 for a rescaled capture and 2047.5
   only for a real 12-bit part**; they are different numbers about different
   things.
2. **The profile itself** (`02`) -- pure data, pure check.
3. **Full scale out of the DSP** (`03`).
4. **Block arithmetic in samples** (`04`) -- the silent one.
5. **Tuner reach from the profile** (`05`).
6. **The gain model** (`06`).
7. **A second backend** (`07`) -- blocked on the device choice.
8. **Artifacts and calibration are per-device** (`08`) -- absorbs
   `.scratch/calibrating-the-flags/`.

## What this is not

- Not a change to any DSP answer. Every capture decodes identically throughout.
- Not a runtime plugin system. Everything stays statically linked.
- Not two channels. The contract must not preclude MIMO; nothing here builds it.
- Not a transmitter. A B210 has a TX chain; this program is a probe.
- Not a rewrite of the acquisition slot. ADR-0002 stands.
