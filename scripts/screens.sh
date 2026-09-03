#!/bin/sh
# Render every screen the program has, so a change that draws can be looked at.
#
#   scripts/screens.sh <directory> [width] [height]
#
# Every recipe plays a capture rather than opening the receiver: a screen has
# to be checkable with no hardware, and a capture draws the same picture twice.
# The one thing it cannot show is a live sweep or scan mid-flight; those have
# their own recipes in the screenshot skill.
set -e

out=${1:-build/screens}
width=${2:-1500}
height=${3:-950}
here=$(dirname "$0")
mkdir -p "$out"

if [ -z "$WAYLAND_DISPLAY" ] && [ -z "$DISPLAY" ]; then
    echo "screens: no graphical session; nothing to render" >&2
    exit 0
fi

gsm=testfiles/gsm_arfcn_69.bin
adsb=testfiles/adsb_cpr_pair.bin
lte=testfiles/lte_b20_pci28.bin

# name:arguments. The duration is the settle time as well as the clock, since
# the frame captured is the last one.
set -- \
    "magnitude:--file $gsm --view magnitude --duration 5" \
    "spectrum:--file $gsm --view spectrum --duration 5" \
    "scatter:--file $gsm --view scatter --duration 5" \
    "waterfall:--file $gsm --view waterfall --duration 20" \
    "survey:--file $gsm --frequency 948.4M --view survey --duration 6" \
    "gsm:--file $gsm --view gsm --arfcn 69 --duration 8" \
    "adsb:--file $adsb --view adsb --duration 8" \
    "lte:--file $lte --view lte --earfcn 6200 --duration 8" \
    "calibration-2g:--file $gsm --view calibration --duration 5" \
    "calibration-4g:--file $gsm --view calibration --calibrate lte --duration 5" \
    "settings:--file $gsm --view settings --duration 5" \
    "help:--file $gsm --view help --duration 5"

for entry in "$@"; do
    name=${entry%%:*}
    args=${entry#*:}
    # shellcheck disable=SC2086
    if "$here/screenshot.sh" "$width" "$height" "$out/$name.png" $args \
        >/dev/null 2>&1 && [ -f "$out/$name.png" ]; then
        printf '  %-16s %s\n' "$name" "$out/$name.png"
    else
        printf '  %-16s FAILED\n' "$name"
    fi
done
