# What a reading's frequency says, and the three things it cannot say yet

`src/reading_origin.h` landed with `.scratch/device-model/issues/11-*` on
2026-09-11. It measures one thing and measures it well: an uncalibrated
receiver *displaces* everything it hears by a known amount, a tone generated
from its own reference is displaced by nothing, and one subtraction separates
the two. On air, with the correction in force, it predicted the three known
comb families to 35, 406 and 836 Hz.

This directory is what ticket 11 deliberately did **not** build, plus one
pre-existing fault its flags made visible. None of it is speculative: each
ticket names a case that has been measured and is currently reported wrongly,
incompletely, or not at all.

## What is there today

`reading_origin.h` answers `RECEIVER` / `EXTERNAL` / `UNEXPLAINED` / `UNKNOWN`
for a reading against any nominal frequency a caller proposes.
`survey_suspect.h` is the only consumer, it proposes the two comb multiples
and nothing else, and it maps the answer onto two flags:

- `RECEIVER` -> `SURVEY_SUSPECT_CLOCK_COHERENT`, printed `clocked-here`;
- `UNEXPLAINED` (bare, separable, no comb, carrier present, not refuted) ->
  `SURVEY_SUSPECT_UNEXPLAINED`, printed `unexplained`;
- `EXTERNAL` -> **nothing at all**.

## The shape of what is missing

**Half the verdict has nowhere to go.** `EXTERNAL` is the answer that
*contradicts* a comb flag, which is the whole argument for having a second
kind of evidence, and it is discarded (issue 01).

**Only one kind of grid is ever proposed.** A comb multiple is a fact about
the receiver; a channel raster is a fact about a service, and the band plan --
which already says what every allocation is *for* -- carries none (issue 02).

**And one row lies about which frequency it is describing** (issue 03).

## What must stay true

ADR-0015 governs all three. These are statements about a **reading**, never
about a transmitter: "clocked here" is the kind of phrase that stops somebody
looking, and "not clocked here" means an oscillator that is not this one --
a hub, a monitor, a second dongle -- and not "a transmitter".

And the refusals stay refusals. A crystal error of zero is no verdict, a
raster finer than twice the tolerance names a channel by rounding rather than
by evidence, and a sweep too coarse to separate the hypotheses says UNKNOWN.
Every one of those is already checked and none of them may be softened to make
a flag appear more often.
