# 13 - ADR-0022's legacy baseline has no way to be claimed

Status: **resolved by amending the ADR, 2026-09-11.** Both triage questions
answered no; the clause is gone, the three dead functions with it, and the
property is checked. See the comments.
Opened 2026-09-10, from a dead-code audit after the survey history was cleared.

ADR-0022 says:

> Existing site-only histories are preserved as unassigned legacy baselines
> but are not merged with new observations automatically. The operator must
> explicitly assign one to a receiving setup before it becomes that setup's
> history.

**Neither sentence is implemented.** This ticket first said the preservation
half worked and only the assignment was missing; that was wrong, and it is the
ordinary shape of a false claim beside right arithmetic -- the file is
*preserved* on disk, so "preserved" reads as implemented, and nobody asked who
opens it.

Nobody opens it. `installation_history_load()` builds its name with
`installation_history_path()`, the receiver-site-antenna shape, and that is the
only history the program reads. The three functions that address a history
**by site** -- `site_history_path()`, `site_history_load()`,
`site_history_save()` -- have no caller anywhere in `src/`; only `tests/`
touches the path builder, to assert the legacy name it produces. So a legacy
baseline is not merged for the trivial reason that it is never read, and the
ADR's "preserved as unassigned legacy baselines" describes the filesystem
rather than the program.

There is likewise no button, no flag and no headless line that assigns one, and
nothing ever tells the operator that a legacy baseline exists.
`site_history_legacy_entries()` was written for exactly that -- "how much a
legacy site-only baseline holds, so it can be offered", as
`03-installation-module.md` puts it -- and it **had no caller from the commit
that added it** (0.45.0) to the day it was deleted. That is the whole of the
evidence: the promise was made in an ADR, a function was written to keep it,
and the offering was never built.

It is deleted rather than left in place, because a function that exists and is
never called reads as an implemented feature to everyone after: the header
now says the assignment does not exist and points here.

## Why this is triage and not ready

The calibration twin is the shape to copy and the reason to hesitate is the
same. ADR-0018's legacy ppm has `installation_legacy_ppm()`, a **Claim +N PPM**
button in the calibration overlay, and `--claim-calibration` with
`--receiver-label` for a dongle with no serial. Three surfaces for one value.
A history is bigger than a value: assigning one means picking a receiver *and*
an antenna for observations that cannot say what took them, and the sweep
count and the per-hour tallies come with it.

Two questions decide whether it is worth building at all:

1. **Is there any legacy history left to claim?** On this machine, no --
   `surveys/history-home-sala-estar.txt` was deleted on 2026-09-10 along with
   every sweep, so there is nothing for the feature to act on here. It is
   a question about anyone else's `surveys/`, and this is a single-operator
   repository.
2. **Is a claimed baseline worth having?** A history's value is that it says
   what a *setup* has heard. Assigning eight sweeps of unknown provenance to
   the current receiver and antenna asserts exactly what ADR-0022 refuses to
   invent -- the operator would be asserting it rather than the program, which
   is the ADR's point, but only if the operator actually knows.

If the answer to both is no, the honest close is to **amend ADR-0022**: say
that a legacy history is neither read nor assigned, and that it ages out by
being replaced rather than claimed. That is a smaller change than building a
surface nobody needs, and it makes the ADR true. It also lets
`site_history_path()`, `_load()` and `_save()` go with it -- three more
functions the program does not call, and the reason the tests still assert the
legacy filename would go too.

**Note what the deletion costs, because it is the argument for the other
branch.** Deleting them makes an existing legacy file unreadable by any code
path, so an operator with one has nothing to claim it *with*. If anyone's
`surveys/` still holds one, the reading half has to be built before the
assigning half, and the reading half is what `site_history_load()` already is.

## What must be checkable either way

That the program does not silently merge one. `check-site-history` covers the
file format; nothing covers "a legacy file and a receiver-scoped file both
exist and the legacy one is not folded in". That check is worth having
whichever way this goes, because it is the property the ADR is actually about
-- and it is **cheaper than it looks now**: "not merged" is currently "not
opened", so the check is over `installation_history_load()` and asserts it
returns the receiver-scoped entries and nothing else. If the reading half is
ever built, the same check keeps meaning what it says.

## Comments

**2026-09-10, from a review of this ticket.** Corrected above: the
preservation half is not implemented either. Verified by
`grep -rn 'site_history_load\b\|site_history_save\b\|site_history_path\b'`
over `src/` and `tests/` -- the only hits in `src/` are the declarations and
the definitions calling each other -- and by `git log -S` on
`site_history_legacy_entries`, which returns the single commit that added it.
The header comment written when the function was deleted also named a
`site_history_load_for_site()` that has never existed; corrected in
`src/site_history.h`.

**2026-09-11 -- amended rather than built.** Both of the two questions that
decide it came back no: there is no legacy history left on this machine
(`surveys/history-home-sala-estar.txt` was deleted on 2026-09-10 with every
sweep), and this is a single-operator repository, so the feature would have
acted on nothing. The second question -- whether a claimed baseline is worth
having -- answers itself once the first does: assigning sweeps of unknown
provenance to a receiver and an antenna asserts exactly what ADR-0022 refuses
to invent, and an operator who genuinely knows can express it with `mv`.

**What changed.**

- `docs/adr/0022-*`: the legacy-baseline clause now says a site-only history
  is not read, not offered and not assignable, and ages out by being replaced.
  A new section says the promise was never implemented and why it is not being
  implemented now; the status line carries the amendment and its date. The
  identity decision -- receiver, site, antenna -- is untouched and was always
  implemented.
- **Deleted**: `site_history_path()`, `site_history_load()` and
  `site_history_save()`, the three entry points that addressed a history by
  site. They had no caller outside `tests/`. This is the same deletion made
  for `site_history_legacy_entries()` and for the same reason: a function that
  exists and is never called reads as an implemented feature to everyone
  after, which was the whole fault.
- `src/site_history.h` and `src/installation.h` say what is true now and point
  here. The header's own opening comment said "one file per site" and named
  `history-<site>.txt` as the format's file; it names the receiving setup's.
- `CLAUDE.md` and `docs/band-surveys.md` both described the site-only name as
  the history the program writes. Both were wrong before this ticket, not by
  it -- the receiver-scoped name landed with ADR-0022 and neither document
  followed.

**The check, which is the part worth having either way.**
`test_a_legacy_baseline_is_inert()` in `check-installation`: a legacy file and
a receiver-scoped file side by side, and `installation_history_load()` returns
the receiver-scoped entries and nothing else -- then the receiver-scoped file
is removed and it returns *no history* rather than adopting the legacy one.
Both directions, because they fail differently: folding one in adds carriers
that were never transmitted to a known baseline, and adopting one hands a
fresh setup somebody else's memory so every real signal reads steady rather
than new. "Not merged" is implemented as "not opened" today; if the reading
half is ever built, the check keeps meaning what it says.

It writes files, so `check-installation` now links `installation.c` and
`config.c` and the test runs in a `mkdtemp` directory of its own. The
operator's `surveys/` is never touched. 69 checks, up from 56.

**One property moved rather than died.** `check-site-history` asserted that a
site typed with spaces or a slash still names one file inside `surveys/` --
over `site_history_path()`. That is worth keeping and
`installation_history_path()` is the only builder left, so the assertion is in
`check-installation` now, including the `surveys/` prefix the old one checked
by construction.

**What this costs, recorded rather than hidden.** An existing legacy file is
now unreadable by any code path. Nothing here has one, so nothing here loses
anything -- but a future reason to read one has to build the reading half
first, and that is `site_history_load_path()` plus a name.
