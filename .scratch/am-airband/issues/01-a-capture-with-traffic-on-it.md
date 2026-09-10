# 01 - A capture with traffic on it, and the gate it has to pass

Status: ready-for-agent (needs the receiver, in daylight)
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
