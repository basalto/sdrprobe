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


## Researched 2026-09-08: `docs/absolute-power-reference.md`

Written against primary sources -- UHD and librtlsdr source, UG-570 -- after
the board was ordered. Its bottom line is **keep dBFS**, and the three facts
behind that are worth having here because they shape what this ticket builds.

**UHD has the API and the B200 is wired into it, but ships no data for it.**
`multi_usrp::set_rx_power_reference()` and siblings take dBm, and
`b200_impl.cpp` builds a power calibration manager per frontend. The only
calibration blobs compiled into libuhd are the X410's ZBX tables, and the image
manifest carries no calibration data at all -- so `has_rx_power_reference()` is
false on a fresh B210 and the setter throws. **The API is not the obstacle; the
data is.**

**The B210's RX gain is a band-dependent table index, not dB.** Verified
directly in `ad9361_device.cpp`: `set_gain()` casts to `int gain_index`, clips
0-76, pokes 0x109, and one of three tables -- <1300 MHz, 1300-4000,
4000-6000 -- decides what a step is worth. So **the calibration table this
ticket produces must be indexed by gain and band together**, and those two
boundaries are the AD9361's rather than anything chosen. A dBFS-to-dBm offset
measured at one gain does not scale to another by arithmetic.

**The AD9361's RSSI is not a shortcut.** UHD's own comment: "The result is in
dB but not in absolute units. If absolute units are required a bench
calibration should be done." UG-570 agrees, and the +-2 dB it quotes is *after*
a factory routine nobody has run on this board.

**The RTL-SDR has nothing.** `rtlsdr_get_tuner_gains()` returns a 29-entry
`const int` array compiled into `rtlsdr.c` -- identical on every unit ever
made, derived from one tuner measured at 928 MHz -- and
`rtlsdr_get_tuner_gain()` returns the last value requested rather than a
readback. A 2021 measurement of an R820T2 against an Anritsu MS2034A found the
nominal 0->8 dB step actually moving the response by 12.8 to 16.1 dB, varying
3.3 dB across 100-1000 MHz.

So this ticket's power-reference half is: **a user-run calibration against a
known source, stored per device, per band and per gain**, with UHD's own table
format and database as the machinery where the device supports it. The two
reporting constraints above stand unchanged.
