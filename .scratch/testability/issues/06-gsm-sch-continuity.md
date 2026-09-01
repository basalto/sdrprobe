# 06 — SCH continuity, and the constellation's scaling

Status: ready-for-agent
Blocked by: (none)

`check_sch_continuity` in `src/view_gsm.c` decides whether consecutive SCH
decodes are consistent — the same BSIC, a frame number advancing by a plausible
amount — and that judgement is what tells the operator a decode can be
believed. It is a pure function over two decodes and a counter, sitting in a
file that links raylib for no reason other than where it was written.

The constellation's amplitude normalisation and de-rotation are the same shape:
arithmetic that decides what is drawn, inside the drawing.

What it would take: move `check_sch_continuity` into `gsm_dsp.c` or a small
header and check it directly — a clean run, a BSIC that changes mid-run, a
frame number that goes backwards, the multiframe wrap (a legitimate backwards
step), and a gap long enough that continuity should be given up rather than
asserted across it. The wrap is the case worth writing first: a decoder that
never wraps and one that wraps wrongly look identical for 3 hours 28 minutes.

## Comments
