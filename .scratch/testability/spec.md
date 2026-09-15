# Testability register

ADR-0012 says every decision the program makes must be reachable by a check
that needs no window, no receiver, and no person. This directory is the
register that decision promises: what is *not* yet reachable, why it matters,
and what it would take. The gap is meant to be measurable rather than felt.

## Reachable today

`make check` runs all of it, in about twenty seconds:

| Check | Covers |
| --- | --- |
| `check-sdr-dsp` | generic DSP core: I/Q conversion, DC removal, FFT, stats, peaks, carrier characterisation |
| `check-gsm-dsp` | ARFCN map, FCCH detector, SCH decode against two real captures |
| `check-adsb-dsp` | preamble, PPM demod, CRC-24, DF17 fields, CPR |
| `check-band-plan` | the allocation table and its lookups |
| `check-options` | the whole command line: every flag, every rejection, every implication |
| `check-survey` | the survey window's zoom, pan, clamp and sweep-target arithmetic |
| `check-survey-sweep` | the sweep's step plan, fold, and what measuring a candidate adds up to |
| `check-suspect` | candidates that look like the receiver: its reference comb, and the DC offset at a step centre |
| `check-calibration` | the lock gate, its robust statistics, and the state machine that fills its buffer |
| `check-layout` | GSM, ADS-B and survey view geometry |
| `check-geometry` | where a chart's plot sits, and which bar the pointer is over |
| `check-input` | which control a key press reaches |
| `check-scan` | the band scan's coverage and the channel it chooses |
| `check-adsb-analysis` | the tuned test, trace latching, the message log, the funnel |
| `check-gsm-continuity` | whether consecutive SCH decodes hang together |
| `check-acquisition` | the block slot, both its modes, and its shutdown |
| `check-pipelines` | assembled program over captures: GSM and Mode S decode, recording and its sidecar, the flags that reach them, and that a decode is repeatable |

## Not reachable

**Ticket 09: what a view says about itself, and what the input state
believes.** `resolved 2026-09-15`. `input_state_now()` in `sdrprobe.c` composes thirteen fields out of
per-view predicates that each read `struct app` from a `view_*.c` linking
raylib -- so none of them, and not the composition, is reachable. Ticket 07
made the input *precedence* checkable and left the step before it alone.

It is not hypothetical: a typing field its view does not report has its
characters drained by `chart_key_pressed()` before its own handler runs, and
the symptom is silence rather than a wrong answer. That has happened twice
already (the settings PPM field and the startup form, fixed in `25e0144`), and
`check-input` cannot see it because it takes `text_focus` as an input. Green
suite, dead keyboard. `src/view_input.h` is the fold now, over an enum of
typing surfaces that `check-input` sweeps; `src/srd_log.h` and
`sdrgui_message_log_row_at()` took the SRD log-row retune out of the drawing.
Doing it found a third instance of the same fault already shipped: the
waterfall's right-click menu and its signal report reached the routing
nowhere, so `q` quit the program from inside a modal report.

**Ticket 10: a marker click decides inside the drawing.** `ready-for-agent`.
`sdrgui_waterfall()` writes `out_clicked_marker_id` from inside its draw loop
and `draw_adsb()` and `draw_srd()` set `selected_log` from it -- so ticket 09's
"no draw function changes selection" holds for message logs and not for
markers. Split out rather than folded in: a log row is a band of constants, a
marker is a two-axis projection with order-dependent label placement over it.

Tickets 01-09 are resolved.

Tickets 01-08 are resolved; each records what moved, what it is checked by,
and what a mutation of it breaks.

Four of them found real faults on the way, which is the argument for the whole
exercise:

| # | Found |
| --- | --- |
| 06 | the SCH hyperframe wrap was reported as a jump, and so was any gap longer than one T1 tick |
| 07 | `q`, `h`, `s` and `c` fired from under a survey field that was being typed into -- a stray letter quit the program |
| 08 | (the premise, not a fault) the hit-test geometry was already pure; it only needed to leave a `.c` that links raylib |
| 04 | (an assumption, not a fault) Hold pins the last frame that *passed*, and a newer passing frame replaces it |

The register's own count: eight reachable, one outstanding.

Add a ticket here when a decision is added that a check cannot reach, rather
than leaving the gap implicit.

## Working rule

A ticket here is closed by moving the decision into a unit and checking it, not
by adding a screenshot. If a decision genuinely cannot leave the draw or input
function, say so on the ticket and mark it `wontfix` with the reason — an
honest exclusion is worth more than a check that asserts nothing.
