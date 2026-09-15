# 01 - Benchmark report contract and reference chain

Status: ready-for-agent

## What to build

Turn the existing DSP benchmark output into a stable human table plus versioned
JSON report, and prove the contract with the generic signal-frame work and one
complete GSM decode-session path over a committed capture.

The benchmark times the assembled path directly and also times its public
steps for diagnosis. It derives the delay budget from the actual pair count and
sample rate, reports minimum, median and p95, and computes p95 headroom. Decode
output is checked before measurements are accepted so a fast path that did no
work cannot look healthy.

The benchmark-result module owns warmup, distributions, budget, headroom,
outcome state and table/JSON agreement. It does not own GSM behavior: a thin
benchmark adapter invokes the shipping GSM session interface and validates its
established Capture outcome.

## Implementation plan

1. Introduce a small benchmark-result model for runs, statistics, budget,
   headroom and outcome validation, independent of table or JSON formatting.
2. Add warm-up and repeated monotonic-clock samples, then calculate minimum,
   median and p95 without including capture loading.
3. Record build/compiler flags, architecture, logical CPU count and available
   platform identification in the JSON so two reports can state whether they
   are comparable.
4. Time the generic signal-frame composition and a GSM session end to end,
   alongside the public conversion, frame and decode operations that explain
   the total.
5. Preserve the existing primitive rows and `BENCH_ARCH` behavior while
   replacing silent fixture fallback with an explicit failure.
6. Document interpretation, including the same-binary null experiment and why
   p95 headroom is not a portable pass/fail threshold.

## Tasks

- [ ] Define a versioned JSON schema and stable stage identifiers.
- [ ] Add `--json <path>` while retaining the human table on stdout.
- [ ] Implement warm-up, sample collection and minimum/median/p95 statistics.
- [ ] Derive block budget from pair count and sample rate for every result.
- [ ] Report p95 headroom in milliseconds and percent, with an explicit
      `overrun` state when negative.
- [ ] Benchmark the assembled generic signal-frame path and its public steps.
- [ ] Benchmark one complete GSM session path and validate its expected cell
      identity before reporting timings.
- [ ] Include compiler/build and host context in the JSON.
- [ ] Refuse a missing required capture instead of timing generated noise.
- [ ] Update benchmark documentation and usage text.

## Acceptance criteria

- [ ] `make bench-dsp` still prints a readable table and can write the same
      run as versioned JSON.
- [ ] Generic signal-frame and GSM chain rows show sample-time budget, minimum,
      median, p95 and p95 headroom.
- [ ] The GSM benchmark refuses to report if the fixture does not decode its
      established identity.
- [ ] End-to-end chain time is measured directly and visually distinguished
      from diagnostic stage timings.
- [ ] LTE-style non-default sample rates are representable by the report model
      without special-case output arithmetic.
- [ ] A missing required fixture fails with its path; no noise substitution is
      reported as a technology benchmark.
- [ ] JSON can be parsed by a standard parser and carries enough build/host
      context to qualify a comparison.
- [ ] No absolute timing threshold is added to `make check`.

## Blocked by

None - can start immediately.

## Not in scope

- All remaining technology chains; ticket 02 owns them.
- Runtime instrumentation in the GUI or acquisition worker.
- Comparing two implementations in separate processes.
- Treating timing variance as a decoder correctness failure.

## Comments

Updated from the 2026-09-15 architecture review. Recommendation strength is
`Worth exploring`: the report module is deep only if timing policy stays
central and technology adapters remain consumers of shipping session
interfaces. Report: `/tmp/architecture-review-20260915-175714.html`.
