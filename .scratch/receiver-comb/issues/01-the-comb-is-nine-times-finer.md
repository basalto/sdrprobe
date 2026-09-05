# 01 — Flag the 1.6 MHz comb without flagging one FM station in sixteen

Status: needs-triage

The spec has the measurement. What is missing is a way to say "this is the
receiver" that a broadcast carrier on the same frequency cannot trip.

## Where to start

Not with the comb spacing, which is settled. With the **discriminator**, and
there is one already named in `survey_suspect.h`: a reference harmonic is a
bare carrier and a service is not. The file says so -- narrowness on its own
is an observation, and it "is what makes the two above worth believing" -- but
`SURVEY_SUSPECT_UNRESOLVED` is never set in a swept survey, because widths are
only measured when the whole survey came from one tuning.

The width is there to be had. Measured on air, at the resolution each sweep
actually had:

| | width | in bins |
| --- | --- | --- |
| comb tone, 240-270 MHz at 3.7 kHz bins | 25-37 kHz | 7-10 |
| FM station, 88-108 MHz at 2.4 kHz bins | 71-190 kHz | 29-78 |

`survey_carriers_from()` produces that width for every carrier in every sweep,
and `sdr_peak` carries a -20 dB extent in survey bins for every candidate.
Either could feed a narrowness test; neither is wired to one.

## What must be checkable

- That 94.4 MHz with a broadcast width is **not** flagged, and 244.8 MHz with
  a tone's width is. Both are real measurements from 2026-09-05 and both are
  in the spec.
- The false-hit rate at each resolution. A thousand frequencies spread across
  the tuner, as `test_a_coarse_sweep_stays_selective` already does for the
  coarse comb -- and a rule for what to do when the bins are too wide to place
  a candidate within a fortieth of 1.6 MHz, which on a full-tuner sweep they
  are. Claiming nothing is a defensible answer there; claiming one in eight is
  not.

## What this must not become

A reason to remove candidates. The comb tones are real signals that the
receiver is making, and `--survey-confirm` confirms them at 19-35 dB. The flag
says a frequency has the signature of an artifact and leaves the operator to
decide (ADR-0015); nine times more of them being flagged must not turn into
nine times more being hidden.
