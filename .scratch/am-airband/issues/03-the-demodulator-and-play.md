# 03 - The demodulator, and Play

Status: needs-triage
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
