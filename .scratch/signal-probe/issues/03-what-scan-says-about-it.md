# 03 - Say it on the Scan panel

Status: resolved

Blocked on 01 and 02.

The Scan panel answers six questions about presence and none about kind. With
the tools behind it, it can say what the measurements support -- and, as
importantly, what they refuse.

## Where to start

`lte_findings.h` is the pattern and it is worth copying deliberately:
sentences, each naming the measurement it rests on, each caveat sharing a line
with the claim it qualifies, and refusals kept rather than omitted. The LTE
version learned all three of those the hard way.

For a signal nobody has identified the refusals are most of the value:

- "a bare tone: 96% of the channel's energy is in one line, so there is
  nothing here to decode" -- which is the 75 MHz answer;
- "a symbol rate of 18.0 kBd" -- which narrows it enormously without naming a
  technology;
- "no symbol rate found, and this is 0.25 s of signal: too short for anything
  under 4 Bd" -- which says why the tool is silent rather than leaving a
  reader to assume it looked and found nothing.

## What must be checkable

The findings are a pure function of the measurements, so they are checkable
without a receiver exactly as `check-lte-findings` is. What is *not* checkable
is whether the panel reads well, and that needs a screenshot (CLAUDE.md).

## What this must not become

A verdict. "Probably TETRA" is a claim this program has no way to stand
behind; "18 kBd, 25 kHz wide, continuous" is a set of measurements that lets a
reader reach for the TETRA view themselves.

## Comments

**2026-09-06 — done.** `src/signal_findings.h`, over `signal_probe`'s
measurements, drawn on the candidate panel above the band plan.

The ordering is the argument. A reader who has already read "Aeronautical
radionavigation -- ILS markers" is reading everything after it as detail about
a beacon, so the measurement of the signal itself has to come first to have a
chance of being believed over the label. ADR-0015 says the allocation is a
lookup and not an identification; putting it second is what makes that true on
screen rather than only in a caption.

On air, both branches:

| frequency | what the panel says |
| --- | --- |
| 75.0005 MHz | a bare carrier, 42 dB over its floor / 87% of the channel stands still / nothing rides it: nothing here to decode |
| 100.2965 MHz | a modulated carrier, 56 dB over its floor / almost none of the channel stands still / something rides it; what, this cannot say / no symbol rate looked for: needs one channel |

75.000 MHz is the case the whole effort started from, and the panel now says
in three lines what took four attempts and a scratch directory to establish.
It sits under a band plan reading ILS markers.

`check-signal-findings` is 78 checks and needs no window; it asserts the
sentences, the refusals, and that every branch fits the panel's width.

### Three faults found by looking at it

**The measure path had no settle.** The sweep discards every block that
arrives before the tuner has moved -- a block already in the pipeline holds
the previous step's samples -- and measuring a candidate did not. So peak
power, prominence, bandwidth and duty had all been computed partly from
whatever the receiver was pointed at before, since the survey was written. A
spectrum *average* blurs one stale block among good ones well enough that
nobody ever saw it. A carrier measurement cannot blur it: the first live run
called the 75.000 MHz clock harmonic "a modulated carrier, 19 dB up, almost
none standing still", where the same signal recorded and measured offline
reads 40.7 dB and 87%. `survey_measure_settled()` is the fix and
`check-survey-sweep` asserts it.

**The panel's prose ran through its own buttons.** Adding four lines pushed
the band plan straight under "Scan this frequency". The text was positioned by
`y +=` between draw calls and so modelled nowhere, which is exactly what
`panel_rows.h` exists to prevent for the views with a table of fields -- this
panel's content is prose whose length depends on what was measured, so it
needs the rule at every line rather than a row capacity worked out once.
`survey_layout.h` now carries a `detail_text` rectangle, nothing may draw past
it, and `check-layout` asserts it clears the buttons at every window size.

**The first draft's sentences were too long and truncated.** They lost
"only 1% of the ch..." and "no symbol rate was looked for: that needs the
chann..." -- in both cases the half carrying the qualification, which is worse
than no caveat at all. The panel is about 430 px and the font about 8.3 px to
the character, so SIGNAL_FINDING_FIT is 48 and a check asserts every branch
against it.

### One hour, again, on the same lesson

The first live run's wrong answer was chased through the DC offset and the
search resolution before the settle turned up. Both had been ruled out by
recording the same frequency and measuring it offline, which gave the right
answer immediately -- and the difference between that and the live path *was
the whole finding*. Recording the control first would have found it in
minutes.
