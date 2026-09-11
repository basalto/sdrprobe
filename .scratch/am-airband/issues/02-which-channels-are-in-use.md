# 02 - Which channels are in use, and for how long

Status: **wontfix, 2026-09-11** -- blocked by 01, whose gate failed on the
installation rather than on the hour. Reopen with an outdoor antenna or a site
near an airfield. See the comments.
Blocked by: 01

This is the part that is a measurement rather than a demodulator, and it is
the reason the effort is worth having at all.

## The shape

The airband is 25 kHz channels (8.33 kHz in parts of Europe, which this does
**not** attempt). At 2 MS/s the receiver sees **80 channels at once**, so the
question "which of these is in use right now" needs no tuning at all -- one
block, one transform, one reduction. `sdr_dsp_channel_powers()` already turns
a spectrum into per-channel powers and is what a band scan uses.

Per channel, per block: the carrier level, and whether it is above the
channel's own recent floor. Over time: when each transmission started, how
long it ran, and the gap since the last -- which is `struct signal_bursts`'
vocabulary exactly (`count`, `occupancy`, `median_seconds`, `median_gap_seconds`,
and the three verdicts `level` / `busy` / `separable`).

The output is a record, and it is the thing that is not a waveform:

```
channel 135.000 MHz  transmissions 14  occupancy 0.08  median 3.2 s  longest 9.1 s
channel 132.063 MHz  transmissions 3   occupancy 0.01  median 2.1 s  longest 2.8 s
```

## What has to be got right

**The receiver's own comb is in this band and is three of its six strongest
signals** -- 129.600 (14.4 x 9), 136.001 (1.6 x 85), 131.201 (1.6 x 82), each
a bare carrier 47-48 dB up with 98% of the channel standing still. They are
permanently "busy" to any level-based scanner. `survey_suspect.h` already
flags all three and this must use it rather than re-deriving anything;
`.scratch/device-model/issues/10-*` is the open question about a *fourth*
family it does not model.

**A channel floor is per channel, not per band.** The band spans 40 dB between
the comb tones and the weak carriers, so one threshold across 80 channels
reports the loud ones as always-on and never sees the quiet ones. ADR-0017's
argument about the survey's candidate bar applies unchanged.

**8.33 kHz channels are out of scope and should be said so on screen**, not
silently mis-binned into 25 kHz. A channel plan that quietly lies about its
own resolution is worse than one that refuses.

## Reuse

`sdr_dsp_channel_powers`, `signal_find_bursts` (whose constants were measured
against Mode S, LTE and a bare carrier, and whose `occupancy` is the statistic
that separated them), `survey_suspect.h`, and the survey's own per-channel
floor reasoning. Very little new arithmetic; the work is the record and the
view.

## What must be checkable

A synthetic band with three channels -- one silent, one continuously on, one
keyed with known on and off times -- must come back with those three verdicts
and the keyed one's transmission count, median length and gap within a
tolerance the check states. That is reachable with no receiver and no window,
which ADR-0012 requires, and it is what a real capture then corroborates.

## Comments

**wontfix, 2026-09-11.** Closed with 01, whose gate this is blocked on and
which cannot be passed at this installation. An hour on air that Friday --
two swept surveys of 118-137 MHz, three 90-second captures, the AM prototype
over five channels, and a control sweep of the VOR band where beacons transmit
*continuously* and cannot be missed for being intermittent -- found **nothing
external across 108 to 137 MHz**. Every candidate is either one of the
receiver's own comb lines or a noise maximum with no carrier, and the loudest
thing in the band is the receiver by 20 dB.

The blocker is the installation, not the hour and not the code: an indoor
telescopic whip at 120 MHz with no ground plane. The same antenna hears ten FM
broadcast carriers at up to 55.9 dB twenty megahertz lower, so it is not
broken; it is deaf up there.

**Reopen when the installation changes** -- an outdoor antenna, or a site
within useful range of an airfield. Nothing in these tickets is wrong and the
design work in `spec.md` stands; what is missing is a signal to point it at,
and building an occupancy record whose only possible output is "the receiver's
comb is busy" would be a screen asserting something false.

**One correction carried out of 01 and into `spec.md` rather than lost**:
`.scratch/device-model/issues/11-*` shows 135.024 (peak 134.999939) reads
61 Hz from exact where an external transmitter at that tuning must read about
4.2 kHz off. It is the receiver too. **Four** of the airband's six strongest
signals are the receiver, not three, and one external carrier survived the
earlier analysis rather than two -- 132.062744, which did not reappear on
2026-09-11.
