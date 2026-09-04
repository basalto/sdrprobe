# 02 — Zoom, pan and select on the spectrum and the waterfall

Status: resolved
Blocked by: 01

The two Scope charts with a frequency axis, and the header fields that say
what is on them.

- Drag a region: the two header fields follow it.
- Left and Right pan; a key resets to the full received span.
- Typing in either field moves the window.
- Inside what the receiver is delivering it is a zoom; outside it is a retune.
  The spec has the argument for making that a consequence rather than a mode.

The spectrum and the waterfall share one window: they are two drawings of the
same received span and a reader who zooms one and not the other has been given
two answers to one question.

## What must be checkable

The pixel-to-frequency mapping and what a drag means (01 provides them), and
the rule that decides zoom from retune -- that one especially, because it is
the only place where looking at something can change what is being received.

## Comments

Resolved. Every clause of it shipped, though not in one go and not without
finding that the hardest part was not the arithmetic.

The window, the drag, the pan and the retune-at-the-edge came first. The
header fields -- start, centre and end, in the order they appear on the axis
-- came later, with the resolution stepper beside them; a field nobody is
typing into follows the window, so the row and the axis can never disagree.
Both charts read one `app->sv.freq`, so zooming one zooms the other.

Three bugs were in the way, and none of them were in the mapping this ticket
was worried about:

- The waterfall discarded the window entirely. Its zoom was gated on
  `calibration_mode`, a flag naming a *screen*, and the overlay was the only
  caller that zoomed for as long as that went unnoticed. The drag worked
  throughout; nothing drew.
- The hit test mapped across the outer rectangle while the trace is drawn
  inside a caption strip and a label gutter, so every drag landed about
  50 kHz left of where it looked. The highlight was computed the same wrong
  way, so the two agreed with each other and only disagreed with the chart.
- The waterfall never drew the band being dragged, so a reader learned what
  they had selected after selecting it.

What the ticket asked to be checkable is: `freq_window_*`, the drag,
`sdrgui_waterfall_span()` and `sdrgui_drag_band_at()`. The lesson worth
carrying to 03 is that the arithmetic was never the risk -- every one of these
was a wiring fault between a correct decision and the drawing that ignored it,
and only a screenshot could see any of them until the decision had a name.
