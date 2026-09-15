# 16 - Retrospective signal analysis outside the overlay

Status: done

## What to build

Form one presentation-free analysis of a retrospective I/Q slice and make the
waterfall popup an adapter over its result.

The current overlay extracts and converts samples, measures a standing
carrier and envelope, then decides among 2-FSK, OOK, continuous carrier and
unclassified/bursty inside a static raylib function. No hardware-free check
reaches those conclusions. It also applies the SRD modulation classifier to a
generic waterfall action, while the Probe context already has
`signal_findings` rules that carefully distinguish measurements, conclusions
and refusals.

The analysis should compose existing `signal_probe` measurements and return
immutable numeric evidence plus only the findings those measurements support.
The overlay remains responsible for selecting an age/frequency, obtaining the
slice and drawing the popup. SRD-specific modulation evidence may be included
when explicitly named as such; it must not silently become a universal
technology identification.

## Why this is deep

The module owns the relationship between carrier, envelope and modulation
evidence. Deleting it would put those decisions back into every retrospective
output adapter. It is not a second DSP implementation: all arithmetic remains
in the existing Probe and SRD modules.

## Cheapest discriminating check

Drive the analysis with the existing bare-carrier capture and one pulsed or
modulated committed capture. The result must preserve the distinction between
a standing carrier, a burst with no standing carrier and an unsupported
identification. A mutation that labels every high-variation envelope OOK, or
every 2-FSK-shaped result as an identified SRD transmission, must fail by
claim rather than by screenshot.

## Acceptance criteria

- [x] Signal-report decisions live in a module with no raylib, `struct app`,
      acquisition, filesystem or stdout dependency.
- [x] The result contains measured facts and explicit refusals, not only one
      modulation label.
- [x] Generic findings use the vocabulary and thresholds already established
      by `signal_findings` and `signal_probe`.
- [x] SRD-specific evidence is named as technology-specific and does not
      identify arbitrary waterfall signals by allocation or shape alone.
- [x] The overlay selects and renders a result without recomputing its
      conclusions.
- [x] Hardware-free checks distinguish bare carrier, modulated carrier,
      pulsed/no-standing-carrier and unsupported cases.
- [x] The report remains usable for Scope and every decode waterfall.
- [x] The new check is gated and its module keeps the Probe DSP dependency
      boundary.

## Blocked by

None - can start immediately.

## Not in scope

- Building a generic technology classifier.
- Changing `signal_probe` arithmetic or measured thresholds.
- Decoding a transmission from the popup.
- Moving IQ-ring persistence or raylib geometry into the analysis.

## Comments

Implemented in `src/signal_analysis.{c,h}` with unit test suite `tests/signal_analysis_test.c` (`check-signal-analysis`). Integrated with `src/overlay_signal_report.c`. All checks pass.
