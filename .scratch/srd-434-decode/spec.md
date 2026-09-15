# Short-range devices at 434 MHz: observe, characterise, decode

## What this is

A technology module for the 433 ISM / SRD allocation, reading on-off-keyed
Manchester traffic. The measurements that justify it are in
`docs/srd-remote-control-ook-capture-and-decode.md`; this is the work they point to.

## Why `srd_` and not an appliance-specific prefix

The prefix is the first decision and it is not cosmetic. The chain that reads
an SRD remote control at 434.417 MHz -- envelope, chip period, Manchester, frames --
is the same chain that reads tyre-pressure sensors, weather stations,
doorbells and garage remotes, all in the same allocation. `band_plan.c`
already calls 430-440 MHz "70 cm amateur / 433 ISM" and describes it as
overlapping uses. A prefix naming one appliance would be wrong within a week,
and renaming a prefix after it has reached a view, a layout header and a check
suite is expensive.

`srd_` is Short Range Device, which is what the allocation is for.

## Scope

**In.** Locating transmissions in a capture or a live stream; carrier and
channel; OOK demodulation to chips; chip-period recovery; Manchester decode to
bits; frame segmentation; presenting all of that.

## What is already established

From the OOK protocol capture:

| Property | Value |
| --- | --- |
| Carrier | 434.417 MHz +/- 1 kHz |
| Modulation | OOK / ASK (FSK refuted) |
| Chip | 500 us, 2000 chip/s |
| Line code | Manchester, 1000 bit/s (PWM refuted) |
| Frame | preamble, data, six 750 us alternations, data; repeated |
| Press | 849 - 1103 ms |

`make probe-ook` reproduces every one of them.

## What is not

The payload semantics remain unknown. Modulation and framing are established;
the decoder does not infer what the opaque payload means.

## Shape of the module, when it is built

Following `view_lte.c` / `lte_layout.h` as the template, and the order in
`AGENTS.md`'s "Adding a view":

- `src/srd_dsp.{c,h}` -- envelope, chip recovery, Manchester, frames. Links
  `-lm` only; never the GUI.
- `src/srd_frame.{c,h}` -- the Decoder-context side, if and when frame layout
  is understood well enough to have one.
- `src/srd_layout.h`, `src/view_srd.c`, `struct srd_view` in `app.h`, a member
  of `enum decode_kind`.
- `tests/srd_dsp_test.c` behind `make check-srd-dsp`, in `CHECK_UNITS`, over
  **synthetic** fixtures.
- Two external captures: one OOK signal and one 2-FSK signal. Unit and
  pipeline checks report their absence as visible skips.

## Tickets

1. `01-signal-probe-is-time-blind.md` -- the standing diagnostic reports a
   confident "no carrier" on a capture holding four transmissions.
2. `02-srd-dsp-ook-manchester.md` -- the DSP module.
3. `03-frame-structure.md` -- bits, boundaries, and the 750 us delimiter.
4. `04-a-second-transmitter.md` -- the measurement that says whether any of
   this generalises.
5. `05-two-reporters-two-loaders.md` -- both `scripts/*_report.c` hand-roll the
   byte-to-float conversion the rest of `scripts/` takes from the one seam.
6. `06-an-envelope-guard-in-the-wrong-units.md` -- envelope guard threshold fix.
7. `07-generic-manchester-and-fsk-decoder.md` -- generic Manchester & 2-FSK frame decoder.
