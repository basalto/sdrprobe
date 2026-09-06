# 02 — The comb flag is disabled in the one sweep whose output is the baseline

Status: resolved

Ticket 01 gave the 1.6 MHz comb a detector that a broadcast carrier cannot
trip. It works, at every sweep width except one: **the full-tuner sweep, which
is the sweep whose output becomes the site history.** There the fine-comb test
is switched off by its own guard, and thirty receiver artefacts are reported as
candidates, some of them among the loudest entries in the survey.

## The measurement

`survey_comb_tolerance()` returns the larger of `RECEIVER_COMB_TOLERANCE_HZ`
(25 kHz) and half a survey bin. `survey_comb_harmonic()` then refuses to answer
at all when that tolerance exceeds `spacing_hz * RECEIVER_COMB_MAX_FRACTION`,
which for the 1.6 MHz comb is 40 kHz. A survey bin is the span over 8192, so
the guard trips on span alone:

```
24-1766 (full tuner)   bin 212646.5 Hz  tolerance 106323.2 Hz  fine comb: DISABLED
225-400                bin  21362.3 Hz  tolerance  25000.0 Hz  fine comb: active
225-320                bin  11596.7 Hz  tolerance  25000.0 Hz  fine comb: active
148-175                bin   3295.9 Hz  tolerance  25000.0 Hz  fine comb: active
```

The coarse 14.4 MHz comb stays active throughout, because its cap is 360 kHz.
14.4 is 9 x 1.6, so **exactly one comb tone in nine is still caught** and the
other eight are reported as signals.

In `surveys/2026-09-05-212301-24M-1766M.json`, of the candidates within 40 kHz
of a 1.6 MHz multiple, **14 are flagged and 30 are not** — and every one of the
14 is also a 14.4 MHz harmonic. Among the 30 unflagged: 480.020 MHz at
-26.2 dBFS (300 x 1.6), 120.010 in the VHF airband (75 x 1.6), 251.213,
267.161, 263.972, 180.827, 217.615.

## The guard is not wrong, and that is the point

At 212 kHz bins, half a bin is 106 kHz and 106 kHz on a 1.6 MHz comb catches
one frequency in fifteen by chance. `survey_comb_harmonic()` is right to refuse:
a flag with those odds is not evidence. The fault is that **nothing takes its
place**, so refusing to flag becomes reporting as real.

## Why the confirmation pass makes it worse rather than better

A crystal spur is the most reproducible signal there is. On a fine sweep of
225-400 MHz the pass **confirmed 23 of 24 candidates 6/6, and 16 of the 23 sit
on the comb.** Of the seven that do not, five are the TETRA carriers already
decoded. The report presents `confirmed` beside a level and a prominence with
nothing to say that two thirds of the strong ones are the receiver hearing
itself.

The spec already says the pass cannot substitute for the flag. This is the
measurement of what happens when it is asked to.

## Where to start

Not by loosening the cap — the odds argument behind it holds. Two directions,
both needing measurement before either is written:

1. **Carry the flag across sweep widths.** A tone's comb membership is a
   property of the frequency, not of the sweep that found it. A full-tuner
   candidate could be checked against the comb at the resolution some *other*
   sweep of that band established, which is what `surveys/history-<site>.txt`
   already exists to remember.
2. **Let the confirmation pass measure width, not just presence.** It already
   revisits each candidate with six blocks at 2 MS/s, where a bin is 244 Hz and
   the guard is nowhere near tripping. The pass has the resolution to answer
   the question the sweep could not; it currently only asks whether the signal
   is there.

Direction 2 looks cheaper and reuses a pass that already runs. Neither should
be built before somebody checks the second observation below.

## A second observation, not yet explained

On the 225-400 MHz sweep (fine comb *active*, 21.4 kHz bins), 21 of 43
**unflagged** candidates still sat within 9.3 kHz of a 1.6 MHz multiple — where
chance over that grid would put well under one. The same band at 11.6 kHz bins
flagged 16 of its 17 on-comb carriers, the exception being 225.609 MHz at
1124.9 kHz wide, which is broad enough to be a real signal near a multiple.

The likely cause is the `bare` gate: the fine-comb test needs narrowness as
well as frequency, and at coarser bins `survey_carrier.h` merges a comb tone
with its neighbours into a carrier too wide to look bare. That is a guess. It
needs the carrier extents from a 21 kHz sweep beside those from an 11 kHz one
before anyone believes it — and if it is right, the width the flag reads is a
grouping artefact rather than the tone's, which is a different bug from the one
above and would want its own ticket.

## Comments

Raised 2026-09-06 from four confirmed sweeps run to answer "what else can I
decode?" — 148-175, 400-406, 440-470, 118-137 and 225-400 MHz. The answer to
that question turned out to depend on this one: at full-tuner resolution the
survey listed 16 candidates in VHF land mobile, and a 3.3 kHz sweep of the same
band found **one carrier, at 172.800 MHz = 6 x 28.8**, the receiver's own
crystal. Everything else was noise in a 212 kHz bin.

## Answer

Status: resolved, by direction 2 -- the confirmation pass measures the width.

It already did. `sdr_carrier_report` carries `bandwidth_hz`, the pass computes
it at 2 MS/s to decide presence, and `survey_confirm_decide()` threw it away.
So the fix was to keep it, and to re-run the suspicion tests at the pass's own
resolution rather than the sweep's: `survey_suspect_confirmed()` builds a plan
whose bin is `sample_rate / fft_size` -- 244 Hz -- where
`survey_comb_harmonic()`'s guard does not trip and the width is real.

The step-centre test is deliberately left out of that call. It asks whether a
frequency sits where the receiver's own offset lands, which is a property of
how the sweep was *stepped*; a confirmation look is tuned to the candidate, so
every candidate would be at its centre and the test would flag all of them.

On 225-400 MHz, where fourteen of thirty-eight on-comb candidates were flagged
before and the rest were not:

```
confirm 230393982 ... 2930 reference,unresolved
confirm 288008118 ... 3906 reference,unresolved
confirm 259201050 ... 2930 reference,unresolved
confirm 240007019 ... 2930 reference,unresolved
confirm 390515137 ... 24414 -
confirm 392127991 ... 23438 -
```

**Three kilohertz against twenty-four.** A crystal spur is a bare tone and a
TETRA carrier is a 25 kHz channel, and at 244 Hz bins that is an order of
magnitude rather than a judgement. Every comb tone in the band is now flagged
and every TETRA carrier is not.

The full-tuner sweep still cannot flag them, and that is not a fault to fix
here: a 212 kHz bin genuinely cannot resolve a 3 kHz tone, and
`survey_comb_harmonic()` is right to refuse. What has changed is that the pass
which revisits those candidates can, so the answer exists for anything
confirmed.
