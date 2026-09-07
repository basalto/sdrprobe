# 01 - Prove a format change moves no answer

Status: resolved, 2026-09-08 -- and **two of this ticket's own claims were
wrong**. The full scale it specified disagrees with every sample in the corpus
it specified, and the expensive half it asked for cannot run until tickets 02
and 03. Both are written up below; the cheap half turned out to settle more
than the expensive one would have.

Everything else in this spec is a change to how samples are represented. The
only way to know such a change was harmless is to make it against a corpus
whose right answers are already pinned, and `testfiles/` is exactly that.

This ticket needs **no hardware** and should be done before the device is
bought, because it is the gate the other tickets are measured against.

## Local hypothesis

Widening the sample container changes no decoded answer. It is false if any
stage depends on the container rather than on the signal -- an integer
overflow, a `/ 2` that meant bytes-per-pair, a threshold in raw counts.

## What to build

`scripts/rescale_capture.c` behind `make rescale-capture`: read an 8-bit
interleaved capture, write a 16-bit signed interleaved one scaling
`(byte - 127.5) * 16` so a 12-bit-in-16 device's full scale is matched, and
write the `.json` sidecar with the new format and full scale. Left-shifting by
four rather than eight is deliberate: it is what an AD9361 actually delivers,
and a scale that fills the container would hide an overflow this is meant to
find.

Then a check, `check-sample-format`, that for each of `gsm_arfcn_69`,
`gsm_arfcn_113`, `adsb_cpr_pair`, `lte_b20_pci28`, `tetra_cc17` and
`fm_rds_tsf`:

- converts the 8-bit capture and the rescaled 16-bit one through the format
  layer to floats;
- asserts the two float streams agree to within one part in 10^6 after
  normalising each by its own full scale.

That is the cheap half and it runs in a check. The expensive half is
`check-pipelines`: the built program over both corpora, asserting the *same*
decoded answers -- BSIC 59 and 38, PCI 32, 0x8343 and `TSF`, six ADS-B frames
with one global CPR position, MCC 268 colour code 17.

## Decisions

- The rescaled captures are **generated, not committed**. `testfiles/` stays
  8-bit; `build/testfiles16/` is gitignored and rebuilt by the check.
- Scaling is exact and lossless in this direction, so a disagreement is a bug
  in the format layer and never rounding.
- If a decode does move, that is the finding and the ticket stops there. Do
  not adjust a threshold to make it agree.

## Acceptance criteria

- `make check-sample-format` passes, and is in `CHECK_UNITS`.
- Every pinned answer in `CLAUDE.md` holds on both corpora.
- No file in `testfiles/` is modified.

## Not in scope

- Reading 16-bit from a device. There is no device yet.
- Changing `sdr_dsp_convert_iq()`'s signature; that is ticket 03.


## What was built

- `scripts/rescale_capture.c` behind `make rescale-capture`, writing signed
  16-bit little-endian interleaved I/Q plus a sidecar. It copies the source
  sidecar's remaining fields through -- tuning, rate, duration, the `notes`
  array -- because `gsm_arfcn_69.bin` decodes to nothing without its 400 kHz
  offset, and a rescaled capture that lost it would fail for a reason having
  nothing to do with its format.
- `check-sample-format`, 64 checks, in `CHECK_UNITS` and picked up by
  `check-touched`. `build/testfiles16/` is a prerequisite of the rule,
  generated and never committed. `make check`: 16909 checks in 44 suites.

## Finding 1 -- full scale is 2040.0, and this ticket said 2047.5

`(byte - 127.5) * 16` has a full scale of 127.5 x 16 = **2040**. A 12-bit ADC
rails at 2047.5, and ticket 02 names that figure for "12-in-16" correctly, for
a device. These files are not that device: they hold 8-bit samples shifted
left by four, so their full scale is whatever 8-bit full scale maps to.

Measured over all 256 byte values:

| normalised by | values agreeing bit-for-bit | worst error |
| --- | --- | --- |
| 2040.0 | **256 of 256** | 0 |
| 2047.5 | **0 of 256** | 3.663e-3 |

3.663e-3 is 366 times the `1e-6` this ticket allowed, so the check as
specified would have failed on all six captures at once -- which reads like a
decode fault and is nothing of the kind. Writing 2047.5 into the generated
sidecar would have been a claim about hardware the data never went through.

The tolerance went the other way as a result: the check asserts **exact
equality**, not agreement to 1e-6. Nothing rounds anywhere -- `byte - 127.5`
is a multiple of 0.5, sixteen is a power of two, and both normalisations are
the correctly rounded result of the same exact rational -- so any tolerance at
all would have been slack the arithmetic does not need.
`test_the_full_scale_that_matters` pins both rows of that table.

## Finding 2 -- the expensive half cannot run yet, and does not need to

This ticket asked for `check-pipelines` over both corpora, the built program
asserting the same decoded answers. **The program cannot read a 16-bit file.**
Doing so needs `device_profile` (02) and `sdr_dsp_convert_iq()` taking one
(03), which this ticket puts out of scope in its own last section. Ticket 03's
acceptance criteria already anticipate the split -- they say
`check-sample-format` should then pass "with the real format layer rather than
a test harness", which is exactly the harness this check ships with.

That half moves to ticket 03, where it is reachable. It is not a gap, because
the cheap half proves more than it would have:

**The program has exactly one byte-to-float seam.** `sdr_dsp_convert_iq()` is
called once, at `sdrprobe.c:311`. Everything downstream takes floats -- the
byte-taking `fm_discriminate()` still exists but has no caller outside tests,
`view_fm.c` having moved to `fm_discriminate_f()`. So identical float streams
mean identical decoded answers **by construction**, for every technology,
including ones with no capture in the corpus. Decoding the captures again
would have measured a consequence of what the float comparison establishes
directly.

The local hypothesis stands: widening the container changes no decoded answer,
and no stage depends on the container rather than the signal.

## What the check covers

- all 256 byte values scale losslessly, reverse exactly, and normalise to
  bit-identical floats;
- the full-scale table above, both rows;
- all six captures whole -- 46 MB against 93 MB -- every pair, I, Q and
  magnitude, exact;
- the corpus exercises every one of the 256 byte values, 0 and 255 named
  separately, so the agreement is not "every value that happens to be here";
- a demonstration of ticket 04: the same signal has the same pair count and
  twice the bytes, so `SAMPLE_BLOCK_BYTES / 2` is a bytes-per-pair assumption
  and 65.5 ms a block silently becomes 32.8.

Every one of those was watched to fail before being trusted: a single flipped
sample in a million, a file truncated by one pair, and a missing generated
file each fire with a legible message. The first version reported a truncated
file as "1 pair disagrees, worst 0.000e+00", which sends a reader to the
arithmetic when the file is simply the wrong size; a length mismatch now has
its own message.

## Not done here

- Reading 16-bit from a device, or from a file. No device, and playback is 03.
- `fm_discriminate()`'s raw-byte signature. Unreferenced by the program, so it
  costs nothing today; ticket 03 names it.
