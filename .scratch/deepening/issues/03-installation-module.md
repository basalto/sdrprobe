# 03 - One installation module behind config, history and sidecar

Status: in progress. **The model and both ADRs' refusals are in**
(`src/installation.h`, `check-installation`, 47 checks). The four writer sites
and the history's on-disk key are next.

Site, antenna and the tuning correction are remembered and saved from four
places, each repeating the same three calls:

| site | what it does |
| --- | --- |
| `view_survey.c:691-693` | `config_remember_site`, `config_remember_antenna`, `config_save` |
| `overlay_calibration.c:673-675` | `config_set_site_ppm`, `config_save` |
| `sdrprobe.c:2793-2812` | `remember_site`, `remember_antenna`, `set_site_ppm`, `config_save` |
| `overlay_settings.c:89` | `config_save` |

The site history is a second store keyed by the same site string, loaded from
disk in `view_survey.c:257,259,577,613` and in `survey_history_refresh`, which
runs on screen change; and `survey_store_write` writes site and antenna a
third time into the sweep's JSON.

ADR-0018 says a correction belongs to a receiver *and* a site; ADR-0022 says a
history belongs to receiver, site and antenna; both say legacy site-only
values must be kept and claimed explicitly, not auto-assigned. With four
writers and three formats, that migration would touch all of them.

## The deepened module

An installation module owning the receiving setup (receiver identity, site,
antenna -- CONTEXT.md terms), its calibration profile, and its receiving setup
history, with one `commit()`. The three file formats become one adapter; an
in-memory adapter serves the checks, which is the second adapter that makes
the seam real. Views and overlays set fields on the installation and never
call `config_save` or `site_history_save` themselves.

The history stays in memory once loaded; the disk is read at start and on an
explicit site change, not on every screen change.

## What moves

- The four writer sites above collapse to `installation_commit`.
- `config.{c,h}` and `site_history.{c,h}` stay as the format layer; their
  parse/format functions are already pure and keep their checks.
- `survey_store_write` takes the installation rather than reading
  `app->config.*`.
- The ADR-0018/0022 migration -- receiver identity from USB serial or label,
  legacy values held unclaimed -- lands here and nowhere else.

## Checks

`check-config` and `check-site-history` keep their format assertions.
A new `check-installation`: a legacy site-only correction is not applied
until claimed; claiming assigns it to the current receiver and site; two
receivers at one site keep separate profiles; two antennas at one site keep
separate histories; commit writes each store once.

## Not in scope

Changing the on-disk formats beyond what ADR-0018/0022 require, which is a
public-interface change under ADR-0016 and gets its own MINOR bump.


## Comments

**2026-09-07 — one more thing belongs in the profile.**

`.scratch/calibrating-the-flags/01` measures the receiver's reference comb from
a sweep taken with the antenna disconnected, replacing the 14.4 MHz constant
compiled into `survey_suspect.h`. That number belongs in the calibration
profile beside the tuning correction, and for the same reason ADR-0018 gives:
it describes one physical setup and applying another's is silent rather than
wrong-looking.

**It is keyed by the receiving setup rather than by the receiver**, which is
the one place it differs from the correction. Unplugging the antenna sorts the
comb tones into two kinds: three of twelve stay exactly where they were, made
and heard entirely inside the receiver, and **the other nine go with the
antenna**, because the dongle radiates its clock and hears itself coming back.
Most of the observable comb is therefore a property of the antenna too, which
is precisely ADR-0022's argument for the history.

So the profile this module owns carries at least: the tuning correction
(receiver + site, ADR-0018), and the comb (receiver + site + antenna,
ADR-0022). Whether those are one record with a wider key or two is a design
question for this ticket rather than for the one that measures the number.

The consequence if it is missing is worth stating because it is invisible: a
wrong comb does not look like a fault, it looks like signals. Unflagged
artifacts enter the history and are remembered, then reported "gone" whenever
a later sweep misses them.


## Built 2026-09-09: the model, and the two refusals

`src/installation.h` is the receiving setup -- receiver, site, antenna -- and
the calibration profiles keyed to it. Pure: a model and some string handling,
no files, so `check-installation` reaches all of it (ADR-0012).

**ADR-0018, the correction.** Keyed by receiver *and* site. A legacy site-only
value is held with an empty receiver, is visible through
`installation_legacy_ppm()` so an operator can be offered it, and
`installation_ppm()` **will not return it**. `installation_claim_legacy()` is
the operator's explicit act; it converts the entry in place rather than copying
it, because claiming one legacy value for two receivers would invent exactly
the provenance the ADR refuses.

**ADR-0022, the history.** Keyed by receiver, site and antenna together.
Changing any member selects a different baseline rather than mutating the
previous one. Gain is deliberately not in the key: the history's presence
claims should survive an ordinary gain adjustment, and the recorded level keeps
the setting needed to read it. **An incomplete setup has no key at all** --
that is the refusal, not a shortfall.

### Receiver identity, and what this dongle actually reports

`device_profile` gained a `serial`, filled by `backend_rtlsdr.c` from
`rtlsdr_get_device_usb_strings()`. Asked directly, the dongle here reports
`77771111153705700` -- a real one, so ADR-0018's USB-serial path works.

`installation_identify()` takes a serial and a label and prefers **the label**.
That is the right way round and the ADR's own reasoning is why: many of these
dongles ship with the same serial or none, uniqueness cannot be judged from one
device, and an operator sets a label precisely to resolve that. A receiver with
neither has no identity, and a correction that cannot say whose crystal it
compensates is not persisted.

### Verified by mutation

Both refusals were reverted and both fail by name:

- applying a legacy value without claiming it -- "it is not applied" fails;
- keying the history by site alone -- "and it is a different one" and "and a
  different receiver too" fail.

## Still to do

- The four writer sites collapse to `installation_commit()`.
- `site_history`'s on-disk key becomes the setup's, with legacy site-only
  files held unassigned -- the same shape as the correction's legacy path.
- `survey_store_write` takes the installation rather than reading
  `app->config.*`.
