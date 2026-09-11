# AM airband: what is on a channel, and who is talking

## The tension, stated once

`rf-environment` says it plainly: *"Something that demodulates to audio is a
demodulator, and sits oddly beside the rest."* Every technology here ends in
something the transmitter is **saying** -- MCC/MNC/LAC/CI, a Master
Information Block, an aircraft position, a station name. Air-traffic voice
ends in a loudspeaker, and no amount of DSP turns it into a field.

There is a shape that does fit, and it is not "an AM receiver". It is
**channel occupancy**: which of the airband's channels are in use, when each
transmission started, how long it lasted, and how strong it was. That is a
measurement in the vocabulary `signal_probe` already speaks -- bursts,
occupancy, gaps -- over a band the receiver can see 80 channels of at once.
The audio is then what FM's **Play** is: the confirmation that the measurement
is about what it claims, not the product.

FM is the precedent and it is a real one: `fm_audio_decode()` exists, plays
through the sound card, and nobody argues it does not belong -- because the
decoder beside it reads RDS. The question this spec has to answer is what the
airband's equivalent of RDS is, and the answer is the occupancy record.

## What is measured, at this site

`surveys/2026-09-10-002420-24M-1766M.json`, then a 128-137 MHz sweep at 0.3 s
dwell with `--survey-confirm`. Nine candidates, six confirmed:

| frequency | verdict | kind | note |
| --- | --- | --- | --- |
| 129.600159 MHz | confirmed 6/6 | bare carrier, 48.0 dB, 98% standing | **14.4 x 9** -- the receiver's comb, flagged |
| 136.000793 MHz | confirmed 6/6 | bare carrier, 47.6 dB, 98% standing | **1.6 x 85** -- the comb, flagged |
| 131.200526 MHz | confirmed 6/6 | modulated carrier, 22.2 dB, 5% standing | **1.6 x 82** -- the comb, flagged |
| 135.023560 (peak at **134.999939**) | confirmed 4/4 | modulated carrier, 38.2 dB, 78% standing | ~~a real AM carrier~~ **the receiver: 61 Hz from exact, see below** |
| 132.062744 MHz | confirmed 6/6 | modulated carrier, 33.6 dB, 57% standing | a real AM carrier |
| 133.109741 MHz | confirmed 6/6 | no carrier, 12.6 dB | 1.34 MHz wide; not a channel |
| 135.359741, 133.896912 | intermittent 2/6 | no carrier | below the bar |

**Four of the six strongest things in the airband are the receiver.** This
said three until the row above was re-read; the arithmetic is in the comments
and in `.scratch/device-model/issues/11-*`. That is the first fact any channel
scanner here has to survive -- and the comb flags catch only three of the
four, which is what ticket 11 closes. A scanner that skipped
`survey_suspect.h` would report 129.600 MHz as a busy channel for ever.

## The measurement that shaped the design

A 20 s capture at 134.8 MHz, 2 MS/s, and a prototype envelope demodulator
(mix to zero, 161-tap window-sinc to +-12.5 kHz, decimate 40 to 50 kHz, detect,
then measure the *audio*).

**The carrier is at +200 kHz, not +224 kHz** -- 135.000 MHz, not 135.024. The
sweep reports both numbers and they mean different things: `candidate` is the
peak (134999939) and the carrier group's `centre_hz` is the middle of its
extent (135023560), 24 kHz apart. A 25 kHz channel centred on the wrong one
**misses the signal entirely**, which is exactly what the first run did: at
+224 kHz the demodulator read 0.0013 carrier and 0.549 depth, indistinguishable
from two controls, flat to 0.6 dB across all 20 s. At +200 kHz it reads 0.0028
and 0.29-0.44. That is the trap, and it is a channelised scanner's first bug.

Positive control for the tool itself, on `testfiles/carrier_75000_bare.bin`:
carrier 0.0064 against a control's 0.0015, depth 0.213 against 0.642. **The
detector has been seen to fire**, which is what makes its nulls worth reading
-- `probe-nbiot --self-test` is the precedent.

## What is not established

**That there is intelligible voice.** The capture was taken at 01:05 local on
a telescopic whip indoors, and 135.000 MHz reads as a weak carrier varying
4.1 dB across seconds with a modulation depth of 0.29-0.44 -- consistent with
AM carrying something, and not proof of speech. The audio-band test that would
settle it (energy in 300-3400 Hz against 4-8 kHz in the same demodulated
stream, the RDS trick) reads -1.1 dB at the signal and -1.0 and -0.7 dB at the
controls: **no speech-band excess anywhere**, which is what an idle channel
looks like.

So the second gate is **not yet passed for voice**, and the missing thing is a
daytime capture with real traffic on it. Nothing should be built on the
strength of a night-time null.

## Order

1. `01` -- a daytime capture, and the gate it has to pass.
2. `02` -- channel occupancy over the whole band at once.
3. `03` -- the demodulator, and Play.

## The prototype

`am_probe_prototype.c` beside this file is what produced the numbers above.
It is a **prototype and not a tool**: it is kept here rather than in
`scripts/` because it has been written once, and this repository's rule is
that a harness earns a `make` target by being written a third time. If ticket
01's captures need it again, and then again, promote it to `probe-am` beside
`probe-signal` -- whose shape it deliberately copies, a measurement at a
signal and the same measurement where nothing should be.

```sh
gcc -O2 -o am_probe am_probe_prototype.c -lm
./am_probe <capture.bin> <offset_hz> [more offsets...]
AM_SLICES=1.0 ./am_probe <capture.bin> <offset_hz>   # per-second carrier
```

## Comments

**2026-09-10, from a review of this spec.** The table above needs one row
re-read, and it costs the effort half its evidence.

**135.000 MHz is the receiver, not a real AM carrier.** An uncalibrated reading
is displaced from the truth by `f_tuned * d` -- 4.2 kHz at 135 MHz for this
dongle's ~31 ppm -- while a tone clocked from the receiver's own reference has
that displacement cancel and reads at its exact nominal. The candidate peak
reads **134.999939, 61 Hz from exact**, where a transmitter would have to read
4.2 kHz off; no true frequency 4.2 kHz away lands on either the 25 kHz or the
8.33 kHz raster. Its 78% standing share and the prototype's speech-band null
were both saying so already. `.scratch/device-model/issues/11-*` has the
arithmetic and the measurement that establishes it (21.8 Hz on
`carrier_75000_bare.bin`, against 2.3 kHz predicted).

So it is **four** of the airband's six strongest signals that are the receiver,
and **one** confirmed external carrier remains: 132.062744, which reads
**+4411 Hz** from 132.058333 against 4066-4290 predicted.

**That survivor is only explicable on the 8.33 kHz raster** -- the nearest
25 kHz channels are 12.3 and 12.7 kHz away, three times the displacement. So
`issues/02-*`'s "8.33 kHz is out of scope and should be said so on screen"
would refuse the only traffic measured at this site. The raster wants to be a
parameter; 240 channels across 2 MS/s instead of 80 is not the expensive part
of that ticket.

Neither correction touches ticket 01, which is still the right first move --
but record at **132.9 MHz**, where the one real carrier is, rather than
134.8 MHz, where the spur is.

## Status, 2026-09-11

**Closed wontfix**, all three tickets. An hour on air found nothing external
across 108-137 MHz, including a control sweep of the VOR band where beacons
transmit continuously. The blocker is the installation -- an indoor telescopic
whip at 120 MHz with no ground plane, against a receiver whose own spurs run
to 70 dB there -- not the hour and not the design in this document, which
stands as written. Reopen with an outdoor antenna or a site near an airfield.

The one external carrier that ever survived analysis here, 132.062744, did not
reappear on 2026-09-11. Whoever reopens this should start from it and from the
8.33 kHz raster it needs -- see the comments below, which are the reason
ticket 02's "8.33 kHz is out of scope" would have refused the only traffic
this site has measured.

**2026-09-11, correcting the correction.** Ticket 11's sign was backwards: the
crystal here is **fast** by 31.84 ppm, so an uncorrected reading of a real
transmitter comes back *low*, not high. The survivor is therefore on
**132.066667**, which predicts 132.062462 against 132.062744 observed -- 282 Hz
-- and not the 132.058333 named above, which the corrected sign puts 8.6 kHz
away. Still only the 8.33 kHz raster: 132.066667 is not a 25 kHz channel
either, so the conclusion about `issues/02-*` stands unchanged. Every "this is
the receiver" verdict above is unaffected, being about a reading sitting *at*
its nominal.
