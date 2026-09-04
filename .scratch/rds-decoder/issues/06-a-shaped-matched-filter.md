# 06 — A matched filter shaped like the pulse

Status: ready-for-agent
Blocked by: (none)

`fm_rds_correlate` uses a rectangular biphase filter: eight samples of +1 then
eight of -1. The transmitted RDS pulse is band-limited, so the true matched
filter is that shaped pulse, and the mismatch costs about a decibel in theory.

## What was measured

A sine-weighted biphase filter was tried against the rectangular one on
`testfiles/fm_rds_tsf.bin`: **153 aligned syndrome hits against 153**. No
difference at all, so the rectangular one stayed.

## Why this is still open rather than closed

That was one capture of a strong station, where the decode was already reading
86% of its blocks and the remaining failures are unlikely to be the filter.
The decibel would show up, if anywhere, on a *weak* station -- and the band
scan now lists several that carry RDS and decode poorly (87.6 read a pilot and
no groups; 93.2 and 97.4 read groups but no name in the time it stopped
there).

## The work

Record one of the marginal stations, measure the block rate with each filter,
and close this either way. A decibel that shows on a weak signal and not a
strong one is worth having; a decibel that shows nowhere is worth writing down
as not worth having.

## Decision

**Build it and measure it against the weakest station reachable here.**

The measurement that exists says no gain on a strong station, which is the
uninformative half: a strong station decodes either way. The question is
whether it recovers one that currently fails, and that is answerable by
experiment rather than by argument.

What makes this worth the time is that the answer is durable in both
directions. A gain justifies the filter; no gain closes the ticket with a
number attached instead of leaving it as a standing suggestion that somebody
re-raises every year.

Constraints: ADR-0003 -- hand-written, self-contained, no library. And the
`dsp-validation` skill applies before trusting any improvement, because a
round trip cannot check a convention both sides share and this repository has
lost months to exactly that twice.

## What must be checkable

- The filter against a synthetic signal at several noise levels, asserting
  where it does and does not help.
- The existing captures keep decoding: `fm_rds_tsf.bin` still reads 0x8343,
  `TSF`, and its programme type.
- The measurement itself recorded in the ticket -- station, level, groups per
  second with and without -- so the answer survives the ticket.
