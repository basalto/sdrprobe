# 01 — From FM to a 1187.5 bps bitstream

Status: resolved
Blocked by: (none)

`src/fm_dsp.{c,h}`, Probe side. Samples in, soft symbols out; it must not claim
to have decoded anything (CONTEXT.md).

## Take a capture first

    ./sdrprobe --headless --record-seconds 4 --technology raw \
        --frequency 89.6M --sample-rate 2048000

It becomes `testfiles/fm_rds_<station>.bin` with the usual sidecar once it is
known what station it is. Four seconds is about 45 groups, enough for a
programme service name to repeat and be checked. The station is local and
permanent, so unlike the GSM captures this one can always be retaken.

## The stages

1. **Discriminator.** `atan2` of each sample against the conjugate of the one
   before. Already prototyped while confirming the subcarrier exists.
2. **Pilot recovery at 19 kHz.** Coherent, because the whole point is what it
   gives away: the RDS subcarrier is exactly three times the pilot and locked
   to it, so tripling a recovered pilot hands over the carrier phase and there
   is no blind loop to converge or fail to.
3. **Bandpass 57 kHz +- 2.4 kHz, mix down, filter to baseband.**
4. **Symbol timing at 1187.5 bps** -- exactly 57000/48, so it is locked to the
   subcarrier too, and only the phase has to be found.
5. **Biphase to differential to bits.** Soft, positive means more likely zero,
   as `gsm_bcch.h` and `lte_mib.h` both do.

## What must be checkable without a receiver

All of it (ADR-0012). The natural fixture is a synthesised multiplex --
pilot, subcarrier, a known bitstream -- pushed through the chain and read back.

**That round trip proves the code agrees with itself and nothing more.** The
check that a shared convention cannot survive is the real capture reading a
programme identification that matches the station it was recorded from, and
that belongs to 02. Read `.claude/skills/dsp-validation/SKILL.md` before
writing either -- biphase polarity and the differential sense are exactly the
kind of convention an encoder and decoder will happily share while both being
wrong.

## Comments

**2026-09-03** — Done. `src/fm_dsp.{c,h}`, Probe side, libm only.
`testfiles/fm_rds_89600.bin` is the capture, two seconds at 2.048 MS/s, named
for its frequency because identifying the station needs the programme
identification code and that is 02's job.

The chain reads 78 of the 91 blocks a two-second capture can hold, and the
87.7 MHz station reads 65 of its 91 as well -- a second, weaker station
corroborating the first.

Four things were measured rather than assumed, and each was wrong first:

- **The RDS band was confirmed present before any of it was written**: pilot
  48.8 dB over the floor at exactly 19000.00 Hz, the RDS band 13 dB over, the
  RDS carrier itself only 5.6 dB. My first measurement said there was no RDS
  at all and was wrong -- a boxcar decimator folded everything above 128 kHz
  back over the band.
- **The lock detector.** Pilot size against the multiplex ranks stations
  backwards, because a loud station has more audio in the denominator. It is
  coherence on two time scales now, measured to separate two stations from an
  empty channel with 0.13 clear either side.
- **The anti-alias filter.** The integrate-and-dump manages 3.9 dB at the fold
  edge, so the stereo subcarrier folded onto the data. Three one-pole sections
  took the clean-block count from 120 to 153 on the four-second recording.
- **The matched filter shape.** A sine-weighted biphase filter was tried
  against the rectangular one and gave 153 hits against 153. The rectangular
  one stays: measuring said the complexity would buy nothing.

The convention the ticket warned about is settled, and by the capture rather
than by a round trip. Running the published (26,16) block code over the bits:
the right differential sense gives 94 syndrome hits with 78 on a single 26-bit
alignment; reading it backwards gives chance. So "no change means zero" is
right, as IEC 62106 says. `check-fm-dsp` carries that as a check, with its own
copy of the syndrome routine -- an oracle that shares code with what it checks
is not an oracle.

Biphase polarity turned out to need no check at all: it flips the sign of every
symbol and the differential decoder multiplies consecutive symbols, so it
cancels. That is what differential encoding is for.

**Left for 02, deliberately:** the station is unidentified, and the matched
filter is rectangular where the transmitted pulse is shaped -- worth about a
decibel, and not worth taking before there is a decode to measure it against.
