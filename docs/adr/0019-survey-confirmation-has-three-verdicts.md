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

## Amendment: a measured emptiness overrides all three

A verdict answers *was it there when I looked*, and all three assume something
was there to look at. A closer pass can also establish that a frequency holds
no transmission at all, and that answer overrides the verdict rather than
competing with it.

Five frequencies in one 290-310 MHz sweep were **confirmed six looks out of
six** while reading no standing carrier, under one per cent of the channel
standing still, and an envelope variation of 0.520 to 0.539 -- against
Rayleigh's 0.5227, the coefficient of variation of a complex Gaussian's
magnitude, which depends on nothing at all. Five independent frequencies
landing there to two decimals are five measurements of noise. A prominence bar
is cleared by noise structure every time it is offered, so counting looks
cannot separate them and more looks cannot help; raising the bar is the
expensive direction this ADR already rejects.

So a candidate carrying `SURVEY_SUSPECT_NO_CARRIER` -- set when **both** an
independent line measurement and the envelope's shape agree there is nothing
there -- does not enter a receiving setup's history, whichever verdict the
count of looks produced. In particular it overrides *intermittent*, which the
Consequences above otherwise admit: noise that came and went is still noise,
and a frequency with no carrier is not a bursty carrier.

The verdict itself is unchanged and still reported. This bars the frequency
from the history, it does not reclassify the observation -- the same
separation the rest of this ADR rests on.

What the flag claims is "indistinguishable from noise", not "is noise": a real
spread signal buried at its own noise floor reads the same, and nothing here
can tell those apart (ADR-0015). `docs/receiver-artifacts.md` has the
algorithm and the thresholds.