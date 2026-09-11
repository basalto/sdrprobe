# 01 - The register: every allocation, and what is done about it

Status: ready-for-human

Ordered by the strongest signal in each, from
`surveys/2026-09-06-200418-24M-1766M.json`.

**As of 2026-09-07 nothing has passed both gates, so there is no decoder to
write.** Both candidates worth gating were measured and came back empty -- AIS
decisively, land mobile weakly -- and all three rows whose labels could not be
right turned out to be the receiver's own comb, bare carriers with nothing
riding them, or noise. That is the outcome, not a failure to look: this
register exists so the next person does not re-ask.

## Decoded

| allocation | n | best | prom | what is read |
| --- | --- | --- | --- | --- |
| GSM 900 / LTE B8 downlink | 9 | -30.1 | 27.7 | MCC 268 MNC 03, LAC, Cell Identity; and LTE band 8 MIB |
| LTE band 20 downlink | 4 | -33.9 | 39.8 | PCI, MIB, RSRP/RSRQ/RS-SINR, delay spread |
| TETRA | 12 | -36.2 | 31.6 | MCC 268, MNC 3, colour code, location area |
| FM broadcast | 8 | -41.6 | 20.2 | RDS: identification, station name, radio text; and audio |

## Ruled out, with the measurement

| allocation | n | best | prom | why not |
| --- | --- | --- | --- | --- |
| UHF television | 40 | -40.6 | 30.6 | DVB-T is 8 MHz. Fails the receiver gate permanently, at any dwell. |
| VHF band III / DAB+ | 22 | -44.7 | 22.3 | Folded at DAB's own 96 ms frame, where the phase reference must repeat identically: **1.4 times its floor**. There is no DAB here. `.scratch/` records this being proposed twice. |
| LTE band 28 downlink | 4 | -39.4 | 34.3 | Carries **5G NR**, not LTE. Its synchronisation signals are 127 subcarriers: 1.905 MHz at 15 kHz spacing and fits, 3.81 MHz at 30 kHz and does not. Measured at 30. `.scratch/nr-cell-search/` |
| NB-IoT, on the band 8 carriers | -- | -- | -- | `probe-nbiot` correlates the length-11 narrowband primary sequence: six band 8 carriers read 2.9-5.1 deviations with 50-79% frame repeat, against a **known-empty** LTE capture at 4.0 and 68%. Not on air. The detector fires at 12.8 deviations on `--self-test`, which is what makes the null worth anything. |
| Aeronautical radionavigation | 5 | -45.5 | 17.9 | 75.0005 MHz is the first member of a **75 MHz x 2^n** family clocked with this receiver -- 75, 150 and 300 MHz present, 37.5, 175 and 225 absent, measured 2026-09-11; it was recorded here as 25 MHz x 3, which 175 and 225 being absent refutes, a clock artifact.  Every ILS marker sideband -- 400, 1300, 3000 Hz, both sides -- sits within 3 dB of a control probe at an empty frequency, where a 30%-depth AM sideband would stand 42 dB up. `testfiles/carrier_75000_bare.bin` |

## Mislabelled -- settled by the finer look, and none of it is a decoder

The confirmation pass measures at 244 Hz where the sweep had 212 kHz bins, and
**now reports what kind of thing it found**, which is what settled these.
Measured 2026-09-07 at a 0.25 s dwell with `--survey-confirm`.

### VHF band I television, 60-68 MHz: bare carriers, not television

| frequency | verdict | over floor | standing | width |
| --- | --- | --- | --- | --- |
| 62.4058 MHz | **a bare carrier** | 43.5 dB | 0.893 | 5.9 kHz |
| 66.6396 MHz | a modulated carrier | 38.4 dB | 0.736 | 2.9 kHz |
| 64.3267, 65.9497 | refuted, 0/6 | -- | -- | -- |

Two narrow carriers a few kilohertz wide, one of them with nothing on it at
all. A television channel is 7 MHz. **62.4 MHz is exactly 39 x 1.6 MHz**, the
receiver's fine comb, and the candidate sits 5.8 kHz off it -- close enough to
suspect and not close enough for the comb test to have flagged it.

### GNSS L1 / E1, 1595-1615 MHz: the receiver

One candidate, and the survey flagged it itself: `reference`. 1612.9297 MHz, a
modulated carrier at 29.7 dB with 0.387 standing still. The 1604.920 MHz peak
at 37.1 dB prominence from the reference sweep **did not reappear**, which
puts it with the mobile-satellite bursts rather than with anything standing.
Either way it is not GNSS, which is spread spectrum below the noise floor and
cannot appear as a peak at all.

### Fixed and mobile, 290-310 MHz: the receiver, bare carriers, and noise

Sixteen asked, eleven "confirmed" on prominence, and the kind takes them
apart:

| frequency | verdict | over floor | standing | envelope | flags |
| --- | --- | --- | --- | --- | --- |
| 302.3999 | a bare carrier | 48.8 dB | **0.969** | 0.130 | reference |
| 296.0010 | a bare carrier | 46.2 dB | 0.952 | 0.142 | reference |
| 295.3394 | a bare carrier | 39.3 dB | 0.875 | 0.257 | -- |
| 299.8596 | a bare carrier | 41.4 dB | 0.859 | 0.277 | -- |
| 303.6731 | a modulated carrier | 40.1 dB | 0.774 | 0.340 | -- |
| 297.7454 | a modulated carrier | 33.4 dB | 0.424 | 0.404 | reference |
| 292.9480, 307.3560, 303.1018, 308.9612, 300.6519 | **no carrier** | ~11 dB | ~0.006 | **0.52-0.54** | mostly reference |
| five more | refuted, 0/6 | -- | -- | -- | -- |

**302.4 MHz is exactly 21 x 14.4 MHz** and **296.0 MHz is exactly 185 x
1.6 MHz** -- the coarse and fine combs, both flagged. The row is the
instrument, four bare carriers with nothing riding them, and five readings
that are noise.

The five reading `no carrier` are worth their own attention and have a ticket:
they cleared the confirmation pass's 6 dB bar six looks out of six, and their
envelope variation of 0.52 to 0.54 is Rayleigh to two decimal places, which is
what an empty channel reads and what nothing else does.

| allocation | n | best | prom | why the label cannot be right |
| --- | --- | --- | --- | --- |
| VHF band I television | 7 | -33.4 | 27.0 | Second strongest group in the sweep, and analogue television has been off the air here for years. |
| GNSS L1 / E1 | 3 | -35.6 | **37.1** | GNSS is spread spectrum **below** the noise floor. Nothing can see it as a 37 dB peak. Whatever this is, it is not GNSS. |
| Marine VHF | 2 | -46.1 | 21.0 | 158.4989 MHz is in **all four** full-tuner sweeps, and **158.4 MHz is exactly 11 x 14.4 MHz** -- the receiver's coarse comb. 99 kHz off, inside one bin, so the comb test refuses at the sweep's resolution. |
| Fixed and mobile | 60 | -46.7 | 18.9 | Sixty candidates at modest prominence across 300 MHz is the shape of a comb, not of sixty transmitters. |

## Worth gating: the candidates

### AIS -- the best-shaped target left

The band plan already names it: "AIS at 161.975/162.025" inside Marine VHF.

- **Fits.** 25 kHz channel, 9600 bps GMSK. Trivially, at 2 MS/s.
- **Reuse.** GMSK demodulation exists in `gsm_dsp`; HDLC with NRZI and a
  CRC-16 is small; the payload is a position, the same shape as ADS-B's CPR
  pairing and its even/odd cache.
- **A message, not a waveform.** MMSI, position, course, speed. This is the
  criterion that earned GSM, LTE, TETRA and RDS their decoders here.
- **Is it there? Unmeasured, and the sweeps cannot say.** It appears in none
  of four full-tuner sweeps -- but AIS is TDMA with 26.7 ms slots and a ship
  reports every few seconds, which is exactly the burst scale a 0.12 s dwell
  step misses. Four misses is not four seconds of listening.

**Measured, 2026-09-07.** Ten seconds recorded at 162.0 MHz -- between the two
channels, so both sit in the window and neither lands on DC -- and the power
in each 25 kHz channel taken over forty 0.25 s windows:

| channel | mean | loudest window | windows 6 dB over the mean |
| --- | --- | --- | --- |
| AIS 1, 161.975 | -45.9 dB | -42.3 | 0 |
| AIS 2, 162.025 | -46.1 dB | -44.1 | 0 |
| control, 161.800 | -46.6 dB | -45.1 | 0 |
| control, 162.200 | -46.5 dB | -45.1 | 0 |

The two AIS channels are 0.5 and 0.7 dB over the controls in the mean, and no
window in any of the four stands 6 dB over its own mean. **There is no burst
structure and no signal.** A real AIS slot is 26.7 ms tens of decibels over
the floor, and ten seconds should catch several reports if any ship were in
range.

**The instrument was made to fire before the null was believed**, which is the
`probe-nbiot` rule: the same measurement over `testfiles/tetra_cc17.bin` puts
the known TETRA carrier at **-30.8 dB against controls at -47 to -52**, 16 to
21 dB of separation. So the flat reading above is the air and not the method.

The honest claim is **not receivable at this site with this antenna** -- a
telescopic whip indoors, at marine VHF, with a building between it and the
Tagus. It is not a claim about what is on the river. An outdoor antenna would
be a different measurement and this one does not settle it.

### Land mobile, 21 candidates, -40.9 dBFS, prominence 21.6 at 164.878 MHz

A real allocation with real traffic and no decoder. The question is whether it
is analogue FM voice -- a waveform, and nothing this program wants -- or
digital: DMR or dPMR, which would be a message. `signal_envelope_stats()` and
`signal_symbol_line()` can tell those apart now without transcribing anything,
which they could not when this band was last looked at.

**Measured, 2026-09-07, and the null is weak.** Ten seconds on 164.878 MHz put
the channel 0.4 dB over controls 200 kHz either side, with no window 6 dB over
its own mean -- nothing transmitting. But the sweep saw this at 21.6 dB of
prominence, and land mobile is push-to-talk: quiet most of the time by design.
Ten seconds of silence is exactly the evidence `.scratch/bursty-signals/`
warns is worth almost nothing. **Settling this needs a watch on the frequency,
not a capture of it.**

## What is not in the sweep and is worth remembering

`.scratch/lte-sib1/` -- System Information Block 1 does not fit. The cell is
50 resource blocks and 1.92 MS/s sees six, so the control message that locates
SIB1 cannot be assembled. The turbo code and CRC-24 were built anyway and are
kept, deliberately consumer-less.

Extended-cyclic-prefix LTE -- the cell search reports it, the broadcast
channel declines it, no commercial FDD cell uses it and there is no capture to
check against.
