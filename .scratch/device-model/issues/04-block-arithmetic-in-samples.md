# 04 - Block arithmetic in samples, not bytes

Status: ready-for-agent
Blocked by: 02

`SAMPLE_BLOCK_PAIRS` is `SAMPLE_BLOCK_BYTES / 2`, and `sdr_dsp.c:78` does the
same `/ 2` again. Both mean bytes-per-pair and neither says so.

**This is the failure that shows nothing.** On a four-byte format the block
silently holds 65536 pairs instead of 131072, every timing budget in
`CLAUDE.md` is wrong by a factor of two, and because ADR-0002 has a slow
renderer drop blocks rather than lag, the symptom is lost decodes rather than
an error.

## What to build

Replace both `/ 2` with `device_pairs_per_block()`. Anywhere a duration is
computed from a block, use `device_block_seconds()`. Then find the rest: a
sweep for `/ 2` and for `* 2` against a byte count is part of this ticket, not
an afterthought.

`bench-dsp` reports against "the 65.5 ms of signal one block covers". That
number becomes the profile's, so the benchmark keeps meaning what it says.

## Acceptance criteria

- `check-device-profile` covers pairs and seconds at both formats.
- `check-acquisition` and `check-pipelines` unchanged.
- `make bench-dsp` prints the budget it actually measured against.

## Not in scope

- Making the block size itself configurable. It stays dump1090's.
