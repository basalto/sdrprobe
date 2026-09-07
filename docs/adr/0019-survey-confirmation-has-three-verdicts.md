# Survey confirmation has three verdicts

## Status

accepted

## Context and decision

A wide sweep gives each frequency only a brief observation. A closer
confirmation pass can find a carrier in every look, no looks, or only some
looks. Treating the last case as absence repeatedly rejected real bursty
signals: a pass over 1600-1670 MHz found four of seven signals in only part of
its six looks, and a binary verdict would have refuted all four.

Confirmation therefore has three first-class verdicts: **confirmed** when the
closer observation supports the sweep's claim, **refuted** when it contradicts
the claim, and **intermittent** when the carrier appears in only some looks.
Intermittent describes the carrier's observed presence independently of whether
the original claim was that it was new or missing. The number of successful
looks and total looks remains evidence beside the verdict rather than being
discarded after classification.

## Considered options

- **Binary confirmation** forces partial presence across an arbitrary
  threshold and either loses real bursty transmitters or overstates their
  consistency.
- **Store only the hit count** retains the evidence but makes every reader
  independently choose semantics, allowing the window, reports, and history to
  disagree about the same observation.

## Consequences

- Survey files and reports preserve both the verdict and its hit count.
- An intermittent new carrier may enter receiving setup history; otherwise
  every later sweep would rediscover and reject the same bursty transmitter.
- Intermittent remains distinct from a receiving setup's history status: one is
  a result of the closer observation, the other summarizes multiple sweeps.