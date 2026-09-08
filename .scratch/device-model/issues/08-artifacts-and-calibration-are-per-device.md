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


## Added 2026-09-08, from ticket 06's triage

**An absolute power reference belongs here.** Ticket 06 set out to answer
whether a "half-known dBm" was worth reporting and found the premise wrong:
36.214 puts RSRP's reference point at the UE's antenna connector, so the
antenna's gain does not enter into it. What is actually missing between this
program and a decibel-milliwatt is a **dBFS-to-dBm offset for the converter**,
at a given frequency and gain -- and neither an RTL-SDR nor a B210 ships with
one.

Getting it is a one-time measurement against a known source, per device and
per frequency. That is the same shape as the ppm calibration this program
already measures, stores per site (`config_site_ppm()`), and gates on -- which
is why it belongs in this ticket rather than in 06.

Two constraints on it, from that triage:

- The reading must **name its reference point** ("at this receiver's antenna
  connector") and sit alongside dBFS, not replace it. dBFS is what compares
  two cells on one receiver and that is used constantly.
- It must **never be presented as comparable with a handset's RSRP**. A
  telescopic whip and a handset's internal antenna intercept different
  fractions of the same field. RSRQ stays the transferable number.
