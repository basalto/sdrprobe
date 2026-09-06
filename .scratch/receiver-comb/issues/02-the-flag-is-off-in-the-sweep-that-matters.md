# 02 — The comb flag is disabled in the one sweep whose output is the baseline

Status: needs-triage

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
