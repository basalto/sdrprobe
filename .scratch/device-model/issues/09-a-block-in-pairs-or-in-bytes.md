# 09 - A block is dump1090's 131072 pairs, or its 262144 bytes. Which?

Status: resolved, 2026-09-08. **Pairs.** The device is real, so the memory is
rounding error beside what the device implies anyway. Both corpora now produce
byte-identical decode output.

Opened 2026-09-08 by a measurement, not by a design idea. Ticket 01's
expensive half finally ran -- the built program over an 8-bit corpus and a
16-bit one -- and the hypothesis it was testing came back false. Not for any
reason that ticket feared: there is no overflow, no threshold in raw counts,
and every decoded **identity** is unchanged. The cost is a block's duration.

## What was measured

| | 8-bit | 16-bit |
| --- | --- | --- |
| every identity: BSIC 59, cell 28 + 2 ports, colour 17 / LA 4375, 0x8343 `TSF`, CPR positions | | **unchanged** |
| LTE Master Information Blocks | 28 | 55 |
| `gsm_arfcn_113` System Information 3 | 2 | 1 |
| `gsm_arfcn_69` broadcast messages | 7 | **2** |
| `gsm_arfcn_69` System Information 3 | 1 | **0** |

A block is `SAMPLE_BLOCK_BYTES` -- 262144 of them -- so at four bytes a pair
it covers 32.8 ms rather than 65.5. Twice as many blocks, each holding half
the signal. LTE reads one message per block and doubles.

GSM loses messages, and the mechanism is `gsm_read_broadcast()`'s own refusal:
*"the block ran past the end of this sample block"*. Four BCCH bursts must be
found **after** the SCH and inside the same block, spanning about 18.5 ms.

| `gsm_arfcn_69` | 8-bit | 16-bit |
| --- | --- | --- |
| SCH decodes | 31 | 42 |
| eligible (`frame % 51 == 1`) | 7 | **9** |
| broadcast messages read | **7** | **2** |

The chances go up and the conversion collapses -- 7 of 7 against 2 of 9 --
because a half-length block usually has no room left after the SCH. This is
worth stating carefully because the first write-up said ARFCN 69 lost its
broadcast "entirely" and blamed four bursts not fitting in 32.8 ms. Neither is
true: it reads two messages, and four bursts do fit. What does not fit is four
bursts *after the SCH*.

`check-pipelines`, section "A wider container", asserts all of the above
including the absence, so this cannot go quiet while the question is open.

## And it costs processing time, not only a decode

Measured 2026-09-08, headless and therefore unpaced, so wall time is
processing time. Twice a run each:

| capture | 8-bit | 16-bit |
| --- | --- | --- |
| `gsm_arfcn_69` | 812 / 868 ms | 793 / 806 ms |
| `tetra_cc17` | 1552 / 1575 ms | 1496 / 1565 ms |
| `lte_b20_pci28` | 746 / 768 ms | **1146 / 1183 ms** |

GSM and TETRA are unchanged, as they should be: the same samples get the same
per-sample work, just in twice as many half-sized helpings.

**LTE is about 55% slower**, and that is the block count rather than the
format. A cell search costs a fixed amount per block -- the PSS correlation and
the integer frequency sweep do not shrink much when the block does -- so
halving the block and doubling the count nearly doubles that fixed cost. It
reads 55 Master Information Blocks where it read 28 and pays for every one.

This strengthens the "pairs" answer rather than merely adding to it: a block
that always holds 131072 pairs keeps the block count constant, so this cost
does not arise at all.

## The question

Ticket 04 says the block size "stays dump1090's", and that is right. It does
not settle **dump1090's what**: its 262144 bytes, or its 131072 pairs. Those
were the same number for as long as this program had one sample container.
They stopped being the same number the day it had two.

## The two answers

**Bytes** -- today. A block is a fixed allocation; how much signal it holds
depends on the device. Costs `gsm_arfcn_69` its broadcast on any wide-container
device, and quietly halves every timing budget in `CLAUDE.md`.

**Pairs.** The worker reads `SAMPLE_BLOCK_PAIRS * bytes_per_pair` bytes, so a
block always holds 131072 pairs and always covers 65.5 ms at 2 MS/s, whatever
the container. Every budget stays true and no decode is lost. It costs memory:
three raw buffers (`latest.data`, `raw`, `file_block`) sized for the widest
container rather than for 8-bit, about 260 KB each, so roughly 780 KB against
the ~3 MB of float arrays `struct app` already holds.

## Why it is not decided here

It is a spec decision with an ADR-0002 flavour -- the block is the unit the
whole acquisition slot is built around -- and it spends memory for a device
nobody has plugged in yet. Worth deciding deliberately rather than as a side
effect of the ticket that found it.

## What would settle it

Whether a wide-container device is actually coming (spec ticket 07 is
`needs-info` on the device choice). If it is, pairs; the memory is
rounding-error beside what a 61.44 MS/s device implies anyway. If a wide
container will only ever be the regression corpus, bytes is survivable and
this ticket becomes a `wontfix` with the measurement written down.


## What was built

`SAMPLE_BLOCK_PAIRS` is 131072 outright rather than `SAMPLE_BLOCK_BYTES / 2`,
and it is the invariant. `acquisition_block_bytes()` is that times the source's
`bytes_per_pair`; the file worker reads a block that many bytes at a time and
`published_pairs += SAMPLE_BLOCK_PAIRS` is a constant again -- correct now
rather than by luck, and in the unit real time is actually measured in.

`SAMPLE_BLOCK_BYTES_MAX` sizes the three block buffers (`latest.data`, `raw`,
`file_block`) for `SAMPLE_MAX_BYTES_PER_PAIR`, which is 4. CF32 would need 8,
nothing produces one, and `sdr_dsp_convert_iq` already refuses it -- so the
buffers do not pay for it and **`acquisition_attach_source()` refuses a wider
container rather than overrunning them**, returning negative where it used to
return void.

`open_capture()` reads the sidecar *before* rounding the file's length, because
how many bytes make a whole I/Q pair is exactly what the sidecar settles. It
was rounding to an even byte count, which is a two-byte container's answer.

`SAMPLE_BLOCK_BYTES` survives as the 8-bit block -- what an RTL-SDR delivers
and what the librtlsdr async read asks for -- and says so.

## The result

Both corpora, all six captures, full stdout: **107 lines each and byte
identical**, the only difference being the wall clock in the ADS-B timestamps,
which is when the two runs happened.

| | before 09 | after 09 |
| --- | --- | --- |
| `gsm_arfcn_69` broadcast messages | 7 -> 2 | **7 -> 7** |
| `gsm_arfcn_69` System Information 3 | lost | **read** |
| LTE Master Information Blocks | 28 -> 55 | **28 -> 28** |
| LTE processing time | +55% | **no difference** |

The LTE timing was re-measured within a single run, because
`does-it-help` now records that this machine's `powersave` governor makes any
cross-session timing comparison worthless below about 16%: 1131/1105 ms at
8 bits against 1138/1105 at 16.

## What it cost

768 KB, three block buffers going from 262144 bytes to 524288. Against the
~3 MB of float arrays `struct app` already holds, and against a device that
will stream at up to 61.44 MS/s.

## Checks

- `check-device-profile` gains `device_block_bytes()` both directions and the
  invariant that matters: the same block is 262144 bytes at 8 bits and 524288
  at 16, and **65.5 ms of signal either way**.
- `check-acquisition` covers `acquisition_block_bytes()` at both widths, the
  refusal of a container wider than the buffers, and zero meaning the house
  convention. Its "oversized block" bound moved to `SAMPLE_BLOCK_BYTES_MAX`,
  since that is what the slot actually holds now.
- `check-pipelines` flipped from asserting the cost to asserting its absence:
  seven broadcast messages with System Information 3 among them, and no more
  than 40 Master Information Blocks. Either one failing means a block has gone
  back to being counted in bytes.
