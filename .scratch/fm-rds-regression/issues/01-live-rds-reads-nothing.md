# 01 — Live RDS reads nothing, on every station

Status: ready-for-agent
Priority: next

Find out why, and make the answer reachable without a receiver.

## Where to start

Not in `rds.c` and not in `fm_dsp.c`. The capture proves both work; see the
spec for the run. Start at the seam between a live receiver and the chain:
block continuity first, then the frame loop, then tuning and level.

## Done when

- The cause is named, with the evidence that names it rather than a change
  that happened to help.
- A **live recording that reproduces it** is in `testfiles/` with a sidecar,
  and a check asserts what it should now read. Without that this can regress
  again in silence -- which is how it arrived.
- If the cause turns out to be a dropped block rather than a decode fault,
  say so in `docs/` where the block slot is described: a lossy slot is right
  for a display and wrong for an accumulator, and the FM view is both.

## What this must not become

A widened tolerance. The pilot's gate takes coherence and presence together
because either alone ranks stations backwards, and the block synchroniser
needs four syndromes in the offset order because one matches by chance about
once in two hundred tries (`rds_sync_odds_per_million()`). Loosening either
would make this report go away without making the decode right.
