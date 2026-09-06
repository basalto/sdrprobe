# 05 - Suppress a found cell before searching for the next

Status: needs-triage

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
