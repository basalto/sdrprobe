# Band Survey (Scope view 5)

Status: ready-for-agent

## Problem

Every view in the Scope tab shows the 2 MHz the receiver is tuned to. To find
out what is on the air you have to already know where to look: type a frequency
into Settings, look, type another. The GSM channel power scan sweeps, but only
across ARFCN 1-124 on a grid that assumes GSM.

There is no way to ask the question an operator actually starts with — what is
out there, and which of it is worth pointing this program at?

## Decision

A fifth Scope view: sweep an operator-chosen frequency range, chart the power
across it, mark where activity stands above the local noise floor, and let the
cursor pick one of those peaks. Selecting a peak retunes to it, measures it for
a couple of seconds, and reports what the samples support — plus the name of
the allocation the frequency falls in, marked as a lookup rather than a
detection.

```
┌─ 5 survey ─────────────────────────────────────┐
│ Range [24 MHz] to [1766 MHz]  [Sweep]  step 1.8│
├────────────────────────────────────────────────┤
│  power across the swept range, peaks marked    │
│     │     █        ██        █   █             │
│  ───┼─────█────────██────────█───█──────────── │
├─────────────────────────┬──────────────────────┤
│ peaks found (18)        │ selected 943.164 MHz │
│   98.100 MHz  -38 dBFS  │  bandwidth  184 kHz  │
│  943.164 MHz  -44 dBFS ◀│  duty  continuous    │
│ 1090.000 MHz  -61 dBFS  │  GSM 900 downlink    │
└─────────────────────────┴──────────────────────┘
```

Decisions taken at triage:

1. **An operator-set range, defaulting to the tuner's full span** (24-1766 MHz
   for the R820T). A full sweep is ~969 steps at 0.17 s — about two and a half
   minutes; a band is seconds. A survey earns its keep by finding what you did
   not expect, so it must not be limited to a list of bands someone chose in
   advance.
2. **Measurements plus a band-plan name.** Everything the Probe context can
   honestly measure — peak power, occupied bandwidth, prominence over the local
   floor, carrier offset, duty, frequency stability — and the allocation the
   frequency falls in, labelled as a frequency lookup. No inferred modulation:
   a guess that is confidently wrong is worse here than no guess.
3. **Selection keeps the survey on screen.** The peak list stays, the detail
   panel fills in, and a button hands off to a decoder when the band plan names
   one. Jumping to the spectrum view would throw away the list you are
   comparing against.

## What may and may not be claimed

This is the Probe context: it ends at signal statistics (`CONTEXT.md`). The
survey may say a carrier is 184 kHz wide, continuous, and 26 dB above its local
floor. It may say 943.164 MHz falls inside the GSM 900 downlink allocation. It
may not say it *is* GSM — nothing has been demodulated. The UI carries that
distinction in words, not only in the code:

> band plan: GSM 900 downlink (ARFCN 40)
>   └ a frequency lookup, not a detection

The handoff button is honest for the same reason: it says *Inspect in Decode ▸
GSM*, which is an invitation to go and find out, not an assertion.

## Vocabulary

`CONTEXT.md`'s **Channel power scan** entry currently lists "survey" on its
_Avoid_ line. That was right when the channel scan was the only sweep; it is
now the wrong word to forbid, because the two things are genuinely different —
one walks a technology's channel grid, the other walks an arbitrary frequency
range and knows nothing about channels. The entry is amended to point at the
new term rather than forbid it, and these are added:

- **Band survey** — a retuning sweep across an operator-chosen frequency range
  that charts received power and marks where activity stands above the local
  noise floor. _Avoid_: Spectrum analyser, scanner.
- **Survey step** — one retune in that sweep, contributing the usable middle of
  its sampled span. _Avoid_: Sample block, channel.
- **Signal candidate** — a peak standing far enough above its local floor to be
  worth looking at, before anything is known about what it carries. _Avoid_:
  Signal, transmitter, detection.
- **Occupied bandwidth** — the width of a candidate between the points where it
  falls a stated number of dB below its peak. _Avoid_: Channel width, baud.
- **Band plan** — a static table mapping frequency ranges to the service
  allocated there; a lookup that says what a frequency is *for*, never what a
  signal *is*. _Avoid_: Identification, classification, detection.

## Constraints

- Peak finding and characterisation are technology-independent, so they belong
  in `sdr_dsp.c` and stay hardware-free and `-lm`-only (ADR-0001, ADR-0003).
- The band plan is a lookup table, not DSP and not GUI: its own file, its own
  check.
- The chart is an `sdrgui_` component and ships with its own hit test from the
  start — `sdrgui_scan_chart_channel_at()` exists because that lesson was
  learned the expensive way.
- Layout is a pure function pinned by `make check-layout`.
- The sweep retunes the receiver, so it must restore the tuning it started
  with, and it cannot run in file mode. Both are true of the GSM scan already.

## Tickets

See `issues/`. Implement in phase order: 01 → 02 → 03 → 04 → 05 → 06.

## Comments

**The two sweep buttons became one.** The spec has "Sweep" and "Sweep region"
as separate controls, and they were built that way. In use they turned out to
do the same thing except in the single case where the view had been narrowed,
which left the operator holding a distinction to use the pair correctly. Sweep
now sweeps whatever the chart is showing -- the fields when zoomed out, the
window when zoomed in -- and typing a range re-anchors the window on it so the
two can never disagree. "Reset zoom" backs out of a narrowed sweep by restoring
a snapshot of the survey it replaced.

**The window arithmetic moved to `src/survey_window.h`** and is checked by
`make check-survey`. Two bugs in it reached the operator first: zoom did
nothing before the first sweep (the window had no extent until one existed),
and the first sweep ignored a selected region (the narrowing test required a
completed sweep). Both were arithmetic, and neither needed hardware to catch.
