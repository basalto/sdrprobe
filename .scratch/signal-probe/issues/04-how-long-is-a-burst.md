# 04 - How long is a burst, and what fraction of the time is it there

Status: resolved

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

## Comments

**2026-09-06 — done, with one expectation of this ticket not met and said so.**

`signal_find_bursts()` in `signal_probe`, fed to `signal_findings` and drawn
on the candidate panel. At the default gap, across every capture here:

| capture | contrast | verdict | bursts | occupancy |
| --- | --- | --- | --- | --- |
| `adsb_cpr_pair.bin` | 26.1 dB | separable | 4 | 0.0037 |
| `lte_b20_pci28.bin` | 20.5 dB | **busy** | -- | 0.796 |
| 75.000 MHz bare carrier | 9.1 dB | level | -- | -- |
| `fm_rds_tsf.bin` | 1.9 dB | level | -- | -- |
| `tetra_cc17.bin` | 3.1 dB | level | -- | -- |
| `gsm_arfcn_69.bin` | 5.2 dB | level | -- | -- |

Three verdicts rather than two, because "nothing to report" has two causes and
a reader who takes one for the other looks in the wrong place: **level** is a
carrier or an empty channel, **busy** is a transmitter that has not stopped.

### Every constant was measured, and three of them were wrong first

**Thresholding a raw envelope does not work.** The first version reported
**134726 bursts in a bare unmodulated carrier** and *none at all* in a Mode S
capture. What a threshold finds in a raw envelope is the modulation; what it
finds in a smoothed one is the transmission.

**A high percentile cannot see a rare event.** The first contrast gate used
the 99.9th percentile as its ceiling. Mode S puts its frames into about 0.04%
of the buffer, so the 99.9th percentile of it is noise, and the capture that
plainly holds bursts read "no burst structure". The ceiling is the maximum of
the *smoothed* envelope instead -- safe to use once smoothed, because a single
sample spike is averaged away and what survives is something that lasted.

**Contrast cannot separate a bursty signal from a modulated one.** At the
default gap an LTE downlink reads 191 "bursts" of 354 us -- its own OFDM
symbols -- at 20.5 dB contrast, against Mode S's 4 bursts at 26.1 dB. No
threshold on contrast tells those apart. Occupancy does, by a factor of two
hundred: 0.796 against 0.0037.

**The gap and the smoothing were both swept, not chosen.** At a 5 us gap an
unmodulated carrier reads 345 bursts and at 20 us it still reads 33; at 100 us
it reads none, 2.9 dB clear of the gate. Smoothing finer than the gap costs
that margin -- a quarter gives 22 false bursts, an eighth 38 -- so the window
is the gap.

**And every length was long by exactly the window.** The average is causal, so
it crosses a floor-relative threshold almost at the true start and does not
fall back until the window has slid off the end. Synthetic bursts of 100, 300
and 1000 us through a 100 us window all read exactly 99.5 us long, the half
sample being the discrete crossing. The window is subtracted, and the same
three then read exact.

### What this ticket expected and did not get

The table above expected `adsb_cpr_pair.bin` to read "roughly 128 or 240
samples", a 56- or 112-bit Mode S frame. It does not. Corrected for the
window, it reads **4 bursts of 14.5 us** at 0.4% occupancy, and the same
capture at a 5 or 20 us gap reads about 46 us. A Mode S frame is 64 or 120 us
and neither number is one.

The likely reason is that Mode S is pulse-position modulated at 1 Mbit/s, so
the data half of the frame is on about half the time and its smoothed envelope
sits near the threshold rather than clear above it -- what the finder locates
is the preamble and whatever of the data stays over the line, not the frame.
It is not established, and it should not be asserted. What the measurement
*does* establish about that capture is right and is the half this ticket was
raised for: pulsed, sparse, 0.4% occupied, against a continuous carrier's
level.

Resolving the frame would need the threshold set from the burst rather than
from the floor, or a two-pass estimate. That is worth a ticket of its own if
anything needs a frame length; nothing does yet.

### Not seen on screen

`check-signal-probe` is 56 checks to 87 and `check-signal-findings` 78 to 257,
including every burst sentence against the panel's width. The panel wiring is
in and the other findings render, but **the burst lines have not been seen
drawn**: three live sweeps found no bursty candidate -- 1090 MHz had no
aircraft in range, and a 1610-1650 MHz sweep found zero candidates, which is
`.scratch/bursty-signals/`'s own finding about how often a sweep catches
these. Worth looking at the next time one turns up.
