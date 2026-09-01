# Testability register

ADR-0012 says every decision the program makes must be reachable by a check
that needs no window, no receiver, and no person. This directory is the
register that decision promises: what is *not* yet reachable, why it matters,
and what it would take. The gap is meant to be measurable rather than felt.

## Reachable today

`make check` runs all of it, in about 18 seconds:

| Check | Covers |
| --- | --- |
| `check-sdr-dsp` | generic DSP core: I/Q conversion, DC removal, FFT, stats, peaks, carrier characterisation |
| `check-gsm-dsp` | ARFCN map, FCCH detector, SCH decode against two real captures |
| `check-adsb-dsp` | preamble, PPM demod, CRC-24, DF17 fields, CPR |
| `check-band-plan` | the allocation table and its lookups |
| `check-options` | the whole command line: every flag, every rejection, every implication |
| `check-survey` | the survey window's zoom, pan, clamp and sweep-target arithmetic |
| `check-calibration` | the lock gate, its robust statistics, and the mixed-source hazard |
| `check-layout` | GSM, ADS-B and survey view geometry |
| `check-pipelines` | assembled program over captures: GSM and Mode S decode, recording and its sidecar, the flags that reach them, and that a decode is repeatable |

## Not reachable

One ticket each in `issues/`. They are ordered by what a bug there would cost
an operator, not by how easy the extraction is.

| # | Area | Why it matters |
| --- | --- | --- |
| 01 | survey sweep state machine | decides what is measured and what is reported as a signal |
| 02 | acquisition slot semantics | decides whether the program sees the signal at all |
| 03 | scan step machine and BCCH choice | picks the channel the operator then studies |
| 04 | ADS-B analysis latching | decides which frame the charts are describing |
| 05 | calibration state machine | decides when a correction is applied to every frequency shown |
| 06 | GSM SCH continuity | decides whether a decode is called trustworthy |
| 07 | frame-loop input precedence | decides which control a key press reaches |
| 08 | component hit tests | decides what the pointer selected |

## Working rule

A ticket here is closed by moving the decision into a unit and checking it, not
by adding a screenshot. If a decision genuinely cannot leave the draw or input
function, say so on the ticket and mark it `wontfix` with the reason — an
honest exclusion is worth more than a check that asserts nothing.
