# 02 — Blocks, groups, and what the station calls itself

Status: needs-triage
Blocked by: 01

`src/rds.{c,h}`, Decoder side, beside `gsm_bcch.c` and `lte_mib.c`: soft bits
in, a message out, no samples and no receiver.

## Synchronisation is a search, so it needs a floor

RDS has no preamble. A block is 16 data bits and 10 check bits from a
shortened cyclic code, and one of five offset words is added to the check
before transmission -- so a receiver finds the block boundary by sliding the
code along the bitstream and seeing which alignment and which offset make the
syndrome vanish.

That is a search over every bit position and five offsets, and a 10-bit
syndrome vanishes by chance about once in a thousand tries. **Establish what
the search scores on noise before believing what it scores on signal.** A
group is four blocks in a fixed offset order, which is the constraint that
makes a false lock unlikely -- and requiring the programme identification to
repeat across groups is what makes it safe, the same rule
`lte_mib_same_cell()` exists for.

## What to report

- Programme identification, from block 1 of every group.
- Programme type and traffic flags, from block 2.
- Programme service name, eight characters assembled from four segments of
  group 0A/0B -- and only shown once every segment has arrived, because a
  half-filled name is a wrong name rather than a partial one.
- Radio text from group 2A/2B, if it comes cheaply.

## The check that matters

`testfiles/fm_rds_<station>.bin` must keep decoding its own programme
identification and programme service name, the way the three GSM captures keep
decoding their own BSICs. A synthesised round trip will pass against any
polarity convention the encoder shares with the decoder; the real capture will
not.

Corroborate the name before pinning it: the programme identification code is
allocated, so the name it decodes to and the country and station the code
implies have to agree with each other. Two independent facts about one signal
is the standard this repository holds itself to.
