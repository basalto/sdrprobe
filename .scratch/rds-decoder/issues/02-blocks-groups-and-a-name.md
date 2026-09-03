# 02 — Blocks, groups, and what the station calls itself

Status: resolved
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

## Comments

**2026-09-03** — Done. `src/rds.{c,h}`, Decoder side, libm only. 77 checks in
`check-rds`.

**The floor first, as the ticket asked.** A ten-bit syndrome matching one of
five offset words happens by chance about 4883 times per million bit
positions; measured on 400000 noise bits it happens 4813 times per million,
which is 1.5% from the prediction. So a lone block agreeing means nothing, and
the synchroniser wants four in the offset order before it believes an
alignment -- predicted at 0.000036 per million. On those same 400000 noise
bits it finds zero groups.

**The capture reads TSF**, and the ticket's corroboration rule is satisfied by
three facts from three different places in the signal:

- the programme service name, assembled from block 4 of eight separate group
  0A transmissions, reads `TSF`;
- the programme type, which lives in block 2 of *every* group and has nothing
  to do with the name, reads `news` -- and TSF Radio Noticias is a news
  station;
- the identification is 0x8343 and the other station recorded at this site the
  same evening reads 0x8442, sharing the top nibble and nothing else, which is
  what a country code does.

The capture is `testfiles/fm_rds_tsf.bin` now, renamed from its frequency, and
its sidecar records all three.

A second station corroborates the machinery rather than the station: 87.7 MHz
gives PI 0x8442, PS `RDS`, PTY `pop music` over 16 groups.

**A half-filled name is not shown.** Two characters at a time means `RADIO 1`
passes through `RA`, `RADI`, `RADIO ` on its way, and each is a station that
does not exist. A name needs all four segments *and* a repeat, because one
pass through four segments can be four segments of two different names and
look perfect. Both cases are checked, as is a name changing mid-assembly not
being spliced onto the old one.

**Radio text is implemented and does not complete in two seconds**, which is
correct rather than a gap: sixteen segments at roughly one 2A group a second
needs about thirteen. `rt_valid` stays false and nothing is shown, which is
the same rule as the name.
