# 03 — The same on the decode views and calibration

Status: needs-triage
Blocked by: 02

The GSM, LTE and FM views each draw a waterfall, and so does the calibration
overlay. Once 02 has the mechanism they should all take it.

Two of them have a complication worth thinking about rather than working
around:

- **The GSM and calibration waterfalls are drawn against an ARFCN axis**, not
  a linear frequency one. The window arithmetic is in hertz; the axis is a
  channel number. Either the window converts, or those two keep their own
  arrangement and this ticket covers three charts rather than five.
- **The LTE view refuses anything but 1.92 MS/s** (ADR-0014). A retune that
  changes the sample rate is not available there, so a region outside the
  received span can only move the centre.
