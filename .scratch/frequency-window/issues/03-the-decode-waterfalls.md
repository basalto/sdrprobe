# 03 — The same on the decode views and calibration

Status: resolved
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

## Comments

Resolved. All five charts take the same window: both Scope charts, the GSM,
LTE and FM waterfalls, and the calibration overlay.

The mechanism came out into `chart_window.{h,c}` -- the drawn range, the drag
state, the anchor, and the gestures over them -- because five copies of "which
pixel is which hertz" is five chances to get the label gutter wrong and this
program has already paid for that once. Retuning is deliberately *not* in
there: the input returns the hertz the receiver would have to move and the
caller decides whether that is allowed, since the Scope may retune freely and
a view reading a capture may not.

**The ARFCN axis needed less than the ticket feared and something else
instead.** The channel-axis branch already drew whichever channel centres fell
inside the drawn range, with a stride that thins the labels when they crowd --
so it handled a zoom correctly the day it was written. Verified on
`gsm_arfcn_69.bin`: the calibration overlay draws channels 62 to 72 with the
carrier at 68-69, which is where that capture's cell is.

The real edge was the opposite one. Labels are channel *centres*, so a window
narrower than the channel spacing can contain no centre at all -- an ARFCN
chart with no ARFCN on it, which reads as broken rather than as zoomed.
`chart_min_span()` floors the zoom at one channel spacing on a channel axis,
because a window exactly one spacing wide always contains exactly one multiple
of the spacing whatever its phase. Checked by walking a window across a whole
channel in twentieths, and by confirming that half a channel sometimes shows
none.

**`zoom_center_hz` is gone**, and that was the tidier half of the work. The
GSM view and the calibration overlay used it to hold one channel on screen --
the same idea as a window, arriving as an argument to the drawing, where there
was no way to say "start here" as opposed to "stay here". Those views now set
their window when a channel is selected, so there is one answer to what is on
screen and it can be dragged.

## A key conflict this created, and the rule that resolved it

Up and Down were the scale keys on any screen with no frequency window --
which was every screen but two, until this ticket gave five of them one. On
those five the arrows would have zoomed *and* changed the colour scale at the
same time.

So: **+ and - are the scale keys everywhere, with no exception, and Up and
Down are zoom.** The cost is that the arrows now do nothing on the magnitude
and I/Q scatter charts, which have no frequency axis. One rule across seven
screens is worth more than the arrows being useful on the two.

## LTE

Unchanged from the ticket: the pan moves the centre and never the rate.
`retune_receiver` only ever moves the centre, so the constraint holds by
construction rather than by a special case -- but it is said out loud in
`view_window_input`, because "it happens not to be able to" is the kind of
safety that a later change removes without noticing.
