# The band plan is a lookup, not an identification

## Status

accepted

## Context and decision

The band survey (Scope view 5) finds carriers and reports what can be measured
about them: power, occupied bandwidth, prominence above the local floor,
carrier offset, duty and frequency stability. Alongside those it prints the
allocation the frequency falls in, from a static table in `src/band_plan.c`.

That table is the first thing in this program that attaches meaning to a
frequency, and it sits directly against the Probe context's boundary:
`CONTEXT.md` says this context ends at signal statistics and never claims to
have decoded a message. A line reading "GSM 900 downlink" under a measured
carrier is one careless reading away from "this is GSM", which the program has
not established and cannot establish without demodulating.

The decision is to keep the two separable in the words, not only in the code:

- The table is named for what it is — a **band plan** — and its glossary entry
  says it "says what a frequency is *for*, never what a signal *is*".
- The UI prints the allocation with the qualifier under it: *a frequency lookup,
  not a detection*.
- The handoff button says *Inspect in Decode ▸ GSM*, which invites the operator
  to go and find out. It does not say "GSM found".
- `band_plan_lookup()` returns NULL outside the table rather than the nearest
  entry. A frequency the table does not cover gets silence, not a guess.

## Consequences

- The survey may be pointed at a band and report a carrier that is not the
  service allocated there — an interferer, a harmonic, a leaking cable
  amplifier. The wording holds up in that case, which is the point: the table
  was never claiming otherwise.
- The table is ITU Region 1 (Europe). Elsewhere entries will be wrong for the
  local allocation. Because the output is framed as a lookup rather than a
  detection, that is a table to correct rather than a claim to retract, and the
  header says which region it encodes.
- Adding a modulation guess ("looks like GMSK") was considered and rejected
  during triage: a guess that is confidently wrong costs more than no guess,
  and it would put an inference where this context promises a measurement.
- If a later cut wants to *identify* a signal, that belongs in the Decoder
  context behind a demodulator, not in this table.
