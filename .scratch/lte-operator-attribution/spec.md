# Which operator is each decoded cell

The LTE chain names a cell by its Physical Cell Identity and nothing else.
That is not an oversight: the operator's identity rides in System Information
Block 1, SIB1 rides the whole carrier, and the carrier is 4.5 or 9 MHz against
the 1.92 MS/s the decoder takes (`.scratch/lte-sib1/spec.md`). **Nothing in
the LTE chain has ever read an MCC, an MNC or a cell global identity, and
nothing here proposes that it should.**

So the question "whose cell is that" is answered outside the decoder, by
where the carrier sits in the licensed band plan. This file records what that
attribution is at the `home-sala-estar` site, how much of it is measured and
how much is inference, and the one carrier it cannot place.

Site: Campo de Ourique, Lisboa. Recorded as `home-sala-estar` in the
configuration, which carries no geography -- worth knowing before comparing
any of this with a sweep taken elsewhere.

The neighbourhood is as precise as this file goes, and deliberately: it is
enough to say which cell sites are in range, which is all the attribution
argument needs, and `.scratch/` is committed and pushed like the rest of the
repository. A street address would be published with it.

## What the receiver decoded

Band 20, from `--lte-chain` and the captures under `captures/`:

| EARFCN | DL MHz | Cell                                   | Reads |
| ------ | ------ | -------------------------------------- | ----- |
| 6200   | 796.0  | PCI 28, 50 blocks (9 MHz), 2 ports     | 158 messages / 175 |
| 6300   | 806.0  | PCI 59, 2 ports                        | 215 / 292 |
| 6400   | 816.0  | none confirmed                         | -- |

Band 8, the second row re-walked live on 2026-09-06:

| EARFCN | DL MHz | Cell                                    | Reads |
| ------ | ------ | --------------------------------------- | ----- |
| 3475   | 927.5  | PCI 330, 25 blocks (4.5 MHz), 4 ports   | 264 / 292 |
| 3625   | 942.5  | PCI 402, 25 blocks, 2 ports, -35.1 dBFS | 119 / 122 looks, `confirmed` |
| 3625   | 942.5  | PCI 190                                 | 0 / 9 looks, `unread` |

The two cells on 3625 have changed places. The 2026-09-06 note in
`.scratch/lte-more-per-carrier/issues/01-every-cell-on-the-carrier.md` has
PCI 190 as the one that decodes; a 254-block run the same day has PCI 402
reading 119 messages and PCI 190 reading none. Both are real, and which one
wins is a level question that moves.

## What GSM decoded, at the same site

GSM does carry the operator's identity in a broadcast this receiver can read,
and four channels place four holders on the 900 MHz band without appealing to
any table:

| ARFCN | MHz   | Result                                             |
| ----- | ----- | -------------------------------------------------- |
| 69    | 948.8 | MCC 268 **MNC 03 (NOS)**, LAC 4010 -- capture, off air since |
| 113   | 957.6 | MCC 268 **MNC 06 (MEO)**, LAC 8420 -- live          |
| 14    | 937.8 | BSIC 10, **NCC 1** -- live, no System Information in 25 s |
| 44    | 943.8 | nothing on air                                     |

NCC 1 at 937.8 is neither NOS's 7 nor MEO's 4, so it is the third operator;
that is an ordering argument, not a decode, and the MNC there is still unread.

## The licensed band plan, and where it fails

Band 20 is unambiguous and the three blocks are 10 MHz each: MEO 791-801,
Vodafone 801-811, NOS 811-821. Every decoded carrier is centred on its
block -- 796, 806, 816 -- and PCI 28's 50 blocks occupy 791.5-800.5, inside
MEO's. The fit is corroboration and not coincidence.

Band 8 is where the published table breaks. spectrum-tracker gives DIGI
925-930, Vodafone 930-935 and 935.1-940.1, NOS 943.1-950.9, and **no band 8
allocation for MEO at all** -- which the ARFCN 113 decode above refutes
outright. So its edges near 940-943 cannot be trusted either, and that is
exactly where a decoded carrier sits.

- PCI 330 at 927.5 occupies 925.25-929.75, inside DIGI's 925-930, whose
  rights run 17.01.2022 to 17.01.2042. A 4.5 MHz carrier centred in a 5 MHz
  block is as tight a fit as band 20's.
- PCI 402 and PCI 190 at 942.5 occupy 940.25-944.75, straddling what the
  table calls unassigned and the bottom of NOS. That is ticket 01.

## Not tickets yet

**EARFCN 6400 confirms nothing.** 816 MHz is NOS's own band 20 block, it is
the one band 20 channel with no confirmed cell here, and
`.scratch/lte-cell-search/issues/05-the-broadcast-channel-on-air.md` records
that the capture misreads the cyclic prefix. Whether that is a weak cell, no
cell, or the decoder giving up on a weak one is unmeasured, and the three
answers want different work.

**CellMapper cannot be read by an agent.** The map is behind a login and the
`api.cellmapper.net` response body opens by forbidding programmatic use of the
data. Anything it can settle has to be settled by a person, which is what
makes ticket 01 `ready-for-human` rather than `ready-for-agent`.
