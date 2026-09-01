# 06 — SCH continuity, and the constellation's scaling

Status: resolved
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

## Answer

Done in `src/gsm_continuity.h`, checked by `tests/gsm_continuity_test.c`
(`make check-gsm-continuity`, 36 checks). `struct gsm_sch_continuity` moved out
of `app.h` with it.

Writing the checks found two real faults in the four lines that were there:

- **The hyperframe wrap was flagged as a jump.** T1 runs 0..2047 and comes
  round every 3 h 28 m; `abs(t1 - last_t1) > 1` reads 2047 -> 0 as a jump of
  2047. `gsm_t1_advance()` counts forwards around the modulus, so the wrap is
  an advance of one. This is the case the ticket said to write first, and it
  was indeed broken.
- **A long gap was flagged as a jump.** The old rule required an advance of at
  most one whatever the elapsed time, so leaving the view for a minute and
  coming back reported a jump on a cell that had done nothing but keep time.
  `gsm_t1_allowed_advance(seconds)` judges the advance against the clock --
  T1 ticks every 6.12 s -- with a tick of slack for the boundary.

One thing added: a BSIC that changes between decodes is now flagged too. A
different BSIC on the same channel means a different cell or a bad decode, and
either way the frame number beside it belongs to something else. Safe because
`gsm_tune_selected()` resets the continuity when the channel changes.

Not done: the constellation's amplitude normalisation and de-rotation, the
other half of this ticket. Those are scaling for a scatter plot -- wrong
scaling is visible as a wrong-looking constellation, which puts it in layer 3
rather than layer 1. Left out deliberately rather than forgotten.
