# 02 - `--not-token`: run `--serve-bind` with no authentication, explicitly

Status: resolved, 2026-09-17

## What was asked

An option to run `server`/`web` bound beyond loopback without a token at
all -- for an operator who judges their own LAN trustworthy enough that
managing a shared secret is more ceremony than the network warrants.

## The decision, and why it isn't just "remove the refusal"

Two shapes were on the table: drop the requirement outright (`--serve-bind
any` alone would then just work), or add a second, equally explicit way
to satisfy it. The first was rejected on sight -- nothing would then
distinguish "I meant to run with no authentication" from "I forgot the
token," and a silent default is exactly the shortcut ADR-0027's original
refusal exists to close off. The second is what shipped: `--not-token`
is a named flag, not an inference from an absent one.

`--serve-bind` beyond loopback still refuses with **neither**
`--serve-token` nor `--not-token` given -- the omission that used to be
silently wrong is now a decision, one way or the other, every time.
`--serve-token` and `--not-token` given **together** are refused too, in
either order, as the contradiction they are rather than one silently
winning.

## What changed

- `src/options.h`: `serve_allow_no_token` (int), set only by `--not-token`.
- `src/options.c`: parses `--not-token` (refuses if given twice); the
  post-loop refusal now accepts either `serve_token` or
  `serve_allow_no_token`; a new refusal catches both being set together.
- `src/viewer_session.c`: a `WARNING:` line, printed before the ordinary
  "Viewer link listening" line rather than folded into it, whenever the
  bind is beyond loopback and no token is set -- reachable only through
  `--not-token`, since the combination is otherwise refused at parse
  time. Fixed a related latent bug found while wiring this in: the
  `SERVE_BIND_ANY` message branch built its URL as `?token=%s` with
  `app->options.serve_token` unconditionally, which would have printed
  `?token=(null)` the first time that path was reached with no token --
  it never had been before `--not-token` existed, since a token was
  always required up to now.
- `docs/adr/0027-*.md`: a further addendum recording the decision and
  restating, for `--not-token` specifically, everything the ADR's own
  amendment already said a token does not provide -- per-user identity,
  transport encryption, rotation, rate limiting -- since `--not-token`
  provides even less.

## Verified

- `--serve-bind any` alone: still refused, names both `--serve-token`
  and `--not-token` as the ways to satisfy it.
- `--serve-bind any --not-token`: accepted, prints the `WARNING:` line,
  then listens; a plain `curl`/WebSocket connect with no token succeeds.
- `--serve-bind ADDRESS --not-token` (a specific interface, not `any`):
  same, verified live against `127.0.0.2`.
- `--serve-token X --not-token` (either order): refused, names the
  contradiction.
- `--not-token` given twice: refused.
- `--not-token` with no `--serve-bind`: accepted and harmless, same
  principle as a lone `--serve-token`.
- `make check-options` (398 checks, up from 388) and a full `make check`
  pass.

## Relationship to other tickets

- Amends `issues/01-serve-bind-and-token.md` and the same ADR-0027
  section that ticket amended; not a new bind mechanism, a second way to
  satisfy the same gate.
