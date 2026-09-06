# 01 - Where is it, and is it a bare tone?

Status: resolved

The first tool, and the one whose absence cost four attempts on 75.000 MHz.

## Two questions, and the trap between them

**Where is the carrier?** Not "the strongest bin", because the strongest bin
in any capture is the receiver's own DC offset at exactly +0 Hz. A search over
the whole span finds it at an empty frequency as readily as an occupied one --
in the 75 MHz case the empty control reported a *stronger* carrier than the
signal. Any tool here must exclude a guard band around zero and say why.

**Is it a bare tone?** A carrier with nothing on it and a carrier carrying
something have the same peak power and the same prominence. What separates
them is where the channel's energy sits: a bare tone puts nearly all of it in
one line, and anything modulated spreads it across the occupied bandwidth.

That ratio is the whole tool, and it is the difference between "there is a
signal at 75 MHz" and "there is nothing here to decode".

## Where to start

`sdr_dsp_characterise_carrier()` already finds a centre, a floor and a width
and is already general. This adds the two things it does not have: a search
that refuses to look at DC, and the fraction of channel energy standing in the
line itself.

## What must be checkable

Synthetic, no receiver (ADR-0012):

- a pure tone at a known offset comes back at that offset, with nearly all the
  channel's energy in the line;
- the same tone with noise added spreads, and the fraction falls;
- **a capture containing only a DC offset returns "not found"** rather than
  reporting DC as a carrier -- the specific failure this is written against;
- a tone inside the guard band is not found, and the guard is a parameter so a
  caller who genuinely wants to look near zero can.

And on real captures: `adsb_cpr_pair.bin` is a modulated carrier at 1090 MHz
and must read as modulated; the 75.000 MHz recording is a bare tone and must
read as one.

## What this must not become

An energy detector. The survey already decides what is a candidate; this
describes one it was handed.

## Answer

Status: resolved. `src/signal_probe.{c,h}`, `check-signal-probe`.

`signal_find_carrier()` finds the strongest line in a window, skipping a guard
band either side of zero, and reports how much of the channel is standing in
that line. `signal_carrier_verdict()` reads the two numbers together.

On four real captures:

```
75.000 MHz       line +49.8 dB   in_line 0.872   a bare carrier
empty control    line  -8.0 dB   in_line 0.000   no carrier
adsb_cpr_pair    line  -2.0 dB   in_line 0.000   no carrier
fm_rds_tsf       line +18.2 dB   in_line 0.000   a modulated carrier
```

Mode S reading "no carrier" is right rather than a miss: it is pulsed, so its
energy is real and its *standing* carrier is not.

## Four things it got wrong before it got them right

Each is in the code as a comment, because each is a mistake the next person
would make.

**The complement of "bare" is not "modulated".** The first verdict was a
single predicate, and an empty frequency has no constant in it either -- so
`in_line` reads 0.00 there exactly as it does on a busy channel, and the tool
called an empty control modulated. It takes both numbers: a line has to be
there before its shape means anything.

**The threshold had to be measured.** A search over thousands of frequencies
takes the largest of thousands of noise samples, so pure noise reliably
produces a line: ten draws gave 8.2 to 13.6 dB. The first threshold was 6 dB,
below what noise alone reaches.

**The floor was nine probes in one place.** Nine chances to land on another
signal, and on a real empty capture they landed on one -- putting the floor
*above* the best line and reporting -16.6 dB, which reads as "less than
nothing there" and means "the probes hit something". It is a median over
thirty-three probes across the whole captured span now. Across the *search
window* was the intermediate version, and it fails when the window is narrower
than the channel: every probe falls inside the carrier's own skirt, none
survives, and the floor comes back zero.

**`in_line` has to be measured in the channel.** The first version compared
the line against the power of every sample handed in, which for a 2 MHz
capture of a narrow carrier is almost all broadband noise far outside the
channel. It read 0.138 for a carrier standing 49 dB over its own floor -- and
the header claimed 0.93, a number that came from reasoning rather than from
running it.

## What it does not do

Say what the modulation *is*. That is tickets 02 and 03, and the line this
effort draws is that these tools narrow what to try next rather than
concluding.
