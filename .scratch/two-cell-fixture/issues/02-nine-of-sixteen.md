# 02 - Nine of sixteen, and the two cells that come back as none

Status: resolved, 2026-09-09. The zero-cell case was a real defect and is
fixed; the rate rose with it, from 9 of 16 to 11, and no constant was moved.
Opened by ticket 01, which built the measure.

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


## Resolved, 2026-09-09

**The gate was on the wrong thing.** `lte_cell_search` admits the strongest
root's cell on the primary and secondary sequences alone -- there is no
coherence test anywhere in it -- and every other caller in this program takes
that answer. `lte_cell_search_all` applied a further gate to *that same cell*
as well as to its neighbours, so it could report fewer cells than the path it
generalises. That is the whole defect, and it needed no threshold changed:

```c
if (r != best.n_id_2 &&
    (!lte_port_coherence(...) || coherence[0] < LTE_PORT_COHERENCE_PRESENT))
    continue;
```

The gate's own comment says what it is for -- a second identity invented out
of a root that did not match -- and `test_one_cell_stays_one` is its check.
Neither is about the cell the carrier is *about*.

### Measured, both implementations over the same bytes

`probe-lte-chain` now prints what the multi-cell path made of each block
beside the single-cell one, which is the line that did not exist and is why
this hid: `--lte-chain` refuses a file, so `lte_cell_search_all` was reachable
only with a receiver attached.

| capture | before | after |
| --- | --- | --- |
| `lte_b20_pci28.bin` | 13 sightings / 12 blocks, silent 0 | 13 / 12, silent 0 |
| `lte_b8_pci330_4port.bin` | 6 sightings / 6 blocks, **silent 1** | 7 / 6, silent 0 |
| a 4 s recording of EARFCN 3625 | 9 / 12, **silent 3**, most 1 at once | 12 / 12, silent 1, **most 2** |

The single-cell search refuses 0, 0 and 1 block of those three. So before the
change the multi-cell path was losing a cell the program otherwise had, on a
**committed test capture**, and on 3625 it never saw two cells at once on a
carrier that demonstrably holds two.

Synthetic population, 40 draws:

| level | 0 cells before / after | 2 cells before / after |
| --- | --- | --- |
| 1.00 | 2 / **0** | 19 / **28** |
| 0.92 | 1 / **0** | 20 / **26** |
| 0.85 | 2 / **0** | 15 / **18** |
| 0.45 | 0 / 0 | 1 / 1 |

Every pair reported is still the right pair. The limit at -6.9 dB does not
move, which is correct: a cell 6.9 dB down is a neighbour and is still gated.

On air, a 45 s walk of EARFCN 3625 before and after: PCI 190 went from 62
looks and 57 decoded broadcast messages to 190 looks and 186 decoded, still
`confirmed` by its own CRC -- which repetition cannot manufacture, so it is
the second cell being recovered rather than reported more often. The tail of
single-sighting identities grew with it, and those are what `lte_confirm`'s
`spurious` verdict is for; on the captures, where the comparison is exact,
the extra sightings are the real second cell.

### What is checked now

- `check_real_capture` asserts on **both** real captures that the multi-cell
  search is never empty on a block where the single-cell search finds a cell,
  and that it sees at least as many. Against the old `lte_dsp.c` that check
  fails on `lte_b8_pci330_4port.bin`, naming the block -- so the defect is
  reachable from a capture now, which was the gap.
- The population check pins `none == 0` outright, and the rate against a
  floor of 6 with 11 measured.

### Not done, deliberately

A recording of EARFCN 3625 was made and is **not** committed. Four seconds of
it holds PCI 402 in every block and 406 in one, and not the 190/402 pair the
fixture imitates -- 190 was strong in a 45 s live walk and absent from this
window. 15.5 MB for one block showing two identities is not worth it, and
`lte_b8_pci330_4port.bin` already demonstrates both the defect and the fix at
3 MB. A capture that reliably holds the pair would still be worth having;
this is not it.
