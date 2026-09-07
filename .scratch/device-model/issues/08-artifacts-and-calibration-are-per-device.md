# 08 - The comb, the crystal, and what calibration is for

Status: needs-triage
Blocked by: 07

This absorbs `.scratch/calibrating-the-flags/`, whose ticket 01 already says
the thresholds "were measured on one dongle at one site and compiled in, and
the 14.4 MHz comb is derived from a 28.8 MHz crystal that another device may
not have".

With a second device that stops being a caveat and becomes a measurement.

## What changes, and what the device changes about it

- **The comb.** `docs/receiver-artifacts.md` derives 14.4 MHz from the RTL's
  28.8 MHz crystal. An AD9361 has a configurable master clock and its own
  artifact profile -- DC offset and quadrature images rather than a harmonic
  comb, and it calibrates both automatically. The receiver-like mark may need
  a different algorithm, not a different constant.
- **What calibration is for.** The whole overlay exists because an RTL's
  crystal drifts with temperature; `config_site_ppm()` keeps a correction per
  site because it is measured against whatever reference a place offers. A
  TCXO barely drifts and a 10 MHz or GPSDO reference does not drift at all.
  The machinery does not break -- it becomes an *instrument* that verifies the
  reference rather than a necessity that enables decoding. That is a change of
  meaning on screen, and ADR-0004's gate should be read again in that light.
- **Per-site ppm** stops being the right shape when the correction is not a
  property of the site.

## Why it is triage and not ready

Every item above needs the device in hand and a sweep taken with it. Writing
constants for an AD9361 from a datasheet is exactly the mistake
`.scratch/calibrating-the-flags/` was opened to record.
