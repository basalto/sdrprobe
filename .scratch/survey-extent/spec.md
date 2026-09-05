# Where a candidate ends, measured by the trough rather than by 20 dB

`sdr_dsp_find_peaks()` decides how wide a candidate is by walking out to the
point where it falls `bandwidth_db` below its own peak. A candidate that never
gets there -- because the noise is closer than 20 dB, or because it is sitting
on something wider than itself -- has no extent, no floor, and is discarded.

ADR-0017 measured what that costs and kept it anyway, because the obvious
replacements cost more. This is the replacement that should not.

## What the rule is doing right

A ripple on the flat top of a television multiplex is a genuine local maximum
with a genuine descent either side, so it clears the topographic gate. In a
band of contiguous multiplexes it never falls 20 dB below itself, so it is
dropped. That is the right answer: it is a feature inside a signal, not a
signal.

Measured on 470-690 MHz, three sweeps each: bounding the walk and measuring the
floor outside the hump takes the survey from 39/40/40 candidates to 59/61/61,
and from 38/40/39 carriers to 58/60/60. Every one of the 28 additions is inside
a DVB-T channel's 8 MHz.

## What it is doing wrong

The same rule drops isolated signals that simply lack 20 dB of headroom:

- **ARFCN 63** in `testfiles/gsm_arfcn_69.bin`, at 947.6347 MHz. The cell's own
  System Information 2, decoded from the same capture by an entirely different
  chain, lists 63 among its neighbours. Two subsystems agreeing on a channel is
  corroboration a survey cannot give itself, and the survey does not report it.
- **The 1090 MHz carrier** in `testfiles/adsb_cpr_pair.bin`, which yields six
  decoded Mode S frames. `adsb_modes1.bin` shows the same carrier 35 dB
  stronger and it is dropped there too.
- **A bare tone at 102.4 MHz**, 64 x 1.6 MHz, on the receiver's reference comb
  and 18 dB above its floor. An 88-108 MHz sweep finds it; a sweep whose walk
  happens not to terminate does not.

Requiring the walk to *close* rather than run out of reach removes the ripples
cleanly -- 45 candidates to 25 on 470-690 MHz, all 20 removals inside a channel
-- and removes all three of these as well. Nothing about a 20 dB threshold
distinguishes them.

## What would

The trough. `survey_carrier_edge()` in `src/survey_carrier.h` already walks
down from a peak and stops where the level climbs back
`SURVEY_CARRIER_SPLIT_DB` above the lowest point it has seen, which is what a
band edge looks like and what a notch inside a modulated carrier does not. It
separates the two cases by construction:

- a ripple's trough is the notch beside it, still inside the multiplex, so its
  floor is the multiplex and its prominence is about a decibel;
- an isolated tone's trough is the noise, so its floor is the noise and its
  prominence is the 18 dB it really stands.

Then the floor bar does the filtering, which is what ADR-0013 asked for and
ADR-0017 could not deliver.

It is already pure, already checked (`check-survey-carrier`), and already
trusted for the carrier grouping -- the layering is the obstacle, since
`survey_carrier.h` includes `sdr_dsp.h` rather than the other way round.
