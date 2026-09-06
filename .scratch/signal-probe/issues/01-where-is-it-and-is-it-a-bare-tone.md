# 01 - Where is it, and is it a bare tone?

Status: ready-for-agent

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
