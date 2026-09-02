# 01 — Cell search: PSS, SSS, and what they pin down

Status: resolved
Blocked by: (none)

`src/lte_dsp.{c,h}`, the Probe-side plugin, and `make check-lte-dsp`.

- EARFCN -> downlink frequency and back, for the bands an RTL-SDR reaches.
- PSS: three Zadoff-Chu roots (25, 29, 34) on the central 62 subcarriers,
  detected by time-domain correlation at 1.92 MS/s. Gives N_ID_2, symbol
  timing, and a carrier frequency offset from the phase across the two halves
  of the sequence.
- SSS: the symbol before PSS, two interleaved length-31 m-sequences. Gives
  N_ID_1, and which half-frame the PSS was in.
- PCI = 3 * N_ID_1 + N_ID_2. Cyclic-prefix length from which spacing fits.

Checked against sequences this file generates itself, and against a synthesised
OFDM frame with noise and a deliberate frequency offset.

## Answer

Done. `src/lte_dsp.{c,h}` and `src/lte_gold.h`, `make check-lte-dsp`, 521
checks. On the live band 20 capture in `testfiles/lte_b20_pci28.bin` it reads
cell 32 (N_ID_1 10, N_ID_2 2), the normal cyclic prefix, and a frequency offset
of about +2.5 kHz, in every block; two more live carriers read cells 160 and
406.

Two things came out different from the plan:

- **The secondary sequence is detected differentially**, each subcarrier times
  the conjugate of its neighbour, rather than by dividing out a channel
  measured from the primary sequence. The planned method works on a synthesised
  frame and fails on air. See `04-the-conjugated-primary-sequence.md`.
- **The frequency offset is measured twice.** The primary sequence gives it to
  a few hundred hertz from 64 samples of lever; the reference signals of slot 1,
  read at symbol 0 and again at symbol 4, refine it over 548.
