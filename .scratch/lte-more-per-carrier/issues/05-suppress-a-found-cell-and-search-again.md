# 05 - Suppress a found cell before searching for the next

Status: ready-for-agent

`lte_cell_search_all()` reports identities that are not cells.
`src/lte_confirm.h` labels them correctly -- `unread`, seen repeatedly and
never decoded -- but labelling is downstream of the problem. On EARFCN 3625,
**PCI 410 is reported in 59 blocks of 364 and decodes nothing**, alongside two
cells that decode routinely.

They are not chance. The search mistakes something about the strongest cell
for a second one and makes the same mistake every block, which is why no count
of sightings can reject them and why the broadcast channel had to.

## Where to start

The likely mechanism, and it is a guess until measured: a strong cell's
primary sequence has sidelobes, and its secondary sequence correlates with
another root's candidate set, because the secondary sequences of different
N_ID_2 are the same two m-sequences at different shifts. So a root that
carries nothing still finds a peak at the strong cell's timing and a
plausible identity to go with it.

If that is right, the fix is the one this ticket is named for and it is what
a scanner does: **take the strongest cell out of the samples and search what
is left**. Subtract its primary and secondary sequences at the timing and
channel already measured, then re-run the detector. An artefact of the first
cell disappears with it; a real second cell does not.

That also reaches a case the current search cannot see at all -- two cells
sharing an N_ID_2 at different frame timings -- because after suppression the
same root is free to peak somewhere else.

## Measure the mechanism before building the fix

Cheap and worth doing first: for each `unread` identity, print where its
primary sequence peaked and compare with the confirmed cell's. If the timings
coincide, the sidelobe explanation holds. If they do not, this ticket is
aimed at the wrong thing and the real mechanism is elsewhere.

`probe-lte-chain` already prints the per-root peaks; what it does not print is
where each one landed.

## What must be checkable

- The synthetic two-cell buffer already exists (`build_two_cell_carrier`), as
  does the single-cell one that must keep reporting one cell. A suppression
  step must not lose the genuine second cell at 1.4 dB down.
- A synthetic carrier with **one** cell must yield no second identity at all
  once suppression is in, which is the whole point -- today it relies on the
  reference-coherence gate to reject them, and that gate lets 410 through on
  air.

## What this must not become

An interference canceller. Subtracting a known sequence at a measured channel
is arithmetic; estimating and removing a whole cell's transmission is a
different project, and the 1.4 dB limit recorded in ticket 01 is not what this
is for.

## The mechanism, measured

The ticket asked for this before anything was built, and the answer is
unambiguous. `--lte-chain` now prints `at`, the distance from the walked
cell's frame boundary to each neighbour's. Over a 15 s run on EARFCN 3625:

```
pci 410   20 blocks   offset +136 samples, min = max = +136
pci 406    5 blocks   offset +135 samples, min = max = +135
```

**Identical to the sample in every block.** A genuine neighbour is a different
site with its own propagation delay and a boundary estimated from a noisy
correlation; it cannot land on a fixed offset twenty times running. These are
a deterministic function of the cell that is really there.

And the number says what kind. One OFDM symbol at 1.92 MS/s is 137 samples --
128 of useful part and 9 of cyclic prefix -- so both sit **one symbol** from
the real cell's boundary. The identities are not random: PCI 402 is root 0,
410 is root 2 and 406 is root 1, so what is happening is another root's
reference correlating against the real cell's transmission one symbol along,
well enough to clear the gates.

The sidelobe explanation in the ticket is therefore confirmed and narrowed:
not a broad shoulder of the same root's peak, but a fixed one-symbol
displacement across roots. Suppressing the found cell removes what they are
correlating against, so it should take them with it.

## What is still to build

Subtracting the cell: read its primary sequence at the timing already
measured, take the complex gain by correlating, subtract `gain * reference`
from the samples, and search again. The obstacle is not the arithmetic, it is
that `lte_cell_search_all` takes `const float *` and a scratch copy of a block
is two megabytes -- this file allocates nothing on that scale today, and a
static buffer would make the search non-reentrant. That decision comes first.

A cheaper rule was considered and **rejected**: throwing away any candidate
within a symbol of an accepted cell. It would work here and it is a heuristic
fitted to one measurement -- co-channel cells in a synchronised network can
genuinely sit within a symbol of each other, and the rule would silently
discard exactly the neighbours this effort exists to find.

## What the cost is now

`make bench-dsp` measures `lte_cell_search_all` at **21.9 ms of a 68.3 ms
block**, against `lte_cell_search`'s 9.9, because it runs the integer sweep
and the secondary sequence for every root that detected. Suppression adds a
second search on top of that. Worth knowing before starting: this is the
budget the fix has to fit in, and it is already a third spent.
