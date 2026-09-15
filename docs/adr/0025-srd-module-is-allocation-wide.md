# The SRD module is allocation-wide, not appliance-specific

## Status

accepted

## Context and decision

The first signals decoded in the 430-440 MHz short-range-device allocation
came from remote controls, which made an appliance-specific module name
available. The
same allocation also carries sensors, weather stations, doorbells, telemetry,
and other remotes. They may use different protocols while sharing useful
operations such as transmission discovery, OOK or 2-FSK demodulation, chip
recovery, and Manchester decoding.

The technology module is therefore named and scoped `srd_` for the allocation,
not for the first appliance observed in it. Protocol-specific frame recognition
may coexist in that module while it remains small, but each recognition must
be named from evidence in the recovered frame rather than inferred from the
frequency, modulation, or appliance that motivated the code.

This scope does not make every signal in 430-440 MHz an SRD device. In Probe
language the module may report an **SRD transmission** located in that working
range. Only recovered transmitted information becomes an **SRD frame** in the
Decoder context, following ADR-0021.

## Considered options

- **Use an appliance-specific module name** gives the first implementation a precise name
  but makes shared demodulation appear specific to one appliance and requires
  a broad rename as soon as another device uses it.
- **Create one module per observed protocol** keeps frame formats isolated but
  duplicates the shared signal and line-code chain before protocol differences
  require separate ownership.
- **Treat every signal in the allocation as one protocol** makes the module
  superficially uniform by turning frequency and modulation measurements into
  identifications they cannot support.

## Consequences

- New work in this allocation starts by asking whether it deepens shared SRD
  operations or introduces a protocol whose complexity warrants its own
  module; the `srd_` prefix alone does not settle that later decision.
- Appliance and device names are returned only where recovered frame structure
  supports them. Unknown and generic frames remain explicitly unknown.
- Shared allocation scope does not introduce a uniform technology interface;
  ADR-0023 still lets the module expose only the operations its signals need.
- The view, checks, diagnostics, and future shared operations keep the `srd_`
  namespace rather than being renamed after whichever device supplied the
  latest capture.