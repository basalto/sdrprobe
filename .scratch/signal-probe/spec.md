# Tools that say what a signal is, not just that it is there

**Scan this frequency** answers six questions today: peak power, height above
the local floor, occupied bandwidth, duty, frequency stability, and which
allocation the frequency falls in. Every one is about *presence*. None of them
says what kind of thing is transmitting, and the last one is a band-plan
lookup that the panel itself labels "a frequency lookup, not a detection".

So a reader who clicks Scan learns that something is there and is left to
guess what. This is the tooling that would tell them.

## The measurement that started it

2026-09-06, a candidate at 75.000 MHz: -44.3 dBFS, 20.8 dB over its floor,
continuous 31/31 blocks, and a band plan reading "Aeronautical
radionavigation -- ILS markers". An ILS marker beacon is AM with a keyed 400,
1300 or 3000 Hz tone, so the question "is it one" is answerable.

It took **four attempts** to answer, and three of them were wrong in the same
way: the search for the carrier found the **receiver's own DC offset** at
exactly +0 Hz, which is the strongest thing in any capture, and then measured
its sidebands. The tell was a control at an empty frequency reporting a
*stronger* carrier than the occupied one.

Answered properly -- by tuning 100 kHz off so the carrier lands clear of DC --
the carrier stands 57.1 dB over its floor with **no sideband at any marker
tone**, all of them at the floor and indistinguishable from the empty control.
It is a bare unmodulated carrier on an exact round frequency: 25 MHz x 3, the
signature of a clock harmonic reaching the antenna rather than a beacon.

Every part of that is general, and every part of it was hand-rolled in a
scratch directory and thrown away. That is the argument for this effort.

## What already exists and is not reachable

The measurements are here. They are welded into places only one technology can
use them from:

| measurement | where it lives | what it is |
| --- | --- | --- |
| symbol rate from the squared magnitude | `tetra_dsp.c`, Oerder-Meyr | the *same statistic* that says a carrier is TETRA at all |
| a burst grid found from symbols alone | `tetra_burst_find()` | needs nothing transcribed, so it works before anybody knows the technology |
| folding at a period, cyclic-prefix autocorrelation | `scripts/signal_periodicity.c` | a `main()`, so nothing can call it |
| fourth-power coherence | `lte_port_coherence()`, and again in `lte_chain_probe.c` | two copies, both LTE-shaped |
| pilot coherence against a tone | `fm_dsp.c` | the general "is this a tone" question |
| carrier centre, floor, prominence, width | `sdr_dsp_characterise_carrier()` | already general, already used by Scan |

`tetra_burst_find()`'s own comment says it "works before anybody knows which
technology this is". That is a signal-identification tool sitting inside a
TETRA plugin.

## What other people's tools do

- **gr-inspector** (GNU Radio, KIT): an energy detector with automatic
  thresholding, a signal separator that down-mixes and decimates what it
  found, an **OFDM estimator** that recovers subcarrier spacing, symbol time,
  FFT length and cyclic-prefix length, and modulation classification from
  cyclostationary features. The shape worth copying is the pipeline: detect,
  isolate, then characterise.
- **Universal Radio Hacker**: automatic detection of ASK, FSK and PSK and
  their parameters, with heuristics stated plainly -- the noise level starts
  as the mean magnitude of the last 10% of samples, and FSK levels come from
  k-means on the rectangular signal.
- **inspectrum**: the manual counterpart -- derived amplitude, phase and
  frequency plots against a cursor, which is what an analyst reaches for when
  automation has no answer.

None of them is a dependency: ADR-0003 keeps the DSP hand-written and
self-contained. They are here as the map of what these tools normally are.

## The shape

One unit, `src/signal_probe.{c,h}`, technology-independent, no window and no
receiver, answering in order:

1. **Where is it, and is it a bare tone?** The question the 75 MHz case
   needed, and the one whose absence cost four attempts.
2. **Is there a symbol rate?** Oerder-Meyr, moved down out of `tetra_dsp`.
3. **Does it repeat at a period?** Folding, moved up out of the probe.
4. **Is it OFDM?** Cyclic-prefix autocorrelation, from the same probe.

Then a findings layer over them, the way `lte_findings.h` sits over the LTE
measurements: sentences with their numbers attached, and refusals where the
measurement cannot reach.

## What this must not become

A classifier that names a modulation from a threshold. Every technology here
was identified by *decoding* something it said, and a tool that guesses "QPSK"
from a fourth-power line is a different kind of claim from one that reads a
Master Information Block. These tools narrow what a reader should try next;
they do not conclude.
