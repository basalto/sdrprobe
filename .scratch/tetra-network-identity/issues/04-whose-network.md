# 04 — Mobile country code, network code, location area

Status: ready-for-agent
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
