# 04 — Mobile country code, network code, location area

Status: needs-triage
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
