# 03 - One installation module behind config, history and sidecar

Status: resolved, 2026-09-09. The model, both ADRs' refusals, the file format,
the history key and the claim path. **MINOR bump to 0.45.0** under ADR-0016:
the config gains a line and the history gains a filename shape.

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

## The file formats, and what a legacy file does

**A new line rather than a wider one.** `calibration <receiver> <ppm> <site>`
carries ADR-0018's profiles; `known_site <ppm> <label>` is untouched and is now
read as the **legacy** value it always was. A file written by this build still
reads correctly in an older one -- it keeps the site list and ignores what it
does not know -- and, more importantly, a legacy value stays visibly legacy
instead of being silently given an owner. The receiver comes first on the line
because it is the field with no spaces in it: a site can be "Rua da Prata 2"
and has to be the tail.

**The history keeps its old filename.** `surveys/history-<site>.txt` is
untouched and `surveys/history-<receiver>-<site>-<antenna>.txt` is the new
shape. The two coexist, which is ADR-0022's migration rather than a
transitional mess: a file written before the ADR cannot say which receiver and
antenna produced it, and inventing that is the one thing it refuses.
`site_history_legacy_entries()` says how much one holds so it can be offered.

`site_history_load/save` split into a by-path pair, so the format layer stays
the format layer -- it reads and writes a file and has no opinion about what
identifies one.

## The claim path, because a refusal needs a remedy

Holding a correction unassigned is only honest if there is a way to assign it.
`--claim-calibration` is the operator's explicit act, and
`--receiver-label` names a receiver whose USB serial is missing or shared.
Exercised end to end on a scratch config:

```
Site "home-sala-estar" has a -31 ppm correction from before calibrations
named a receiver.
It is not applied. --claim-calibration assigns it to this one
(77771111153705700).
```

and after claiming, `known_site 0 home-sala-estar` plus
`calibration 77771111153705700 -31 home-sala-estar`, with the next run silent
because the profile answers. A receiver with no identity says so and points at
`--receiver-label` rather than offering a claim it cannot make.

**Nothing in this repository's own `surveys/` or config was touched**, and the
real config turns out to have `known_site 0` for every site -- no legacy
correction to migrate here at all.

## The last two, 2026-09-09

**A sweep names the setup that took it.** `survey_store_write` reads the
installation, and the JSON's `receiver` block gained an `id` -- written as
`null` rather than omitted when there is none, so a reader can tell "nobody
said" from "not recorded". A sweep that cannot say which receiver took it
cannot be matched to a calibration or to a baseline, which is the argument of
both ADRs.

That surfaced a line that was quietly lying. A headless survey of a *capture*
printed `survey-history site ... sweeps 1 signals 2 new 2 quiet 0` while
writing nothing at all: a capture reports no receiver, so the setup has no key
and ADR-0022 declines the file. It says so now -- *"survey-history not kept: a
baseline needs a receiver, a site and an antenna"* -- which is the ordinary
answer for a replayed file rather than a fault.

**The overlay offers the claim.** A `Claim -31 PPM` button, in the gap between
Scan and the channel field, drawn only when there is one to claim and
**zero-width when the window is too narrow to hold it** -- the same rule the 4G
controls use, so `check-layout` skips it rather than reporting a collision.
`Apply PPM` records against receiver *and* site through the installation
instead of writing a site-only value.

### And the message that was a lie

With the claim in place the overlay read `current correction: -31 PPM` under a
startup line saying *"It is not applied"*. Both were true of different code:
the warning was new and `options.ppm = config_site_ppm(...)` at startup was
not, so the program announced a refusal and then applied the value anyway.

It could not have been right there: that block runs **before the device is
open**, so it cannot know which receiver it is deciding for. The correction is
chosen after `installation_load()` now, where the serial is in hand -- an
explicit `--ppm` recorded against receiver and site, otherwise a claimed
profile restored and said so, otherwise nothing. The overlay reads `+0 PPM`
with the claim offered beside it, and the two agree.
