# 01 - Who owns the carrier at 942.5 MHz

Status: ready-for-human

EARFCN 3625 carries two cells this receiver decodes -- PCI 402 reading 119
Master Information Blocks in 122 looks on 2026-09-06, and PCI 190 alongside it
-- and there is no published allocation they fit.

The carrier is 25 resource blocks, so it occupies **940.25-944.75 MHz**. The
band 8 table this repo could find puts Vodafone's upper block at 935.1-940.1
and NOS's at 943.1-950.9, leaving 940.1-943.1 unassigned. A 5 MHz channel
centred on 942.5 fits neither, and overlaps both edges.

## Why the decoder cannot answer it

The operator's identity is in SIB1, SIB1 rides the whole carrier, and the
carrier is wider than the 1.92 MS/s the decoder takes. This is the same wall
`.scratch/lte-sib1/` is closed against, and nothing about it has changed.
**No amount of work on `lte_dsp.c` reaches this answer.**

## What is already ruled out

Two GSM decodes at this site read the operator's identity directly and bracket
the carrier without settling it:

- ARFCN 113 at 957.6 MHz reads MCC 268 MNC 06, so **MEO holds the top of the
  band** -- which the published table omits entirely, and which is the reason
  its edges near 940-943 are not evidence.
- ARFCN 69 at 948.8 MHz read MCC 268 MNC 03, so NOS is above 943.1 as listed.
  That cell has since gone off the air with refarming, which is itself the
  likeliest explanation for an LTE carrier appearing at the band's low edge of
  NOS's holding.

A live check of ARFCN 44 at 943.8 MHz found **nothing on air** -- consistent
with NOS having refarmed that part of their block, and consistent with the
carrier being theirs, but it identifies nobody.

## What would settle it

Any one of these, in decreasing order of how quickly it can be done:

1. **CellMapper, logged in.** Filter MCC 268, LTE, and look for a site in
   range of the receiver reporting **EARFCN 3625 with PCI 402 or PCI 190**. The MNC on the matching entry is the answer. Try MNC 03 (NOS)
   first. An agent cannot do this: the map is login-walled and the API forbids
   programmatic use of the data.
2. **The MNC at 937.8 MHz.** ARFCN 14 gave BSIC 10, NCC 1, but no System
   Information in 25 s. A longer dwell -- `./sdrprobe --headless --arfcn 14
   --decode --duration 90` -- reads MCC/MNC and pins the holder immediately
   below the disputed range. If it is Vodafone, 940.1 is a real boundary and
   the carrier is NOS's, spilling below the listed edge.
3. **ANACOM's assignment itself.** The 2026 renewal decision splits each
   operator's 900 MHz holding in half by expiry date, so a current per-block
   plan exists on paper. The block boundaries in that decision, not a
   third-party table, are the authority.

## What must be checkable

Nothing in the program changes, so nothing new becomes checkable. The answer
is a fact about the environment and belongs in
`.scratch/lte-operator-attribution/spec.md`, not in a `check-*` suite --
the band plan `check-band-plan` covers is allocations, not licensees, and
adding licensees to it would put a fact that changes with a regulator's
decision behind a green tick.

If the answer turns out to be that the licensed edges differ from the table,
that is worth a line in `src/lte_dsp.c`'s band table comment, which currently
says only `{ 8, 3450, 3799, 925000000.0, "900 MHz" }`.

## Comments
