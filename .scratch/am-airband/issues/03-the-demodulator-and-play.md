# 03 - The demodulator, and Play

Status: **wontfix, 2026-09-11** -- blocked by 01 and 02, and its own honest note
applies: without 02's occupancy record this is a demodulator on its own. Reopen
with an outdoor antenna or a site near an airfield. See the comments.
Blocked by: 01, 02

The audio, which is the confirmation rather than the product -- the same
relation `fm_audio_decode()` has to `rds.c`.

## The chain

Mix the channel to zero, filter to its width, detect the envelope, remove the
carrier, and hand the rest to the sound card. The prototype in `spec.md` is
the whole of it in about 120 lines: a 161-tap Hamming-windowed sinc at
+-12.5 kHz, decimation by 40 from 2 MS/s to 50 kHz, `sqrt(I^2+Q^2)`, and a
running mean removed.

Three things it does **not** do and a real one must:

- **A whole decimation.** 2 000 000 / 40 is exactly 50 kHz and 2 048 000 / 40
  is not; `fm_audio_init()` already solves this by choosing the factor from
  the rate rather than fixing it, and the same rule applies.
- **Automatic gain.** AM level follows the transmitter and the path, and an
  airband receiver switches between stations 40 dB apart. `struct fm_audio`
  has a level follower already.
- **No de-emphasis.** That is an FM broadcast convention and has no business
  here; the reason is worth a comment, because the FM path is the obvious
  thing to copy from and this is the line not to copy.

## Where it belongs

`src/am_dsp.{c,h}` with an `am_` prefix, on the **Probe** side of the context
map: an envelope is a signal measurement and says nothing a transmitter
intended in the Decoder sense (ADR-0021). If 02's occupancy record is built,
that is Probe too. Nothing here is a Decoder outcome, and the context map
should say so rather than leaving it ambiguous.

`fm_discriminate()` is the precedent for how narrow this should be: one
function, floats in, floats out, no state it does not need.

## The honest note

This produces audio and audio is not a message. `spec.md` argues the occupancy
record in 02 is what makes the effort belong here; **if 02 is not built, this
ticket should not be either** -- it would be a demodulator on its own, which
is the thing `rf-environment` says sits oddly beside the rest. That is a
judgement the operator can overrule, and it should be overruled explicitly
rather than by drifting into it.

## What must be checkable

A synthetic AM signal -- a carrier at a known level with a known tone at a
known depth -- must come back with that depth within a tolerance, and the tone
at that frequency in the demodulated audio. Then the real capture from 01
corroborates it, because a synthetic AM signal agrees with whatever assumption
built it.

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
