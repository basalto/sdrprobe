# 06 - A gain model that is not a list of tenths of a dB

Status: **panel done 2026-09-08**; the dBm half moved to ticket 08 and is
design-only until there is an instrument. Triaged first, and that mattered:
the second question rested on a wrong premise. Both open questions are answered
below; the second one turned out to rest on a wrong premise, and correcting it
changes what the ticket should build.
Blocked by: 02 for the panel; the dBm half is blocked by 08.

`rtlsdr_get_tuner_gains()` returns a device-specific discrete list in tenths
of a dB, and `overlay_settings.c` -- 18 of the program's 19 remaining driver
calls outside `sdrprobe.c` -- is built around exactly that shape, as are
`app.h:950-951`'s `supported_gains` and `supported_gain_count`.

An AD9361 has a continuous range in dB, typically 0 to about 76 with a 1 dB
step, and it is a *calibrated* gain in a way the RTL's is not.

## Open questions for triage

- Does the Settings panel offer a list, a slider, or both depending on the
  model? A slider over 29 discrete R820T steps is worse than the list.
- A calibrated front-end gain is the missing term between dBFS and dBm.
  `CLAUDE.md` says repeatedly that RSRP is dBFS "since nothing here knows the
  antenna's gain" -- with this device the *receiver's* half is known and the
  antenna's is not. Is a half-known dBm worth reporting, or is that exactly
  the kind of verdict `signal_findings.h` refuses to make?

The second question is the interesting one and it should be answered before
the panel is drawn, not after.

## Not in scope

- AGC. The program sets a fixed gain deliberately, because a level that moves
  under the measurement makes every survey incomparable.


## Triage

### Q1: list, slider, or both? -- **Both, chosen by the model, and the enum already says which.**

`device_profile.gain_model` is `GAIN_MODEL_LIST` or `GAIN_MODEL_RANGE` and the
panel renders per model. There is nothing to decide beyond that, because the
ticket's own objection settles it: a slider over 29 discrete R820T steps is
worse than the list -- it invites values the tuner will silently round, and
`overlay_settings.c` already has to compare a requested gain against a
reported one for exactly that reason. A continuous 0-76 dB range in 1 dB steps
is the opposite case: a list of 77 entries is a scrollbar.

So: `GAIN_MODEL_LIST` keeps today's list. `GAIN_MODEL_RANGE` gets a value
field with a stepper, bounded by min/max and stepping by `gain_step`. The unit
comes from `gain_unit`, so tenths of a dB stay tenths and dB stay dB with no
conversion in the panel.

The work is real but mechanical: 18 of the program's 19 remaining `rtlsdr_`
calls outside `sdrprobe.c` are in this file, and `app.h`'s `supported_gains` /
`supported_gain_count` are the list shape leaking into `struct app`. Those
become reads of `app->device`, which is where the list already lives.

### Q2: is a half-known dBm worth reporting? -- **The premise is wrong: it is not half-known, and the missing term is not the antenna.**

This is the question the ticket said to answer before drawing anything, and
answering it properly changed it.

**36.214 puts RSRP's reference point at the UE's antenna connector.** The
antenna's *gain* is therefore not part of the quantity at all -- it is what
relates connector power to the field, which is a different measurement. So
`lte_dsp.h`'s explanation was slightly wrong about why this program cannot
report dBm, and it has been corrected: what is missing is not the antenna's
gain but an **absolute power reference for the converter** -- how many dBm at
the input a full-scale sample corresponds to, at a given frequency and gain.

**Neither device has one**, and a claim made here first time round was wrong:
that "an AD9361 reports its gain in dB, which is better than tenths of a dB
from a tuner whose absolute gain is unspecified". It does not.
`docs/absolute-power-reference.md` established it from UHD's source and it was
checked against `ad9361_device.cpp` directly: `set_gain()` casts the value to
`int gain_index`, clips it to 0-76 and pokes it into register 0x109, and which
of **three band-dependent tables** is loaded -- below 1300 MHz, 1300 to
4000 MHz, 4000 to 6000 MHz -- decides what a step is worth. UHD advertises
`meta_range_t(0.0, 76.0, 1.0)` over that index. It is a gain-table index
presented as dB, which is the same kind of thing the RTL's tenths are, not a
better kind.

Two consequences, and the second is the one that matters:

- **`GAIN_UNIT_DB` would be a lie for a B210's RX chain.** The profile's gain
  unit needs a third value -- an index -- or the panel will label a number in
  units it does not have. TX is the exception: that is a genuine 0.25 dB
  attenuator.
- **A calibration cannot be taken once and scaled by the gain setting.** It has
  to be indexed by **gain and band together**, because the dB-per-index curve
  changes at 1300 MHz and 4000 MHz. An earlier note here suggested a
  calibrated gain would let one measurement hold across settings; it will not.

Getting to dBm is therefore a one-time measurement against a known source, per
device, per frequency and per gain -- **the same family of thing as the ppm
calibration**, which this program already measures, stores per site, and gates
on. That is ticket 08's subject, not this one's.

So the answer is in three parts:

1. **Until such an offset exists, keep dBFS.** Nothing changes. The Probe
   context may not claim a decibel-milliwatt it did not measure.
2. **When it exists, report dBm and name its reference point** -- "at this
   receiver's antenna connector" -- alongside dBFS rather than instead of it.
   dBFS is what compares two cells on one receiver, and that comparison is
   used constantly (EARFCN 3625's two cells at -33.3 and -35.0 dBFS).
3. **Never present it as comparable with a handset's RSRP**, and this is the
   part that would be tempting and wrong. A telescopic whip and a handset's
   internal antenna intercept different fractions of the same field. A correct
   dBm at this connector is still a different number from a handset's, and
   labelling it "RSRP (dBm)" invites exactly the comparison it cannot support.
   **RSRQ remains the transferable one**, because it is a ratio through one
   chain, and `check-lte-dsp` already pins that by doubling a buffer and
   asserting RSRP moves 6.02 dB while RSRQ does not move at all.

Is that a verdict `signal_findings.h` would refuse? **No** -- and the
distinction is worth keeping straight. That module refuses to name a
technology from a measurement, which is an inference. A calibrated power
reading with its reference point stated is a measurement, and stating where it
was referenced is what makes it one rather than a claim.

### What this ticket should build

Only Q1: the panel, driven by `gain_model`, and `supported_gains` /
`supported_gain_count` folded into `app->device`. No dBm.

The dBm work belongs in **08** as "an absolute power reference, measured and
stored per device like ppm", and it needs sign-off on the reporting convention
in part 2 above before anything draws it.

## Not in scope (unchanged)

- AGC. The program sets a fixed gain deliberately, because a level that moves
  under the measurement makes every survey incomparable.


## Built

The triage's answer was that a list and a range are both **steppers**, so the
panel needs one shape rather than two. That turned out to be the whole design:
what differs is only what the steps are.

`device_profile.h` gains three:

- `device_gain_option_count(p)` -- a tuner's 29 entries, or `(max - min) / step
  + 1` for a range, or 0 for a source with no gain to set;
- `device_gain_option_value(p, i)` -- the i'th setting in the profile's own
  unit, with out-of-range reading the floor rather than past anything;
- `device_gain_format(p, value, out, n)` -- **the unit is the profile's and
  nothing converts**.

`GAIN_UNIT_INDEX` is new and is the reason that last one exists. An AD9361's
receive gain is a gain-table index that UHD advertises as
`meta_range_t(0.0, 76.0, 1.0)`, and what a step is worth depends on which of
three band tables is loaded. A panel writing "40 dB" there would be lying, so
the unit says `index 40`.

`struct app`'s `supported_gains` and `supported_gain_count` are **gone**. They
were a pointer and a count beside the profile that already held both, which is
two things that could disagree. `print_supported_gains()` takes the profile
now and prints in its unit, so a rejected `--gain` on a range device will not
list tenths of a dB that do not exist.

## Checks

`check-device-profile` is 107, up from 88: both models enumerate, a coarser
step gives fewer options rather than a different shape, an index formats as an
index and a real dB gain as dB, no model offers nothing rather than one option,
and a range with no step refuses to be enumerated. Verified by mutation --
letting an index format itself as dB fails by name, and an off-by-one in the
range count fails three.

## Looked at

On the **live receiver**: the panel reads `29.7 dB` with its steppers, which is
what it read before, now through the profile. Under **file playback** it still
reads `capture (not adjustable)` -- the capture profile has `GAIN_MODEL_NONE`,
so there are no options to step through and nothing to draw.

## What is deliberately not here

The dBm reading. It went to ticket 08 with the two constraints on how it may
ever be reported, and `docs/absolute-power-reference.md` established that
neither device ships with the data it would need. Nothing on screen mentions a
calibration that does not exist.
