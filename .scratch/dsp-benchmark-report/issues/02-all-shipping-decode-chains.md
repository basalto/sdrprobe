# 02 - Benchmark all shipping decode chains

Status: ready-for-agent

## What to build

Extend ticket 01's report to every shipping decode session: ADS-B, LTE, FM,
TETRA and SRD, while retaining GSM, the generic signal frame, existing
primitive rows and survey peak measurement. Each technology uses a committed
representative capture, validates the expected decode outcome, times the
assembled per-block session path directly, and reports diagnostic timings for
the public operations that compose it.

Where a technology performs work only on some blocks, report both the
per-delivered-block chain cost and the cost/count of the conditional operation.
Where a capture is deliberately optional, report a named skip and preserve it
in JSON instead of turning the missing fixture into a green measurement.

## Implementation plan

1. Give each technology a benchmark adapter that loads its sidecar metadata,
      validates the fixture and invokes its shipping session interface as the
      named end-to-end block operation, plus public diagnostic steps.
2. Run ADS-B, LTE, FM and TETRA against committed captures with established
   decoded identities; use an available non-sensitive SRD fixture or mark that
   benchmark explicitly skipped when only private rolling-code captures could
   exercise it.
3. Separate always-run work from conditional work such as LTE broadcast
   attempts and FM/RDS accumulation, and include invocation counts beside
   timing distributions.
4. Add a summary ordered by least p95 headroom so the first overrun risk is
   visible without comparing every row manually.
5. Confirm that changing the benchmark harness leaves each fixture's existing
   decoded result unchanged.

## Tasks

- [ ] Add ADS-B session total and public-stage measurements with frame and CPR
      outcome validation.
- [ ] Add LTE session total and public-stage measurements at 1.92 MS/s with
      cell and broadcast outcome validation.
- [ ] Add FM session total and public-stage measurements with RDS station
      outcome validation.
- [ ] Add TETRA session total and public-stage measurements with network
      identity validation.
- [ ] Add SRD session total and public-stage measurements using a distributable
      fixture, or an explicit optional skip when none can establish the path.
- [ ] Preserve GSM, generic frame, primitive and survey measurements in the
      unified report.
- [ ] Keep decode decisions and cross-block state in shipping session modules;
      benchmark adapters own only fixtures, invocation and outcome checks.
- [ ] Record conditional-stage invocation counts and distinguish cost per call
      from amortized cost per delivered block.
- [ ] Add a least-headroom-first summary for complete real-time chains.
- [ ] Emit all technology results and skips through the same JSON schema.
- [ ] Document fixture selection, supported comparisons and commands.

## Acceptance criteria

- [ ] One benchmark invocation reports the generic frame and all six shipping
      technology sessions in both table and JSON forms.
- [ ] Every reported technology first satisfies an established real-capture
      decode invariant; missing required captures fail visibly.
- [ ] LTE uses its 1.92 MS/s sample-time budget and every other path derives its
      own budget from fixture metadata.
- [ ] Conditional work includes invocation counts and cannot be mistaken for
      work performed on every block.
- [ ] The summary identifies the chain with least p95 headroom and labels any
      negative result as an overrun.
- [ ] Optional SRD absence is reported as a skip in both outputs, never as zero
      cost or a successful empty decode.
- [ ] Existing `BENCH_ARCH=-march=native` use remains available.
- [ ] Benchmark execution is not part of `make check` and introduces no
      machine-dependent test threshold.

## Blocked by

- Ticket 01 - Benchmark report contract and reference chain.

## Not in scope

- GUI rendering time, USB transfer latency or acquisition scheduling latency.
- Offline retrospective analysis without a block-delivery deadline.
- Private timing hooks inside monolithic DSP operations.
- A performance regression gate shared across unlike machines.

## Comments

Updated from the 2026-09-15 architecture review. Do not create a uniform
technology interface: ADR-0023 still requires technology-specific adapters
over their existing session interfaces. Report:
`/tmp/architecture-review-20260915-175714.html`.
