# 04 - NB-IoT: it fits, but is it there?

Status: wontfix

NB-IoT is the one member of the LTE family this receiver can capture whole:
one resource block, **180 kHz**, against 1.92 MS/s. Where SIB1 needs 4.5 to
9 MHz and is permanently out of reach, an MIB-NB is not.

**No code until the second gate is measured.** The `rf-environment` skill has
two, and NB-IoT passes the first arithmetically and has never been put through
the second. The band 28 ticket was written before that check and had to be
withdrawn (`.scratch/nr-cell-search/`); this one starts with the measurement.

## Where to start

The measurement, not the decoder.

An in-band deployment puts the carrier in one resource block of a host LTE
cell, an anchor at a fixed offset from the host's centre; a standalone one
sits in a re-farmed GSM channel, 200 kHz wide. Neither is visible in a 212 kHz
survey bin, and the operator here runs band 8 with GSM beside LTE -- which is
where a standalone carrier would be.

NPSS is the thing to look for and it needs no identity: it repeats **every
10 ms in subframe 5**, is the same sequence in every cell, and is eleven
symbols of a length-11 Zadoff-Chu. A correlation against it, folded at 10 ms,
answers "is it there" the way `probe-periodicity` answers it for LTE against
NR -- a peak far above its own floor at a phase that holds.

Only if that reports something does the rest follow: NSSS for the identity,
then NPBCH for the MIB-NB, which carries the operation mode, the frame number
and the access barring.

## What must be checkable

The gate measurement first, as a probe rather than a check -- it works on a
signal nothing here understands. A decoder gets checks when there is something
to decode.

## What this must not become

A decoder for a carrier that is not on air. If NPSS does not correlate
anywhere in band 8 or in the GSM channels beside it, this closes `wontfix`
with the number, and that is a good outcome -- it is the same conclusion DAB
reached and cost an afternoon rather than a week.

## Answer

Status: wontfix. It fits the receiver and **it is not on air here**, which is
what the ticket said would close it.

### The detector, and its positive control

`make probe-nbiot` correlates against the narrowband primary synchronisation
signal: 36.211 clause 10.2.7.1.1, a length-11 Zadoff-Chu with root u = 5, the
same in every cell so no identity is needed, on subcarriers 0 to 10 of the
anchor's resource block across symbols 3 to 13 of subframe 5. Non-coherent
across the eleven symbols -- each matched on its own 128 samples and the
magnitudes summed -- because a coherent match over the whole 785 us would need
the tuning error under a few hundred hertz, which no receiver here promises.

**`--self-test` is the half that makes the null worth anything.** A negative
result from a detector nobody has seen fire is worth nothing, which is the
mistake this repository keeps finding in its own work. The sequence laid into
noise of the same amplitude comes back at:

```
  best 3.8646 at sample 1201        (laid at 1200)
  the peak stands 12.8 deviations above the floor
  one frame later: 4.0700 (105% of the peak)
```

### What the air says

Six band 8 carriers, chosen from a 925-960 MHz confirmed sweep by how close
their measured width sits to an anchor's 180 kHz, and by level:

```
                     width      peak      repeat at 10 ms
936.773 MHz        181.6 kHz    3.3 sd        66%
939.554 MHz        186.5 kHz    3.4 sd        50%
947.563 MHz        203.1 kHz    4.1 sd        79%
936.619 MHz        162.1 kHz    2.9 sd        70%
937.978 MHz        163.1 kHz    3.9 sd        54%
950.169 MHz        247.1 kHz    5.1 sd        66%

self-test (NPSS present)                     12.8 sd       105%
lte_b20_pci28.bin (no NPSS)                   4.0 sd        68%
```

Every one of them is indistinguishable from the LTE capture that certainly has
no NPSS in it, and none is anywhere near the positive control. **The repeat is
the figure that decides**: a synchronisation signal comes back at 10 ms
essentially whole, and nothing here reaches 80%.

### What this does not say

Six carriers of thirty-three, picked by width. An **in-band** anchor sits in
one resource block of a host LTE cell, and the standard keeps it out of the
middle of the carrier -- so it is outside the central 1.08 MHz this receiver
sees when tuned to a cell, and testing for one means tuning to each candidate
resource block offset in turn. That was not done, and this ticket does not
claim it was.

What is claimed is narrower and is what the ticket asked for: no standalone
anchor at the carriers in band 8 whose width best matches one. If NB-IoT is
here it is in-band, and finding it is a search rather than a measurement.

`make probe-nbiot FILE_NBIOT=--self-test` keeps the detector honest for
whoever runs that search.
