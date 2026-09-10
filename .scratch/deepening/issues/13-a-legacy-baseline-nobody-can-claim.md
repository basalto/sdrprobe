# 10 - ADR-0022's legacy baseline has no way to be claimed

Status: needs-triage
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
