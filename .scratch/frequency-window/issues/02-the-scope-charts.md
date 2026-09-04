# 02 — Zoom, pan and select on the spectrum and the waterfall

Status: ready-for-agent
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
