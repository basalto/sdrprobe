# 12 - Form one survey record before rendering or storing it

Status: ready-for-agent

`survey_session` now owns the sweep, confirmation pass, watch and measurement,
and both window and headless adapters drive that one machine. The finished
result still crosses a shallow module. `survey_store.{c,h}` includes `app.h`
and combines three responsibilities:

- materializing `survey_candidate` facts from peaks, current tuning, device
  reference clock, DC-filter choice, band plan and scratch storage;
- gathering receiving setup, gain and dwell from unrelated parts of
  `struct app`;
- naming and writing JSON files under `surveys/`.

The headless text path separately formats the same finished survey. The store
check allocates a whole `struct app` to write one file, which is the clearest
sign that the interface asks callers and tests to know almost as much as the
application.

## Hypothesis

A plain survey record can capture the complete meaning of one finished survey
before any output adapter sees it. The window, headless text and JSON writer
can then consume the same record without `survey_store` including `app.h`. It
is false if either output legitimately needs live mutable application state
after the record is formed.

The cheapest disproof is to construct the existing store fixture from plain
inputs -- plan, candidates/carriers, confirmation targets, receiving setup,
gain and dwell -- and produce the same JSON without allocating `struct app`.

## The deepened module

Introduce a survey-record module in the Probe context. It forms a finished,
immutable record from explicit facts:

- plan and dwell;
- candidate maxima and grouped survey carriers;
- optional measured carrier details and receiver-artifact resemblance;
- confirmation targets, evidence and verdicts;
- receiving setup identity and applied gain;
- recording time supplied by the adapter.

The JSON store becomes one adapter over the record. The headless line report
becomes another. The window reads the same record when saving. File naming,
collision avoidance and directory creation remain implementation of the JSON
adapter, not properties of the record.

Candidate materialization belongs in the record module because it decides what
a candidate means. It must take explicit tuning/profile facts and scratch
space rather than `struct app`. The band plan remains a lookup dependency, as
ADR-0015 requires; the record does not identify a technology from it.

## Implementation plan

### Phase 1 -- define and check the record

Add `survey_record.{c,h}` with a plain immutable record and an assembly input.
Reuse existing candidate, carrier and confirmation types where their meaning
already matches; copy receiving setup, gain, dwell and recording time into the
record. Build the current store fixture with no `struct app` and compare every
field that the JSON writer currently emits.

Files: `src/survey_record.{c,h}`, `tests/survey_record_test.c`, `Makefile`.

Focused validation: `make check-survey-record`.

### Phase 2 -- make candidate materialization explicit

Move `survey_candidates_from()` to the record module or replace it with the
record assembler. Pass centre frequency, sample rate, reference clock,
DC-filter state, optional spectrum and scratch storage explicitly. Preserve
the measured-frequency precedence for receiver-artifact flags and band-plan
lookup.

Files: `src/survey_record.{c,h}`, `src/survey_store.{c,h}`,
`src/view_survey.c`, `src/survey_report.c`.

Focused validation: `make check-survey-record`, `make check-suspect`, and
`make check-band-plan`.

### Phase 3 -- put both adapters on one record

Build a record in the window and headless paths before formatting either one.
Compare the candidate/carrier counts and confirmation verdicts from both paths
on the deterministic capture survey. Keep progress/event lines in
`survey_report.c`; only finished-survey output moves to the record.

Files: `src/view_survey.c`, `src/survey_report.c`,
`src/survey_record.{c,h}`.

Focused validation: `make check-survey-session` and the deterministic
headless survey pipeline.

### Phase 4 -- reduce the JSON store to an adapter

Make `survey_store_write()` accept a record. Remove `app.h` from
`survey_store.c`, `survey_store.h` and `survey_store_test.c`. Keep filename,
collision avoidance, JSON escaping and filesystem policy in this adapter.
Assert byte-identical JSON for the current fixture before deleting the old
entry point.

Files: `src/survey_store.{c,h}`, `tests/survey_store_test.c`.

Focused validation: `make check-survey-store` and
`make check-survey-record`.

### Phase 5 -- finish and document

Delete the old application-taking candidate/store interfaces, audit that both
output adapters contain formatting rather than survey decisions, and update
`docs/band-surveys.md` only if a separately approved schema change occurred.
Run the deletion test: removing the record should force candidate meaning and
confirmation matching back into both adapters.

## Checks

Extend `check-survey-store` or add `check-survey-record` to cover:

- a record is assembled from plain data with no `struct app`;
- measured and unmeasured candidates keep their distinct null/value fields;
- each carrier and candidate receives only its matching confirmation verdict;
- receiving setup identity, antenna, site, gain and dwell are copied into the
  record and cannot change underneath it;
- text and JSON adapters report the same counts and verdicts;
- receiver-artifact flags and allocation lookup use the measured frequency
  when available and the found frequency otherwise;
- JSON remains structurally valid and collision-safe.

Focused validation: `make check-survey-store`, `make check-survey-session`,
and the deterministic headless survey pipeline. Final gate:
`make check-touched` and `make check`.

## Tasks

- [ ] Add `survey_record.{c,h}` and register complete Makefile prerequisites.
- [ ] Add `check-survey-record` to `CHECK_UNITS` and clean bookkeeping.
- [ ] Model plan, dwell, setup, gain, candidates, carriers and confirmation.
- [ ] Copy all record-owned strings/data so the record is immutable.
- [ ] Assemble the current JSON fixture without allocating `struct app`.
- [ ] Make candidate materialization take explicit tuning/profile facts.
- [ ] Pin measured-frequency precedence for flags and allocation lookup.
- [ ] Build one record in the Survey window adapter.
- [ ] Build the same record in the headless adapter.
- [ ] Compare both adapters on the deterministic capture survey.
- [ ] Make finished headless text consume the record.
- [ ] Make `survey_store_write()` consume the record.
- [ ] Remove `app.h` from `survey_store.{c,h}` and its test.
- [ ] Preserve filename collision handling and JSON escaping checks.
- [ ] Verify deterministic text and JSON output are unchanged.
- [ ] Update `AGENTS.md`, `CLAUDE.md` and `docs/ARCHITECTURE.md`.
- [ ] Run `make check-touched` and `make check`.

## Acceptance criteria

- `survey_store.{c,h}` no longer includes or forward-declares `struct app`.
- One immutable record is the interface used by window save, headless report
  and JSON storage.
- Candidate meaning is decided once from explicit Probe facts.
- Output adapters contain formatting and I/O, not survey decisions.
- Existing JSON schema and deterministic text output remain unchanged unless
  a separately recorded format decision changes them.

## Not in scope

- Changing candidate, carrier or confirmation algorithms.
- Reopening ADR-0015 by treating the band plan as identification.
- Moving the live survey state machine out of `survey_session`.
- Replacing the JSON format or `scripts/survey_tool.py`.
- Introducing a generic persistence seam with only one file format.