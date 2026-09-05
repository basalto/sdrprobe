# 05 — A capture worth keeping, and a way to read it without a window

Status: needs-triage
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
