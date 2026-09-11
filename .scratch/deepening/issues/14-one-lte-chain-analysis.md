# 14 - One LTE chain analysis, live or captured

Status: **resolved 2026-09-11.** Phases 1-6 done; Phase 5 decided **against**
reuse by `lte_session`, on the deletion test as the ticket asks. Both captures
byte-identical, the live pair indistinguishable under alternating runs, and
the overlap turned out narrower than the problem statement supposed. See the
comments.

## Summary

Form one application-independent LTE chain analysis from centred I/Q and use
it from both the live `--lte-chain` path and the capture-only
`probe-lte-chain` adapter. The shared module owns the public cell search,
primary-cell choice, broadcast attempts, per-cell evidence and run statistics.
The capture probe keeps its white-box diagnostics that deliberately inspect
`lte_dsp.c` internals.

This is not a new decoder and not a uniform interface across technologies. It
is one LTE module with two real adapters: a live signal source and a capture.

## Problem

The two LTE chain diagnostics answer overlapping questions through separate
implementations:

- `run_headless()` in `src/sdrprobe.c` searches every cell on a carrier,
  chooses a primary by primary-synchronisation correlation, tries the
  broadcast channel under three antenna-port hypotheses, accumulates cell
  verdicts and measurement statistics, and prints findings from a live run;
- `scripts/lte_chain_probe.c` converts a capture, runs single- and multi-cell
  search, tries the same broadcast hypotheses and accumulates its own run
  totals before adding deeper diagnostic controls.

Some rules have already drifted and been repaired individually. The repeated
Master Information Block rule was written twice until ticket 02 proved the two
forms equivalent and moved it to `lte_mib_repeat_observe()`. The multi-cell
capture path was added only after the live-only path had become able to return
fewer cells than the single-cell search without a committed capture reaching
it. Those are two instances of the same architectural fault: the public chain
has no owner.

The overlap must not be overstated. `probe-lte-chain` also measures primary
root scores, cyclic-prefix alternatives, timing nudges, CRC distance,
broadcast repetition controls and reference-sequence coherence. Those are
white-box diagnostics, not the shared chain. Moving them into the production
module would widen its interface and erase the reason the probe compiles
`lte_dsp.c` directly.

## Exploration result

### Hypothesis

A plain LTE chain-analysis module can process one block and accumulate the
facts both adapters share without knowing `struct app`, acquisition, files,
stdout or private DSP symbols. Deleting it would put primary selection,
broadcast attempts, cell tallies and run statistics back into both adapters.

The hypothesis is false if either adapter needs probe-only arrays in the
shared result, or if using the module makes either adapter run cell search or
broadcast extraction twice per block.

### Cheapest discriminating check

Drive `testfiles/lte_b20_pci28.bin` through the proposed module and a thin
capture adapter. It must report the same public per-block facts as the current
probe while performing one `lte_cell_search_all()` walk. Then drive
`testfiles/lte_b8_pci330_4port.bin` through it to show that the result is not
fitted to one physical cell identity or antenna-port count.

If matching the current probe requires exposing root-score landscapes,
timing-sweep arrays or private `lte_dsp.c` types, stop: the proposed interface
is too wide and the extraction does not create depth.

### Shared and private work

Shared implementation:

- public multi-cell search and the no-cell result;
- primary choice by strongest primary-synchronisation correlation;
- reference power, channel shape and antenna-port coherence for that primary;
- broadcast extraction and parity result under the three existing combining
  hypotheses;
- adjacent-message agreement through `lte_mib_repeat_observe()`;
- per-identity looks, decodes and verdicts through `lte_confirm`;
- primary-cell smallest, mean and largest measurements through `lte_stats`.

Capture-probe implementation only:

- per-root primary-synchronisation score landscapes;
- cyclic-prefix competitor and timing-sidelobe diagnostics;
- sample and whole-subframe timing sweeps;
- every-identity descrambling sweep;
- parity-bit distance and broadcast QPSK coherence;
- broadcast repetition at positive and control subframes;
- private reference-sequence coherence measurements.

The normal LTE decode session is not a third adapter in the first slice.
`lte_session` latches one cell for the view and ordinary headless decode;
chain analysis examines every identity and reports attempts. Reuse between the
two is considered only after the live and capture adapters agree, and only if
it removes implementation without adding a mode-filled interface.

## Proposed solution

Add `lte_chain_analysis.{c,h}` in the Decoder context. It owns a run state and
one block's result, composed from the existing public LTE modules:
`lte_dsp`, `lte_mib`, `lte_session`'s port hypotheses and repeat rule,
`lte_confirm`, `lte_stats` and `lte_findings`.

The caller supplies centred I/Q, pair count, sample rate, full scale and time.
The result exposes measured facts and events, not formatted lines and not
pointers into caller-owned scratch. Acquisition remains in the live adapter;
capture reading and white-box diagnostics remain in the probe; text remains
in each output adapter until both have a genuinely common record worth
formatting once.

Do not make a generic chain interface for GSM, TETRA, FM or ADS-B. ADR-0023
requires technology DSP modules to share dependency and testability
constraints, not one function shape.

## Wins

- **Locality:** primary selection has one implementation.
- **Locality:** cell evidence belongs to one run state.
- **Leverage:** live and capture use the same public walk.
- **Leverage:** one check reaches multi-cell chain behavior.
- **Depth:** adapters stop knowing broadcast-attempt ordering.
- **Testability:** both real LTE captures reach the same interface.
- **Performance:** one cell search and one measurement pass per block.

## Implementation plan

### Phase 1 - freeze the two adapters' public evidence

Record the public facts currently produced from both committed LTE captures:
cells per block, selected primary, broadcast hypothesis that passed, decoded
and agreed totals, per-cell looks/decodes/verdicts, and run statistics. Keep a
separate list of probe-only diagnostics so output parity does not accidentally
turn private instrumentation into the shared interface.

Record one short live `--lte-chain` run with its EARFCN, applied correction,
block count, cell count, decoded/agreed totals and per-cell verdicts. This is
the refactor's on-air baseline; captures cannot exercise acquisition or prove
that the same carrier is selected after a retune.

Files: this ticket, `src/sdrprobe.c`, `scripts/lte_chain_probe.c`.

Gate: the baseline names which figures must remain equal and which are allowed
to vary with the air. No expected physical cell identity may be copied from a
new run of the code under test; use the committed capture invariants.

### Phase 2 - add the checked analysis module

Create `lte_chain_analysis.{c,h}` and `check-lte-chain-analysis`. Start with
plain block input, one block result and accumulated run state. Compose the
existing public operations without changing their arithmetic or thresholds.
Instrument the fixture or inject a counted search operation internally so the
check proves one multi-cell search per block rather than merely seeing equal
answers after duplicated work.

The first real-capture cases are physical cell identity 28 under the normal
cyclic prefix and physical cell identity 330 with four antenna ports. Add a
synthetic or existing fixture only for control-flow failures that a real
capture cannot force deterministically; do not use a synthetic to establish a
radio convention.

Files: `src/lte_chain_analysis.{c,h}`,
`tests/lte_chain_analysis_test.c`, `Makefile`.

Focused validation: `make check-lte-chain-analysis`,
`make check-lte-session`, and `make check-lte-dsp`.

### Phase 3 - move the live adapter

Replace the public analysis inside the `--lte-chain` branch with the new
module. Keep retuning, acquisition, duration, stdout spelling and the
diagnostic's stopping policy in `sdrprobe.c`. The adapter may format the
module's block events and final state; it must not repeat primary selection,
broadcast attempts, tallies or statistics.

Compare a live run with the Phase 1 baseline. Block totals may move with air
and timing; unexplained changes in primary selection, the relation between
decoded and agreed, or per-cell verdicts are regressions until independently
corroborated.

Files: `src/sdrprobe.c`, `src/lte_chain_analysis.{c,h}`, `Makefile`.

Focused validation: `make check-lte-chain-analysis`,
`make check-pipelines`, then the recorded live command.

### Phase 4 - move the capture adapter

Make `probe-lte-chain` use the same module for shared facts. Leave its direct
inclusion of `lte_dsp.c` and every private diagnostic listed above intact.
Remove only duplicated public analysis. The probe should print its detailed
evidence around the common result rather than ask the module for private
intermediates.

Run both committed LTE captures and compare the Phase 1 public facts exactly.
Probe-only lines must remain available; their order may change only when the
new structure makes that necessary and the ticket records why.

Files: `scripts/lte_chain_probe.c`, `src/lte_chain_analysis.{c,h}`, `Makefile`.

Focused validation:

```sh
make probe-lte-chain FILE_LTE=testfiles/lte_b20_pci28.bin
make probe-lte-chain FILE_LTE=testfiles/lte_b8_pci330_4port.bin
make check-lte-chain-analysis
```

### Phase 5 - assess, do not assume, reuse by `lte_session`

Apply the deletion test after both adapters move. Compare the remaining
implementation in `lte_session.c` with the analysis module. Reuse the analysis
only if the ordinary one-cell session becomes smaller without asking callers
to choose diagnostic modes, allocate every-cell results, or pay the multi-cell
cost when they do not need it. Otherwise document the distinct jobs and leave
the session alone.

Files, only if the check supports the move: `src/lte_session.{c,h}`,
`tests/lte_session_test.c`, `src/view_lte.c`.

Focused validation: `make check-lte-session`, `make check-lte-dsp`,
and `make check-pipelines`.

### Phase 6 - close the old implementations

Delete the duplicated primary-selection, broadcast-attempt, tally and
statistics code from both adapters. Add the new header to `APP_HDR`, the new
check to `CHECK_UNITS`, and every source/header prerequisite to the relevant
Make rules. Update `AGENTS.md` and `CLAUDE.md` with the ownership split and
the two-adapter deletion test.

No ADR is required unless implementation proposes a common interface across
technologies or changes an LTE result. Either would exceed this ticket.

## Tasks

- [ ] Capture the public/probe-only evidence table for both LTE captures.
- [ ] Record one live `--lte-chain` baseline and its receiving setup.
- [ ] Add `lte_chain_analysis.{c,h}` with no application, GUI, receiver or
  file dependency.
- [ ] Add and gate `check-lte-chain-analysis` with complete prerequisites.
- [ ] Pin physical cell identity 28 and its normal cyclic prefix from the
  band 20 capture.
- [ ] Pin physical cell identity 330 and four antenna ports from the band 8
  capture.
- [ ] Pin the invariant that multi-cell analysis never loses the public
  single-cell result on either capture.
- [ ] Pin primary selection by strongest primary-synchronisation correlation.
- [ ] Pin decoded versus adjacent-agreement counts as distinct facts.
- [ ] Pin per-cell confirmed, unread and spurious verdict behavior.
- [ ] Pin statistics reset when the selected physical cell identity changes.
- [ ] Prove one multi-cell search is performed per processed block.
- [ ] Migrate live `--lte-chain` without changing its text contract.
- [ ] Compare the migrated live run with the recorded baseline.
- [ ] Migrate the shared part of `probe-lte-chain`.
- [ ] Preserve every listed white-box capture diagnostic.
- [ ] Compare both capture runs with the public-evidence baseline.
- [ ] Decide `lte_session` reuse from the deletion test, not by symmetry.
- [ ] Remove duplicated public chain implementation from both adapters.
- [ ] Update Makefile dependencies, `CHECK_UNITS`, `AGENTS.md` and
  `CLAUDE.md`.
- [ ] Run the focused and final validation gates.

## Validation gate

### Structural

- `lte_chain_analysis` includes no `app.h`, raylib, acquisition, backend or
  filesystem header and links with `-lm` plus existing LTE modules.
- Live and capture adapters both cross its interface.
- Neither adapter independently chooses the primary, walks port hypotheses,
  updates cell tallies or accumulates primary statistics.
- Probe-only diagnostics remain outside the module.
- One block performs no duplicate cell search or broadcast extraction for the
  same identity and hypothesis.

### Behavioral

- `testfiles/lte_b20_pci28.bin` retains physical cell identity 28, normal
  cyclic prefix and its established broadcast result.
- `testfiles/lte_b8_pci330_4port.bin` retains physical cell identity 330 and
  four antenna ports.
- The multi-cell result includes whatever `lte_cell_search()` finds on every
  committed LTE block; it may add neighbours but may not lose the primary.
- Decoded, agreed, confirmed, unread and spurious keep their distinct existing
  meanings.
- Public capture evidence is identical before and after the refactor.
- A live run has no unexplained change in selected cell, decoded/agreed
  relation or verdicts.

### Performance

- The analysis searches a block once and remains within LTE's 68.3 ms block
  budget on the existing benchmark machine.
- Any timing claim uses same-process alternating measurements or instruction
  counts; cross-binary wall-clock differences below the measured machine drift
  are not evidence.

### Commands

```sh
make check-lte-chain-analysis
make check-lte-session
make check-lte-dsp
make probe-lte-chain FILE_LTE=testfiles/lte_b20_pci28.bin
make probe-lte-chain FILE_LTE=testfiles/lte_b8_pci330_4port.bin
make check-pipelines
make bench-dsp
make check-touched
make check
```

The live command recorded in Phase 1 is also mandatory before resolution.
`make check` alone cannot validate acquisition, retuning or an on-air carrier.

## Acceptance criteria

- One module owns the public LTE chain walk and accumulated evidence.
- Live and capture are real adapters over that module.
- The capture probe retains its white-box diagnostic reach.
- The interface contains measured facts, not formatted text or private DSP
  implementation types.
- Existing real-capture identities and broadcast facts remain independently
  corroborated.
- No block pays for duplicated cell search or broadcast extraction.
- Deleting the module would restore substantial implementation to both
  adapters.

## Not in scope

- A uniform interface across technology DSP modules.
- Changing LTE thresholds, sequences, signs, bit order or field layout.
- Replacing `lte_session` unless Phase 5 demonstrates greater depth.
- Moving acquisition, retuning or duration policy out of `run_headless()`.
- Moving private white-box DSP measurements into production code.
- Redesigning the diagnostic text format without a separate compatibility
  decision.

## Comments

**Explored 2026-09-11.** The review's original picture made the two paths look
more alike than they are. The useful shared implementation is the public
chain evidence, not the whole probe. Keeping that distinction is the condition
under which this becomes a deep module rather than a large diagnostic result
type exposing every internal measurement.

## Comments

### Phase 1, 2026-09-11: the baselines, and one thing that must be decided first

**The two adapters disagree about what a block is, by a factor of two.**
`scripts/lte_chain_probe.c:41` defines `BLOCK_PAIRS (16 * 16384)` = **262 144
pairs**; the program's `SAMPLE_BLOCK_PAIRS` is **131 072**
(`src/acquisition.h:28`). So `probe-lte-chain` reads `lte_b20_pci28.bin` as
**12 blocks** where the live path would see 24, and every per-block figure
below is per-*probe*-block.

That is the same confusion `.scratch/device-model/issues/09-*` resolved for
the program and did not reach the probe: dump1090's block was 262144 **bytes**
and 131072 **pairs**, and those stopped being one number the day there were
two containers. `16 * 16384` is the byte count wearing the pairs name.

It is load-bearing here rather than untidy. This ticket's gate is "the same
public per-block facts", and that sentence has no meaning across adapters that
mean different things by "block" -- and `CLAUDE.md` already records what block
size does to this exact decoder: halving it took LTE from 28 Master
Information Blocks to 55 and cost 55% more processing time, because a MIB
attempt needs enough samples to reach across a 40 ms period. Doubling it moves
the other way.

**Phase 2 must not start until this is decided**, and the decision is not
obviously "make the probe match". A 262144-pair block gives the probe more
room for the white-box diagnostics it exists for, and its numbers have been
read and quoted at that size. The three honest options:

1. **The module takes pair count as an argument and neither adapter's choice
   is baked in** -- which the proposed interface already says ("the caller
   supplies centred I/Q, pair count, sample rate, full scale and time"), so
   the module is fine either way and only the *comparison* in Phases 3 and 4
   needs care. Cheapest, and it means the Phase 4 gate must compare the probe
   against **its own** Phase 1 numbers, never against the live ones.
2. **Make the probe use `SAMPLE_BLOCK_PAIRS`** and re-baseline it. Honest, and
   it changes every number in every transcript that quotes `probe-lte-chain`.
3. **Leave it and say so in the probe's header**, which is what should happen
   regardless of 1 or 2.

Recommended: **1 plus 3**. The module is block-size agnostic by construction;
the probe keeps its block and gains a comment saying why it differs; no gate
ever compares a live figure with a capture figure.

### The frozen public evidence

**Captures, `make probe-lte-chain`, at the probe's 262144-pair block:**

| | `lte_b20_pci28.bin` | `lte_b8_pci330_4port.bin` |
| --- | --- | --- |
| blocks | 12 | 6 |
| blocks with a cell | 12 | 6 |
| blocks with a message | 12 | -- |
| cell-sightings | 13 | 7 |
| most at once | 2 | 2 |
| multi-cell silent | 0 | 0 |
| single-cell refused | 0 | 0 |
| identity | 28, normal CP | 330 |
| ports | 2 | 4 |
| broadcast | 1-port combining, 50 RB (9.00 MHz), PHICH normal 1/6 | -- |

The invariant this ticket's gate names -- *the multi-cell result never loses
the single-cell one* -- holds on both: silent 0 against refused 0.

**Probe-only diagnostics that must stay out of the shared interface**, present
in the same run and deliberately not in the table above: per-root PSS score
landscapes (`N_ID_2 0/1/2` with offsets), CP normal-versus-extended
competitors, timing shift and sidelobe, parity-bit distance per combining
hypothesis (1-port best 0 mean 0.0, 4-port best 0 mean 3.8), per-port
reference coherence (0.729 / 0.901 / 0.411 / 0.326 against a 0.30 chance
level), and broadcast repetition at +1..+5 in subframes 0 and 7.

**Live, `--lte-chain --lte-chain-seconds 60`, receiver 77771111153705700 at
home-sala-estar, telescopic, +32 ppm restored, 2026-09-11 20:19-20:21 local:**

| | EARFCN 6200 | EARFCN 3625 |
| --- | --- | --- |
| carrier | 796 000 000 Hz | 942 500 000 Hz |
| rate | 1 920 000 | 1 920 000 |
| blocks | 878 | 878 |
| cell-sightings | 232 | 756 |
| decoded | 96 | 713 |
| agreed | 95 | 712 |
| identities reported | 13 | 10 |
| confirmed | 28 (97 looks, 96 decoded) | 402 (720/715), 190 (9/7) |
| loudest unread | 146 (97 looks, 0 decoded) | 410 (161/0) |

**What may vary with the air and what may not.** Block counts, sighting
counts, and which spurious identities appear at one or two looks are air and
timing and may move freely. These may not, and are regressions until
independently corroborated:

- the **confirmed** set. 6200 must confirm 28; 3625 must confirm 402, and 190
  is the second cell this ticket's own history says was being lost;
- **decoded >= agreed**, always, and by a small margin -- 96/95 and 713/712.
  Agreement is adjacent-message equality, so it can never exceed decodes;
- **PCI 146 at 97 looks and 0 decodes stays `unread`**. It is seen exactly as
  often as the real cell and never decodes, which is the case `lte_confirm`
  exists for and the one a sightings threshold gets wrong;
- the primary is chosen by **strongest PSS correlation** and not by reference
  power -- `CLAUDE.md` records a run of 146 blocks reset to 3 when it was.

**One number to understand rather than freeze.** On 3625 the per-cell decodes
sum to 722 (715 + 7) against a summary `decoded 713`. That is consistent with
the summary counting *blocks in which something decoded* while the per-cell
figure counts *decodes per identity*, so a block decoding for two identities is
one and two respectively. The refactor must keep both meanings distinct --
this ticket's task list already says so -- but the relation should be asserted
rather than assumed, because if it is not that, it is a double-count.

### Phases 2-6, 2026-09-11

**Phase 2.** `src/lte_chain_analysis.{c,h}` and `check-lte-chain-analysis`, 54
checks over both committed captures, linking `-lm` and the existing LTE
modules. `struct lte_chain_run` accumulates and `struct lte_chain_result`
carries one block's facts; the block is `struct lte_chain_block`, so **pair
count is an argument** and the disagreement above costs nothing -- option 1 of
the three, as recommended.

The check reproduced a number it was not fitted to, which is the useful kind of
agreement: **29 of 30 blocks** on `lte_b20_pci28.bin`, the same figure
`check-lte-session` pins through a different interface, with the one refusal
being the "primary sequence without a secondary" block `CLAUDE.md` already
records. Two suites reaching it through different code is corroboration.

Two of this suite's first assertions were wrong and the captures were right:

- **"every block holds a cell"** -- it is 29 of 30, as above. Written from
  optimism rather than from the file.
- **"two antenna ports"** asserted against `lte_port_coherence()`'s estimate,
  which reads **3** in at least one block of the band 20 capture. The
  CRC-masked count in the message says 2 throughout and is the thing to
  assert; the coherence estimate corroborates it from the reference phases and
  shares no code with it, but it is a per-block draw and pinning it is pinning
  noise. Both captures now assert the message's count, the resource blocks and
  **which combining hypothesis read it** -- 1-port on band 20 and 4-port on
  band 8, so a module with a favourite fails one of them.

**Phase 3, and the measurement that matters.** The live adapter formats the
module's result and keeps acquisition, duration, the stopping policy and every
line's spelling. The first comparison against the Phase 1 baseline looked
alarming -- EARFCN 6200 went from 232 cells and 96 decodes to 506 and 423 --
and **it was the air, not the change.**

Six alternating 20-second pairs of the old and new binaries on the same
carrier, which is the only way to tell those apart:

| run | old cells/decoded | new cells/decoded |
| --- | --- | --- |
| 1 | 139 / 115 | 123 / 101 |
| 2 | **83 / 30** | 131 / 98 |
| 3 | 144 / 109 | 108 / 61 |
| 4 | 243 / 234 | **286 / 286** |
| 5 | 182 / 162 | 193 / 167 |
| 6 | 206 / 192 | 97 / 82 |

The **old** binary alone spans 83 to 243 cells and 30 to 234 decodes. The
means differ by 6% (166 against 156 cells, 140 against 133 decodes) against a
per-run spread of about +/-100%. The two are indistinguishable, and a
single-run comparison would have "shown" either a large improvement or a large
regression depending on which minute it was taken in.

What is exact in all twelve runs: **292 blocks** every time, and
**agreed = decoded - 1** every time, in both binaries. Agreement is
adjacent-message equality, so the first decode has nothing to agree with; that
relation is the one the ticket names and it held throughout. PCI 28 confirmed
in every run.

**Phase 4, which is the deterministic proof.** Both committed captures produce
**byte-identical output** before and after -- every public line and every
white-box line, `diff` clean. The probe keeps its direct `#include "lte_dsp.c"`
and every private diagnostic.

One thing was deliberately not changed and is worth recording: **the probe
still walks its own cell from `lte_cell_search`**, not the module's primary.
Every diagnostic after that point -- timing sweeps, cyclic-prefix competitors,
parity distance, every-identity descrambling -- is measured against that cell,
and swapping it would silently change all of them. The two selections can
differ in principle (the live adapter takes the strongest correlation on the
carrier), and they agree on both captures.

**The overlap was narrower than the problem statement supposed**, which the
exploration comment had already half-said. The probe does not use
`lte_confirm`, `lte_stats`, `lte_mib_repeat`, `lte_reference_power`,
`lte_channel_shape` or `lte_port_coherence` **at all** -- it has its own
per-run totals and reaches coherence through `lte_dsp.c`'s statics. The real
duplication was the multi-cell walk and its accumulation, and that is what
moved. The module's tallies and statistics accumulate in the probe and are not
printed, so a future line can have them without a second implementation.

**Phase 5: do not reuse by `lte_session`, on the deletion test.**
`lte_session.c` is 172 lines against the module's 211 and never calls
`lte_cell_search_all`. To share, the session would need the module to accept a
`struct lte_trace` it does not have, to *skip* the multi-cell walk, and to
keep producing the status strings and the `off_grid` / `block_too_short`
events that are the whole of what a view needs. That is two mode flags and a
widened result for no removed implementation -- the "mode-filled interface"
this ticket refuses -- and it would put the multi-cell cost in the interactive
path, where the budget is 68.3 ms and `CLAUDE.md` already prices the cell
search at about 14 ms and each broadcast attempt at about 9. Distinct jobs,
documented in `CLAUDE.md`, session left alone.

**Phase 6.** `lte_chain_analysis.h` is in `APP_HDR`, `lte_chain_analysis.c` in
`APP_SRC`, `check-lte-chain-analysis` in `CHECK_UNITS`, and both the check and
`probe-lte-chain` list every prerequisite. Both Makefile audits are clean.
`make check` is **19137 in 60 suites**.

### What this did not do

The `lte-chain` text contract is untouched, no LTE threshold, sequence, sign,
bit order or field layout moved, and no ADR was needed. `run_headless()` keeps
acquisition, retuning and duration. No generic chain interface exists for any
other technology, and ADR-0023 is the reason.
