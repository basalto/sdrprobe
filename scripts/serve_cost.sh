#!/bin/sh
# What `--serve` costs, per subscription, measured from /proc.
#
# Written three times into a scratch directory before it became this: once to
# find that a `receiver_state` subscriber pinned a core (98.6% against 9.8%
# with no client -- .scratch/web-visualization/issues/10-*), once to show the
# pacing fixed it, and once to sweep the transform size and find that the
# transform is 80% of the Scope path. Every one of those numbers decided
# something, and the first two runs of it left their evidence in a transcript.
#
# It measures the *server* process only. The client is a load generator and
# its own cost is not the question; `scripts/viewer_client.py` is that client,
# so nothing here reimplements the wire format.
#
# Usage:
#   make bench-serve
#   make bench-serve SUBS_SERVE=receiver_state FFT_SERVE=16384
#   make bench-serve FREQ_SERVE=434M WINDOW_SERVE=30
#
# A live receiver is needed: this is a question about what the program does
# with blocks arriving in real time, and a capture that plays back as fast as
# it is consumed answers a different one.
set -u

PORT="${PORT_SERVE:-8765}"
FFT="${FFT_SERVE:-2048}"
SUBS="${SUBS_SERVE:-}"
FREQ="${FREQ_SERVE:-100M}"
WINDOW="${WINDOW_SERVE:-20}"
BIN="${BIN_SERVE:-./sdrprobe}"

HZ=$(getconf CLK_TCK)
TMP="${TMPDIR:-/tmp}/serve_cost.$$"
mkdir -p "$TMP" || exit 1
trap 'rm -rf "$TMP"' EXIT

"$BIN" --headless --serve --serve-port "$PORT" --frequency "$FREQ" \
    --fft "$FFT" >"$TMP/server.out" 2>&1 &
PID=$!
# Long enough for the device to open and the first blocks to flow; a
# measurement started during the open would charge it to the loop.
sleep 3
if ! kill -0 "$PID" 2>/dev/null; then
    echo "serve-cost: the server did not start:"
    sed 's/^/  /' "$TMP/server.out"
    exit 1
fi

CPID=""
if [ -n "$SUBS" ]; then
    python3 scripts/viewer_client.py --port "$PORT" --subscribe "$SUBS" \
        >"$TMP/client.out" 2>&1 &
    CPID=$!
    sleep 2
    if ! kill -0 "$CPID" 2>/dev/null; then
        echo "serve-cost: the client did not stay up:"
        sed 's/^/  /' "$TMP/client.out"
        kill -INT "$PID" 2>/dev/null
        exit 1
    fi
fi

# utime + stime out of /proc, which counts this process only -- `time` on the
# pipeline would charge the client's Python to the answer.
cpu_ticks() { awk '{print $14 + $15}' "/proc/$PID/stat" 2>/dev/null; }

T0=$(cpu_ticks)
sleep "$WINDOW"
T1=$(cpu_ticks)

[ -n "$CPID" ] && kill -INT "$CPID" 2>/dev/null
kill -INT "$PID" 2>/dev/null
wait "$PID" 2>/dev/null

if [ -z "${T0:-}" ] || [ -z "${T1:-}" ]; then
    echo "serve-cost: the server exited during the window:"
    sed 's/^/  /' "$TMP/server.out"
    exit 1
fi

printf '%-28s fft %-6s %5.1f%% of a core over %ss\n' \
    "${SUBS:-no client}" "$FFT" \
    "$(echo "scale=3; ($T1 - $T0) * 100 / $HZ / $WINDOW" | bc)" "$WINDOW"
