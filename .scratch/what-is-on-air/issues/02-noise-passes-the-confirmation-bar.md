# 02 - Noise clears the confirmation pass, six looks out of six

Status: resolved

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

## Comments

**2026-09-07 — done.** A fourth suspicion flag rather than a fourth verdict.

`SURVEY_SUSPECT_NO_CARRIER` sits beside `reference`, `step-centre` and
`unresolved`, and that is the right home for three reasons: it composes with
presence instead of overloading it, so a frequency can be confirmed *and*
empty; the flag machinery is already carried through the candidate list, the
headless report and both saved-file writers; and the list already had a
marking convention to extend.

`survey_confirm_is_empty()` is the decision, a pure function of the two
measurements, and **both have to agree**. Either alone gets a case wrong that
the other catches, and both failures are on record: a pulsed transmission has
real energy and no standing carrier, so the carrier test alone calls Mode S
empty; a weak signal buried at its own noise floor reads Rayleigh, so the
envelope test alone calls it empty too.

The tolerance is 0.10 around Rayleigh, measured from both sides. The five
noise readings ran 0.520 to 0.539 -- at most 0.017 out. The nearest thing that
must *not* be caught is a GSM carrier at 0.792, 0.27 out, and every other
real signal measured here is further: LTE 1.098, Mode S 1.057, TETRA
0.252-0.272, a bare carrier 0.137-0.248, FM broadcast 0.032. A tenth is five
times the noise spread from the noise and less than a third of the way to the
nearest signal.

**Barred from the history, before the verdict rather than after**, because it
overrides all three: a confirmed empty frequency is still empty and an
intermittent one is noise that came and went.

On air, 290-310 MHz again: `no-carrier` on both frequencies reading Rayleigh,
and the eight marked `~` in the window against the receiver's one `*`.

### Two faults found by looking at the screen

**The rows disagreed with their own header.** The list matched each candidate
against the pass's targets on the candidate's own frequency with a 2.4 kHz
tolerance -- and the pass asks about *carriers*, at their measured centre, so
a station's shoulders are maxima several kilohertz away and matched nothing.
The header counted ten and the list showed none. It matches through the
carrier now, the way `survey_store.c` already did.

**The header truncated.** "1 marked *   10 mar..." lost the symbol that names
the second count, which is the same fault the findings sentences had.

### One case the flag does not catch, and it stays uncaught

299.4262 MHz read an envelope of 0.547 with 1.4% of the channel standing
still -- noise on both counts -- and **15.8 dB** over its floor, just above
`SIGNAL_CARRIER_PRESENT_DB`'s 15. So the carrier test passes it and the flag
stays silent.

That threshold was measured: ten draws of pure noise reached 8 to 14 dB over
their own median. 15.8 is above the pack, and the noise flagged in the same
sweep read 9.5 and 10.9. Loosening the threshold to catch this one would be
tuning a constant until it says what I want, and the conservative direction is
the right one here -- a real weak signal that is not thrown away.
