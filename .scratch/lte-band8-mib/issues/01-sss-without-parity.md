# 01 — Find which stage loses band 8's broadcast channel

Status: needs-triage

The spec has the evidence and the two hypotheses nobody has tested. This is
about picking one and measuring it, not about writing a decoder.

## Where to start

`make probe-lte-chain` already walks the chain and prints every stage; what it
does not print is *why* the parity does not fit, because at that point there
is nothing left to print. Two things would say:

1. **The cyclic prefix decision and its margin.** The chain reports `normal`
   and reports it as a fact. It is a correlation with a runner-up, like every
   other decision here, and the runner-up is not shown. If extended scores
   nearly as well on this signal, that is the answer and it is one number away
   from being visible.
2. **Whether the four broadcast symbols are being sampled where they should
   be.** The frame boundary comes from the primary sequence, whose peak here
   is 0.583 against a runner-up of 0.479. A timing error of a few samples
   costs the broadcast channel and costs synchronisation almost nothing, so
   the two can disagree.

The honest first move is to make the chain print what it already knows and
looked at nothing new: the cyclic-prefix correlation both ways, and the
timing's peak against its neighbours.

## What must be checkable

- Whatever is added prints for the *working* capture too, so the two can be
  compared. `testfiles/lte_b20_pci28.bin` decodes 28 messages in 50 blocks and
  is the control: a number that looks alarming on band 8 means nothing until it
  has been read on a cell that works.
- If the cyclic prefix turns out to be the answer, the synthetic buffers in
  `tests/lte_dsp_test.c` can carry an extended-prefix case, which they do not
  today -- `build_buffer` passes 0 for `extended_cp` at every call site.

## What this must not become

A second decoder for a cell nobody can hear. If the answer turns out to be
that this cell is simply too weak for its broadcast channel at this antenna,
that is a finding and the ticket closes: `wontfix`, with the number that says
so. The point is to know which stage loses it, not to make it decode.
