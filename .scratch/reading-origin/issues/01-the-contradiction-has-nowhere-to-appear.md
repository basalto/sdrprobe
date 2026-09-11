# 01 - The verdict that contradicts the comb has nowhere to appear

Status: ready-for-agent -- **decided 2026-09-11: a third flag, and the comb mark
stands.** See the comments.
Opened 2026-09-11, from `.scratch/device-model/issues/11-*` landing.

`reading_origin_for()` returns four answers and `survey_suspect_origin()` maps
three of them:

| verdict | flag |
| --- | --- |
| `RECEIVER` | `SURVEY_SUSPECT_CLOCK_COHERENT`, printed `clocked-here` |
| `UNEXPLAINED` | `SURVEY_SUSPECT_UNEXPLAINED`, printed `unexplained` |
| `UNKNOWN` | none, correctly -- nothing was established |
| **`EXTERNAL`** | **none** |

`EXTERNAL` is discarded, and it is the answer that *contradicts* a comb flag.
That contradiction is the whole argument for having a second kind of evidence
at all: a comb multiple is a coincidence argument, and this one is a
cancellation argument that shares no arithmetic with it, so the case where
they disagree is where the second opinion earns its place.

## The case, which is measured and is not hypothetical

**94.4 MHz is 1.6 x 59 and is also the loudest FM broadcast station at this
site**, confirmed at 46 dB. `CLAUDE.md` and `docs/receiver-artifacts.md` both
already record it as the reason the fine comb requires narrowness: a frequency
test alone would set a candidate aside from a report's per-allocation bests,
hiding a real transmitter, which is worse than the fault it fixes.

The width settles it today. The *reading* settles it independently: a real
transmitter there reads about 3.0 kHz displaced, so `survey_suspect_origin()`
declines to set `clocked-here` and `survey_suspect_origin_at()` returns
`READING_ORIGIN_EXTERNAL` positively -- at a confirmation pass's 977 Hz bin,
not at a band II sweep's, which cannot separate the hypotheses at 94 MHz.

An operator looking at that candidate sees `reference` and nothing arguing
back. The program knows better and does not say so.

## Why this is triage and not ready

**What it should say, and how loudly.** The obvious shape is a third flag and
a sentence -- "on the comb, but reads displaced: not clocked here" -- and the
obvious risk is that it reads as a *retraction* of the comb mark rather than
as evidence beside it. `survey_suspect_reason()` returns one sentence and the
chart draws one mark, so a candidate carrying both has to resolve to
something, and `sdrgui_survey_peak_mark()`'s existing precedence (empty beats
receiver-like) is the pattern to extend rather than to copy blindly.

**Whether it may suppress `SURVEY_SUSPECT_REFERENCE`.** Deliberately not done
when the flags were added: `survey_suspect.h` "never removes a candidate and
never says a peak *is* an artifact", and silently clearing a mark an operator
has learned to read is a bigger call than adding one. But the evidence here is
genuinely stronger than the coincidence it contradicts, and leaving both
standing means the suspicious *count* beside the candidate list keeps
including candidates the program has evidence against. Decide it explicitly;
do not let it be decided by whichever `if` is written first.

**How often it fires.** Unmeasured. It needs a signal within a comb tolerance
of a multiple *and* a bin fine enough to separate the hypotheses there, and on
the one band where that is known to happen -- band II, 94.4 MHz -- a sweep's
bin is too coarse and only a confirmation pass can reach it. It may be that
this is a confirmation-pass flag only, which would be a simpler ticket.

## What must be checkable

`check-suspect` already asserts the 94.4 MHz case both ways, including that a
band II sweep says `UNKNOWN` there and a pass says `EXTERNAL`. What is missing
is an assertion about what the operator is *told*, which is the same gap the
flags themselves had: they were set correctly and had no names for a whole
live sweep, and a feature that works and says nothing is indistinguishable
from one that does not work.

## Comments

**Decided 2026-09-11.** A **third flag** -- the reading is displaced, so
whatever is here is not clocked by this receiver -- drawn and listed **beside**
`reference` rather than instead of it. `SURVEY_SUSPECT_REFERENCE` is not
suppressed.

The reasoning is `survey_suspect.h`'s own and it is worth restating because the
other branch is tempting: this file "never removes a candidate and never says a
peak *is* an artifact", and clearing a mark an operator has learned to read is
a bigger act than adding one beside it. The coherence evidence is stronger than
the coincidence it contradicts, and *stronger* is not the same as *entitled to
overrule silently*.

**The cost is accepted and must be recorded rather than hidden**: the
suspicious count beside the candidate list keeps counting candidates the
program has evidence against. That is the known price of this choice. If it
turns out to mislead in practice -- a band II sweep whose count says "mostly
the receiver" when three of them read displaced -- the honest fix is to report
*both* numbers, not to start suppressing.

Two things still to settle in implementation, neither of them this decision:

- **The mark and the sentence.** `survey_suspect_reason()` returns one
  sentence and `sdrgui_survey_peak_mark()` draws one mark, so a candidate
  carrying comb-plus-displaced has to resolve to something. Extend the
  existing precedence (empty beats receiver-like) rather than copying it.
- **Whether it is a confirmation-pass flag only.** Unmeasured. It needs a
  signal within a comb tolerance of a multiple *and* a bin fine enough to
  separate the hypotheses there, and on the one band where that is known to
  happen -- band II at 94.4 MHz -- a sweep's bin is too coarse and only a pass
  can reach it. If it is pass-only, say so in the header; do not widen a
  tolerance to make it fire in a sweep.
