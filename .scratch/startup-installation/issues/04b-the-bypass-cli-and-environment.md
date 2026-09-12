# 04b - Saying what the form would ask, from a flag or the environment

Status: resolved, 2026-09-12

The overlay exists to find out the installation. A run that already states it
has nothing to be asked, and should not be stopped to be asked it.

`--site` and `--antenna` already exist and already persist. What does not
exist is an environment route, and that is the one a launcher, a systemd unit
or a field script can actually use -- editing an `ExecStart` line is not
always available, and wrapping the binary in a shell script to add two flags
is how a deployment acquires a second place to get the site wrong.

## What to build

| variable | equivalent |
| --- | --- |
| `SDRPROBE_SITE` | `--site` |
| `SDRPROBE_ANTENNA` | `--antenna` |
| `SDRPROBE_RECEIVER_LABEL` | `--receiver-label` |
| `SDRPROBE_NO_STARTUP` | `--no-startup` |

**One precedence rule: a flag beats an environment variable beats the config
file.** And a value arriving by any of the three routes is remembered the same
way -- a site named once should be offerable next time however it was named.
Two rules here is how one place becomes two.

**Read through a lookup, not through `getenv`.** `parse_options()` is pure and
`check-options` reaches every flag and every rejection because of it. Put the
environment in `options_apply_environment(struct options *opts, const char
*(*lookup)(const char *))`: a check hands it a table, `main` hands it `getenv`.
Calling `getenv` inside the parser puts the one thing a check cannot control
in the middle of the one thing every check controls.

**Where it runs.** After `parse_options()` and before the config is reconciled
in `main`, so the existing "given here they are written to the configuration
file and stand until changed again" comment stays true of all three routes.

## The trap

An empty environment variable is **not** a value. `SDRPROBE_SITE=` set by a
unit file that forgot to fill it in must leave the config's site alone and
must **not** suppress the form -- otherwise a misconfigured launcher silently
produces sweeps filed under whatever the last site was. Treat empty as absent,
and pin it.

## Acceptance criteria

- `make check-options` covers: each variable applied; a flag overriding its
  variable; a variable overriding the config; an empty variable treated as
  absent and **not** suppressing the form; an unset environment changing
  nothing.
- `options_apply_environment()` takes the lookup as an argument and the test
  passes a table, never `getenv`.
- `--help` lists the variables. A bypass nobody can discover is a bypass
  nobody uses.

## Not in scope

- An environment route for anything else. These four are the ones the form
  asks about; a general "every flag also has a variable" is a bigger promise
  than this needs.


## What was built

`options_apply_environment(options, lookup)` and `startup_form_wanted()` in
`options.c`, both pure; `--no-startup`; the four variables in `--help`.
`check-options` covers every case including an empty variable applying nothing
and **not** suppressing the form.

`getenv()` returns `char *`, so `main` passes a one-line `environment()`
wrapper rather than widening the signature -- a lookup handing out a mutable
pointer into the environment invites a caller to write through it.

Verified end to end: `SDRPROBE_SITE=test-site ... --debug-log -` put
`site "test-site"` on the `installation` line.
