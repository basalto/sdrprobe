# Survey history belongs to a receiving setup

## Status

accepted

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

Existing site-only histories are preserved as unassigned legacy baselines but
are not merged with new observations automatically. The operator must
explicitly assign one to a receiving setup before it becomes that setup's
history. This retains useful observations without inventing which receiver and
antenna produced them.

## Considered options

- **History per site** conflates changes in equipment with changes on air.
- **History per site and antenna** separates the largest reception difference
  but still lets different receivers contribute incompatible measurements.
- **Automatically assign old history to the current setup** is convenient but
  creates provenance the stored data cannot establish.

## Consequences

- History persistence and lookup must use receiver, site, and antenna together.
- Changing any member of the receiving setup selects a different baseline
  rather than mutating the previous one.
- Survey files continue to record observation settings, including gain, even
  when those settings are not part of history identity.
- Migrating the history format is a public file-format change under ADR-0016.