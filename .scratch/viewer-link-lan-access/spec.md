# Viewer link: reachable from the LAN, behind a token

A second laptop on the same LAN as the receiver has no route to the
Viewer today. ADR-0027 bound the link to loopback unconditionally and
named the reason: the bind address was the whole authorization
boundary, since reaching `127.0.0.1` already requires an account on the
machine. Opening it was considered and declined in that same ADR,
because it "makes authentication, transport security and a threat model
prerequisites of the control path rather than later work" -- exactly
the shape of shortcut this repository's own culture (`CLAUDE.md`) warns
against taking.

## Decided, 2026-09-17

Amend ADR-0027 rather than route around it. `--serve-bind any|ADDRESS`
opts into binding beyond loopback; it requires `--serve-token SECRET` in
the same invocation, refused at parse time otherwise (`options.c`).
Every request the link then receives -- the plain page and the
WebSocket upgrade alike -- must carry `?token=SECRET` in its path
exactly, checked before either is served. Loopback with no flags is
unchanged, byte-for-byte, in every existing behaviour.

See the ADR's own "Amendment, 2026-09-17" section for the full
reasoning, including what a shared secret in a URL does **not** provide
(per-user identity, transport encryption, rotation, rate limiting) and
why TLS was judged worse than the plaintext token for this specific
case (a self-signed certificate's browser warning, for a tool meant to
be opened without ceremony).

## What this is not

- Not a general-purpose auth system. One shared secret, one class of
  access (full control), no per-device identity.
- Not a substitute for the SSH tunnel this repository already
  documents for a network that is not a small, trusted home LAN.
- Not a change to loopback behaviour. Every default-path caller --
  the window's own headless serve, every test, every capture-driven
  `--serve` invocation -- is unaffected.

## Amendment, 2026-09-17 -- `--not-token`

Asked for directly: a way to run `--serve-bind` with no authentication
at all, for an operator who judges their network trustworthy enough not
to want a secret to manage. Given as a named, explicit flag rather than
by loosening the refusal into silence -- `--serve-bind` beyond loopback
still refuses with neither `--serve-token` nor `--not-token`, and
refuses again if both are given together. Loud at runtime as well as at
parse time: a `WARNING:` line naming exactly what it means precedes the
ordinary listening line. See `issues/02-not-token.md`.

## Tickets

1. `issues/01-serve-bind-and-token.md`
2. `issues/02-not-token.md`
