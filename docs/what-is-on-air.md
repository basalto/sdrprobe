# What is on air here, and what of it is worth decoding

An assessment of the radio environment at one site, and of every allocation
the survey finds signals in: what this program does about each, and where
something was ruled out, the measurement that ruled it out rather than the
opinion.

It exists because the same questions keep being re-asked. DAB+ has been
proposed twice and refused twice on the same folded correlation; 5G NR got a
ticket written for it before anybody measured its subcarrier spacing. Both are
recorded here so the third time is cheap.

`.scratch/what-is-on-air/` is the working effort behind this; this file is the
answer as it stands.

**Aeronautical VHF has its own document**, because it took an hour of
measurement and produced a null with a control behind it:
`docs/aeronautical-vhf-at-this-site.md`. The short version is that across
108-137 MHz this setup hears **nothing external** -- every candidate is either
a carrier clocked with the receiver's own crystal or a noise maximum with no
carrier -- while the same antenna hears ten FM broadcast carriers twenty
megahertz lower. The receiver's own comb reaches **70 dB** over the floor in
that band, about 20 dB above anything outside it.

**And the reference sweep below is gone from disk.** `surveys/` was cleared on
2026-09-10; the numbers in the next table are what it said and can no longer
be re-derived from the file. The newest sweep is
`surveys/2026-09-10-002420-24M-1766M.json`.

## The site, and the sweep this rests on

`home-sala-estar`, a telescopic whip indoors, 29.7 dB of gain, an R820T behind
an RTL2832U. The reference sweep is
`surveys/2026-09-06-200418-24M-1766M.json`:

| | |
| --- | --- |
| range | 24-1766 MHz, everything the tuner reaches |
| bin | 212.6 kHz -- wider than a land-mobile channel |
| candidates | 342: 251 clean, **91 resembling the receiver** |
| grouped into | 64 signals, 17 of them holding several maxima |
| confirmation | asked about 24: 10 held up, 2 came and went, **12 did not** |
| site history | 16 saved surveys, about 134 signals |

Two of those numbers deserve reading twice.

**Ninety-one of 342 candidates are the instrument, not the band.** They sit on
multiples of 14.4 MHz, half the RTL2832U's 28.8 MHz reference, and on the
1.6 MHz tones between them. More than a quarter of what a raw sweep reports is
the receiver talking to itself.

**Twelve of twenty-four confirmations failed.** Above 1.5 GHz that is most of
the list, and it is not all noise: a 0.12 s dwell catches a bursty transmitter
about as often as it misses it, which is why the pass has a third verdict.

## The two gates

From the `rf-environment` skill, and both were learned by getting it wrong.

**Does it fit the receiver?** About 2.4 MS/s before samples drop. Measure the
bandwidth of the thing a decoder must see *whole* -- not the channel.

**Is it actually there?** An allocation is a lookup, not an identification
(ADR-0015). Band 28 is labelled an LTE downlink and carries 5G NR; 75.000 MHz
is labelled ILS markers and carries a clock harmonic.

Only then is it ranked: on whether it ends in a **message rather than a
waveform**, on reuse, and on whether it stays on air.

## Decoded

| allocation | n | best | prom | what is read |
| --- | --- | --- | --- | --- |
| GSM 900 / LTE B8 downlink | 9 | -30.1 | 27.7 | MCC 268 MNC 03, LAC, Cell Identity; and the band 8 MIB |
| LTE band 20 downlink | 4 | -33.9 | 39.8 | PCI, MIB, RSRP/RSRQ/RS-SINR, delay spread |
| TETRA | 12 | -36.2 | 31.6 | MCC 268, MNC 3, colour code, location area |
| FM broadcast | 8 | -41.6 | 20.2 | RDS identification, station name, radio text; and audio |

Four technologies, three of them cellular, all four ending in something the
transmitter is *saying*. MCC 268 is Portugal, read independently off GSM and
off TETRA.

## Ruled out, with the measurement

| allocation | n | best | prom | why not |
| --- | --- | --- | --- | --- |
| UHF television | 40 | -40.6 | 30.6 | DVB-T is 8 MHz. Fails the receiver gate permanently, at any dwell, on any antenna. |
| VHF band III / DAB+ | 22 | -44.7 | 22.3 | Folded at DAB's own 96 ms frame, where the phase reference symbol must repeat identically: **1.4 times its floor**. There is no DAB here. |
| LTE band 28 downlink | 4 | -39.4 | 34.3 | Carries **5G NR**. Its synchronisation signals are 127 subcarriers -- 1.905 MHz at 15 kHz spacing and fits, 3.81 MHz at 30 kHz and does not. Measured at 30. |
| NB-IoT, on the band 8 carriers | -- | -- | -- | Six carriers read 2.9 to 5.1 deviations of the narrowband primary sequence with 50-79% frame repeat, against a **known-empty** LTE capture at 4.0 and 68%. The detector fires at 12.8 deviations on `--self-test`, which is what makes the null worth anything. |
| Aeronautical radionavigation | 5 | -45.5 | 17.9 | 75.0005 MHz is the first member of a **75 MHz x 2^n** family clocked with this receiver -- 75, 150 and 300 MHz present, 37.5, 175 and 225 absent, measured 2026-09-11; it was recorded here as 25 MHz x 3, which 175 and 225 being absent refutes.  Every ILS marker sideband -- 400, 1300, 3000 Hz, both sides -- sits within 3 dB of a control probe at an empty frequency, where a 30%-depth AM sideband would stand 42 dB up. `testfiles/carrier_75000_bare.bin` |
| AIS | -- | -- | -- | Ten seconds at 162.0 MHz put both channels 0.5 and 0.7 dB over controls 200 kHz either side, with no window 6 dB over its own mean. See below. |
| SIB1 | -- | -- | -- | The cell is 50 resource blocks and 1.92 MS/s sees six, so the control message that locates SIB1 cannot be assembled. The turbo code and CRC-24 were built anyway and are kept deliberately consumer-less. |

## Mislabelled: the receiver, bare carriers, or noise

The confirmation pass measures at 244 Hz where the sweep had 212 kHz bins, and
now reports what kind of thing it found. That is what settled these -- none of
them is a decoder.

**VHF band I television, 60-68 MHz**, the second-strongest group in the sweep.
Two narrow carriers a few kilohertz wide, one of them *bare*: 62.4058 MHz at
43.5 dB with 89% of the channel standing still. A television channel is 7 MHz.
(That share was measured before `carrier_power_fraction` became a mean over
segments; the statistic reads a little higher now for a bare carrier and
higher again for a modulated one, and the verdict is unaffected --
`.scratch/standing-fraction-drifts/`.)
**62.4 MHz is exactly 39 x 1.6 MHz**, and the candidate sits 5.8 kHz off it.

**GNSS L1 / E1.** One candidate, which the survey flagged itself as the
receiver's own comb. The 1604.920 MHz peak at 37.1 dB prominence from the
reference sweep did not reappear, which puts it with the mobile-satellite
bursts. Either way it is not GNSS, which is spread spectrum *below* the noise
floor and cannot appear as a peak at all.

**Fixed and mobile, 290-310 MHz**, sixty candidates in the reference sweep.
Asked properly: four to six bare carriers, two modulated, and five readings
that are noise. **302.4 MHz is exactly 21 x 14.4 MHz** and **296.0 MHz is
exactly 185 x 1.6 MHz**, both flagged.

## AIS: the best-shaped target left, and not receivable here

The band plan already names it -- "AIS at 161.975/162.025" inside Marine VHF --
and it is the strongest candidate this repository could still take on:

- **Fits.** 25 kHz channel, 9600 bps GMSK, trivially, at 2 MS/s.
- **Reuse.** GMSK demodulation exists in `gsm_dsp`; HDLC with NRZI and a
  CRC-16 is small; the payload is a position, the same shape as ADS-B's CPR
  pairing and its even/odd cache.
- **A message, not a waveform.** MMSI, position, course, speed.

Measured 2026-09-07, ten seconds recorded at 162.0 MHz -- between the two
channels, so both sit in the window and neither lands on DC -- with the power
in each 25 kHz channel taken over forty 0.25 s windows:

| channel | mean | loudest window | windows 6 dB over the mean |
| --- | --- | --- | --- |
| AIS 1, 161.975 | -45.9 dB | -42.3 | 0 |
| AIS 2, 162.025 | -46.1 dB | -44.1 | 0 |
| control, 161.800 | -46.6 dB | -45.1 | 0 |
| control, 162.200 | -46.5 dB | -45.1 | 0 |

Nothing. A real AIS slot is 26.7 ms tens of decibels over the floor, and ten
seconds should catch several reports if any ship were in range.

**The instrument was made to fire before the null was believed.** The same
measurement over `testfiles/tetra_cc17.bin` puts the known TETRA carrier at
**-30.8 dB against controls at -47 to -52** -- 16 to 21 dB of separation. So
the flat reading is the air and not the method.

The honest claim is **not receivable at this site with this antenna**: a whip
indoors, at marine VHF, with a building between it and the Tagus. It is not a
claim about what is on the river, and an outdoor antenna would be a different
measurement.

## Land mobile: unsettled, and the null is weak

21 candidates, best -40.9 dBFS at 21.6 dB of prominence, around 164.878 MHz. A
real allocation with real traffic and no decoder, and the question is whether
it is analogue FM voice -- a waveform, which this program does not want -- or
digital, DMR or dPMR, which would be a message.

Ten seconds on the frequency put it **0.4 dB over controls** with no burst
structure. But push-to-talk is quiet most of the time by design, and ten
seconds of silence against a sweep that saw 21.6 dB of prominence is close to
no evidence at all. **This needs a watch on the frequency, not a capture of
it.**

## The state of it

**Nothing has passed both gates, so there is no decoder to write.** Both
candidates worth gating came back empty -- AIS decisively, land mobile weakly
-- and all three rows whose labels could not be right turned out to be the
receiver's own comb, bare carriers with nothing riding them, or noise.

That is the finding, not a failure to look. Four technologies are decoded, six
more are closed with measurements behind them, and the two that remain open
are open for reasons this file names.

## Reading this against a sweep of your own

Everything above is one site with one antenna, and levels compare only within
one of each. `docs/band-surveys.md` is the file format and the workflow; the
`rf-environment` skill is the analysis over a series. The two gates are not
optional and the second one is the one that gets skipped.
