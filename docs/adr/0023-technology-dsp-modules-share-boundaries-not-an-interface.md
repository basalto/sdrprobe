# Technology DSP modules share boundaries, not an interface

## Status

accepted

## Context and decision

ADR-0001 split generic SDR primitives from per-technology DSP and described a
plugin contract with exactly two operations: channel mapping and reference-tone
detection. That described the first GSM calibration module but not the later
systems. ADS-B has no channel map, LTE searches synchronization sequences, FM
recovers a multiplex and audio, and TETRA recovers a non-integral symbol grid.
They are statically linked and do not implement one interchangeable interface.

The canonical unit is therefore a **technology DSP module**, not a plugin. The
modules share architectural constraints: no GUI or receiver dependency,
independent hardware-free checks, technology-prefixed names, and reuse of
generic SDR primitives where those primitives fit. Each module exposes the
operations required by its radio standard; no common function table or fixed
set of hooks is required.

This revises ADR-0001's plugin contract while preserving its generic-core and
per-technology separation. It also preserves ADR-0009's conclusion that a
decode path need not force artificial reuse of generic primitives.

## Considered options

- **Keep “plugin” for a variable static interface** preserves familiar wording
  but continues to imply runtime loading or substitutability that does not
  exist.
- **Introduce a uniform interface** makes modules look interchangeable by
  forcing unrelated operations behind generic hooks, without a caller that
  needs to interchange them.

## Consequences

- Adding a technology means adding a focused module and checks, not satisfying
  channel-map or reference-tone hooks it may not need.
- Shared DSP belongs in the generic core when it is genuinely independent of a
  radio standard; reuse is preferred but not manufactured.
- Existing “plugin” wording in current guidance and source comments should move
  to “technology DSP module.” Historical ADR text remains as the record of the
  earlier model.