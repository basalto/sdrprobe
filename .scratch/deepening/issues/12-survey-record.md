# 12 - Form one survey record before rendering or storing it

Status: resolved, 2026-09-10

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

- [x] Add `survey_record.{c,h}` and register complete Makefile prerequisites.
- [x] Add `check-survey-record` to `CHECK_UNITS` and clean bookkeeping.
- [x] Model plan, dwell, setup, gain, candidates, carriers and confirmation.
- [x] Copy all record-owned strings/data so the record is immutable.
- [x] Assemble the current JSON fixture without allocating `struct app`.
- [x] Make candidate materialization take explicit tuning/profile facts.
- [x] Pin measured-frequency precedence for flags and allocation lookup.
- [x] Build one record in the Survey window adapter.
- [x] Build the same record in the headless adapter.
- [x] Compare both adapters on the deterministic capture survey.
- [x] Make finished headless text consume the record.
- [x] Make `survey_store_write()` consume the record.
- [x] Remove `app.h` from `survey_store.{c,h}` and its test.
- [x] Preserve filename collision handling and JSON escaping checks.
- [x] Verify deterministic text and JSON output are unchanged.
- [x] Update `AGENTS.md` and `CLAUDE.md` (`docs/ARCHITECTURE.md` names
  neither module, so it needed no change).
- [x] Run `make check-touched` and `make check`.

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

## Comments

**Phase 1 done, 2026-09-10.** `src/survey_record.{c,h}`,
`tests/survey_record_test.c` and `check-survey-record` -- **62 checks**, and
the suite links `-lm` alone: no `app.h`, no raylib, no driver. The hypothesis
holds. The whole store fixture -- two maxima, one carrier holding both, one
confirmed target carrying the numbers a real pass measured on the 75.000 MHz
harmonic -- assembles with no `struct app` anywhere. Nothing else was touched,
so this phase carries no regression risk: `make check` is 17860 in 56 suites.

The fixture is the store check's own, value for value, so phase 4 can assert
byte-identical JSON against the same numbers rather than against a second
fixture -- a copy of a fixture is a second fixture, which is the lesson
`probe-two-cell` cost.

**Three things the modelling turned up.**

**The dwell in the file is the session's, not the plan's.** `struct
survey_plan` carries a `dwell_seconds` and `survey_store_write()` has always
written `app->survey.session.dwell_seconds` instead. They agree on any sweep
this program plans for itself, and the store check's fixture makes them differ
-- plan 0, session 0.12 -- so a record that read the plan's would change the
output while every other field agreed. It is a separate field in the record
and the check pins both, because "these two numbers are always the same" is
the sort of thing that is true until it is not.

**`survey_confirm_kind_at()` returns a pointer into the array it was handed**,
which is right for a printf loop and wrong for a record: a record that
outlives the arrays it was formed from is the point of having one. The record
stores the *index*, and the check copies a record, wipes the original, and
asserts the kind still points inside the copy.

**`SURVEY_VERDICT_PENDING` is what prints as `"unconfirmed"`.** Nothing said
so, and the two names are not obviously the same thing -- nobody asked, versus
asked and not upheld. The check pins the enum and the string together so a
rename cannot quietly change the file.

## Phase 3 needs a live sweep, and the ticket does not say so

The plan validates phases 3 and 4 on "the deterministic capture survey", and a
capture survey is **one step**: one tuning, no retune, no watch, no fold across
steps. So byte-identical output over a capture cannot cover `watch_carriers`,
per-step folding, or the `intermittent` verdict that only a multi-look pass
produces -- and those are exactly the fields the record now carries.

This is the hole the survey-session extraction fell into: 55 suites green,
both capture surveys byte-identical, the screen byte-identical, and the settle
that throws away stale blocks disabled, because a capture never retunes. What
caught it was one line on a live sweep. Phase 3 should compare a swept run
against the old binary as well, and this ticket should say so rather than
leaving the capture to look sufficient.

**Phases 2 to 5 done, 2026-09-10.** `check-survey-record` is **98 checks** and
`make check` is 17887 in 56 suites.

- **Phase 2.** `survey_candidates_from()` is `survey_record_candidates()`,
  taking a `struct survey_record_tuning` -- centre, rate, reference clock,
  DC-filter state -- instead of `struct app`. Arithmetic unchanged.
- **Phase 3.** Both adapters build a record before formatting anything, and
  `survey_tuning_from()` in `view.h` is the single place those four facts are
  read out of `struct app`; three sites assembled them separately.
- **Phase 4.** `survey_store_write(record, path, size)`. **`survey_store.{c,h}`
  has no `struct app` in it at all**, its check builds a record instead of
  `calloc`-ing an application, and the rule no longer needs raylib or
  librtlsdr headers -- it links `-lm` like the rest.
- **Phase 5.** `struct survey_candidate` and `survey_flag_text()` moved down
  into `survey_record.h`, which is what removed the include cycle and put the
  candidate type under the module that decides what a candidate means.

**Byte-identical**, which was the requirement: the capture survey's text output
diffs clean against the pre-change binary, and its JSON differs only in
`recorded_at`. Both audits clean, and the survey screen was looked at.

**The deletion test passes.** Removing the record would put the confirmation
matching, the totals and the tuning assembly back into both adapters -- and
back into a `printf` loop, since that is where they were.

**One claim of mine was wrong before the code was.** The first fixture for "a
candidate takes its carrier's verdict" put the maximum at bin 2662, which is
94 500 228 Hz -- **227 Hz past the carrier's upper edge**. No carrier held it,
the check correctly returned PENDING, and the failure was a false claim beside
right arithmetic. Bin 2600 sits inside the carrier and 51 kHz from the
frequency the pass asked about, forty times the half-bin tolerance, so it can
only come back confirmed *through* the carrier -- which is the decision under
test. The comment in the check says so, since the wrong fixture is the more
instructive of the two.

**What is left, and it is this ticket's own gap.** The byte-identical proof is
over a **one-step** capture survey. `watch_carriers`, per-step folding and the
`intermittent` verdict are not exercised by it, and a live sweep against the
previous binary is still owed -- see the note above.
