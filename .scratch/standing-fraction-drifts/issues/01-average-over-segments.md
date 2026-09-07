# 01 - Average the standing fraction over segments

Status: needs-triage

## What changes

`constant_fraction()` in `src/signal_probe.c` takes one mean over the whole
buffer. Take a mean per segment instead, and average the fractions.

The segment length is the decision and it is measurable: long enough that the
low-pass still isolates the channel, short enough that a drifting carrier does
not walk a turn inside it. The flat region above says one second is still safe
on this receiver, so a segment of a quarter of that has three-quarters of a
turn of margin at the drift measured here -- but "measured here" is one dongle,
and `fm_pilot_ppm`'s spread is the reason not to trust one.

## What has to be re-measured, not assumed

`SIGNAL_BARE_FRACTION` is 0.80 and every number behind it came from the
unsegmented form:

| signal | unsegmented |
| --- | --- |
| a synthetic tone in no noise | 1.00 |
| the 75.000 MHz harmonic | 0.87 |
| a bare carrier in a 5 kHz channel | 0.951 |
| TETRA, pi/4-DQPSK | near 0 |
| FM broadcast | 0.003 |
| an LTE downlink | near 0 |

A segmented mean cannot reproduce these -- if it did, it would not have fixed
anything. So the threshold is re-derived from the new table, with the same
discipline: it sits under every positive and above every negative, and the
margin either side is stated.

`make probe-signal` produces that table across the captures, which is the
tool this ticket exists to use.

## What must be checkable

The property that started this: **the answer must not depend on how much
signal it is handed.** A synthetic carrier with a small deliberate drift,
measured over 0.07 s and over 2 s, must give the same fraction to within a
stated tolerance. That check is the fix's whole point and the current code
fails it, which is the right way round for a check written first.

And the existing checks keep their meaning: a pure tone still reads near 1, an
empty channel still reads near 0, and the 75.000 MHz capture still reads as a
bare carrier at every length.
