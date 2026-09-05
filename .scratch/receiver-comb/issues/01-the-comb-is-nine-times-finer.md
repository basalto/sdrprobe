# 01 — Flag the 1.6 MHz comb without flagging one FM station in sixteen

Status: resolved

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

## Comments

**2026-09-05 — resolved.** The comb is flagged at 1.6 MHz, and the confound is
handled by requiring two things to agree rather than by choosing a tolerance.

The frequency test refuses when it cannot place a candidate to a fortieth of
the spacing -- 40 kHz, so a sweep wider than about 650 MHz never uses the fine
comb and a full-tuner sweep behaves exactly as before. And the narrowness test,
which `survey_suspect.h` already described as "what makes the two above worth
believing" but which was never set in a swept survey, now is: candidates carry
`extent_hz` from the peak's own -20 dB extent in the survey array, and
`survey_is_unresolved()` judges it against the *sweep's* resolution rather than
the transform's. Judging against the transform called every tone in every swept
survey "resolved", which is how the one observation that separates a bare
carrier from a service came to be unavailable in exactly the case that needs
it.

On air, both halves hold:

| sweep | candidates | flagged `reference` | before |
| --- | --- | --- | --- |
| 240-270 MHz, 0.2 s | 16 | 7, every one an exact multiple of 1.6 MHz | 2 |
| 88-108 MHz, 0.2 s | 35 | 0 | 0 |
| 24-1766 MHz, 0.1 s | 244 | 35, all on the coarse comb | 35 |

94.4 MHz is 59 x 1.6 and is the loudest station at this site, confirmed at
46 dB above its floor; it measures 27 survey bins and is not flagged. 102.4 MHz
is 64 x 1.6 and there really is a comb tone on it -- swept at the transform's
own 977 Hz it comes back four bins wide and reads `reference,unresolved`. Swept
as part of 88-108 MHz, whose bins are 2.44 kHz, the same tone quantises to six
bins and is not claimed. That is the sweep declining rather than missing, and
it degrades the same way the frequency test does: a coarser sweep says less.
The alternative is a tone rule loose enough to catch a pager, which
`survey_suspect.h` warns against by name.
