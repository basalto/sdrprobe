# Survey history belongs to a receiving setup

## Status

accepted; **the legacy-baseline clause amended 2026-09-11**, because it
described a feature that was never built. See "What a legacy baseline actually
is" below. The identity decision -- receiver, site and antenna -- is unchanged
and was always implemented.

## Context and decision

A survey history currently groups observations by site, while survey metadata
also names the antenna and the receiver may change. That makes a hardware
change look like a change in the radio environment: replacing a telescopic whip
with a rooftop antenna can make known carriers appear missing and previously
unheard carriers appear new, even though nothing on air changed.

A **receiving setup history** therefore belongs to one receiving setup,
identified by receiver, receiving site, and antenna. Gain remains observation
metadata rather than part of the identity: the history's presence and
prominence claims should survive ordinary gain adjustment, while the recorded
absolute level retains the setting needed to interpret it.

A site-only history written before this decision is **not read, not offered
and not assignable**. It is not merged with new observations for the simple
reason that nothing opens it: the program builds one history name,
`installation_history_path()`'s, and a file under the old name is invisible to
every code path. Such a baseline ages out by being replaced -- the new setup
starts empty and fills up -- rather than by being claimed.

## What a legacy baseline actually is

This clause originally promised that existing site-only histories were
"preserved as unassigned legacy baselines" and that "the operator must
explicitly assign one to a receiving setup". **Neither half was ever
implemented**, and the promise stood for long enough to be cited as though it
were.

The tell is that "preserved" reads as implemented when the file merely sits on
disk. `site_history_legacy_entries()` was written to offer one and had no
caller from the commit that added it (0.45.0) to the day it was deleted; the
three functions addressing a history *by site* had no caller outside `tests/`.
There was no button, no flag and no line of output that mentioned a legacy
baseline existed.

It is amended rather than implemented because assignment cannot be done
honestly at this scale. A calibration is one number and ADR-0018 can offer it
with **Claim +N PPM**; a history is a sweep count, per-hour tallies and
hundreds of entries, and assigning it means asserting a receiver *and* an
antenna for observations that cannot say what took them. That assertion is
what this ADR exists to refuse. An operator who genuinely knows the provenance
can rename the file, which is one shell command and leaves the knowledge where
it belongs -- with the person who has it.

The cost is recorded rather than hidden: **an existing legacy file is now
unreadable by any code path.** Nothing here reads one, so nothing here loses
anything; a future reason to read one would have to build the reading half
first.

## Considered options

- **History per site** conflates changes in equipment with changes on air.
- **History per site and antenna** separates the largest reception difference
  but still lets different receivers contribute incompatible measurements.
- **Automatically assign old history to the current setup** is convenient but
  creates provenance the stored data cannot establish.
- **Offer an unassigned legacy baseline for the operator to claim** is what
  this ADR said and is what the amendment removes: it is three surfaces and a
  reading path for a file this repository no longer has one of, and the
  operator's own knowledge is expressible as `mv`.

## Consequences

- History persistence and lookup must use receiver, site, and antenna together.
- Changing any member of the receiving setup selects a different baseline
  rather than mutating the previous one.
- Survey files continue to record observation settings, including gain, even
  when those settings are not part of history identity.
- Migrating the history format is a public file-format change under ADR-0016.
- A history file under the old `surveys/history-<site>.txt` name is inert. The
  property worth checking is that it stays inert while a receiver-scoped file
  exists beside it, which is what `check-installation` asserts.
