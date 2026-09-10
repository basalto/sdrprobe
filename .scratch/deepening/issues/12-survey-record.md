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

1. Define the plain record and an assembly function using the existing
   `survey_candidate`, carrier and confirmation types where they fit.
2. Change `survey_candidates_from()` to take explicit centre frequency,
   sample rate, reference clock, DC-filter state and scratch storage.
3. Build the record in the two current adapters and compare their candidate
   arrays before changing output.
4. Make `survey_store_write()` accept the record and remove `app.h` from
   `survey_store.{c,h}` and its check.
5. Move headless finished-survey formatting to consume the same record while
   leaving session event/progress lines where they are.
6. Update `docs/band-surveys.md` only if the explicit model reveals a real
   schema change; otherwise output must remain byte-identical.

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