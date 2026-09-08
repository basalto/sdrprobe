# 06 - A gain model that is not a list of tenths of a dB

Status: triaged 2026-09-08, **ready-for-agent for the panel** and
**ready-for-human for the dBm convention**. Both open questions are answered
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

**Neither device has one.** An AD9361 reports its gain in dB, which is better
than tenths of a dB from a tuner whose absolute gain is unspecified, but
"gain in dB" is not "dBFS to dBm". Getting there is a one-time measurement
against a known source, per device and per frequency -- **the same family of
thing as the ppm calibration**, which this program already measures, stores
per site, and gates on. That is ticket 08's subject, not this one's.

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
