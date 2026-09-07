# 03 - One installation module behind config, history and sidecar

Status: ready-for-agent

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
