# DSP benchmark report

## Feature description

Extend the existing DSP benchmark into a capture-driven report of what each
shipping real-time DSP path costs. The report should show direct end-to-end
time for a block, diagnostic time for each public step in that path, the block's
sample-time budget, and the remaining headroom before the path overruns and
ADR-0002 begins dropping blocks.

The report has two outputs from one run: a human-readable table and versioned
JSON for comparison over time. It covers the generic acquired-signal frame and
the GSM, LTE, ADS-B, FM, TETRA and SRD decode sessions using representative
committed captures. Existing primitive and survey measurements remain
available.

Timing is not a correctness gate. Machine load, clock policy and thermal state
move wall-clock results, so `make check` must not fail on an absolute duration.
Decode invariants are established before timing and a report identifies the
host/build context needed to compare runs honestly.

## Measurement contract

- The budget is derived from pairs processed divided by that path's sample
  rate, not from a universal 65.5 ms constant. LTE therefore keeps its 1.92
  MS/s budget.
- Warm-up iterations are excluded.
- Each stage and chain reports minimum, median and p95 milliseconds per block.
- Headroom is `budget_ms - p95_ms`; negative headroom is labelled `overrun`.
- The directly timed end-to-end chain is authoritative. Stage measurements
  are diagnostic and are not asserted to sum to the chain total because cache
  state and repeated setup differ.
- A public operation that internally contains several algorithms is one step.
  Production modules do not gain timing callbacks solely to expose private
  implementation details.
- Offline analyses without a per-block delivery deadline report throughput,
  not invented real-time headroom.

## Plan

1. Establish the timing statistics, JSON schema, host/build metadata, budget
   arithmetic and one complete reference chain in the existing benchmark.
2. Add representative fixtures and adapters for every remaining shipping
  decode session, preserving each capture's format, full scale and sample
  rate. The adapters call shipping session interfaces; they do not reproduce
  decode-chain orchestration inside the benchmark.
3. Keep the report outside `make check`, document how to perform a same-process
   or null comparison, and make unsupported/missing optional captures explicit
   rather than silently benchmarking noise.

## Constraints

- Preserve the current `make bench-dsp` entry point.
- Compile the same shipping DSP/session sources and optimization flags as the
  application.
- Never substitute synthetic noise when a named real-capture benchmark fixture
  is missing; skip an explicitly optional fixture or fail visibly.
- Validate expected decode/session outcomes before reporting their timing.
- Do not time file I/O as DSP unless a separately named load measurement is
  requested.
- Keep benchmark output stable enough for scripts without making wall-clock
  values test assertions.
- Keep timing policy in the benchmark report module and technology behavior in
  the shipping session modules.

## Tickets

1. `issues/01-report-contract-and-reference-chain.md`
2. `issues/02-all-shipping-decode-chains.md`

Tickets are ordered by dependency.
