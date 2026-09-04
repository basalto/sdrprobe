# 06 — A matched filter shaped like the pulse

Status: needs-triage
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
