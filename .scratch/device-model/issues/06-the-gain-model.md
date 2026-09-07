# 06 - A gain model that is not a list of tenths of a dB

Status: needs-triage
Blocked by: 02

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
