# 01 - Average the standing fraction over segments

Status: resolved, 2026-09-09

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


## What was done

`constant_fraction()` in `src/signal_probe.c` takes a mean per segment now and
averages the per-segment fractions. `SIGNAL_STANDING_SEGMENT_BLOCKS` is **64**
blocks of the decimated channel, and the number was measured rather than
picked.

### The check, written first, and it failed first

The property this ticket exists for -- *the answer must not depend on how much
signal it is handed* -- is
`test_the_standing_fraction_does_not_depend_on_the_look()`. A synthetic
carrier at 300 kHz drifting 20 Hz/s, measured over one sample block and over
0.5 s of the same carrier:

| | unsegmented | segmented |
| --- | --- | --- |
| 0.066 s | 0.9996 | 1.0000 |
| 0.500 s | **0.0974** | **0.9993** |

Synthetic rather than the capture, because a capture cannot separate the two
candidate causes and a synthetic carrier can: with the drift set to zero the
unsegmented form read 1.000 at every length from 0.066 s to 2 s, so the
mechanism is the drift and not the length. That control is a check of its own
(`test_a_steady_carrier_reads_the_same_at_any_length`) so a future change
cannot trade one for the other.

### Choosing the segment length by measuring where it breaks

Two pressures, both measured, pulling opposite ways. Short segments are
biased upward -- the statistic is `|mean|^2/mean(|.|^2)` over a segment's
blocks, which for a single block is exactly 1.0 whatever it holds, so B blocks
of noise read about 1/B. Long segments cancel, which is the defect. A bare
carrier over 2 s in a 5 kHz channel, four times narrower than any shipped
caller asks for:

| drift | B=64 | B=128 | B=256 |
| --- | --- | --- | --- |
| 0 Hz/s | 1.000 | 1.000 | 1.000 |
| 5 | 0.996 | 0.982 | 0.932 |
| 10 | 0.982 | 0.932 | 0.766 |
| 20 | 0.932 | 0.765 | 0.442 |
| 50 | 0.671 | 0.357 | 0.188 |

against a noise floor of 0.015 / 0.007 / 0.004 and a modulated carrier at
0.033 / 0.018 / 0.008. **64 has the most drift margin of the three and still
keeps noise an order of magnitude under the threshold.** It holds a bare
carrier over 0.98 out to 10 Hz/s, which at 75 MHz is 0.13 ppm per second; and
both shipped callers floor the channel at `SURVEY_CARRIER_MIN_CHANNEL_HZ`,
four times wider than that table's, so a segment there is 3.2 ms rather than
12.8 and the margin is larger again.

### The ticket was right about the combining rule and it took measuring to see

This ticket said "average the per-segment fractions". The obvious alternative
is to sum the numerators and denominators, which has the tidier property of
being *identical* to the old statistic when there is one segment -- and that
was the reading taken first.

Measured, the two are **indistinguishable on anything power-stationary**:
identical to three decimals across every case in both tables above, because
the segments then share a denominator. They part company on a carrier that
keys on and off, which is the one case the choice decides:

| duty | averaged | summed |
| --- | --- | --- |
| 0.1 | 0.114 | 0.790 |
| 0.3 | 0.311 | 0.929 |
| 0.5 | 0.507 | 0.958 |
| 1.0 | 1.000 | 1.000 |

Averaging returns the duty. **So does the unsegmented form** -- its numerator
is `(duty*A)^2` and its denominator `duty*A^2` -- so averaging is the change
that leaves every other answer alone, and summing would have called a carrier
keyed a tenth of the time "nearly bare" at 0.790. The ticket's rule, for a
reason the ticket did not give.

## The re-measured table, and the threshold

Over the whole of each capture:

| signal | unsegmented | segmented |
| --- | --- | --- |
| a synthetic tone, no drift | 1.000 | 1.000 |
| **the 75.0005 MHz harmonic, 2 s** | **0.779** | **0.923** |
| Mode S, `adsb_cpr_pair` | 0.253 | 0.327 |
| FM broadcast, `fm_rds_tsf` | 0.000 | 0.145 |
| TETRA, `tetra_cc17` | 0.001 | 0.100 |
| GSM, `gsm_arfcn_69` | 0.000 | 0.063 |
| LTE, `lte_b20_pci28` | 0.000 | 0.028 |
| TETRA, `tetra_cc32` | 0.000 | 0.015 |

Every negative rose, because within one segment a modulated signal is partly
coherent. The single positive rose further, and in the direction that matters:
0.779 was *below* the threshold and called a bare carrier modulated.

**`SIGNAL_BARE_FRACTION` stays 0.80**, and the margins are the argument: 0.12
under the lowest positive and 0.47 above the highest negative. Lopsided on
purpose -- the positives are what drift, and the fix bought them 0.14 of
headroom -- and nothing in the corpus reads between 0.33 and 0.92, so there is
no measurement that would justify moving it into that gap. Re-derived rather
than kept by default: the old value happened to be right for the new table
too, and saying so is not the same as not having looked.

## What the shipped paths do

**On the one sample block both callers hand it, every verdict is unchanged**;
only the numbers move.

| signal | old | new | verdict |
| --- | --- | --- | --- |
| 75.0005 MHz harmonic | 0.888 | 0.905 | a bare carrier, both |
| Mode S | 0.098 | 0.335 | a modulated carrier, both |
| FM broadcast | 0.007 | 0.190 | a modulated carrier, both |
| TETRA cc17 | 0.013 | 0.108 | a modulated carrier, both |
| GSM ARFCN 69 | 0.013 | 0.065 | a modulated carrier, both |
| LTE b20 | 0.003 | 0.031 | a modulated carrier, both |

And `make probe-signal` over the whole 2 s of `carrier_75000_bare.bin` now
reads *a bare carrier, 45.5 dB, standing 0.923* where it read *a modulated
carrier* at 0.779 -- which is the point the defect was found at.

**PATCH, 0.46.2** (ADR-0016). No flag, no format, no screen. The `kind`
record's `standing_share` field keeps its name and meaning and now reports it
length-independently, and a reading corrected is not a contract broken -- the
same clause that makes a corrected decode a PATCH.

## Two wrong claims written on the way, both caught by running them

Worth recording because they are the ordinary shape: right arithmetic, false
prose, and this suite already warns against both.

- "A tone under six times its own amplitude in noise reads low." It reads
  **0.679** in a 40 kHz channel, because the channel filter throws almost all
  broadband noise away -- which is exactly what
  `test_only_in_channel_energy_counts` was written to say.
- "Noise will read the segment's 1/64 bias." Through `signal_find_carrier()`
  it reads **0.00**: the search finds no coherent line to mix against, so the
  bias is not reachable from outside and cannot be asserted there.

The third check is the honest version of both: a wider channel admits more of
the noise, measured monotone from 0.679 at 40 kHz to 0.123 at 640.
