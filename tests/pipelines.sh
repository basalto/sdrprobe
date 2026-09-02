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
checks=0

# Every assertion counts itself, so the suite can say how much it proved -- the
# same contract tests/check.h gives the unit suites.
fail() {
    printf '    FAIL  %s\n' "$1" >&2
    failures=$((failures + 1))
}

checked() {
    checks=$((checks + 1))
}

# One line per pipeline: what ran, and what it found.
report() {
    printf '    %-30s %s\n' "$1" "$2"
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
    checked
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
    report "$(basename "$capture")" \
        "$decodes bursts, BSIC $expected, $distinct frame numbers"
}

printf '  GSM decode\n'
check_gsm testfiles/gsm_arfcn_73.bin 73 56
check_gsm testfiles/gsm_arfcn_69.bin 69 59

# --- LTE: the cell search, off a live band 20 capture ---------------------
# The whole path: --earfcn picks the carrier and, with it, LTE's own 1.92 MS/s
# grid; the search reads the identity off the two synchronisation signals. The
# identity is the assertion, because a conjugated primary sequence reports a
# neighbouring one and nothing synthetic notices.
printf '  LTE cell search\n'
checked
lte=$(run --file testfiles/lte_b20_pci32.bin --headless --earfcn 6200 \
          --decode --once)
lte_cells=$(printf '%s\n' "$lte" | grep -c "^LTE  cell ")
if [ "$lte_cells" -lt 1 ]; then
    fail "lte_b20_pci32 found no cell"
elif ! printf '%s\n' "$lte" | grep -q "^LTE  cell 32 (N_ID_1 10, N_ID_2 2)"; then
    fail "lte_b20_pci32 did not read cell 32; got: $(printf '%s\n' "$lte" |
         grep '^LTE  cell ' | head -1)"
elif ! printf '%s\n' "$lte" | grep -q "normal CP"; then
    fail "lte_b20_pci32 did not read the normal cyclic prefix"
else
    report "lte_b20_pci32.bin" "cell 32, N_ID_2 2, normal prefix"
fi

# An LTE run is refused on any sample rate but LTE's own: the plugin's
# arithmetic is that grid, and resampling silently would be worse than saying
# no (ADR-0014).
checked
if run --headless --technology lte --sample-rate 2M --decode --once \
       --file testfiles/lte_b20_pci32.bin | grep -q "^Usage:"; then
    report "--technology lte" "refuses a sample rate that is not 1.92 MS/s"
else
    fail "--technology lte accepted a sample rate other than 1.92 MS/s"
fi

# --- ADS-B: frames, and a position that needed two of them ----------------
printf '  Mode S decode\n'
decode_adsb() {
    run --file testfiles/adsb_cpr_pair.bin --headless --technology adsb \
        --decode --once
}
checked
adsb=$(decode_adsb)
frames=$(printf '%s\n' "$adsb" | grep -cE "^[0-9][0-9]:[0-9][0-9]:[0-9][0-9] ")
positions=$(printf '%s\n' "$adsb" | grep -c "lat ")
if [ "$frames" -lt 5 ]; then
    fail "adsb_cpr_pair decoded $frames frames, expected at least 5"
elif [ "$positions" -lt 1 ]; then
    fail "adsb_cpr_pair resolved no CPR position; the even/odd pairing cache is
          the reason this capture is kept"
else
    report "adsb_cpr_pair.bin" "$frames frames, $positions resolved position(s)"
fi

# The same capture must decode the same messages every time. It did not until
# headless playback was made lossless: the block slot overwrites by design
# (ADR-0002), the idle poll is longer than the 65.5 ms a block covers, and the
# count fell from 6 to 1 on a busy machine. Every assertion above is worthless
# if this one fails -- they would pass or fail with the machine's load.
checked
again=$(decode_adsb | grep -cE "^[0-9][0-9]:[0-9][0-9]:[0-9][0-9] ")
if [ "$again" -ne "$frames" ]; then
    fail "the same capture decoded $frames frames and then $again: headless
          playback is dropping blocks, so no count here means anything"
else
    report "run twice" "$again frames again"
fi

# --- Past the SCH: what the cell is saying --------------------------------
#
# The SCH gives a cell's identity code and its clock. One layer further, the
# BCCH says which network it belongs to. Nothing here needs ground truth: the
# Fire code is 40 parity bits over 184, so a block that passes is right or is a
# one-in-a-million-million accident -- and MCC 268 is Portugal, where the
# capture was recorded.
printf '  Broadcast\n'
broadcast() {
    run --file testfiles/gsm_arfcn_69.bin --headless --arfcn 69 --decode --once
}
checked
bcch=$(broadcast | grep "^BCCH ")
blocks=$(printf '%s\n' "$bcch" | grep -c "^BCCH ")
if [ "$blocks" -lt 3 ]; then
    fail "ARFCN 69 gave $blocks broadcast blocks, expected several"
elif ! printf '%s\n' "$bcch" | grep -q "MCC 268 MNC 03"; then
    fail "the network came back as something other than MCC 268 MNC 03; the
          capture was recorded in Portugal and 40 parity bits stand behind it"
elif ! printf '%s\n' "$bcch" | grep -q "System Information 3 .*LAC 4010  CI 5131"; then
    fail "System Information 3 did not report LAC 4010 and Cell Identity 5131"
else
    report "gsm_arfcn_69.bin" "$blocks blocks, MCC 268 MNC 03, CI 5131"
fi

# The cell's own neighbour list names ARFCN 73 -- which is the other capture in
# testfiles/, recorded from that neighbour. Two independent recordings agreeing
# about the shape of the network is worth more than either alone.
checked
if ! printf '%s\n' "$bcch" | grep "System Information 2 " | grep -qw 73; then
    fail "System Information 2 did not list ARFCN 73 among the neighbours"
else
    report "neighbour list" "names ARFCN 73, the other capture"
fi

# The same capture, the same messages. A decode that varies is a decode that
# cannot be diffed against yesterday.
checked
if [ "$(broadcast | grep '^BCCH ')" != "$bcch" ]; then
    fail "the same capture broadcast differently the second time"
else
    report "run twice" "identical messages"
fi

# A second cell, recorded a year after the first two and from a different
# operator. Its BCC is 6 where ARFCN 69's is 3, so the bursts are found by a
# different training sequence -- which is what says the demodulator generalises
# rather than fitting the one cell it was written against.
checked
other=$(run --file testfiles/gsm_arfcn_113.bin --headless --arfcn 113 --decode \
            --once | grep "^BCCH ")
if ! printf '%s\n' "$other" | grep -q "MCC 268 MNC 06 .*LAC 8420  CI 16134"; then
    fail "ARFCN 113 did not report MCC 268 MNC 06, LAC 8420, Cell 16134"
else
    report "gsm_arfcn_113.bin" \
        "$(printf '%s\n' "$other" | grep -c "^BCCH ") blocks, MNC 06, CI 16134"
fi

# --- The survey, read by a program rather than clicked at -----------------
#
# The survey was the one view with no way in from a script: an agent could
# check its arithmetic but not the thing that matters -- given this signal,
# does it report the right candidates? A capture holds one tuning, so the
# survey of it is one step, and the answer is the same every run.
printf '  Survey\n'
survey_gsm() {
    run --file testfiles/gsm_arfcn_69.bin --frequency 948.4M --headless \
        --survey --once
}
checked
survey=$(survey_gsm)
candidates=$(printf '%s\n' "$survey" | grep -c "^candidate ")
carriers=$(printf '%s\n' "$survey" | sed -n 's/^survey candidates .* carriers \([0-9]*\)$/\1/p')
centre=$(printf '%s\n' "$survey" | grep "^candidate " | head -1 | cut -d' ' -f5)

if [ "$candidates" -lt 1 ]; then
    fail "the GSM capture surveyed to no candidates at all"
elif [ "${carriers:-0}" -ne 1 ]; then
    fail "ARFCN 69's carrier came back as ${carriers:-no} carriers, not one"
elif [ "$centre" -lt 948700000 ] || [ "$centre" -gt 948900000 ]; then
    fail "the measured centre $centre Hz is not ARFCN 69's 948.8 MHz"
elif ! printf '%s\n' "$survey" | grep -q "GSM 900 / LTE B8 downlink"; then
    fail "the survey did not look the carrier up in the band plan"
else
    report "gsm_arfcn_69.bin" \
        "$candidates candidates, 1 carrier at $centre Hz"
fi

# Byte for byte the same, twice. A survey an agent cannot diff against
# yesterday's is a survey it cannot use to notice anything.
checked
if [ "$(survey_gsm)" != "$survey" ]; then
    fail "the same capture surveyed differently the second time"
else
    report "run twice" "identical output"
fi

# A capture with no carrier in it must survey to nothing, and say so, rather
# than inventing a candidate out of the noise floor. Mode S is pulses: there
# is no carrier standing above anything.
checked
if ! run --file testfiles/adsb_cpr_pair.bin --headless --survey --once |
     grep -q "^survey candidates 0 "; then
    fail "a capture of Mode S pulses produced carrier candidates"
else
    report "adsb_cpr_pair.bin" "no carriers, as there are none"
fi

# --- Recording: the file and the sidecar that explains it -----------------
#
# Recording tees off inside the acquisition thread, so a capture played back
# through it exercises the same path a live one takes.
printf '  Recording\n'
checked
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
        report "$(basename "$recorded")" "$size bytes, sidecar complete"
    fi
    rm -f "$recorded" "$sidecar"
fi

# --- The flags that reach those paths -------------------------------------
#
# check-options proves the parser; this proves the program acts on it.
printf '  Flags\n'
checked
if ! run --file testfiles/adsb_cpr_pair.bin --headless --technology adsb \
        --decode --once | grep -q "End of capture."; then
    fail "--once did not stop at the end of the capture"
else
    report "--once" "stops at the end"
fi

checked
quiet=$(run --file testfiles/gsm_arfcn_73.bin --headless --arfcn 73 --decode \
            --once --gsm-features none | grep -c "^SCH ")
loud=$(run --file testfiles/gsm_arfcn_73.bin --headless --arfcn 73 --decode \
           --once --gsm-features filter,finecfo,trellis | grep -c "^SCH ")
if [ "$loud" -le "$quiet" ]; then
    fail "the SCH refinements decoded $loud bursts against $quiet without them"
else
    report "--gsm-features" "$loud bursts against $quiet without them"
fi

if [ -n "${CHECK_TALLY:-}" ]; then
    printf '%d %d\n' "$checks" "$failures" >> "$CHECK_TALLY"
fi
if [ "$failures" -ne 0 ]; then
    printf '  %-34s %4d checks   %d FAILED\n' "assembled program" "$checks" \
        "$failures" >&2
    exit 1
fi
printf '  %-34s %4d checks   ok\n' "assembled program" "$checks"
