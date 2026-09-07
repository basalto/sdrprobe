# The flag thresholds are one receiver's numbers, compiled in

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

**The comb is the one that is certainly wrong elsewhere.** 28.8 MHz is common
on RTL2832U dongles and it is not universal -- 26 MHz and 24 MHz parts exist,
and a receiver that is not an RTL-SDR at all shares none of this. A survey run
on such a device flags the wrong frequencies as the instrument and, worse,
fails to flag the right ones: the spurs then enter the site history as
signals and are remembered for ever.

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
2. The answer **stored per device**, so it is not re-measured every session.
3. A **recommendation** when an unknown device appears, because a wrong comb is
   silent: it does not look like a fault, it looks like signals.
4. The thresholds reachable from **Settings**, since the numbers above are
   defaults rather than laws -- with the arithmetic that constrains each of
   them enforced, not just displayed.
