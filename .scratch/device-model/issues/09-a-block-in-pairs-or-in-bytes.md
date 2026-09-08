# 09 - A block is dump1090's 131072 pairs, or its 262144 bytes. Which?

Status: needs-triage

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
| `gsm_arfcn_69` System Information 3 | 1 | **0** |

A block is `SAMPLE_BLOCK_BYTES` -- 262144 of them -- so at four bytes a pair
it covers 32.8 ms rather than 65.5. Twice as many blocks, each holding half
the signal. LTE reads one message per block and doubles. GSM's System
Information needs **four consecutive normal bursts**, which a 32.8 ms block
cannot hold, so ARFCN 69's broadcast disappears completely.

`check-pipelines`, section "A wider container", asserts all of the above
including the absence, so this cannot go quiet while the question is open.

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
