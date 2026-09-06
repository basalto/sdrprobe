# 05 - Suppress a found cell before searching for the next

Status: wontfix

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

## Answer

Status: wontfix. Built, measured, reverted -- and the measurement that killed
it was already on this page before the code was written.

### Why it cannot work

The artefacts sit **+136 samples** from the real cell's frame boundary, which
is what the section above established. A primary sequence is **128 samples**.
So the symbol that would be suppressed and the window the artefact correlates
over share **no samples at all**:

```
suppressed   useful_start        .. useful_start + 128
artefact     useful_start + 136  .. useful_start + 264
overlap      0
```

One OFDM symbol at 1.92 MS/s is 137 samples, and +136 is the *next* symbol --
in subframe 0 that is slot 1 symbol 0, the one carrying the reference signals
and the broadcast channel. The false roots are not locking onto a sidelobe of
the primary sequence. They are locking onto the symbol after it.

That is a different fault from the one this ticket named, and removing it
would mean removing the cell's whole transmission -- which the ticket's own
"what this must not become" rules out, and rightly: that is interference
cancellation, not arithmetic on a known waveform.

### What it did when built anyway

Subtracting the primary sequence at its measured position, with the tuning
error's rotation applied to the reference, then re-running the detector on the
suppressed copy and keeping only roots that still detected. Over twenty
seconds on EARFCN 3625, against the same channel measured immediately after
reverting:

```
                 with suppression        without
pci 402          125 looks, 122 msgs     117 looks, 110 msgs
pci 190            4 looks,   0 msgs      64 looks,  40 msgs
pci 410           27 looks,   0 msgs      13 looks,   0 msgs
pci 406           15 looks,   0 msgs      31 looks,   0 msgs
parity           122 of 278              150 of 253
```

**Every artefact survived and a real cell was lost.** PCI 190 decodes forty
messages without it; with it, the second pass rejected the root it lives on
often enough to reduce it to four sightings and nothing read. That is the
worst possible trade and it is the one the arithmetic predicts: the filter
cannot see the artefacts, so all it can do is discard genuine cells that
happen to detect less strongly once the winner is gone.

The synthetic checks passed throughout, including the two-cell carrier -- at
1.4 dB apart with an 800-sample offset the second cell is nowhere near the
suppressed symbol, so the buffer could not show what the air did.

### What is left

Nothing to build here. The defence that works is the one already in place:
`src/lte_confirm.h` labels these `unread` -- seen repeatedly, never decoded --
and it does so for the right reason, that a broadcast channel scrambled with
an identity and checked by a CRC cannot be repeated into existence.

If anyone returns to this, the question to ask first is why a Zadoff-Chu root
correlates at 0.35 to 0.48 against an ordinary data symbol, because that is
the actual mechanism and it is still unexplained. The gates were tuned against
noise, and this is not noise.
