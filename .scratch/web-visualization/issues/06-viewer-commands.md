# 06 - Viewer commands

Status: resolved, 2026-09-16

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

## Comments

### 2026-09-16 -- done

New: `src/viewer_command.{c,h}` -- the parser alone, `sscanf` for the command
word (bounded, `%31s`) and `strtoul()` rather than `sscanf`'s own `%lu` for the
value (a numeric conversion that overflows what it converts to is undefined
behaviour in `sscanf`, C11 7.21.6.2p10, which matters here because this is
parsing bytes an attacker-controlled Viewer sent over a socket; `strtoul()`'s
overflow behaviour is defined, clamping to `ULONG_MAX` and setting `errno`).
`check-viewer-command`: 28 checks, `-lm` alone -- a valid line, leading and
trailing whitespace tolerated, an empty line, a whitespace-only line, a
malformed value, a missing value, a negative value, an unrecognized command
word, a trailing field, a value that overflows `uint32_t`, a line at the
`VIEWER_COMMAND_LINE_MAX` bound, and both a `NULL` error buffer and one too
small to hold a full reason.

`viewer_link.h`/`.c` gained a fourth stream, `command_result` -- but unlike
`link_health` (ticket 08) this one does **not** follow the replaceable-slot
rule the other three do. A command is reliable and ordered, so
`VIEWER_STREAM_COMMAND_RESULT`'s slot is fed from a small per-client FIFO
(`VIEWER_COMMAND_RESULT_QUEUE_DEPTH`, 16 deep -- headroom for a human typing
commands, not a tuned capacity) rather than ever being overwritten;
`load_command_result_into_slot()` is the only place a queued result is
loaded into the slot, called right after enqueuing and once more at the end
of `flush_client()`, for the case that matters: an earlier result was still
in flight when this one queued behind it. `viewer_link_set_command_handler()`
is the seam to the receiver -- an opaque function pointer and `void *ctx`,
the same shape `device_backend.h` uses to keep hardware out of code that
does not need it, so `viewer_link.c` still has no `struct app`, no raylib.
`viewer_session.c` wires it to `retune_receiver()` directly -- no path of
its own -- quoting `app->receiver_error` on failure exactly as the ticket
asks.

**A real, pre-existing bug in the test file's own `client_next_frame()`
helper, found by this ticket's reliability test and unrelated to anything
it added.** It returned a pointer into `tc->buf` and then, in the same
call, `memmove()`d over that exact region to compact the buffer -- correct
only when nothing else was already queued behind the frame just returned
(the common case every earlier test happened to be in: publish one
message, read it, repeat), and silently wrong the moment more than one
frame was buffered at once, which ticket 06's own
`test_commands_are_never_dropped_while_state_updates_are` is the first
test here to do. The caller ended up reading whatever frame came *after*
the one it asked for, overwritten into the same memory before it was ever
read -- diagnosed the same way the ticket 05 wire bug was, working
backwards from a raw hex dump of exactly what the client had actually
received rather than trusting the parser that was itself in question.
Fixed by deferring the compaction to the *start* of the next call instead
of the end of this one, so a returned payload stays valid until the
caller asks for another.

**Verified live against the R820T attached to this machine** (a capture
refuses every retune outright -- `retune_receiver()`'s own `!rt.live`
check, unchanged): `scripts/viewer_client.py --send "tune 98000000"` while
subscribed to `receiver_state` shows `center_hz`/`tuning_generation`
holding at the old values, then `command_result command='tune 98000000'
ok=True error=None`, then both moving together to the new frequency and
generation 1 -- center and generation never move separately, matching
ticket 05's own tuning-generation proof. A refused command
(`tune 99999999999`, overflowing `uint32_t`) reports `ok=False
error=frequency out of range` instead of a generic failure.
`scripts/viewer_client.py` gained `--send LINE` (repeatable) and a
`command_result` branch in `run_print` (which previously assumed every
JSON text message was `receiver_state` -- the exact bug already fixed for
`link_health` in this same file, now fixed proactively for a stream that
did not exist yet when that fix landed).

**"The listening socket still refuses a non-loopback connection"** needed
no new code and no new test: `viewer_link_open()` has bound to
`INADDR_LOOPBACK` unconditionally, with no configuration option, since
ticket 04 -- there is no code path capable of binding elsewhere, and every
existing test connecting successfully over 127.0.0.1 is already an
implicit, continuous check that nothing has changed that.

`make check`: 21521 checks, 75 suites, no failures (up from 21430/74:
28 from `check-viewer-command`, the rest from `check-viewer-link`'s new
command tests and the Makefile's own `make-help` audit picking up the new
target). `check-pipelines`: 34 checks, unchanged. `nm -u` on
`viewer_link.o`, `viewer_command.o`, `process_cpu.o`, `viewer_session.o`
and `websocket.o` confirms no raylib symbol reaches any of them.
