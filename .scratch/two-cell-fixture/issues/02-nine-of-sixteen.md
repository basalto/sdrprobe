# 02 - Nine of sixteen, and the two cells that come back as none

Status: needs-triage
Opened by ticket 01, 2026-09-09, which built the measure.

`make probe-two-cell` builds forty carriers each holding PCI 190 and PCI 402
with different interfering traffic, and reports what `lte_cell_search_all`
makes of them:

| level | dB | 0 cells | 1 cell | 2 cells |
| --- | --- | --- | --- | --- |
| 1.00 | 0.00 | 2 | 18 | 19 |
| 0.92 | -0.72 | 1 | 18 | 20 |
| 0.85 | -1.41 | 2 | 23 | 15 |

Two questions, and the second is the one that should be answered first.

## Two cells reported as none

On 2 of 40 at equal power the search returns **zero** cells on a carrier
holding two strong ones. That is worse than returning one, and it is not a
detection failure: `pss_detect_scan` detects both roots, `cell_from_pss`
returns the right identity for both, and both are then rejected by

```c
if (!lte_port_coherence(...) || coherence[0] < LTE_PORT_COHERENCE_PRESENT)
    continue;
```

`probe-two-cell` prints `coh` per root, and on those carriers both sit just
under 0.55 -- each cell's reference signals are read through the other cell's
transmission, so a carrier with two equal cells depresses both. A gate whose
whole purpose is to reject *invented* identities is rejecting two real ones
that the primary and secondary sequences both agree on.

## Nine of sixteen

The rate the capability check now pins. Raising it is the obvious next thing
and it is a trap, so what has to be established first is which of these is
true:

- the gate is too tight for a carrier with two loaded cells, and there is a
  test that separates a real second cell from an invented one **without**
  depending on a level -- for instance the primary-sequence peak and the
  secondary margin already in `struct lte_cell`, which an invented identity
  does not have either; or
- the fixture is harsher than the air. Both cells transmit at 0.9 across
  +-50 subcarriers on every symbol, which is full load on both, and the real
  pair on EARFCN 3625 decode 119 and 16 messages respectively. Real cells are
  not fully loaded and do not share a site.

**Do not lower `LTE_PORT_COHERENCE_PRESENT` until that is settled.** Its
header comment says what it is protecting against and a single-cell buffer
duly reported two before it existed; `check-lte-dsp`'s
`test_one_cell_stays_one` is the check that would notice, and it is one
carrier, so it is weak evidence in exactly the way ticket 01's positive was.
Whatever is tried, the population harness is the measure and
`does-it-help` is the discipline.

## The evidence that is missing

There is no real two-cell capture in `testfiles/`. EARFCN 3625 carries the
pair this fixture imitates and is on air at this site; a recording of it would
be the only check here that a wrong convention cannot satisfy, which is the
argument `CLAUDE.md` makes for every other real capture in the set. It would
also settle the question above directly: if the gate passes both cells on air
and fails them on the fixture, the fixture is the thing that is wrong.
