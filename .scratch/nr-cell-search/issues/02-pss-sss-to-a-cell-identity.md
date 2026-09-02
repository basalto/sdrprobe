# 02 — Primary and secondary signals to a physical cell identity

Status: wontfix
Blocked by: 01

`src/nr_dsp.{c,h}`, a plugin beside `lte_dsp.c` (ADR-0001), reusing the FFT and
the correlation machinery and sharing none of the sequences.

## The primary signal

A length-127 BPSK m-sequence, three cyclic shifts, one per N_ID_2. Believed to
be generated from `x(i+7) = (x(i+4) + x(i)) mod 2` with the shift as
`(n + 43 * N_ID_2) mod 127` -- **verify every part of that against 38.211
before writing a check that depends on it.**

The trap is structural and worth naming in advance: three shifts of a single
sequence are as easy to confuse with each other as LTE's roots 29 and 34 were,
and a shift error produces a confident detection under the wrong N_ID_2, which
then seeds the secondary search wrongly and hides itself. That is the fault
that cost the LTE side a day. A synthetic round trip cannot catch it, because
the generator and the detector share the mistake.

## The secondary signal

A Gold sequence from two length-127 m-sequences, 336 values of N_ID_1, with the
shifts believed to be `m0 = 15 * floor(N_ID_1 / 112) + 5 * N_ID_2` and
`m1 = N_ID_1 mod 112`. Same instruction: cite it, do not recall it.

`PCI = 3 * N_ID_1 + N_ID_2` carries over from LTE, giving 1008 identities.

## What must be true before this is believed

- Real-signal agreement: an identity that repeats across blocks of the capture
  from 01, not one read once.
- The identity must be reachable by a check needing no receiver (ADR-0012),
  against the recorded capture, the way `check-lte-dsp` reads cell 28.
- At least one measurement that does not share the detector's code path --
  see `.claude/skills/dsp-validation/SKILL.md` for what qualifies. The
  20 ms periodicity from 01 is one such, and it is available before any
  sequence model exists.

Explicitly out of scope: the broadcast channel. 240 subcarriers do not fit this
receiver, and pretending otherwise would produce a decoder nobody can run.

## Comments

**2026-09-02 - wontfix.** 01 measured the spacing at 30 kHz, so the primary
and secondary signals span 3.81 MHz and an RTL-SDR reaches about 2.4 MS/s.
There is no version of this detector the hardware can run. Reopen only if the
receiver changes, or if a longer capture overturns 01's measurement -- which
would need a prefix-length correlation window and many more synchronisation
blocks than three seconds holds.
