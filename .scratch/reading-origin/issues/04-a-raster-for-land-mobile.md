# 04 - Land mobile has no raster, and two measured signals are waiting on one

Status: needs-triage
Opened 2026-09-11, from the first live run of `SURVEY_SUSPECT_UNEXPLAINED`.

A 128-152 MHz sweep with a confirmation pass produced two rows that are almost
certainly the same signal, confirmed 6 of 6 at 13 dB:

```
confirm 148569336 new confirmed 13.0 6/6 3906 unresolved,unexplained 148499396
confirm 148315918 new confirmed 13.5 6/6 4883 unresolved,unexplained 148499383
```

Both measure to **148.4994 MHz**, and against an external transmitter on
**148.500000** that is **-604 Hz** -- inside one 977 Hz bin, with the
correction in force. So it reads exactly where a real transmitter on a round
land-mobile frequency belongs.

It comes back `unexplained` only because nothing proposed 148.500000 as a
nominal. `issues/02-*` put a raster on the VHF airband and on nothing else, so
`reading_origin_for()` was never offered a channel here and had to answer by
elimination.

## Why it is in scope

Land mobile has **no decoder**, which is `02`'s rule for where a raster may go:
an allocation with a decoder can be asked directly and a second statement of
its grid could disagree with the module that decodes it. Land mobile can be
asked by nothing, which is exactly where a table earns its place.

## Why this is triage and not ready

**The spacing has to be sourced, not guessed**, and that is the whole of the
work. The band plan's own rule is "an allocation, never an identification, and
a gap in preference to a guess", and its entries "name the band edges the QNAF
itself uses, to the kilohertz, rather than rounding to something tidier". A
raster invented from two observations at 148.4994 would be exactly the guess
that rule refuses -- and the observations would then be confirming the number
they produced, which is a round trip.

European land mobile VHF is commonly 12.5 kHz with 25 kHz legacy and 6.25 kHz
narrowband in places, and **which of those applies to 148-149 MHz in Portugal
is not established here**. ANACOM's QNAF is the source the rest of the table
uses and is where the answer has to come from.

Two things to get right once it is sourced:

- **The base, not just the spacing.** A base wrong by half a step puts every
  channel half a step out and reads as unexplained everywhere, which
  `check-band-plan` asserts against but only within the allocation's edges.
- **Resolvability.** `reading_raster_is_resolvable()` refuses a grid finer
  than twice the tolerance, so at a confirmation pass's 977 Hz a **6.25 kHz**
  raster is fine and anything under about 2 kHz is not. If the real answer is
  a 6.25 kHz grid, note that the chance of a coincidental match rises to
  2*977/6250 = **31%** -- which is why `02` restricted a raster to the
  external hypothesis only, and that restriction is load-bearing here.

## What would confirm it independently

The two rows are one signal seen twice, so they are one observation and not
two. A second, better one is cheap: a narrow sweep of 148-149 MHz with
`--survey-confirm` measures it at a finer bin, and `probe-signal` at that
offset says whether anything rides it. If it is a real land-mobile carrier it
should be modulated and intermittent; if it reads as a bare standing carrier
that is a different finding and the raster would be the wrong explanation.

Do that before transcribing anything.
