# Troubleshooting the Viewer link

Practical commands for exercising and diagnosing `--serve` (ADR-0027): the
headless Viewer link, its four streams, and the page that draws them. This is
a runbook, not a spec -- see `.scratch/web-visualization/` for the tickets and
`docs/adr/0027-viewer-link-carries-derived-state-over-loopback.md` for the
decisions behind the shape below.

`sdrprobe server` is `--headless --serve` and `sdrprobe web` adds a browser;
every command below spells the flags out because each is demonstrating a
particular combination (a port, a capture, a scripted retune), and the two
spellings stay interchangeable.

## 1. Build and run the unit tests first

No server, no browser, no receiver -- if something is broken, this is where
it shows up cheapest.

```sh
make all                      # build ./sdrprobe

make check-websocket           # RFC 6455 codec, SHA-1, base64
make check-scope-view-model    # the Scope's view model
make check-frame-advance       # the per-block advance step
make check-viewer-link         # the link over real loopback sockets
make check-process-cpu         # the server-CPU percentage arithmetic

make check                     # everything, about 90s
make check-pipelines           # the built binary over testfiles/, byte-identity
```

## 2. Start a server

```sh
# Against a capture -- deterministic, repeatable, no hardware:
./sdrprobe --headless --serve --serve-port 8765 --file testfiles/gsm_arfcn_69.bin

# Higher resolution (also raises bytes/sec -- see section 5):
./sdrprobe --headless --serve --serve-port 8765 --fft 16384 --file testfiles/gsm_arfcn_69.bin

# Against a live receiver, if one is attached:
./sdrprobe --headless --serve --serve-port 8765 --frequency 100.3M

# With the link's own debug-log tracing on (see section 6):
./sdrprobe --headless --serve --serve-port 8765 --file testfiles/gsm_arfcn_69.bin --debug-log -
```

Then open `http://127.0.0.1:8765/` in a real browser -- spectrum, waterfall,
and the Health panel are all live there. `Ctrl-C` stops it.

## 3. Drive it from the CLI, no browser needed

`scripts/viewer_client.py` is a from-scratch RFC 6455 client kept specifically
for this -- it shares no code with `src/websocket.c`, so it is an independent
check on the wire format, not a client built from the same assumptions as the
server.

```sh
# Print every message as it arrives:
python3 scripts/viewer_client.py --port 8765

# A fixed number, then exit:
python3 scripts/viewer_client.py --port 8765 --count 20

# bytes/sec, message rate, and end-to-end age per stream:
python3 scripts/viewer_client.py --port 8765 --stats 5

# Just one stream:
python3 scripts/viewer_client.py --port 8765 --subscribe spectrum

# Just link health (sent/dropped/high-water/server CPU -- ticket 08):
python3 scripts/viewer_client.py --port 8765 --subscribe link_health --count 5

# The freshness proof: a deliberately slow client that must stay current
# rather than replay a backlog once it resumes reading:
python3 scripts/viewer_client.py --port 8765 --slow 3 --stats 2

# Two clients at once -- one fast, one slow -- against the same server:
python3 scripts/viewer_client.py --port 8765 --stats 8 &
python3 scripts/viewer_client.py --port 8765 --slow 3 --stats 5
```

Or reach the wire by hand:

```sh
websocat ws://127.0.0.1:8765/viewer
subscribe spectrum waterfall receiver_state link_health
```

## 4. Exercise the tuning-generation rule

Needs a live receiver -- a capture holds one frequency and `retune_receiver()`
refuses to move it (`"Tuning requires a live receiver: a capture holds one
frequency"`).

```sh
./sdrprobe --headless --serve --serve-port 8765 \
    --frequency 100300000 --serve-retune-after 3:98000000

python3 scripts/viewer_client.py --port 8765 --subscribe receiver_state --count 200
# watch center_hz and tuning_generation flip together at the 3-second mark
```

## 5. Reading `link_health` and the stderr disconnect report

`link_health` (ticket 08) is the one stream that answers "is this actually
healthy" without guessing:

```
link_health     server_cpu=59.6% spectrum sent=343 dropped=0 waterfall sent=342
                dropped=0 receiver_state sent=342 dropped=0 high_water=16.4 KB
                age=1.1 ms
```

Units throughout are decimal SI (AGENTS.md's convention): a byte count --
`high_water`, a buffer size -- is KB/MB at 1000/1e6; a throughput --
`viewer_client.py --stats`'s per-stream line, the page's Health panel's
"received" figure -- is Kbps/Mbps, bits rather than bytes, same decimal
scale. Never KiB/MiB, never a byte count silently divided by 1024 behind
a "KB" label.

- **A rising `dropped` count with a low, stable `age`** is the transport
  working as designed: a slow client is falling behind and the freshness
  rule is protecting it from a growing backlog, not failing.
- **A rising `dropped` count with a rising `age`** would mean the
  replace-and-drop rule itself is broken -- this should not happen; if it
  does, it is the bug ticket 05 found and fixed (see section 7).
- **`high_water` at or near 262.1 KB** (`VIEWER_LINK_CLIENT_SNDBUF`,
  `256 * 1024` bytes -- a genuinely binary buffer size, so the source
  comment says `256 KiB`; what is reported here is always the decimal
  figure) means the kernel send buffer is genuinely full, which is what
  triggers drops in the first place.
- **`server_cpu` near or above 100%** on a single-threaded serving loop plus
  the acquisition worker is a real signal the block rate cannot keep up;
  compare against `--fft` (a smaller transform costs less per block).

When any client disconnects, the server itself prints a summary to stderr,
independent of the Viewer ever seeing it:

```
viewer link: client fd 6 disconnecting, send queue high-water 16.4 KB
  spectrum       sent 342 dropped 12
  waterfall      sent 340 dropped 14
  receiver_state sent 354 dropped 0
  link_health    sent 354 dropped 0
```

## 6. `--debug-log` for the link itself

Off by default, free when off (one pointer check). Turned on, it logs a
client connecting, a subscription changing, and a stream stalling or
clearing -- not a per-message trace, which would flood the log for nothing;
see `src/debug_log.h`'s own comment on why.

```sh
./sdrprobe --headless --serve --serve-port 8765 --file testfiles/gsm_arfcn_69.bin \
    --debug-log /tmp/viewer.log
tail -f /tmp/viewer.log
```

A stalled stream looks like this -- the byte offset it stalled at, and how
many bytes were still pending when it cleared:

```
1.098  viewer    client fd 6 connected
1.158  viewer    client fd 6 subscribed: spectrum waterfall receiver_state
1.214  viewer    client fd 6 stream waterfall stalled at 65084/65566 bytes
4.151  viewer    client fd 6 stream waterfall stall cleared (482 bytes were still pending)
```

## 7. If messages look corrupted on the wire

This happened once (ticket 05): under sustained backpressure with two or
more subscribed streams sharing one socket, a fresh frame from one stream
could be spliced into the middle of another stream's still-partially-sent
frame. Fixed by `flush_client()`'s `inflight_stream` tracking -- at most one
frame per client is ever mid-wire at a time -- and pinned by
`test_no_cross_stream_interleaving_under_backpressure` in
`tests/viewer_link_test.c`.

If something like it recurs, `scripts/viewer_client.py` trusts its own frame
decoder the same way a real Viewer does, so it will not show the corruption
directly -- it needs a client that assumes nothing about framing. That is
what caught it originally: connect, send `subscribe ...`, then read raw bytes
off the socket and walk RFC 6455 headers by hand rather than trusting a
length field once read. Flag the first byte offset where the header is not
one of the legal opcodes (`0x0, 0x1, 0x2, 0x8, 0x9, 0xA`), not RSV, not
masked (a server frame is never masked) -- that offset is where to start
correlating against `--debug-log`'s own trace from section 6.

## 8. Cleaning up

```sh
fuser -k 8765/tcp    # kill whatever is listening on the port
```

Avoid `pkill -f <pattern>` here if the pattern also appears in the `pkill`
command's own argument list (e.g. the port number) -- it will match and kill
its own invoking shell.
