# 04 - Block arithmetic in samples, not bytes

Status: resolved for the arithmetic, 2026-09-08 -- **and its "Not in scope"
line is now contradicted by measurement.** See Finding 3 in ticket 01: keeping
the block in bytes costs `gsm_arfcn_69` five of its seven broadcast messages,
System Information 3 among them. That needs a decision and a new ticket; it was
not taken here.

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


## What was done

- `acquisition` carries `bytes_per_pair` from the source's profile.
  Playback paces by `SAMPLE_BLOCK_BYTES / bytes_per_pair` rather than by
  `SAMPLE_BLOCK_PAIRS`, which would have run a 16-bit capture at twice real
  time.
- The recording arithmetic -- the byte limit and the sidecar's
  `duration_seconds` -- multiplies by the container's width instead of by 2,
  in three places.
- `SAMPLE_BLOCK_PAIRS` keeps its value and gains a comment saying what it
  actually is: **the array bound**, correct because it is the largest of the
  two, and not a general answer to how many pairs a block holds. Ask
  `device_pairs_per_block()` for that.
- `check-device-profile` covers pairs and seconds at both formats, as the
  acceptance criteria ask.

## The line that did not survive contact

> **Not in scope:** Making the block size itself configurable. It stays
> dump1090's.

Written before the built program had ever read a 16-bit capture. It had.
A block of `SAMPLE_BLOCK_BYTES` covers 32.8 ms at four bytes a pair instead of
65.5. GSM's System Information needs four normal bursts found **after** the SCH
and inside the same block -- `gsm_read_broadcast()` refuses with "the block ran
past the end of this sample block" -- so `gsm_arfcn_69` reads two messages where
it read seven, System Information 3 among the five it loses. LTE doubles its
Master Information Blocks for the same reason. Identities never move; counts
do, and one pinned answer vanishes.

"It stays dump1090's" is right. The question the line does not settle is
**dump1090's what** -- its 262144 bytes, or its 131072 pairs. Those were the
same number for as long as there was one container. They are not any more, and
the pair reading is the one that keeps a decode.

Not taken here, because it is a spec decision and because it doubles three
block buffers for a device nobody has plugged in. `check-pipelines` asserts
the current cost so the choice stays visible.
