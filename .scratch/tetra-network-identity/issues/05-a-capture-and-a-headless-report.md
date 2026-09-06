# 05 — A capture worth keeping, and a way to read it without a window

Status: resolved
Blocked by: 04

Everything above is checked against a capture in `captures/`, which is
gitignored -- so none of it is reachable by `make check` on a fresh clone. This
is the ticket that fixes that, and it is the one ADR-0012 cares about.

- A `testfiles/tetra_<detail>.bin` with its sidecar, short enough to commit and
  long enough to carry several synchronisation bursts. `fm_rds_tsf.bin` is the
  precedent for choosing the length by measuring where it stops working rather
  than by rounding up: three seconds rather than two, because two named the
  station only sometimes.
- `--headless --technology tetra --decode --once`, printing what was read, so
  the answer is reachable from a script.
- The invariant `check-pipelines` will assert, chosen the way the GSM captures'
  were: something the capture must keep saying.

## What this must not become

A capture chosen because it happened to work. The GSM set has three because the
BCC picks the training sequence, so one capture hides a hardcoded value; if
anything here is per-network, one capture hides it the same way.

## Comments

**2026-09-06 — resolved. `testfiles/tetra_cc17.bin` and `tetra_cc32.bin`, a
headless report, and `check-pipelines` asserting both.**

### Two captures, and the second is the point

The ticket warned against a capture chosen because it happened to work, and
against one capture hiding anything per-network. It hides exactly one thing
here, and it is the important one: **the broadcast channel is scrambled with
the network's own colour code**, read out of the synchronization block first.
A decoder that hardcoded a colour code would read one capture perfectly and
fail the other, and one capture could never show it.

So there are two, from two cells:

| capture | colour code | location area |
| --- | --- | --- |
| `tetra_cc17.bin` | 17 | 4375 |
| `tetra_cc32.bin` | 32 | 4658 |

That is the same argument that gives the GSM set three captures for three BCCs,
and `check-pipelines` asserts both identities and that no block fails its
parity.

### Length, chosen by measuring

Half a second, 2 MB each. There is no cliff to find here -- decodes are linear
in length, because this base station sends a synchronization burst in *every*
timeslot, about seventy a second, so 130 ms already carries nine. Half a second
carries roughly thirty-five and the assertion has ample margin. That density is
this deployment's doing and not a property of TETRA; a network sending one
burst per multiframe would need a capture twenty times longer.

### A recording mistake worth keeping

The second capture was first taken at 392.84 MHz, a round number off the
survey's carrier list. Half its blocks failed. The cause was not the decoder:
the actual carrier is 33.5 kHz away, the coarse estimator pinned itself at the
edge of its search range, and blocks alternated between finding it and not.

Re-recorded at 392.8735 the offset is under 200 Hz and every block decodes. The
sidecar carries the reason, because the trap generalises: **TETRA channels are
25 kHz apart, so a coarse search wide enough to cover a large tuning error is
also wide enough to select the neighbouring channel.** Widening the search
until the capture worked would have hidden that and left a decoder that
sometimes reads the wrong carrier.

### The report

`--headless --technology tetra --decode --once` prints one line per identity
rather than one per burst -- the identity repeats seventy times a second and is
the same every time:

```
TETRA  MCC 268  MNC 3  colour 17  LA 4375  (4 burst(s), 4 block(s), 4 broadcast)
```

and, when nothing decodes, says so with the lock figure, so a failure is
distinguishable from an empty channel:

```
TETRA  lock 0.22  0 burst(s), none with the parity checking
```
