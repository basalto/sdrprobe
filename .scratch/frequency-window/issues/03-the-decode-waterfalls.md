# 03 — The same on the decode views and calibration

Status: ready-for-agent
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

## Decision

**The window stays in hertz everywhere, and the ARFCN axis becomes a label
format over it.** All five charts take the same mechanism; none of them keep a
private arrangement.

That settles the first complication in favour of the more work. The reason to
prefer it is that a channel number is a *rendering* of a frequency, not a
different quantity -- `GSM900_BASE_HZ` and `GSM900_ARFCN_SPACING_HZ` already
convert one to the other, and the component is already handed both. A parallel
channel-shaped window would have been a second source of truth for where the
reader is looking, and this repository has paid for that arrangement twice
already (`row_list.h`, `chrome_tab_rect`).

The accepted cost: **a zoom can land mid-channel**, so the GSM axis will draw
partial ARFCNs at its edges. That is honest -- the reader really is looking at
part of a channel -- but it needs deciding how a partial channel is labelled
rather than letting it fall out of the arithmetic. A label centred in a
channel that is half off screen will drift off its own gridline.

The second complication stands as written: **LTE is fixed at 1.92 MS/s
(ADR-0014)**, so a pan running off the edge can move the centre and nothing
else. It must never try to widen the rate to cover a request.

## What must be checkable

- Hertz to ARFCN and back, at the edges of the band and outside it. `ARFCN 0`
  and `ARFCN 124` are the ones that will be wrong.
- Which channels a zoomed window overlaps, including the partial ones, since
  that is what decides the labels.
- That the LTE view's overflow path moves the centre and never the rate.
