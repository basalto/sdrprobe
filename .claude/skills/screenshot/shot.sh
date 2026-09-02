#!/bin/sh
# Screenshot sdrprobe at a chosen window size.
#
#   shot.sh <width> <height> <output.png> [sdrprobe arguments...]
#
# A tiling compositor decides a window's size, so the plain --screenshot run
# gets whatever the layout had spare. This floats the window first and asks for
# a size, which is what makes a capture legible and repeatable.
#
# Needs Hyprland. Without hyprctl it still runs and still writes the PNG, at
# whatever size the layout gives -- the resize is the only part that is lost.
set -e

[ $# -lt 4 ] && { echo "usage: shot.sh <width> <height> <out.png> [args...]" >&2; exit 2; }
width=$1; height=$2; out=$3; shift 3

# Resolve the output before moving, so a relative path means what the caller
# meant by it rather than something under the repository.
case "$out" in
    /*) ;;
    *) out="$(pwd)/$out" ;;
esac

# Repository root, so the recipes' relative capture paths resolve.
cd "$(dirname "$0")/../../.." || exit 1

./sdrprobe --screenshot "$out" "$@" &
app=$!

if command -v hyprctl >/dev/null 2>&1; then
    # Wait for the window to be mapped. Its class is "sdrprobe signal
    # visualizer", and a selector that matches nothing is dispatched happily
    # and does nothing, so the address is what we target.
    address=
    tries=0
    while [ $tries -lt 60 ]; do
        address=$(hyprctl clients -j | python3 -c '
import json, sys
for client in json.load(sys.stdin):
    if client.get("class", "").startswith("sdrprobe"):
        print(client["address"])
        break
' 2>/dev/null)
        [ -n "$address" ] && break
        tries=$((tries + 1))
        sleep 0.1
    done

    if [ -n "$address" ]; then
        # Tiled windows cannot be resized freely, so float it first.
        hyprctl dispatch \
            "hl.dsp.window.float({ window = \"address:$address\" })" >/dev/null
        hyprctl dispatch "hl.dsp.window.resize({ x = $width, y = $height, \
            relative = false, window = \"address:$address\" })" >/dev/null
    else
        echo "shot.sh: no sdrprobe window appeared; capturing unsized" >&2
    fi
fi

wait $app
