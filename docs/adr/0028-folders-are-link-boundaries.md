# Folders are link boundaries

## Status

accepted

## Context and decision

`src/` held 194 files in one directory. Every boundary this program has was
real and enforced -- `app.h` compiles without raylib (ADR-0007 outward),
every decision is reachable without a window (ADR-0012), the components take
plain data, and `check-no-window-link` refuses a server that reaches for the
GUI -- but none of them was *visible*. A reader learned the layering by
running an audit or by reading `CLAUDE.md`, not by looking at the tree.

**`src/` is now seven directories, and the order between them is the rule:**

    core -> tech -> model -> runtime -> server -> gui -> app

A file in a layer may include only headers from its own layer and the layers
beneath it. `scripts/layer_audit.py`, behind `make check-layers` and in
`CHECK_UNITS`, reads every include in all 194 files and fails on an upward
one, naming the file, the header and the layer it belongs to.

| layer | holds | links |
|---|---|---|
| `core/` | `sdr_dsp`, `signal_*`, `device_profile`, `band_plan`, `capture_sidecar`, the survey's analysis headers | `-lm` |
| `tech/` | `gsm_*`, `lte_*`, `adsb_*`, `tetra_*`, `fm_*`, `rds`, `srd_*` | `-lm`, core |
| `model/` | the four view-model **contracts**, `survey_mark.h`, `site_seen.h`, `survey_tuning.h` | `-lm` |
| `runtime/` | acquisition, the backends, the receiver lease, `frame_advance`, `*_runtime.c`, options, config, the survey machine and its record -- **and the four builders that fill the models** | `-pthread`, librtlsdr; **no raylib** |
| `server/` | `websocket`, `viewer_*`, `browser`, `process_cpu` | no raylib |
| `gui/` | `sdrgui*`, `view_*`, `overlay_*`, `*_layout.h`, `raygui_impl` | raylib |
| `app/` | `sdrprobe.c`, `app_main.c`, and the two binaries' `main()` | both |

## Why this way

**It was done last, and that is the whole reason it is mechanical.** Tickets
01-04 of `.scratch/layer-boundaries/` delivered every bit of the decoupling
without moving a file. Done first, this would have relocated the
entanglement into subdirectories and fixed nothing.

**The order was decided on measurement, and then the measurement was
rejected.** Both candidates were first run against the include graph as it
stood: `model` above `runtime` gave three violations, beneath it gave six,
so `model` went above. That was the cheap answer, not the right one.

A model is a **contract** -- plain structs a reader depends on. The Viewer
link serialises them; `views/survey.js` keys tables by the names in them. A
contract that reaches up into the layer which happens to compute it is a
contract its readers cannot have without dragging the application in behind
it. So `model` belongs beneath `runtime`, and the six violations were the
work, not an argument against it.

The fix separates the contract from the builder. `*_view_model.h` stays in
`model/`; the four `.c` files that *fill* them moved to `runtime/`, where
reading `struct app` is exactly what they are for. Two values came down with
them, both of which cross the wire and neither of which needs the machinery
that produces it: `enum site_seen` out of `site_history.h` (which reads a
file) into `model/site_seen.h`, and `struct survey_record_tuning` out of
`survey_record.h` into `model/survey_tuning.h`.

`survey_record` and `survey_store` stayed in `runtime/` throughout, for the
reason that first looked like a counter-example: runtime writes them, a file
and a script read them, and they never cross a seam to a *reader*.

**`model/` now depends on nothing above `tech/`.** That is the property, and
it is what makes the layer worth having: a reader -- the Viewer link, a
future client -- can take a contract without taking the program.

Two other files were in the wrong place and the audit is what said so.
`view_input.h` is raylib-free routing state, sibling of `input_route.h`, and
was in `gui/` only because it is named `view_*`. The survey's analysis
headers -- `survey_carrier`, `survey_sweep`, `survey_suspect`,
`survey_confirm`, `survey_bands` -- are pure arithmetic over readings, the
same family as `clock_chain.h` and `reading_origin.h`, and belonged in
`core/`.

**A rule, not a list.** ticket 04's header audit was a `grep` for
`$(SRC)/<file>` kept in `CLAUDE.md`, and it began reporting 104 of 104
headers missing the moment the paths grew a folder. It failed loudly, which
was luck: a pattern that matched *everything* instead would have reported
none and been believed. `check-layers` needs no pattern, because it reads the
folders.

**What this catches that the linker cannot.** A file may include a header and
call nothing from it. That is precisely how `viewer_session.c` and
`survey_report.c` came to include `view.h`, and so `<raylib.h>`, while
`check-no-window-link` passed and every symbol resolved -- and while
`make sdrprobe` would have failed on a machine with no raylib dev headers,
which is the entire reason that binary exists. `check-no-raylib-headers`
covers the same ground for one library and is kept: it names the symptom a
reader on a headless box actually hits, and survives this being deleted.

## Consequences

**Every include names its layer**, against a single `-Isrc`:
`#include "core/sdr_dsp.h"`. Done in two commits rather than one -- seven
`-I` flags first, so the move itself was `git mv` and Makefile paths and
nothing else, then 635 include lines across `src/`, `tests/` and `scripts/`.
Two reviewable steps instead of one 1,450-line diff, each green on its own.

The spelling earns its churn twice. The dependency is visible where it is
*written*, so a `runtime/` file reaching into `gui/` says so on the line
rather than in a folder listing. And with one `-I` a bare name resolves only
inside the including file's own directory, so **a cross-layer include that
does not name its layer cannot compile** -- the rule is held by the compiler
on every build, not only by the audit. `check-layers` asserts the spelling
as a second property for the case somebody restores a per-folder `-I`, which
would silently make bare cross-layer includes work again.

**The move changed nothing observable, and that was measured rather than
assumed.** The binary from before the move and the binary after were both run
over four headless cases -- a GSM decode, an FM decode, a capture survey and
an ADS-B decode with CPR. Three are **byte-identical**; the fourth differs by
one second of wall clock in the ADS-B timestamps, with every decoded field --
position, altitude, the raw frame hex -- identical, which is the difference
`CLAUDE.md` already records for that capture. `make check`: 83 suites, 22323
checks.

**`make check-touched` needed no change**, which was verified per folder
rather than hoped: it substitutes `$(SRC)` for `src` in the Makefile's
prerequisite paths, and those paths now carry the folder, so the result is
the real path either way. All seven layers pick the right suites.

**Resolved tickets under `.scratch/` keep their old paths.** A path in a
finished ticket is where the file *was*, and rewriting it would make the
record lie about the past. Living material -- `CLAUDE.md`, `AGENTS.md`,
`docs/`, the skills, source comments -- was rewritten: 158 paths.

**`tech/` is 50 files and stays flat.** Splitting it per technology
(`tech/gsm/`, ...) was left open deliberately, to be decided on the evidence
of living with the move rather than before it.

## Relationship to other decisions

This extends **ADR-0007** (components take plain data and never see `struct
app`) outward from `sdrgui` to runtime, models and server, and gives
**ADR-0012** (every decision reachable without a window) a boundary the build
enforces rather than a convention a reviewer maintains. It does not change
what any layer does; it makes where a thing belongs answerable by looking.
