# The flag thresholds are one receiver's numbers, compiled in

**Absorbed 2026-09-08 into `.scratch/device-model/issues/08-*`**, which is
where a second receiver made this urgent rather than theoretical. The comb half
is done -- the reference is the device profile's and a source with no clock
gets no comb tests -- and the rest is a re-measurement task for when the
hardware arrives. Read 08 first; this stays for the measurements in it, which
are the record of what measuring looked like.

Every constant that decides whether a candidate is the instrument or the band
was measured on **one** R820T behind **one** RTL2832U, at one site, and then
written into a header. They are good numbers for this receiver and there is no
reason to think they are right for another.

## What is compiled in

| constant | value | where it came from |
| --- | --- | --- |
| `RECEIVER_REFERENCE_HZ` | 28.8 MHz | the RTL2832U's crystal on **this** dongle |
| `RECEIVER_COMB_HZ` | half of it, 14.4 MHz | measured on a disconnected sweep |
| the fine comb | 1.6 MHz, 28.8/18 | three arguments in `survey_suspect.h`, all from this receiver |
| `RECEIVER_COMB_TOLERANCE_HZ` | 25 kHz | how far off a multiple a real signal may sit |
| `RECEIVER_COMB_MAX_FRACTION` | 1/40 | past this a comb flag is chance rather than evidence |
| `SIGNAL_CARRIER_PRESENT_DB` | 15 | ten draws of noise reached 8 to 14 dB over their own median |
| `SIGNAL_BARE_FRACTION` | 0.80 | a synthetic tone reads 1.00, the 75 MHz harmonic 0.87 |
| `SURVEY_NOISE_ENVELOPE_TOLERANCE` | 0.10 | five noise readings within 0.017 of Rayleigh, nearest signal 0.27 away |
| `SURVEY_MIN_PROMINENCE_DB` | 8 | the descent a maximum must make to be a candidate |
| `SURVEY_CONFIRM_PROMINENCE_DB` | 6 | deliberately under the sweep's own bar |

**The comb is the one that might be wrong elsewhere, and the "might" is the
honest word.** An earlier draft of this said 26 MHz and 24 MHz parts exist.
**That was asserted without checking and should not be repeated.** 28.8 MHz is
what the RTL2832U's datasheet specifies, so it may be near-universal on these
dongles.

What *is* established is narrower: the comb here was measured, and no other
receiver's has been. So the case is not "the constant is wrong elsewhere" but
"the constant is unverified elsewhere, and a measured one would be evidence
rather than assumption".

The asymmetry of the harm is worth stating too, because an earlier draft
paired the two directions as though they were the same size. A real signal
wrongly flagged is a coincidence at 2 x 25 kHz / 14.4 MHz = **0.35% per
candidate**, about one in a 342-candidate sweep, and it is *marked rather than
dropped* -- the report says "nothing has been removed" and
`survey_confirm_should_record()` bars only the empty flag from the history.
Failing to flag the real spurs is the whole comb: **91 of 342 candidates** in
the reference sweep, which would enter the site history as signals and be
reported "gone" whenever a later sweep missed them. The second direction is
two orders larger than the first.

## Why the numbers cannot simply be widened

Every one of them was chosen by measuring where it breaks, and the reasoning
is in the headers beside them. Widening `RECEIVER_COMB_TOLERANCE_HZ` costs
precision the other way: `survey_suspect.h` records that at 14.4 MHz spacing a
full-tuner sweep's 106 kHz half-bin is 1.5% of the spacing, and at 1.6 MHz the
same tolerance is 13% -- which is why `RECEIVER_COMB_MAX_FRACTION` refuses to
answer at all rather than guess. A knob without that arithmetic behind it is a
way to make the flags agree with whatever somebody expected.

So this is not "expose the constants". It is: **measure them on the receiver
that is plugged in, keep the answer, and say when it is missing.**

## The measurement already exists as advice

The candidate panel has told the operator to do it by hand from the start:

> unplug the antenna and sweep again: what stays is the receiver

That is the calibration. What stays with the antenna off is the instrument,
and its spacing is the comb -- read off the frequencies it leaves rather than
assumed from a crystal nobody has seen. `survey_suspect.h` says the 1.6 MHz
comb was established exactly that way.

## The shape

1. A **calibration** the operator starts, which sweeps with the antenna
   disconnected and derives the comb from what it finds.
2. The answer **stored against the receiving setup**, and *not* per device --
   see below, because that was the wrong answer at first.
3. A **recommendation** when no calibration matches, because a wrong comb is
   silent: it does not look like a fault, it looks like signals.
4. The thresholds reachable from **Settings**, since the numbers above are
   defaults rather than laws -- with the arithmetic that constrains each of
   them enforced, not just displayed.

## Keyed by the receiving setup, and the reason is in the measurement

An earlier version of this said the comb belongs to the **device**: a crystal
travels with the hardware, so the same dongle has the same comb in every room,
unlike the tuning correction that ADR-0018 keys by receiver *and* site.

**That is wrong, and `survey_suspect.h` already had the evidence.** Unplugging
the antenna sorts the comb tones into two kinds: three of twelve --
489.6, 547.2 and 604.8 MHz -- stay exactly where they were, made and heard
entirely inside the receiver. **The other nine go with the antenna**, because
the dongle radiates its clock and hears itself coming back.

So the majority of the observable comb is *radiated and received*, which makes
it a property of the antenna as much as of the crystal. A calibration measured
on a telescopic whip does not describe what a rooftop antenna will hear back.

That puts it exactly where ADR-0022 puts the history: on a **receiving setup**
-- receiver, receiving site and antenna. Which is also the answer that needs
no special case, since the installation module in `.scratch/deepening/03`
already owns that identity.

The spacing itself is a property of the crystal and does not change. What
changes is which harmonics are strong enough to be found, and a fit needs
enough of them.
