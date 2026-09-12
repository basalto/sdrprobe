#!/bin/sh
# Which of these nominal frequencies carry a tone clocked by this receiver?
#
# Written six times in one session before it became a file -- as
# `pass.sh`, `e4k.sh`, `comb.sh`, `comb_r820t.sh`, `fam.sh` and `fam_r.sh`,
# each a copy of the same loop with a different list -- which is what
# `AGENTS.md` says a scratch harness is the raw material for. Every number in
# `docs/two-receivers-compared.md` and in the 2026-09-12 sections of
# `.scratch/device-model/issues/10-*` came out of one of those copies.
#
# The measurement it automates, and the two conditions that make it mean
# anything:
#
#   **Uncorrected.** With no correction applied, a tone clocked by this
#   receiver reads its *exact nominal* whatever the crystal error, while an
#   external one reads low by f*k (`issues/11-*`). So the offset from nominal
#   is the discriminator, and +488 Hz at a 976.6 Hz bin is half a bin -- on
#   the nominal. That is why PPM defaults to 0 and why changing it changes the
#   question rather than the precision.
#
#   **At max gain, and say what the antenna was doing.** Default gain sees the
#   loud half of anything. The odd multiples of 14.4 MHz were recorded as
#   absent on one board and as "not the inside kind" on the other, and both
#   readings were an artifact of stopping at default gain with an antenna
#   connected -- with the antenna off at max gain all seven are present and
#   internal. A null about an internal line is worth nothing with an antenna
#   on it: a stronger external signal a few kilohertz away wins the
#   nearest-candidate comparison and the pass reports it, displaced, as
#   external. That failure looks exactly like a measurement.
#
# Usage:
#   make probe-artifacts NOMINALS="129.6M 144M 158.4M"
#   make probe-artifacts NOMINALS="75M 150M 300M" GAIN_ARTIFACTS=29.7
#   make probe-artifacts NOMINALS=@comb        # a named list, see below
#
# The antenna is not something this can check. Say which pass it was in the
# label and pair the two; a single pass establishes presence and nothing else.
set -e
cd "$(dirname "$0")/.."

to_hz() {
    case "$1" in
        *[Mm]) echo "$1" | sed 's/[Mm]$//' | awk '{printf "%.0f", $1 * 1000000}' ;;
        *[Kk]) echo "$1" | sed 's/[Kk]$//' | awk '{printf "%.0f", $1 * 1000}' ;;
        *)     echo "$1" ;;
    esac
}

NOMINALS=${NOMINALS:?set NOMINALS, e.g. NOMINALS="129.6M 144M" or @comb}
GAIN=${GAIN_ARTIFACTS:-max}
PPM=${PPM_ARTIFACTS:-0}
SPAN=$(to_hz "${SPAN_ARTIFACTS:-2M}")
DWELL=${DWELL_ARTIFACTS:-1.0}
LABEL=${LABEL_ARTIFACTS:-}

# Named lists, so a campaign is a word rather than a line of frequencies.
case "$NOMINALS" in
  @comb)   NOMINALS="28.8M 43.2M 57.6M 72M 86.4M 100.8M 115.2M 129.6M 144M 158.4M 172.8M 187.2M 201.6M 216M" ;;
  @ladder) NOMINALS="30M 60M 120M 240M 480M 960M" ;;
  @family) NOMINALS="75M 135M 150M 300M 540M" ;;
esac

# `--ppm 0` is recorded against this receiver and site permanently
# (`issues/12-*`), and an uncorrected sweep is this tool's default, so it
# would erase a measured calibration every run. Until that is fixed, keep the
# writes in a scratch copy of the config.
GUARD=$(mktemp -d)
mkdir -p "$GUARD/.config/sdrprobe"
[ -f "$HOME/.config/sdrprobe/config" ] &&
    cp "$HOME/.config/sdrprobe/config" "$GUARD/.config/sdrprobe/config"
trap 'rm -rf "$GUARD"' EXIT
export HOME="$GUARD"

printf 'artifact sweep  gain %s  ppm %s (%s)  span %s Hz  dwell %s%s\n' \
    "$GAIN" "$PPM" \
    "$([ "$PPM" = 0 ] && echo 'uncorrected: a coherent tone reads its exact nominal' || echo 'corrected')" \
    "$SPAN" "$DWELL" "${LABEL:+  [$LABEL]}"
printf '%-14s %12s %10s %9s   %s\n' nominal offset level prominence verdict

for n in $NOMINALS; do
    f=$(to_hz "$n")
    lo=$((f - SPAN / 2)); hi=$((f + SPAN / 2))
    out=$(./sdrprobe --headless --ppm "$PPM" --gain "$GAIN" --survey \
              --survey-range "${lo}:${hi}" --survey-dwell "$DWELL" 2>/dev/null \
          | grep '^candidate ' || true)
    echo "$out" | awk -v f="$f" -v span="$SPAN" '
        BEGIN { best = -1 }
        $1 == "candidate" {
            d = $2 - f; if (d < 0) d = -d
            if (best < 0 || d < best) { best = d; hz = $2; lvl = $3; pr = $4 }
        }
        END {
            # One survey bin over this span, which is what "exact" is measured
            # against: a tone at a bin centre reads half a bin high.
            bins = 2048; if (span > 2000000) bins = 8192
            binhz = span / bins
            if (best < 0 || best > 60000) {
                printf "%-14.6f %12s %10s %9s   %s\n", f/1e6, "-", "-", "-", "absent"
            } else {
                v = (best <= binhz) ? "on the nominal" : "displaced: not this clock"
                printf "%-14.6f %+12.0f %10s %9s   %s\n", f/1e6, hz - f, lvl, pr, v
            }
        }'
done
