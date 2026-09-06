# 04 - How long is a burst, and what fraction of the time is it there

Status: needs-triage

Blocked on nothing. Independent of 01-03: it works in the time domain where
those work in the frequency domain, and it is the one feature of the standard
monitoring chain this repository has no form of at all.

## What is missing

The reference pipeline lists nine features under extraction. Eight of them
exist here in some form. **Duration does not exist in any form**, and the
occupancy that does exist is measured at the wrong resolution:
`survey_measure_duty()` folds one *block* at a time, and a block is 65.5 ms.

So a Mode S squitter of 120 microseconds and a continuous broadcast carrier
are the same measurement to it: both are "up" in the block they fall in. That
is a factor of five hundred, and it is the difference between a transmitter
and a transmission.

## Two things already went wrong for want of it

**The confirmation pass refutes real signals.** `.scratch/bursty-signals/`
measured five identical sweeps of 1550-1766 MHz minutes apart: fifteen
frequencies, no frequency twice, every one 30 dB and more above its floor.
Those are real bursts on channels that move, and the pass -- six blocks on the
frequency -- calls nine of ten refuted. A duty measured in microseconds would
say "bursty, 8 ms on, seconds off" instead of "not there".

**`signal_probe` shrugs at a pulsed signal.** `adsb_cpr_pair.bin` reads
`carrier_over_noise_db` -2.0 and is reported as no carrier. That is *correct*
-- Mode S has no standing carrier -- and it is unhelpful, because the capture
plainly holds six decodable frames. "Pulsed, bursts of about 120 us" is the
answer, and it is the same measurement as duration.

## The shape

Envelope, a threshold, hysteresis, and the runs that come out. Reported per
burst: start, length, and the gap to the next; reported per capture: how many,
their median length, and the fraction of time occupied.

**The threshold is measured, not chosen**, the way `SIGNAL_CARRIER_PRESENT_DB`
was: run it on pure noise and on an empty frequency, and put it above what
those produce. A run finder over an envelope always finds runs.

Two traps to write down before they cost an afternoon:

- **A burst that straddles the end of the buffer is reported short.** Either
  count only bursts wholly inside and say how many were dropped, or report the
  truncation. Silently averaging a half burst into the median is the failure
  mode, and it biases every answer the same direction.
- **The R820T's gain moves.** An absolute envelope threshold measures the AGC
  as much as the signal, so the threshold is relative to the noise *in this
  buffer*, and the measurement does not carry between buffers at different
  gains -- the same constraint that makes RSRP dBFS rather than dBm.

## What must be checkable

Three captures already in `testfiles/`, with three different known answers,
and no receiver needed (ADR-0012):

| capture | what it is | what the measurement must say |
| --- | --- | --- |
| `adsb_cpr_pair.bin` | Mode S, 8 us preamble + 56 or 112 us of data at 2 MS/s | short bursts, roughly 128 or 240 samples, most of the time idle |
| `tetra_cc17.bin` | TETRA, a synchronization burst in every timeslot | a burst grid at the 14.167 ms timeslot |
| `fm_rds_tsf.bin` | FM broadcast | one run, occupied throughout, no burst structure |

The third is the one that matters most: a burst finder that reports bursts on
a continuous carrier is worse than none, and only a continuous capture can
catch it.
