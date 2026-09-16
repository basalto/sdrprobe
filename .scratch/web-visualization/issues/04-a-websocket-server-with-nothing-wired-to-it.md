# 04 - A WebSocket server with nothing wired to it

Status: resolved, 2026-09-16

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

## Comments

### 2026-09-16 -- done

`src/websocket.h`/`.c` (new): SHA-1 (FIPS 180-4), base64 encode (RFC 4648),
the handshake's accept value, minimal HTTP/1.1 request parsing (method,
path, headers, upgrade detection), RFC 6455 frame decode/encode, ping/pong,
close, and fragmented-message reassembly. Nothing in it knows `struct app`
exists.

**Every expected test value was computed independently on this machine**
rather than transcribed from memory -- `sha1sum`, the system `base64`
tool, and a five-line Python script for the RFC 6455 worked example -- and
one of them caught a real error before it shipped: the "quick brown fox"
sentence is 43 bytes, not the 44 I first hand-counted, so the check failed
on its first run with a wrong digest that had nothing wrong with
`websocket_sha1()` itself. Fixed by having the check measure its own C
string literals with `strlen()` rather than a second hand count, so the
same slip can't recur silently.

`check-websocket`: 72 checks, `-lm` alone (`ldd` confirms no `libraylib`,
no `librtlsdr`). Covers: SHA-1 across the empty string, "abc", the FIPS
multi-block vector, and the 55-vs-56-byte padding boundary explicitly;
base64 across every RFC 4648 padding case; the RFC 6455 section 1.3 worked
example exactly; HTTP parsing (a valid upgrade, a plain GET, an incomplete
buffer, a malformed request line); frame decode against a masked client
frame, the extended 16-bit length form, an RSV bit, and -- the round-trip
trap named in the ticket -- a frame built by the server-side *encoder*
(therefore unmasked) handed to the decoder, which must refuse it rather
than silently accepting its own output; a fragmented text message and both
of the assembler's protocol-violation cases; a ping mid-stream answered by
an exact pong; and a close handshake carrying a status code.

**The browser proof, and what it actually took.** `scripts/websocket_echo_server.c`
(new, `make websocket-echo-server PORT_WEBSOCKET=<port>`) is a throwaway,
one-connection-at-a-time server over `websocket.c` alone -- not part of
`sdrprobe`, not in `APP_SRC`. It serves one page at `/` (a self-contained
test that opens `ws://<host>/echo`, sends an 8-byte binary payload, and
reports PASS/FAIL by comparing the echo byte-for-byte) and echoes at
`/echo`.

Two independent verifications, because the check above cannot reach a
network stack or a real browser:

- **An independent Python client**, sharing no code with `websocket.c`
  beyond the RFC itself, computed its own accept value and round-tripped a
  binary frame, a fragmented text message, a ping, and a close against the
  actual compiled server -- all matched.
- **A real headless Chromium**, pointed at the served page and left to run
  on its own wall clock, opened the WebSocket, sent the payload, received
  it back through this module's frame codec, and rendered **PASS**.

The second one took several attempts to *capture*, and every failure was
in the harness, not the server: `chromium --headless --screenshot` and
`--virtual-time-budget` both take the shot before a real (non-virtual)
network round trip finishes, and driving the DevTools protocol through
`Page.navigate` had its own timing quirk. The tell was the server's own
request log: `GET /echo -> upgrade` kept appearing *before* the screenshot
attempts showed anything but "connecting...", proving the handshake itself
was never the problem. Pointing headless Chromium directly at the URL on
its command line and reading the DOM after a plain real-time `sleep`
resolved it. Recorded so nobody re-diagnoses this as a protocol bug.

Acceptance criteria against the ticket:

- `check-websocket` links `-lm` alone, no raylib, no librtlsdr; in
  `CHECK_UNITS` (72 suites total now, one up); `src/websocket.h`/`.c` in
  the Makefile's header list (the MISSING audit is clean).
- SHA-1 and base64 checked against independently-computed vectors,
  including both base64 padding cases and the SHA-1 block boundary.
- The RFC 6455 handshake example reproduces exactly:
  `s3pPLMBiTxaQ9kYGzzhZRbK+xOo=`.
- Frame decode is checked against a masked client frame and separately
  refuses an unmasked (encoder-built) one.
- A fragmented message, a ping mid-stream and a close handshake each have
  a case.
- A real browser connected to the throwaway echo endpoint and
  round-tripped a binary payload -- verified twice, independently, as
  above.
- No raylib or librtlsdr symbol in the check binary or the echo server
  (`ldd` confirms both).

`make check`: 21328 checks, 72 suites, no failures. `tests/pipelines.sh`
byte-identical apart from the ADS-B capture's wall-clock filename --
nothing here touches `sdrprobe` itself, which this run confirms.
