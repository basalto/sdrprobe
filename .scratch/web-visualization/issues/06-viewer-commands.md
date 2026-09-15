# 06 - Viewer commands

Status: ready-for-agent

## Goal

The inbound half: a Viewer retunes the receiver, and is told what happened.

## Shape

Commands are **whitespace-delimited text lines** -- `tune 948400000` -- parsed
with `sscanf`. This is deliberate and it is not laziness. This program writes
JSON and has never parsed it; `src/capture_sidecar.h:18` says so outright:
*"This is not a JSON parser and must not become one."* A JSON command channel
would be that parser. A line format keeps the refusal, fits in about twenty
lines, and makes the whole control path drivable by hand with `websocat`.

Commands are **reliable and ordered**, unlike a State update. A State update
is dropped to stay current; a command never is, because a dropped retune is
not a stale picture but a receiver pointed somewhere nobody asked for. Every
command gets exactly one result naming the command it answers.

A command goes through `retune_receiver()` and the transaction in
`src/receiver_runtime.c` like every other caller -- stop, apply, flush, read
back, restart, with rollback at each step. It does not get its own path. On
failure the result quotes `app->receiver_error`, which is the one buffer that
now owns the reason a retune failed.

## Security

None, and that is the decision rather than an omission. ADR-0027: the bind
address is the authorization boundary, so reaching 127.0.0.1 already requires
an account on the machine. **If this is ever bound beyond loopback, this
ticket's design is void** and authentication comes first.

## Acceptance criteria

- [ ] `tune <hz>` retunes and the charts follow, with the tuning generation
      advancing so in-flight updates are not drawn under the new metadata.
- [ ] A refused command reports why, quoting `receiver_error` rather than a
      generic failure.
- [ ] The command parser has a check linking `-lm` alone: a valid line, a
      malformed line, a line with a trailing field, an out-of-range value, an
      empty line, and a line long enough to test the bound. A parser reading
      bytes from outside the process is checked for what it rejects, not only
      for what it accepts.
- [ ] Commands are never dropped under load, while State updates on the same
      connection still are. Both properties asserted in the same run.
- [ ] The listening socket still refuses a non-loopback connection.

## Not in scope

- Sweeps, recording, calibration, band scans. One command proves the path; the
  rest follow once it has.
- Authentication of any kind, and any bind address but loopback.
- Multiple Viewers commanding at once. Worth deciding before it is possible,
  but not here.
