# Calibration profiles identify both receiver and site

## Status

accepted

## Context and decision

A frequency correction compensates the crystal error of one physical receiver,
but the measurement that produced it also depends on the reference available at
a receiving site and on conditions at the time. A correction keyed only by site
silently assumes that one configuration is used with one receiver; changing
receivers at the same site can then apply another receiver's correction.

Persisted corrections are therefore **calibration profiles identified by both
receiver and receiving site**. A receiver is identified by its USB serial when
that serial is present and unique. A receiver without a unique serial uses a
stable operator-assigned label. Device index is not an identity: it can change
when receivers are disconnected or enumerated in a different order.

Existing site-only corrections are preserved as unassigned legacy values but
are not applied automatically. The operator must explicitly claim a legacy
value for the current receiver, after which it becomes that receiver's profile
for the site. This avoids silently attaching a plausible correction to the
wrong crystal while retaining measurements whose provenance the operator can
still establish.

## Considered options

- **One correction per site** keeps the current file shape, but conflates the
  place where a correction was measured with the receiver whose error it
  compensates.
- **One correction per receiver** identifies the crystal correctly, but loses
  where and against which available reference the value was established.
- **Assign legacy values to the first receiver seen** is convenient but makes
  an irreversible provenance claim the program cannot justify.

## Consequences

- Configuration and survey metadata must carry a stable receiver identity in
  addition to site and antenna.
- Selecting a receiving site restores a correction only when a profile also
  matches the current receiver.
- Receivers without unique USB serials require a stable label before their
  calibration can be persisted.
- Migrating the persisted configuration is a public file-format change under
  ADR-0016 and must preserve unclaimed legacy values.