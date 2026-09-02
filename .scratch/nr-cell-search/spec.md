# 5G NR cell search on n28

## Why

Scanning band 28 finds no LTE cell, and the band is not empty. A survey of
755-825 MHz on 2026-09-02 found ten candidates across the 700 MHz downlink,
the strongest at 774.2 MHz, -41.3 dBFS with 6.5 dB of prominence -- about
10 dB below the band 20 carriers measured in the same sweep as a control, and
clustered roughly as three 10 MHz blocks at 758, 768 and 778 would look.
`--lte-scan 28` over the same spectrum: `found 0 dropped 1`.

Portugal cleared 700 MHz in 2020 and n28 is where 5G went. So the likely
occupant is NR, which this program cannot see -- but "likely" is the whole
problem. The survey proves occupancy and says nothing about technology, and a
genuinely weak LTE carrier would leave the same evidence. There is currently
no measurement in this repository that can tell the two apart, which is the
gap this effort closes.

## Why the LTE search cannot be stretched to cover it

Every layer differs, and none of it is a parameter:

- **The sequence.** LTE's primary signal is a length-63 Zadoff-Chu, roots 25,
  29 and 34. NR's is a length-127 BPSK m-sequence with three cyclic shifts.
  Correlating one against the other yields nothing; this is a new detector,
  not a new constant.
- **The secondary signal.** LTE interleaves two length-31 m-sequences over 168
  values of N_ID_1. NR uses a length-127 Gold sequence over 336, so 1008
  identities rather than 504. `PCI = 3*N_ID_1 + N_ID_2` survives, and little
  else does.
- **Where it sits.** LTE's primary signal is on the 62 subcarriers either side
  of the carrier centre, twice a frame. NR's synchronisation block is not at
  the carrier centre at all: it sits on the GSCN raster and repeats every
  20 ms for initial access.
- **The raster.** The LTE scan walks the 100 kHz EARFCN grid. GSCN points
  below 3 GHz are 1.2 MHz apart, so an NR sweep of the same spectrum is a
  different and much shorter walk.

## What is reachable, and what is not

The receiver decides the scope, and it decides it narrowly.

An RTL-SDR runs about 2.4 MS/s before it starts dropping samples. NR's
primary and secondary signals occupy 127 subcarriers each; at 15 kHz spacing
that is 1.905 MHz, which fits the 1.92 MS/s grid this program already runs LTE
on (ADR-0014) with almost nothing to spare. The synchronisation block as a
whole is 240 subcarriers -- 3.6 MHz at 15 kHz spacing -- so the broadcast
channel and the Master Information Block behind it are out of reach on this
hardware no matter how the code is written.

**So the deliverable is an identity and nothing above it**: a physical cell
identity, its GSCN, and a correlation strong enough to believe. That is enough
to answer the question that prompted this -- what is transmitting in 758-788
MHz -- and it is the honest limit of the dongle.

At 30 kHz subcarrier spacing even the identity is out of reach (127 * 30 kHz =
3.81 MHz). Which spacing n28 actually uses is therefore not a detail: it
decides whether this effort is possible at all, which is why it is ticket 01.

## Discipline

Every constant here -- the generator polynomials, the initial register states,
the cyclic shifts, the subcarrier offsets, the m0/m1 formulas -- must be
checked against 38.211 and against a reference implementation before it is
believed, and the check must be something a wrong-but-self-consistent
implementation cannot pass. See `.claude/skills/dsp-validation/SKILL.md`; the
LTE side of this repository spent a day on exactly that mistake, and NR's
three cyclic shifts of one m-sequence are the same kind of trap as LTE's two
conjugate roots.
