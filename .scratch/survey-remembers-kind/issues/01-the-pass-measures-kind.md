# 01 - The confirmation pass measures what kind of thing it found

Status: resolved

## What changes

`survey_confirm_target` gains what `signal_probe` measures, and the pass fills
it in on the looks where the signal was present:

- `struct signal_carrier` -- is there a standing carrier, and how much of the
  channel stands still
- `struct signal_bursts` -- separable, busy, or a level
- `struct signal_envelope` -- how much the magnitude varies, against Rayleigh

Held rather than averaged, the same way the pass already holds its widths:
these are measurements of a signal that may be bursty, and averaging a look
that caught nothing into one that did is how a real transmitter becomes noise.

## The tuning

The pass must retune `SURVEY_OFFSET_HZ` below each target rather than onto it,
because `signal_find_carrier()` guards DC and would otherwise skip the signal.
The spec has the measurement of what that costs -- about 2 dB, against a
threshold with more margin than that.

`sdr_dsp_characterise_carrier()` is already given the target's *absolute*
frequency, so it needs nothing but the tuned frequency it is handed.

## What must be checkable

`check-survey-confirm` covers the pass's arithmetic today and must cover this:
that a look which found nothing does not overwrite a look that did, and that a
target never measured reads as never measured rather than as zero. Both are
the same fault the duty lines in `signal_findings.h` guard against, and zero
is the most misleading value a measurement can have.

## Comments

**2026-09-07 — done.** The pass measures kind on the look it reports, and
prints a `kind` line beside each `confirm` one.

On air, a headless confirmed sweep of 88-96 MHz:

```
confirm 89503418 new confirmed 46.4 6/6 76172 -
kind 89503418 a modulated carrier 52.9 0.003 0.064 level 0.0000
confirm 93696777 new confirmed 27.3 6/6 2930 unresolved
kind 93696777 a modulated carrier 46.2 0.515 0.426 level 0.0000
```

Those two rows are the argument for the whole effort. Both are strong,
confirmed, continuous carriers, and the `confirm` line says nearly the same
about each. Only the standing fraction -- 0.515 against 0.003 -- separates a
broadcast station carrying a programme from a narrow 2.9 kHz thing with half
its channel not changing.

Every station in the band read `a modulated carrier` with a fraction of 0.001
to 0.007, continuous, occupancy zero. The envelope variation ran 0.064 for the
strongest to 0.464 for the weakest, which is the signal-to-noise dependence
`signal_probe.h` records: the statistic is a scale and a weak signal in a
channel is measuring some of the noise with it.

### Three decisions, and why

**The kind comes from the same look that gave the level.** Taking it from
whichever look ran last would put a carrier fraction beside a prominence
measured in a different block, and for a bursty target those are different
signals -- one look caught the transmission and the other caught the gap.
`survey_confirm_better()` is that decision, extracted so
`check-survey-confirm` covers it.

**A target the pass never caught prints no line.** Zeros would be worse than
silence: a standing fraction of 0.000 reads as "heavily modulated" and a
burst count of zero as "continuous". The envelope reports -1 where it refused,
for the same reason -- zero is its strongest claim.

**The tuning moved 300 kHz below the target.** Not a fix: the spec has the
measurement showing it costs about 2 dB rather than gaining any. It is
enablement -- `signal_find_carrier()` guards DC so it cannot lock onto the
receiver's own offset, and tuned onto the target a guarded search skips the
signal it was pointed at.

### One trap worth recording

The headless pass keeps its own target array and never advances
`confirm.index`, so a function writing the kind through that index would have
put every target's measurement into the first one -- and it would have looked
right on screen, where the index does advance. It goes through the
scratch-then-copy that `survey_confirm_decide()` already uses for the level,
the width and the suspicion.
