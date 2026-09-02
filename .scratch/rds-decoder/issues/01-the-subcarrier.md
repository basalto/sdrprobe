# 01 — From FM to a 1187.5 bps bitstream

Status: ready-for-agent
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
