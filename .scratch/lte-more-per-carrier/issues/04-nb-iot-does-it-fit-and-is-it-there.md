# 04 - NB-IoT: it fits, but is it there?

Status: needs-triage

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
