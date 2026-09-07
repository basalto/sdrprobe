# 01 - Prove a format change moves no answer

Status: ready-for-agent

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
