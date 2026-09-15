# 03 - A WebSocket server with nothing wired to it

Status: ready-for-agent

## Goal

HTTP/1.1 and RFC 6455 in this repository's own idiom: hand-written, minimal,
and checkable with no window, no receiver and no network.

## What it is

- HTTP/1.1 request parsing, enough to serve a static page and to recognise an
  `Upgrade: websocket` request. Nothing more -- this is the sibling of
  `src/capture_sidecar.h`, whose comment reads *"This is not a JSON parser and
  must not become one."*
- `Sec-WebSocket-Accept`: base64(SHA-1(key + RFC GUID)). Both primitives
  written here, both with published test vectors.
- Frame encode and decode: text and binary opcodes, the client-to-server XOR
  mask, continuation frames, ping/pong, close.
- A listening socket bound to **127.0.0.1 only**, per ADR-0027. Binding
  elsewhere is not a configuration option in this ticket.

## Why hand-written

The marginal cost over the HTTP parser needed anyway for the upgrade and for
serving the page is SHA-1, base64 and frame masking. All three are pure byte
manipulation against published vectors, which is exactly what this
repository's check culture is for, and it keeps `vendor/` at the one entry it
has had.

## Acceptance criteria

- [ ] `check-websocket` links `-lm` alone -- no raylib, no librtlsdr -- and is
      in `CHECK_UNITS`, per the NOT GATED audit in `CLAUDE.md`.
- [ ] SHA-1 checked against the published vectors including the empty string
      and a message crossing the 55-byte padding boundary; base64 checked
      against RFC 4648's vectors including both padding cases.
- [ ] The handshake reproduces RFC 6455 section 1.3's worked example exactly:
      key `dGhlIHNhbXBsZSBub25jZQ==` gives accept
      `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`.
- [ ] Frame decode is checked against a **masked** client frame; a decoder
      that ignores the mask bit passes an unmasked round trip and fails on air,
      which is the round-trip trap `CLAUDE.md` warns about.
- [ ] A fragmented message, a ping mid-stream and a close handshake each have
      a case.
- [ ] A real browser connects to a throwaway echo endpoint and round-trips a
      binary payload. The suite cannot prove this part.
- [ ] `src/websocket.h` and its `.c` appear in the Makefile's header list --
      the MISSING audit in `CLAUDE.md`.

## Not in scope

- Any sdrprobe state, any subscription, any command.
- TLS, authentication, permessage-deflate, any bind address but loopback.
- Serving anything but one page and one echo, for the browser check above.
