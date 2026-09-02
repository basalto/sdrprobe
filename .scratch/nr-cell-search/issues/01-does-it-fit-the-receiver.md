# 01 — Does an n28 synchronisation block fit this receiver?

Status: resolved
Blocked by: (none)

The whole effort turns on one number: the subcarrier spacing n28 uses for its
synchronisation block.

| spacing | primary/secondary signal | fits 1.92 MS/s? |
| --- | --- | --- |
| 15 kHz | 127 * 15 kHz = 1.905 MHz | yes, barely |
| 30 kHz | 127 * 30 kHz = 3.81 MHz | no, and no rate this dongle has would |

38.101-1 lists which cases a band supports, and low FR1 bands are usually
Case A, which is 15 kHz. **That is a recollection, not a citation** -- find the
table, name it, and quote the row for n28. If it is 30 kHz, this effort stops
here and the spec says so; that is a perfectly good outcome for a day's work
and much better than discovering it after writing a detector.

## Then take a capture

Independently of the tables, record what is actually on air, because the answer
has to survive contact with the operator's real configuration:

    ./sdrprobe --headless --record-seconds 2 --technology lte --frequency 774.2M

774.2 MHz is the strongest thing the survey found in the band. The sidecar
conventions are in AGENTS.md; `--technology lte` is the closest existing label
and the note field should say plainly that the capture is of a suspected NR
carrier, not an LTE one.

What to get out of it:

- **Is there a repeating structure at 20 ms?** That is the default period for a
  synchronisation block, and nothing in LTE repeats at that interval. It is the
  cheapest positive evidence that the occupant is NR, and it needs no sequence
  model at all -- which is exactly the kind of measurement the LTE work learned
  to start from.
- **How wide is the occupied energy?** A 10 MHz NR carrier and a 10 MHz LTE
  carrier look similar in a survey, but LTE puts a strong narrow spike at the
  carrier centre every 5 ms and NR does not.

## Answer

**It is NR, it runs at 30 kHz, and it does not fit. 02 and 03 are closed.**

Measured rather than looked up, which turned out to matter: the recollection
in this ticket was 15 kHz, and 15 kHz is wrong.

`captures/lte_20260902-174641.bin` -- 3 s at 774.2 MHz, 1.92 MS/s. The cell is
live and the command at the top of this ticket retakes it, which is why an
11 MB capture is not in `testfiles/`. Both measurements below are in
`make probe-periodicity`, and both were calibrated on
`testfiles/lte_b20_pci28.bin` first, where the answer is known.

### It is not LTE

Correlate the signal with a delayed copy of itself and fold the result over
the delay, so a burst at a fixed phase averages up and everything else averages
down. No sequence model, and immune to the 36 ppm tuning error, since a
frequency offset is one constant phase across a lag correlation.

| period | control (LTE cell 28) | 774.2 MHz |
| --- | --- | --- |
| 5 ms | **4.2x floor** | 1.1x floor |
| 10 ms | 3.4x | 1.6x |
| 20 ms | 3.3x | **8.1x**, phase 17.233 ms |
| 40 ms | 2.5x | 4.6x, *same phase* |

LTE's primary signal is every 5 ms and this has nothing there at all -- 1.1
times its own floor is the correlation finding nothing. What it has instead is
a burst every 20 ms, which is the default period of an NR synchronisation
block. The 40 ms row lands on the identical phase, as a harmonic of one 20 ms
event must and an independent finding would not.

### And it runs at 30 kHz, which is the answer that closes this

Every OFDM symbol opens with a copy of its own tail, so the signal correlates
with itself at a lag of exactly one useful symbol: 128 samples for 15 kHz at
1.92 MS/s, 64 for 30 kHz.

| | control (known 15 kHz) | 774.2 MHz |
| --- | --- | --- |
| lag 64 (30 kHz) | 0.568 | **0.607** |
| lag 128 (15 kHz) | **0.708** | 0.551 |
| floor, other lags | ~0.55 | ~0.495 |

The control picks its own known spacing, so the method reads true. The capture
picks 64.

A second measurement agrees, which is the part that makes it safe to act on.
The 20 ms burst is 129 us wide at half maximum against the control's 117 us
for one 15 kHz symbol, so the correlating span is roughly 84-120 us. Three
symbols -- primary signal, one PBCH symbol, secondary signal, the part that
repeats identically -- is 107 us at 30 kHz and 214 us at 15 kHz. Nothing
supports 15.

### What failed, and is not being read as evidence

Restricting the prefix measurement to the burst itself, to rule out the SSB
using a different spacing from the traffic, returned lag 96: 20 kHz, which NR
does not define. The window was too small for the statistics and the 24-sample
correlation was wider than the prefix it was hunting, so the estimator broke
down and returned a winner anyway, exactly as a search with no floor always
will. It is discarded rather than interpreted. Overturning the conclusion would
mean a much longer capture and a prefix-length window; nothing here argues for
spending that.

### So

At 30 kHz the primary and secondary signals span 127 x 30 kHz = 3.81 MHz. An
RTL-SDR manages about 2.4 MS/s before it drops samples and 3.2 at its
absolute limit, so **not even the cell identity is reachable**, never mind the
240-subcarrier block carrying the broadcast channel at 7.2 MHz. This is the
outcome the ticket was written to catch, and it cost a capture and an
afternoon rather than a detector.

What survives is worth more than the detector would have been: there is now a
measurement in this repository that tells LTE from NR and names the grid,
`make probe-periodicity`, and it works on signals nothing here can demodulate.
