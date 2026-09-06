# 03 - Say it on the Scan panel

Status: needs-triage

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
