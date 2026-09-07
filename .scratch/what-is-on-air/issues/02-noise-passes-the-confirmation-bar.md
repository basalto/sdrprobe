# 02 - Noise clears the confirmation pass, six looks out of six

Status: ready-for-agent

A confirmed sweep of 290-310 MHz on 2026-09-07 returned five signals that the
pass confirmed 6/6 and that are not signals:

| frequency | prominence | over floor | standing share | envelope |
| --- | --- | --- | --- | --- |
| 292.9480 MHz | 12.0 dB | 11.9 | 0.006 | 0.533 |
| 307.3560 MHz | 12.6 dB | 11.9 | 0.007 | 0.537 |
| 303.1018 MHz | 11.1 dB | 10.8 | 0.008 | 0.525 |
| 308.9612 MHz | 13.8 dB | 11.3 | 0.005 | 0.520 |
| 300.6519 MHz | 12.5 dB | 11.0 | 0.005 | 0.539 |

Every one of them: no standing carrier, a standing share under one per cent,
and an envelope variation of 0.52 to 0.54.

**0.5227 is Rayleigh**, the coefficient of variation of a complex Gaussian's
magnitude. It depends on nothing -- not on level, not on gain, not on
bandwidth -- and it is what `signal_probe.h` records an empty channel reading.
Five independent frequencies landing on it to two decimal places are five
measurements of noise.

## Why the pass says confirmed

`SURVEY_CONFIRM_PROMINENCE_DB` is 6.0, deliberately lower than the sweep's own
bar, and the file says why: "being too strict here would refute real signals,
which is the more expensive error -- it teaches the history that a real
transmitter is noise". That reasoning is sound and this is its cost. A
prominence of 11 dB over a *local* floor is reachable by noise structure,
`survey_probe_threshold`'s own finding being that a search over thousands of
frequencies takes the largest of thousands of noise samples.

Six looks does not help, because noise is available in every look. The pass is
counting how often something cleared a bar, and noise clears it every time.

## What to do about it

**Not by raising the bar.** That is the expensive direction and the file
already argues it out.

The carrier measurement is a second, independent statistic and that is what
makes it useful here: `carrier_over_noise_db` is a line against the median of
33 single-frequency probes, and `SIGNAL_CARRIER_PRESENT_DB` is 15 -- set
because pure noise reliably reaches 8 to 14 dB over its own median. All five
of these read 10.8 to 11.9. They fail a threshold that was measured against
exactly this.

So a verdict of `confirmed` with **no standing carrier and an envelope at
Rayleigh** should not be recorded as a signal in the site history. Whether it
becomes a fourth verdict or a flag on the existing one is the design question:
`.scratch/bursty-signals/` added `intermittent` rather than overloading the
two it had, for reasons that apply again.

## What must be checkable

That the decision is a pure function of the two measurements, in
`check-survey-confirm` (ADR-0012). And the numbers above are the fixture: five
real readings, each of which must come out as noise, against the bare carrier
at 302.3999 MHz in the same sweep -- 48.8 dB and 0.969 standing still -- which
must not.

## What this must not become

A reason to distrust `intermittent`. A bursty transmitter caught in one look
of six is a real signal with a low duty; these are noise in all six. The two
look alike only if you count looks and nothing else, which is the whole point.
