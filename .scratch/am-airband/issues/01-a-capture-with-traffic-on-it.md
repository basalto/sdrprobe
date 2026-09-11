# 01 - A capture with traffic on it, and the gate it has to pass

Status: **the gate failed, 2026-09-11** -- and not for want of daylight. See
the comments; this blocks 02 and 03 on the installation rather than the hour.
Blocks: 02, 03

Nothing here should be built before this, because the only airband capture
taken so far was at 01:05 local and has **no transmission in it**. The
prototype's audio-band test reads -1.1 dB of speech-band excess at the signal
against -1.0 and -0.7 dB at two controls: an idle channel.

## What to record

Two or three captures of 20-30 s at 2 MS/s, in the working day, centred so the
channels found on air fall clear of DC:

```sh
./sdrprobe --headless --record-seconds 30 --technology raw \
    --frequency 134.8M --sample-rate 2000000
```

134.8 MHz puts 135.000 at +200 kHz and 134.0-135.8 in the span. A second at
132.9 MHz covers 131.9-133.9 and puts 132.063 at -837 kHz. Take them at
different times of day: the point is a transmission, and one capture without
one proves nothing about the band.

**Tune from the candidate peak, never the carrier group's centre.** Those are
24 kHz apart on this very signal, and a 25 kHz channel on the wrong one misses
it -- see `spec.md`.

## The gate

Not "can I hear it". Three numbers, all of which the prototype in
`.scratch/am-airband/` already computes:

1. **A carrier that comes and goes.** Per-second carrier level at the channel,
   against the quietest second of the same look. An idle channel is flat --
   the night capture varies 0.6 dB over 20 s at an empty offset and 4.1 dB at
   the weak one. A transmission should stand well clear; **decide the bar from
   the measurement, not before it**, and record what was seen.
2. **Speech where speech lives.** Energy in 300-3400 Hz against 4-8 kHz in the
   same demodulated stream, at the channel and at a control offset where
   nothing is. This is the RDS two-band trick and it is the half that makes
   the answer evidence rather than a level.
3. **Modulation depth in the right range.** Airband AM runs 0.6-0.9 on speech
   peaks. The night capture reads 0.29-0.44 on a carrier and 0.55 on noise --
   note that **noise reads high**, because depth is normalised by a carrier
   that is not there, so this number alone separates nothing and is only
   meaningful beside (1).

## Positive control, which is not optional

`testfiles/carrier_75000_bare.bin` at +300 kHz: the prototype reads carrier
0.0064 and depth 0.213 against a control's 0.0015 and 0.642. Any reworking of
the demodulator has to keep firing on that, or its nulls mean nothing.

## What becomes a test capture

Whichever recording holds a clean transmission, trimmed to a few seconds
around it, as `testfiles/am_airband_<channel>.bin` with a sidecar. It is the
only fixture that can hold the decisions in 02 and 03 honest, and everything
in `CLAUDE.md` about real captures applies: a synthetic AM tone agrees with
whatever assumption built it.

## Comments

**An hour on air, Friday 2026-09-11 from 10:09 local. The gate does not pass,
and the reason is not the time of day.**

Two swept surveys of 118-137 MHz (0.3 s and 2.0 s dwell, 18 and 20
candidates, every one confirmed or refuted), three 90-second captures at
121.3, 134.3 and 129.4 MHz, the AM prototype over five channels, and a control
sweep of the VOR band.

**Every candidate is one of two things, and neither is a transmission.**

*A continuous line at an exact round frequency.* Measured at 2.0 s dwell they
snap onto the receiver's own combs -- 127.999817 is 1.6 x 80, 129.600159 is
14.4 x 9, 135.999207 is 1.6 x 85, 120.000427 is 1.6 x 75 -- plus two that fit
no modelled comb and still read exact: **130.000000** and **135.000000**.

*A noise maximum with no carrier.* 121.808350, 128.926392, 134.502075 and
133.544189 were all confirmed 6/6 by the sweep's prominence bar and all read
**"no carrier"** at 2.6 to 12.9 dB when `signal_probe` measured them properly.

**The ppm was measured rather than assumed**, which is what makes "exact" mean
anything: GSM ARFCN 113 gave **-35.96 ppm, locked, sem 0.04 over 498
measurements**. At 130 MHz that is **4.7 kHz** of displacement, so an external
transmitter *cannot* read at its nominal frequency. Every real line here reads
within 800 Hz of exact. They are all clocked with the receiver.

**No transmission in any second of 4.5 minutes.** Per-second carrier at five
channels, largest excursion over the quietest second:

```
121.808  0.6 dB      128.926  0.6 dB      129.601 (comb)  0.3 dB
135.025  0.3 dB      130.014  1.8 dB
134.502  0.5 dB
```

`spec.md` records an *idle* channel varying 0.6 dB. The prototype's detector
was seen to fire in the same captures -- on 129.600000, reading carrier 0.0067
against its 0.0015 controls, almost exactly its 75 MHz positive control -- so
these nulls are worth reading.

### The control, which is what turns this from "no traffic now" into a finding

A sweep of **108-117.975 MHz**, where VOR and ILS beacons transmit
*continuously* and cannot be missed for being intermittent. The only two bare
carriers in it are **115.197876 = 1.6 x 72 at 70.3 dB** and **110.400513 =
1.6 x 69 at 46.0 dB** -- the receiver again. Everything else reads standing
0.02 to 0.49, no carrier signature, and none sits a channel-plus-4.1-kHz from
any VOR frequency.

So across the whole of aeronautical VHF, 108 to 137 MHz, **this receiving
setup hears nothing external at all**, while the same antenna hears ten FM
broadcast carriers at up to 55.9 dB prominence twenty megahertz lower. It is
not broken; it is deaf up there, and the receiver's own comb is the loudest
thing in the band by 20 dB.

### What this means for 02 and 03

`spec.md` says "three of the six strongest things in the airband are the
receiver". It is understated: **all of them are.** The one candidate that
survived the earlier analysis, 132.062744, did not reappear today.

**More captures at this site will not pass this gate.** The blocker is the
installation, not the hour -- an indoor telescopic whip at 120 MHz with no
ground plane, in a receiver whose own spurs run to 70 dB there. What would
change it is an outdoor antenna, or a site within useful range of an airfield.
Tickets 02 and 03 should stay closed until one of those exists, and the honest
close if neither will is `wontfix`.
