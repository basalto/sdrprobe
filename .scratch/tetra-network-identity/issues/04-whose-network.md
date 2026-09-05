# 04 — Mobile country code, network code, location area

Status: resolved
Blocked by: 03

The answer the spec is named after. Past the synchronisation block, the
broadcast network channel carries the identity a terminal needs to decide
whether it may camp: mobile country and network code, location area, and the
service the cell offers.

## What must be checkable

- A fixed vector decoding to a known identity, field by field.
- **The real capture, and corroboration from outside it.** MCC 268 is Portugal
  and `gsm_bcch` already reads 268 from an entirely different technology on a
  different band -- two independent chains agreeing on a country code is the
  kind of evidence a round trip cannot give (`dsp-validation`). If this reports
  a country that is not 268 it is wrong, and if it reports 268 that is worth
  something.
- `check-pipelines` pins it against the capture, as GSM's MCC 268 MNC 03 and
  the LTE cell identity are pinned.

## Comments

**2026-09-06 — half of this is already done, by ticket 03.**

The SYNC PDU's last 29 bits are the D-MLE-SYNC PDU, so the mobile country and
network codes come out of the synchronization block itself, not from a separate
channel: **MCC 268, MNC 3**, constant across all 202 bursts of the capture,
with colour code 17. MCC 268 is Portugal and `gsm_bcch` reads the same 268 from
a different technology on a different band, which is the corroboration this
ticket asked for.

What is left is the rest of the identity -- the location area, the subscriber
class, whether the cell is barred -- and it is on the broadcast network channel
rather than here: block 2 (216 bits) and the 30 broadcast bits of the same
burst, per table 9.9. That needs its own coding chain (clause 8.3.1.4.1 for
BNCH) and its own PDU parse (D-MLE-SYSINFO).

So this is now a smaller ticket than it was, and a well-posed one: the burst is
already located to the symbol and the parity already passes on block 1.

**2026-09-06 — resolved. The broadcast channel decodes too.**

The rest of the identity is on the broadcast network channel, in block 2 of the
same burst: 216 scrambled bits at bits 283 to 498, which is 34 symbols after
the synchronization training sequence begins. The chain is the same shape at
different lengths -- (140,124) over 144 type-2 bits with a (216,101)
interleaver, against the synchronization block's (76,60) over 80 with (120,11)
-- so `tetra_sync.c` now has one chain function and two callers rather than two
chains and two places to get the scrambler wrong.

**190 of 202 blocks pass their parity**, and what they say is constant:

```
LA 4375   subscriber class ffff   service details d67
```

The twelve that fail are what a 216-bit block at rate 2/3 does where a 120-bit
one survives; nothing is reported from them.

### The bit order was settled by the parity, not by reading

The 30-bit extended colour code is MCC, MNC and colour code concatenated, and
the ordering is in figure 23.5 -- which is an image and does not survive
extraction from the PDF. So it was settled the way everything else here has
been: four candidate orderings, tried on air.

| ordering | blocks passing |
| --- | --- |
| **MCC(10), MNC(14), colour(6), MSB first** | **190** |
| colour, MCC, MNC | 0 |
| MNC, MCC, colour | 0 |
| all three LSB first | 0 |

190 against 0, 0 and 0. A 16-bit parity does not pass 190 times by accident,
and this is the same instrument that caught the scrambler seed and the phase
table -- the only one in this effort that has ever been able to tell a right
transcription from a wrong one.

The check asserts the colour code is load-bearing rather than decorative: the
same block read with all zeros, or with a colour code one out, does not decode.
Without that, the scrambling would be doing nothing and any network's broadcast
would read as any other's.

### What the effort set out to answer

**MCC 268 (Portugal), MNC 3, colour code 17, location area 4375.**
