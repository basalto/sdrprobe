#!/usr/bin/env python3
"""Emulate a Viewer, from the command line -- for testing the Viewer link
(ADR-0027, src/viewer_link.c) without a browser.

sdrprobe's own WebSocket handshake and frame codec (src/websocket.c) were
proved correct against a from-scratch client once already, in the ticket
that built them; this is that same client made permanent and reusable
rather than retyped into a heredoc every time the link needs exercising.
It implements RFC 6455 itself -- nothing here imports a websocket library
or shares code with src/websocket.c -- so it stays an independent check on
the wire format, not a client built from the same assumptions as the
server.

Usage:

    ./sdrprobe --headless --serve --file testfiles/gsm_arfcn_69.bin &

    # Print messages as they arrive, forever:
    python3 scripts/viewer_client.py

    # A fixed number of messages, then exit:
    python3 scripts/viewer_client.py --count 20

    # A timed run with a bytes/sec, message-rate and end-to-end age summary
    # -- what ticket 05's "Measured" acceptance criterion asks for:
    python3 scripts/viewer_client.py --stats 5

    # Only some streams:
    python3 scripts/viewer_client.py --subscribe spectrum,receiver_state

    # Link health only -- sent/dropped/high-water and server CPU (ticket 08):
    python3 scripts/viewer_client.py --subscribe link_health --count 5

    # Retune the receiver and watch the command_result (ticket 06) --
    # needs a live receiver; a capture refuses every retune:
    python3 scripts/viewer_client.py --send "tune 948400000" --count 5

    # A deliberately slow client: read nothing for N seconds, then resume
    # and report what came back -- the one behaviour ADR-0027's whole
    # transport design exists to prove (a Viewer stays current, never
    # replays a backlog):
    python3 scripts/viewer_client.py --slow 3 --stats 2

    # Run two of these at once, one plain and one --slow, against the same
    # server, to see the fast one accumulate sent counts with no drops
    # while the slow one's dropped count rises on the SERVER's side (read
    # from the server's own stderr, which this script does not see).

End-to-end age assumes the client and server share a machine and hence a
CLOCK_MONOTONIC epoch (true on Linux, where Python's time.monotonic() is
that same clock) -- it is not a claim about a network path and would need
rework to measure one.
"""
import argparse
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OPCODE_TEXT = 0x1
OPCODE_BINARY = 0x2
OPCODE_CLOSE = 0x8
OPCODE_PING = 0x9
OPCODE_PONG = 0xA

MESSAGE_TYPE_NAMES = {1: "spectrum", 2: "waterfall", 3: "survey_spectrum"}

# survey_spectrum and survey_state (ticket 07) were missing here until
# ticket 14 needed to subscribe to them for a bench-serve comparison and
# found `--subscribe survey_spectrum` refused as an "unknown stream" --
# this script's own copy of the stream list had not been touched since
# before those two existed. `decode_binary()`'s header offsets below
# needed the same catch-up (viewer_link.h's own comment on
# VIEWER_MESSAGE_SURVEY_SPECTRUM is what they are transcribed from).
ALL_STREAMS = ("spectrum", "waterfall", "receiver_state", "link_health",
               "survey_spectrum", "survey_state")


class ViewerClient:
    """A minimal RFC 6455 client: enough to subscribe and read what a
    Viewer link sends, nothing about sdrprobe's own frame layout baked in
    beyond the header this script deliberately knows how to decode."""

    def __init__(self, host, port, timeout=10):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self._buf = b""
        self._handshake(host, port)

    def _handshake(self, host, port):
        key = base64.b64encode(os.urandom(16)).decode()
        request = (
            f"GET /viewer HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            f"Upgrade: websocket\r\n"
            f"Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode()
        self.sock.sendall(request)
        response = self.sock.recv(4096)
        status = response.split(b"\r\n", 1)[0]
        if b"101" not in status:
            raise RuntimeError(f"handshake refused: {status!r}")
        expected = base64.b64encode(
            hashlib.sha1((key + GUID).encode()).digest()
        ).decode()
        if expected.encode() not in response:
            raise RuntimeError("Sec-WebSocket-Accept did not match -- "
                              "server and client disagree about the "
                              "handshake")

    def subscribe(self, streams):
        self._send_text("subscribe " + " ".join(streams))

    def send(self, line):
        """A raw command line (ticket 06) -- `tune <hz>`, whitespace-
        delimited, exactly what a Viewer sends. No parsing here; the
        server's own viewer_command.h owns what a line means."""
        self._send_text(line)

    def _send_text(self, text):
        self._send_frame(OPCODE_TEXT, text.encode())

    def _send_frame(self, opcode, payload):
        mask = os.urandom(4)
        length = len(payload)
        first = 0x80 | opcode
        if length <= 125:
            header = bytes([first, 0x80 | length])
        elif length <= 0xFFFF:
            header = bytes([first, 0x80 | 126]) + struct.pack(">H", length)
        else:
            header = bytes([first, 0x80 | 127]) + struct.pack(">Q", length)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def recv_message(self, timeout=None):
        """One complete WebSocket message: (opcode, payload). Answers pings
        with pongs itself, transparently -- a Viewer link does not need a
        caller of this to know about keepalives. Never called while
        deliberately simulating a slow client -- that is done by simply
        not calling this for a while, which is the whole point."""
        if timeout is not None:
            self.sock.settimeout(timeout)
        while True:
            frame = self._recv_frame()
            if frame is None:
                raise ConnectionError("the Viewer link closed the connection")
            opcode, payload = frame
            if opcode == OPCODE_PING:
                self._send_frame(OPCODE_PONG, payload)
                continue
            if opcode == OPCODE_PONG:
                continue
            if opcode == OPCODE_CLOSE:
                raise ConnectionError("the Viewer link sent a close frame")
            return opcode, payload

    def _recv_frame(self):
        while True:
            parsed = self._try_parse_one_frame()
            if parsed is not None:
                return parsed
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                return None
            self._buf += chunk

    def _try_parse_one_frame(self):
        buf = self._buf
        if len(buf) < 2:
            return None
        b0, b1 = buf[0], buf[1]
        opcode = b0 & 0x0F
        masked = bool(b1 & 0x80)
        length = b1 & 0x7F
        offset = 2
        if length == 126:
            if len(buf) < offset + 2:
                return None
            length = struct.unpack(">H", buf[offset:offset + 2])[0]
            offset += 2
        elif length == 127:
            if len(buf) < offset + 8:
                return None
            length = struct.unpack(">Q", buf[offset:offset + 8])[0]
            offset += 8
        mask_key = None
        if masked:
            if len(buf) < offset + 4:
                return None
            mask_key = buf[offset:offset + 4]
            offset += 4
        if len(buf) < offset + length:
            return None
        payload = buf[offset:offset + length]
        if mask_key:
            payload = bytes(c ^ mask_key[i % 4] for i, c in enumerate(payload))
        self._buf = buf[offset + length:]
        return opcode, payload

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def decode_binary(payload):
    """The wire format viewer_link.h documents: version, type, reserved,
    tuning_generation, timestamp_ms, bins, then the type's own arrays, all
    little-endian. Returns a dict; raises on a payload too short for its
    own declared header, which a version mismatch would produce.

    Type 3 (survey_spectrum, ticket 07) has a wider header than the other
    two -- `lower_hz`/`upper_hz` at offsets 20/24 before the one float32
    array, rather than starting the array at 20 -- because a bin index
    means nothing without the range it was swept over beside it
    (viewer_link.h's own comment on VIEWER_MESSAGE_SURVEY_SPECTRUM)."""
    if len(payload) < 20:
        raise ValueError(f"binary message too short: {len(payload)} bytes")
    version, mtype = payload[0], payload[1]
    generation, timestamp_ms, bins = struct.unpack_from("<IQI", payload, 4)
    lower_hz = upper_hz = None
    if mtype == 3:
        header_len = 28
        array_count = 1
        if len(payload) < header_len:
            raise ValueError(f"binary message too short: {len(payload)} bytes")
        lower_hz, upper_hz = struct.unpack_from("<II", payload, 20)
    else:
        header_len = 20
        array_count = 2 if mtype == 1 else 1
    arrays_bytes = len(payload) - header_len
    expected = bins * 4 * array_count
    if arrays_bytes != expected:
        raise ValueError(
            f"type {mtype} declares {bins} bins ({expected} bytes) but "
            f"carries {arrays_bytes}")
    arrays = []
    offset = header_len
    for _ in range(array_count):
        arrays.append(struct.unpack_from(f"<{bins}f", payload, offset))
        offset += bins * 4
    result = {
        "stream": MESSAGE_TYPE_NAMES.get(mtype, f"type{mtype}"),
        "version": version,
        "tuning_generation": generation,
        "timestamp_ms": timestamp_ms,
        "bins": bins,
        "arrays": arrays,
    }
    if mtype == 3:
        result["lower_hz"] = lower_hz
        result["upper_hz"] = upper_hz
    return result


def run_print(client, count):
    n = 0
    while count is None or n < count:
        opcode, payload = client.recv_message()
        now_ms = time.monotonic() * 1000.0
        if opcode == OPCODE_TEXT:
            state = json.loads(payload)
            if state.get("type") == "link_health":
                print(f"link_health     server_cpu={state['server_cpu_percent']:.1f}% "
                     f"spectrum sent={state['spectrum_sent']} "
                     f"dropped={state['spectrum_dropped']} "
                     f"waterfall sent={state['waterfall_sent']} "
                     f"dropped={state['waterfall_dropped']} "
                     f"receiver_state sent={state['receiver_state_sent']} "
                     f"dropped={state['receiver_state_dropped']} "
                     f"high_water={format_bytes(state['send_queue_high_water'])} "
                     f"age={now_ms - state['timestamp_ms']:.1f} ms")
            elif state.get("type") == "command_result":
                print(f"command_result  command={state['command']!r} "
                     f"ok={state['ok']} error={state['error']}")
            elif state.get("type") == "survey_state":
                print(f"survey_state    status={state['status']!r} "
                     f"candidates={state['candidate_count']} "
                     f"age={now_ms - state['timestamp_ms']:.1f} ms")
            else:
                print(f"receiver_state  center={state['center_hz'] / 1e6:.6f} MHz "
                     f"rate={state['sample_rate_hz'] / 1e6:.3f} MS/s "
                     f"ppm={state['ppm']:+d} generation={state['tuning_generation']} "
                     f"age={now_ms - state['timestamp_ms']:.1f} ms")
        else:
            msg = decode_binary(payload)
            print(f"{msg['stream']:<14}  bins={msg['bins']:<6} "
                 f"bytes={len(payload):<7} generation={msg['tuning_generation']} "
                 f"age={now_ms - msg['timestamp_ms']:.1f} ms")
        n += 1


def format_bytes(n):
    """Decimal SI, matching AGENTS.md's units convention: a byte count
    (storage, a buffer size) as KB at 1000 / MB at 1e6 -- never KiB/MiB."""
    if n >= 1e6:
        return f"{n / 1e6:.2f} MB"
    return f"{n / 1e3:.1f} KB"


def format_bps(bytes_per_second):
    """Decimal SI, matching AGENTS.md's units convention: throughput as
    bits/sec (a network figure), Kbps at 1000, Mbps at 1e6 -- never a
    byte count wearing a bits-per-second label, never binary."""
    bits = bytes_per_second * 8
    if bits >= 1e6:
        return f"{bits / 1e6:.2f} Mbps"
    return f"{bits / 1e3:.1f} Kbps"


def run_stats(client, seconds):
    """What ticket 05's 'Measured' criterion asks for: throughput per
    stream, message rate, and end-to-end age (min/mean/max) -- printed as
    one summary rather than a message per line, since that is what a
    number to write into a ticket looks like."""
    per_stream = {}
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        try:
            opcode, payload = client.recv_message(timeout=remaining)
        except socket.timeout:
            break
        now_ms = time.monotonic() * 1000.0
        if opcode == OPCODE_TEXT:
            state = json.loads(payload)
            stream = state.get("type", "receiver_state")
            ts_ms = state["timestamp_ms"]
        else:
            msg = decode_binary(payload)
            stream = msg["stream"]
            ts_ms = msg["timestamp_ms"]
        s = per_stream.setdefault(stream, {"count": 0, "bytes": 0, "ages": []})
        s["count"] += 1
        s["bytes"] += len(payload)
        s["ages"].append(now_ms - ts_ms)

    print(f"{seconds:.1f} s window:")
    for stream in ALL_STREAMS:
        s = per_stream.get(stream)
        if not s:
            print(f"  {stream:<14} nothing received (not subscribed, or "
                 f"the server never had it ready)")
            continue
        ages = s["ages"]
        print(f"  {stream:<14} {s['count']:>4} messages, "
             f"{format_bps(s['bytes'] / seconds):>12}, "
             f"age min/mean/max = {min(ages):.1f}/"
             f"{sum(ages) / len(ages):.1f}/{max(ages):.1f} ms")


def run_slow(client, sleep_seconds, then_seconds):
    """Simulates a slow Viewer by simply not reading for `sleep_seconds` --
    the server's own send buffer and this link's one-pending-per-stream
    rule do the rest (ADR-0027). Prints what arrives once reading resumes;
    a healthy link shows recent data, not a queue of everything missed."""
    print(f"not reading for {sleep_seconds:.1f} s (simulating a slow "
         f"Viewer)...")
    time.sleep(sleep_seconds)
    print("resuming reads:")
    run_stats(client, then_seconds)


def main():
    parser = argparse.ArgumentParser(
        description="Emulate a Viewer against sdrprobe's Viewer link, "
                   "for testing without a browser.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--subscribe", default=",".join(ALL_STREAMS),
                       help="comma-separated streams: spectrum,waterfall,"
                            "receiver_state,link_health (default: all four)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--count", type=int, metavar="N",
                     help="print N messages, then exit")
    mode.add_argument("--stats", type=float, metavar="SECONDS",
                     help="run for SECONDS, then print a rate/age summary")
    parser.add_argument("--slow", type=float, metavar="SECONDS",
                       help="do not read for SECONDS before starting "
                            "(pairs with --stats to show what a resumed "
                            "slow client receives)")
    parser.add_argument("--send", action="append", metavar="LINE",
                       help="a raw command line to send after subscribing "
                            "(ticket 06), e.g. --send 'tune 948400000'; "
                            "repeatable, sent in order")
    args = parser.parse_args()

    streams = [s.strip() for s in args.subscribe.split(",") if s.strip()]
    unknown = set(streams) - set(ALL_STREAMS)
    if unknown:
        parser.error(f"unknown stream(s): {', '.join(sorted(unknown))}")

    client = ViewerClient(args.host, args.port)
    client.subscribe(streams)
    print(f"subscribed to {', '.join(streams)} at "
         f"{args.host}:{args.port}", file=sys.stderr)
    for line in args.send or []:
        client.send(line)
        print(f"sent: {line}", file=sys.stderr)

    try:
        if args.slow is not None:
            run_slow(client, args.slow, args.stats or 2.0)
        elif args.stats is not None:
            run_stats(client, args.stats)
        else:
            run_print(client, args.count)
    except (ConnectionError, KeyboardInterrupt) as exc:
        print(f"stopped: {exc}", file=sys.stderr)
    finally:
        client.close()


if __name__ == "__main__":
    main()
