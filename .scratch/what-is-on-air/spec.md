# What is on air here, and what of it is worth decoding

A register of every allocation the survey finds signals in, what this program
does about each, and -- where something was ruled out -- the measurement that
ruled it out rather than the opinion.

It exists because the same questions keep being re-asked. DAB+ has been
proposed twice and refused twice on the same folded correlation; 5G NR got a
ticket written before anybody measured its subcarrier spacing. Both are
recorded here so the third time is cheap.

## The site and the sweep behind it

`home-sala-estar`, telescopic whip, 29.7 dB gain, R820T. The reference sweep
is `surveys/2026-09-06-200418-24M-1766M.json`:

| | |
| --- | --- |
| range | 24-1766 MHz, the whole tuner |
| bin | 212.6 kHz -- wider than a land-mobile channel, and the reason a sweep cannot identify anything |
| candidates | 342, of which 251 clean and **91 resembling the receiver** |
| grouped into | 64 signals, 17 holding several maxima |
| confirmation | asked about 24: 10 held up, 2 came and went, 12 did not |
| site history | 16 saved surveys, ~134 signals |

Two numbers there are worth reading twice. **91 of 342 candidates resemble the
receiver rather than the band** -- the 14.4 MHz comb off a 28.8 MHz crystal --
so more than a quarter of what a raw sweep reports is the instrument.
And **twelve of twenty-four confirmations failed**, which above 1.5 GHz is
most of the list and is `.scratch/bursty-signals/`: a 0.12 s dwell catches a
bursty transmitter about as often as it misses it.

## The two gates, before anything gets a ticket

From `.claude/skills/rf-environment/`, and both were learned by getting it
wrong:

1. **Does it fit the receiver?** About 2.4 MS/s before samples drop. Measure
   the bandwidth of the thing a decoder must see *whole*, not the channel.
2. **Is it actually there?** An allocation is a lookup, not an identification
   (ADR-0015). Band 28 is labelled an LTE downlink and carries 5G NR;
   75.000 MHz is labelled ILS markers and carries a clock harmonic.

Then it is ranked on whether it ends in a **message rather than a waveform**,
on reuse, and on whether it stays on air.
