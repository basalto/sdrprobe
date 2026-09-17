# 01 - `--serve-bind` and `--serve-token`: the Viewer link beyond loopback

Status: resolved, 2026-09-17

## What was asked

Whether a second laptop on the same LAN could be a Viewer client. The
honest answer started as "no, and that's ADR-0027's own point" -- the
bind address is the whole authorization boundary, and there is no flag
to change it. Asked to change that anyway, with the tradeoff named
first: a shared-secret token, or the SSH tunnel that already works with
no code change. The token was chosen.

## What changed

- `src/viewer_link.h`/`.c`: `viewer_link_open()` takes a bind address
  (host byte order -- `INADDR_LOOPBACK`, `INADDR_ANY`, or a specific
  interface's address) and an optional required token. `struct
  viewer_link.required_token` is NULL by default (ADR-0027's original
  boundary, unchanged). `token_authorized()` reads `?token=...` out of
  the request path -- an exact byte match, no URL-decoding, on the same
  principle `websocket.h` states about itself ("not a URL parser"). A
  request that fails it gets `401 Unauthorized` before the upgrade or
  the page, never after.
- `src/options.h`/`.c`: `--serve-bind any|ADDRESS` and `--serve-token
  SECRET` (8-128 characters, `[A-Za-z0-9_-]` only -- safe unescaped in
  a URL). Binding beyond loopback with no token is refused at parse
  time, not left to start an unauthenticated listener a firewall
  happens to be the only thing in front of.
- `src/viewer_session.c`: resolves the bind address from options,
  prints the actual listening address (not a hardcoded `127.0.0.1`),
  and includes `?token=...` in every URL it prints or auto-opens.
- `scripts/viewer_client.py`: `--token`, so the existing diagnostic
  client can reach a token-gated link.
- `docs/adr/0027-*.md`: amended with the decision, what a shared secret
  in a URL does and does not provide, and what live testing showed.

## A pre-existing bug found while testing this, fixed alongside it

`viewer_session_run()`'s loop never read `--duration` -- only
`stop_requested()` (SIGINT/SIGTERM). `sdrprobe.c`'s other headless loop
(decode/playback) has always honoured its own duration; this one,
added later, never gained the same check. Every `--serve --duration N`
run, with or without this ticket's flags, silently outlived N until
killed. Pulled into a pure predicate, `viewer_duration_elapsed()`
(`viewer_session.h`), mirroring `viewer_update_due()`'s own shape --
a decision belongs out of the loop that cannot be reached without a
receiver (ADR-0012) -- and unit-tested in `viewer_session_test.c`.
Unrelated to the LAN/token work itself, but this ticket's own testing
is what found it, and it is now fixed rather than filed and left.

## Verified

- **Live, against a real bind beyond loopback** (`127.0.0.2`, which
  routes over loopback but exercises the real non-default `bind()`
  path rather than a synthetic one): no token and the wrong token both
  refused with `401` before an upgrade or the page; the correct token
  succeeds for both. Checked at the HTTP layer (`curl`) and the
  WebSocket layer (`scripts/viewer_client.py --token`).
- **The default path is unchanged**: no `--serve-bind`/`--serve-token`
  reproduces the exact pre-amendment startup message and behaviour.
- `make check-viewer-link` (187 checks, up from 174 -- three new tests:
  the plain page's token gate including off-by-one lengths and other
  query parameters either side, the upgrade's token gate, and query
  parsing among neighbours), `make check-options` (383, up from 365 --
  bind-kind parsing, the address round-trip through `inet_pton`, both
  refusals, both duplicate-flag guards), `make check-viewer-session`
  (22, with the new duration predicate's own tests) all pass.
- `make check` (77 suites) and `make check-pipelines` both pass with no
  regression.
- The `MISSING:`/`NOT GATED` audits (`CLAUDE.md`) stay clean -- no new
  header outside `APP_HDR`, no new `check-*` rule outside
  `CHECK_UNITS`.

## Take into account

- **This is not a general auth system.** One shared secret grants full
  control -- the same commands a loopback account already could send.
  A network with an untrusted device on it should use the SSH tunnel
  instead; this amendment does not replace it, and says so in the ADR.
- **No TLS.** A self-signed certificate's browser warning was judged
  worse, for a tool meant to be opened without ceremony, than the
  token crossing the LAN in plaintext. Named as a real gap in the ADR,
  not silently accepted.
- **No rotation, no expiry, no rate limiting.** A wrong token costs one
  refused TCP connection, nothing more; a leaked token is compromised
  until the operator changes it by hand.
- No UI for entering a token was added to the web page. One line in
  `src/viewer_page.h`'s `connect()` forwards whatever query string
  loaded the page (`location.search`) onto its own WebSocket open call
  -- the token travels with the page's own URL, so the one thing an
  operator types (the URL) is the only thing either check reads.

## The error was not clear, and is fixed

**First reported live**, against the actual built binary, not found by
any check: `--serve --serve-bind any --serve-token` (no value) and
`--serve-bind any --serve-token 12345` (5 characters) both refused
correctly but printed nothing beyond the full usage dump -- the same
treatment every other bad flag combination gets, and for most of them
that is right, per `main()`'s own comment: *"every other parse failure
is a bad flag or a bad combination, and the usage text is the whole
answer there."* These two are not that shape: an address that fails
`inet_pton()` and a token of the wrong length are rules specific to one
flag each, nameable exactly, and a first-time reader has no way to
guess the token's charset or length bounds from a 100-line usage dump.

**Fixed the way `unknown_command` already sets the precedent for**:
`options->serve_bind_error[160]`, filled in at the exact point of
refusal (each of five clauses: `--serve-bind` with no value or given
twice, an address `inet_pton()` refuses, `--serve-token` with no value
or given twice, a token outside 8-128 characters, a token with a
disallowed character, and the post-loop "beyond loopback with no
token" refusal), printed by `main()` before the usage dump exactly as
`unknown_command` already is. Every message names the specific value
or count involved (`"--serve-token must be 8-128 characters (got 5)"`,
`"--serve-bind \"not-an-address\" is neither..."`) rather than a generic
restatement of the rule. Five new `check-options` assertions pin the
message content, not just the refusal (388 checks, up from 383).

Verified against the exact two commands reported:

```
$ ./sdrprobe --serve --serve-bind any --serve-token
./sdrprobe: --serve-token needs a value, and only once

$ ./sdrprobe --serve --serve-bind any --serve-token 12345
./sdrprobe: --serve-token must be 8-128 characters (got 5)
```

both followed by the usage dump as before, but no longer *only* the
usage dump.

## Relationship to other tickets

- Independent of ticket 14 (the web view restructuring) -- no file
  either ticket touches overlaps with the other's.
- The SSH-tunnel path this repository already documents remains the
  answer for anything other than a small, trusted home LAN.
