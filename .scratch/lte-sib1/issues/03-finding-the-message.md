# 03 — PCFICH, PDCCH, and the resource blocks SIB1 sits in

Status: wontfix
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

## Comments

**2026-09-05 — wontfix, for the reason the spec now carries.** Not because the
work is hard: because the message cannot reach this receiver.

The control region this ticket is about occupies the first one to three symbols
**across the whole system bandwidth**, with its resource-element groups
interleaved over every resource block for frequency diversity. The receiver
sees six blocks of the cell's fifty -- 1.08 MHz of 9 MHz, twelve per cent -- and
a control channel element is nine groups that the interleaver has scattered the
length of the band. Not one can be assembled, so there is nothing for a search
space to search.

The search-space arithmetic this ticket asks to check is also smaller than it
looks, and worth writing down so nobody re-derives it: for the **common** space
the candidates do not depend on the cell identity at all. 36.213 section 9.1.1
puts `Y_k = 0` there, so the candidates are simply
`L * ((0 + m) mod floor(N_CCE / L)) + i`, four of them at aggregation level 4
and two at level 8. The cell identity enters only the UE-specific space, which
SIB1 never uses. The ticket's "pure arithmetic over the cell identity" was
wrong about which space it meant.

Reopen if a six-resource-block cell turns up. Nothing else changes this.
