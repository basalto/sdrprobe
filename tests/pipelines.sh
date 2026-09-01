#!/bin/sh
# Whole paths through the assembled program, driven from the command line and
# asserted on stdout. No hardware, no window, no person: the captures in
# testfiles/ stand in for the receiver (ADR-0012, layer 2).
#
# Unit checks prove the pieces; this proves they are wired together. A change
# that leaves every unit check passing while breaking the decode path, the
# recording path, or the flags that reach them fails here instead of on
# someone's desk.
#
#     make check-pipelines
#
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit 2
probe=./sdrprobe
failures=0

fail() {
    printf 'pipeline: %s\n' "$1" >&2
    failures=$((failures + 1))
}

# Run the probe with a time limit and hand back its output. A pipeline that
# hangs is a failure, not a wait.
run() {
    timeout -k 5 90 $probe "$@" 2>&1
}

if [ ! -x "$probe" ]; then
    echo "pipeline: build $probe first (make)" >&2
    exit 2
fi

# --- GSM: a capture must still decode its own BSIC ------------------------
#
# The two GSM captures are the only checks a wrong SCH field layout cannot
# satisfy: the synthetic round trip in check-gsm-dsp passes against any layout
# the encoder shares, but these were recorded off the air and their BSIC is a
# fact about the world.
check_gsm() {
    capture=$1
    arfcn=$2
    expected=$3
    output=$(run --file "$capture" --headless --arfcn "$arfcn" --decode --once)
    decodes=$(printf '%s\n' "$output" | grep -c "^SCH ")
    wrong=$(printf '%s\n' "$output" | grep "^SCH " | grep -cv "BSIC $expected ")

    if [ "$decodes" -lt 5 ]; then
        fail "$capture decoded $decodes SCH bursts, expected at least 5"
        return
    fi
    if [ "$wrong" -ne 0 ]; then
        fail "$capture reported $wrong bursts with a BSIC other than $expected"
        return
    fi
    # The frame number has to advance: a decoder returning a constant would
    # otherwise satisfy everything above.
    distinct=$(printf '%s\n' "$output" | grep "^SCH " |
               sed -n 's/.*frame \([0-9]*\).*/\1/p' | sort -u | wc -l)
    if [ "$distinct" -lt 2 ]; then
        fail "$capture reported one frame number across $decodes decodes"
        return
    fi
    printf '  %-34s %2d bursts, BSIC %s, %d frame numbers\n' \
        "$(basename "$capture")" "$decodes" "$expected" "$distinct"
}

echo "GSM decode"
check_gsm testfiles/gsm_arfcn_73.bin 73 56
check_gsm testfiles/gsm_arfcn_69.bin 69 59

# --- ADS-B: frames, and a position that needed two of them ----------------
echo "Mode S decode"
decode_adsb() {
    run --file testfiles/adsb_cpr_pair.bin --headless --technology adsb \
        --decode --once
}
adsb=$(decode_adsb)
frames=$(printf '%s\n' "$adsb" | grep -cE "^[0-9][0-9]:[0-9][0-9]:[0-9][0-9] ")
positions=$(printf '%s\n' "$adsb" | grep -c "lat ")
if [ "$frames" -lt 5 ]; then
    fail "adsb_cpr_pair decoded $frames frames, expected at least 5"
elif [ "$positions" -lt 1 ]; then
    fail "adsb_cpr_pair resolved no CPR position; the even/odd pairing cache is
          the reason this capture is kept"
else
    printf '  %-34s %2d frames, %d resolved position(s)\n' \
        "adsb_cpr_pair.bin" "$frames" "$positions"
fi

# The same capture must decode the same messages every time. It did not until
# headless playback was made lossless: the block slot overwrites by design
# (ADR-0002), the idle poll is longer than the 65.5 ms a block covers, and the
# count fell from 6 to 1 on a busy machine. Every assertion above is worthless
# if this one fails -- they would pass or fail with the machine's load.
again=$(decode_adsb | grep -cE "^[0-9][0-9]:[0-9][0-9]:[0-9][0-9] ")
if [ "$again" -ne "$frames" ]; then
    fail "the same capture decoded $frames frames and then $again: headless
          playback is dropping blocks, so no count here means anything"
else
    printf '  %-34s %2d frames again\n' "run twice" "$again"
fi

# --- Recording: the file and the sidecar that explains it -----------------
#
# Recording tees off inside the acquisition thread, so a capture played back
# through it exercises the same path a live one takes.
echo "Recording"
before=$(ls captures/ 2>/dev/null | wc -l)
run --file testfiles/adsb_cpr_pair.bin --headless --record-seconds 1 \
    --technology adsb >/dev/null
recorded=$(ls -t captures/*.bin 2>/dev/null | head -1)
sidecar=${recorded%.bin}.json
if [ -z "$recorded" ] || [ "$(ls captures/ | wc -l)" -le "$before" ]; then
    fail "recording produced no capture"
else
    size=$(stat -c %s "$recorded")
    if [ ! -f "$sidecar" ]; then
        fail "recording produced no sidecar beside $recorded"
    elif ! grep -q '"provenance": "recorded by sdrprobe"' "$sidecar"; then
        fail "the sidecar does not claim its own provenance"
    elif ! grep -q '"technology": "adsb"' "$sidecar"; then
        fail "the sidecar did not record the technology it was told"
    elif ! grep -q '"short_blocks": 0' "$sidecar"; then
        fail "the recording reported gaps in a paced playback"
    elif [ "$size" -lt 3000000 ]; then
        fail "one second at 2 MS/s produced only $size bytes"
    else
        printf '  %-34s %s bytes, sidecar complete\n' "$(basename "$recorded")" \
            "$size"
    fi
    rm -f "$recorded" "$sidecar"
fi

# --- The flags that reach those paths -------------------------------------
#
# check-options proves the parser; this proves the program acts on it.
echo "Flags"
if ! run --file testfiles/adsb_cpr_pair.bin --headless --technology adsb \
        --decode --once | grep -q "End of capture."; then
    fail "--once did not stop at the end of the capture"
else
    printf '  %-34s stops at the end\n' "--once"
fi

quiet=$(run --file testfiles/gsm_arfcn_73.bin --headless --arfcn 73 --decode \
            --once --gsm-features none | grep -c "^SCH ")
loud=$(run --file testfiles/gsm_arfcn_73.bin --headless --arfcn 73 --decode \
           --once --gsm-features filter,finecfo,trellis | grep -c "^SCH ")
if [ "$loud" -le "$quiet" ]; then
    fail "the SCH refinements decoded $loud bursts against $quiet without them"
else
    printf '  %-34s %d bursts against %d without them\n' "--gsm-features" \
        "$loud" "$quiet"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures pipeline check(s) failed" >&2
    exit 1
fi
echo "pipeline checks passed"
