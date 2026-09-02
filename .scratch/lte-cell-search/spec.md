# LTE: find the cell, then read what it broadcasts

The repo decodes two technologies. GSM goes carrier -> FCCH tone -> SCH
(identity and clock) -> BCCH (what the cell says). Mode S goes preamble ->
bits -> message. LTE has the same shape and none of the code:

| GSM | LTE |
| --- | --- |
| FCCH pure tone | PSS, a Zadoff-Chu sequence on the central 62 subcarriers |
| SCH -> BSIC + frame number | SSS -> PCI, half-frame, cyclic-prefix length |
| BCCH -> System Information | PBCH -> Master Information Block |

## Scope

Cell search and the MIB. Settled with the operator on 2026-09-01:

- **Depth**: PSS -> N_ID_2, symbol timing, carrier frequency offset; SSS ->
  PCI, frame timing, CP length; PBCH -> MIB (downlink bandwidth, PHICH
  configuration, SFN, antenna-port count).
- **Sample rate**: 1.92 MS/s, the LTE grid itself (128 x 15 kHz). LTE captures
  are recorded at that rate rather than the 2 MS/s house rate, so no resampler
  sits ahead of the correlator. `--sample-rate 1.92M`.
- **Band**: 20 (791-821 MHz downlink) first.

## Out of scope, and why

**SIB1 and everything above it.** SIB1 rides PDSCH, scheduled by PDCCH, spread
across the cell's whole bandwidth -- 10 or 20 MHz for a typical band 20
carrier. An RTL-SDR sampling at 1.92 MS/s sees 1.08 MHz of it. This is a
hardware limit, not a missing function: no amount of code reads SIB1 from these
samples. PSS, SSS and PBCH all live inside the central 1.08 MHz by design,
exactly so a handset can find a cell before it knows the bandwidth, and that is
what makes this scope reachable at all.

**Uplink.** Nothing here demodulates a handset.

## What must be true when it is done

- `make check-lte-dsp` and `make check-lte-mib`: no window, no receiver, nobody
  watching (ADR-0012).
- A real capture in `testfiles/`, recorded from a live band 20 cell, whose PCI
  and MIB the checks pin -- the LTE equivalent of the three GSM captures'
  BSICs. A synthetic round trip proves the code agrees with itself; only real
  air proves it agrees with the standard.
- `./sdrprobe --file testfiles/lte_<band>_<detail>.bin --headless
  --technology lte --decode --once` prints the cell and its MIB.

## Where it got to

**Cell search: done, and working on air.** `src/lte_dsp.{c,h}`,
`make check-lte-dsp`, 521 checks, including the identity of a live band 20 cell
in `testfiles/lte_b20_pci32.bin`. Three live carriers read cells 32, 160 and
406, each the same in every block and each cross-checked by a second,
independent detector.

**The Master Information Block: written and checked, not yet read off air.**
`src/lte_mib.{c,h}`, `make check-lte-mib`, 289 checks; it recovers a
synthesised broadcast channel exactly for one, two and four antenna ports, and
survives noise equal to the signal. Against live captures nothing decodes,
because the cell-specific reference signals do not lock and so there is no
channel estimate behind the soft bits. `issues/05` says what is known and what
would settle it.

The one thing worth carrying forward from building this is in `issues/04`: a
round trip cannot check a convention that both directions share, and the fault
it hid — a conjugated Zadoff-Chu sequence, which swaps two of the three roots
and hides the third — produced a detector that looked entirely healthy.

## Since

`issues/06-finding-a-cell-without-flags.md`: the view could only be used by
restarting with `--earfcn`, so it grew a band selector, a scan, and the ability
to take the receiver to 1.92 MS/s and give it back. `issues/07-analysis-charts.md`:
the three measurements behind the numbers.

## Tickets

- `issues/01-cell-search.md` -- PSS, SSS, PCI, timing, CP length
- `issues/02-pbch-mib.md` -- channel estimate, PBCH extraction, MIB decode
- `issues/03-capture-and-view.md` -- a real capture, the CLI, the view
- `issues/04-the-conjugated-primary-sequence.md` -- the fault, and why no
  synthetic check could have found it
- `issues/05-the-broadcast-channel-on-air.md` -- what is still open
- `issues/06-finding-a-cell-without-flags.md` -- the band selector and scan
- `issues/07-analysis-charts.md` -- the charts behind the numbers
