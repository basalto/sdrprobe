# 03 — PCFICH, PDCCH, and the resource blocks SIB1 sits in

Status: needs-triage
Blocked by: 02

Where the MIB is at a fixed place a receiver can find from the synchronisation
signals alone, SIB1 has to be *located*: how many symbols the control region
takes (PCFICH), then a downlink control message whose parity is masked by
SI-RNTI (0xFFFF), then the resource blocks it points at.

The search is the work. The control message can be in any of several candidate
positions in the common search space, at several aggregation levels, and the
only way to know which is to try each and see whose parity checks out -- the
same shape as the MIB's four scrambling offsets, but wider.

SIB1 is easier than the general case in two ways worth exploiting: it is
always in subframe 5 of even frames, and it always uses DCI format 1A or 1C on
the common search space.

## What must be checkable

- Which candidate positions the common search space contains, at each
  aggregation level, for a given cell identity and subframe. That is pure
  arithmetic over the cell identity and is where an off-by-one hides.
- That a wrong SI-RNTI finds nothing, which is what stops a false positive
  being reported as a cell's identity.
