# 06 — A matched filter shaped like the pulse

Status: resolved
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

## Comments

Resolved: built, measured, **not adopted**, and written up in
`docs/rds-matched-filter.md` so the question is closed with a number rather
than an opinion.

The ticket asked for a marginal station to test against. There is not one.
Every carrier in band II reachable from this site either decodes well or
carries no RDS at all -- 87.6, 95.3 and 96.6 were each listened to for twenty
seconds and produced nothing, while the seven that do carry it name themselves
within a couple of seconds. So the weak signal had to be made: `make
probe-fm-filter` adds noise to a real recording's I/Q and runs both filters
over the identical noisy samples, six draws per point.

Two details that decided whether the measurement meant anything. Noise goes on
the I/Q rather than the baseband, because the discriminator is nonlinear and
has a threshold -- adding noise after it would not reproduce how a weak signal
actually degrades. And the shaped taps are scaled by root two so the two
filters carry equal energy; without that the comparison would have measured
the scaling instead of the shape.

The result is the interesting part: **the shaped filter is never worse and
about 14% better in blocks at the threshold edge**, exactly as theory
predicts. It is still not worth adopting, for three reasons that only showed
up in the data. The window where the two differ at all is three decibels
wide -- identical at +13 dB and above, both dead at +9 -- because FM's own
threshold closes it from below. Whether the station gets *named*, which is
what a person actually sees, did not improve: 5/6 against 4/6, then 4/6
against 3/6, then 0/6 against 1/6, all one-trial differences in both
directions. And nothing reachable from here lives in that window, so a change
that cannot be observed from this site has regressions that cannot be observed
either.

Kept: `enum fm_rds_filter`, `fm_rds_soft_bits_with()` and the probe. The
default is one argument away, and if a marginal station ever matters -- a
distant transmitter, a worse antenna, an indoor site -- the measurement is one
command and the answer may come out differently.
