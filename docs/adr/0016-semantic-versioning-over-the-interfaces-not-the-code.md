# Semantic versioning, over the interfaces rather than the code

## Status

accepted

## Context and decision

The program now has more than one way in -- a window, a command line, a set of
headless reports other programs parse, and files it keeps between runs -- and
until now no way to say which build produced any of them. A survey JSON, a
`cell` line, a bug report with a screenshot: none of them said what they came
from. So the program carries a version, shows it in the window's bottom-right
corner beside a contact address, and prints it from `--version`.

**It follows Semantic Versioning 2.0.0.** The awkward part of applying SemVer
to an application is that the specification is written about an API, and
nothing links against this program. The decision is therefore about *what
counts as the public interface*, and the answer is: the things other people's
work can break against.

- **The command line.** Flag names, the values they accept, and what they
  refuse. A script that runs `--lte-scan 20` is depending on this.
- **The headless reports.** `--decode`, `--survey`, `--lte-scan`,
  `--lte-chain`, `--calibrate`, and in particular the `candidate` and `survey`
  record lines, which `scripts/survey_tool.py` parses today and which are
  documented as a format in `docs/band-surveys.md`.
- **The files.** `~/.config/sdrprobe/config`, the survey JSON under
  `surveys/`, `surveys/history-<site>.txt`, and the capture sidecars. These
  outlive the build that wrote them, which is what makes them an interface
  rather than an implementation detail.

MAJOR when one of those breaks, MINOR when one gains something backwards
compatible, PATCH when behaviour is corrected without either.

**The screens are deliberately not in that list.** A new view, a new key, a
moved panel, a rearranged tab: MINOR at most, because nothing can depend on
them programmatically -- keys cannot even be injected here, as the
`screenshot` skill sets out. This is not a claim that the interface does not
matter to a person; it is a claim that a person is not broken by it in the way
a script is.

The consequence worth stating outright: **a decode that starts reading a field
it previously got wrong is a PATCH, even though the numbers coming out of it
change.** The wrong answer was never the contract. This repository has shipped
a conjugated LTE primary sequence and a scattered GSM SCH field layout, both
green throughout; correcting either would change every reading and break
nothing anybody was entitled to rely on.

## Starting at 0.9.0, and what 1.0.0 would mean

Under SemVer a leading zero says the public surface may move without a MAJOR
bump. It is the honest number here, and recently: the tabs were reorganised,
the survey stopped being a Scope view and became a top-level tab, the centre
frequency moved out of the Settings panel, and the Scope grew a header with
editable fields. Each of those would have been a MAJOR bump under a 1.x
promise, and none of them should have been discouraged.

1.0.0 is a promise to stop doing that without counting it. It should be made
when the command line, the headless reports and the file formats have gone a
while without needing to move -- not when the program feels finished, which is
a different and much vaguer condition.

## Consequences

`src/version.h` holds the three numbers and builds the strings from them, so
the window's corner and `--version` cannot disagree about which build this is.
`check-layout` parses the version rather than comparing it to a literal --
the shape is the invariant, not the value, so bumping it does not mean editing
a check -- and asserts the corner stays inside the window and clear of the
chrome at four window sizes. `check-options` covers the flag.

Bumping the version is editing three numbers in one header. Nothing derives it
from git, because a build from a dirty tree would then claim to be a tag it is
not.
