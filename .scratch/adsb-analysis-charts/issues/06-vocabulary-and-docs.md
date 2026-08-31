# 06 — Vocabulary and documentation

Status: resolved
Blocked by: 05

- `docs/contexts/decoder/CONTEXT.md`: add **Frame trace**, **Preamble score**
  and **Decision margin** with their _Avoid_ lines, beside the terms the GSM
  burst charts added. The _Avoid_ lines carry the load here: "constellation"
  and "symbol" must stay out of this view, and "coherence" belongs to the Probe
  context's tone detector, not to a preamble match.
- `src/overlay_help.c`: extend the **ADS-B log** topic, or split a new one, to
  cover the three charts and the scatter — what each plots and what a bad one
  looks like. Keep the existing rule that every figure quoted comes from a
  constant, not from memory.
- `AGENTS.md`: the ADS-B view's paragraph, and `src/adsb_layout.h` /
  `src/overlay_help.c` in the file list.
- `README.md`: one line in the feature list.
- `docs/sdrprobe-implementation.md`: the view's section.

No ADR is contradicted by this feature. If the funnel counters end up changing
what `adsb_demod()` accepts rather than only what it reports, that **would**
touch ADR-0009 and needs saying out loud before it lands.

## Comments

**No section added to `docs/sdrprobe-implementation.md`.** That document is the
original implementation spec, and it has no ADS-B section to extend -- its scope
list still records Mode S decoding as out of scope, which is historically true
of the cut it describes. Adding a section would mean retrofitting a spec to
match code written after it. The view is documented in `AGENTS.md`, the README
and the help overlay instead.

**A fourth term earned its place:** *decode funnel*, for the counters. Without a
name for them the header line has to be described in terms of what it is not.
