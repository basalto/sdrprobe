#!/bin/sh
# Is there a clock-coherent tone at this frequency, and whose clock is it?
#
# `.scratch/device-model/issues/10-*` and `docs/rtl-sdr-spurs-reference.md`.
# This one-liner was written about a dozen times in one afternoon -- sweep a
# window around a nominal, find the candidate nearest it, and subtract -- and
# every number in that campaign landed in a transcript rather than a ticket,
# which is the thing `AGENTS.md` says a repeated scratch script is for.
#
# The subtraction is the whole point and it is easy to get backwards. With a
# crystal error k and a correction c applied, the receiver's residual is
# e = k - c, and:
#
#   an EXTERNAL source on the nominal reads   nominal / (1 + e)
#   a source CLOCKED BY THIS RECEIVER reads   nominal * (1 + k) / (1 + e)
#
# So uncorrected (c = 0) a coherent tone reads its exact nominal and an
# external one reads low by f*k; corrected, the two swap. They are f*k apart
# either way -- the correction never narrows the gap, it only decides which
# hypothesis sits on the nominal.
#
# Usage:  make probe-tone FREQ_TONE=150M [PPM_TONE=32] [SPAN_TONE=2M]
#         make probe-tone FREQ_TONE=150M APPLIED_TONE=0   # uncorrected sweep
#
# PPM_TONE is the crystal's own error, which is the *negation* of the residual
# `cal-measure` prints. APPLIED_TONE is the correction in force, and defaults
# to PPM_TONE because that is what the program restores at startup.
set -e

hz() { echo "$1" | sed 's/[Mm]$/000000/; s/[Kk]$/000/'; }

FREQ=$(hz "${FREQ_TONE:?set FREQ_TONE, e.g. FREQ_TONE=150M}")
SPAN=$(hz "${SPAN_TONE:-2M}")
PPM=${PPM_TONE:-32}
APPLIED=${APPLIED_TONE:-$PPM}
DWELL=${DWELL_TONE:-0.4}
BIN=${WINDOW_TONE:-60000}

LO=$(( FREQ - SPAN / 2 ))
HI=$(( FREQ + SPAN / 2 ))

[ "$APPLIED" = "$PPM" ] && PPMARG="" || PPMARG="--ppm $APPLIED"

OUT=$(./sdrprobe --headless $PPMARG --survey \
        --survey-range "${LO}:${HI}" --survey-dwell "$DWELL" 2>/dev/null \
        | grep '^candidate' || true)

echo "$OUT" | FREQ="$FREQ" PPM="$PPM" APPLIED="$APPLIED" BIN="$BIN" \
    LO="$LO" HI="$HI" awk '
BEGIN {
    f = ENVIRON["FREQ"] + 0; k = ENVIRON["PPM"] / 1e6;
    c = ENVIRON["APPLIED"] / 1e6; w = ENVIRON["BIN"] + 0;
    e = k - c;
    coherent = f * (1 + k) / (1 + e);
    external = f / (1 + e);
    bc = 0; bcgap = 0; be = 0; begap = 0;
}
/^candidate/ {
    # Nearest to EACH hypothesis, not nearest to the nominal.
    #
    # The two predictions sit either side of the nominal and can be tens of
    # kilohertz apart, so "the candidate nearest f" picks the wrong one
    # whenever another signal is closer to f than the tone is. Written the
    # obvious way first, this reported 479994629 for a 480 MHz sweep whose
    # coherent tone is at 480015137, and called it unexplained. It is the same
    # trap `reading_origin.h` documents for channel rasters: the hypothesis
    # decides where to look.
    gc = $2 - coherent; if (gc < 0) gc = -gc;
    ge = $2 - external; if (ge < 0) ge = -ge;
    if (bc == 0 || gc < bcgap) { bc = $2; bcgap = gc; clvl = $3; cpr = $4; cfl = $8; }
    if (be == 0 || ge < begap) { be = $2; begap = ge; elvl = $3; epr = $4; efl = $8; }
}
END {
    printf "%.6f MHz  swept %.3f-%.3f  crystal %+g ppm, applied %+g\n",
           f / 1e6, ENVIRON["LO"] / 1e6, ENVIRON["HI"] / 1e6,
           ENVIRON["PPM"] + 0, ENVIRON["APPLIED"] + 0;
    printf "  a coherent source would read %.0f, an external one %.0f  (%.0f Hz apart)\n", coherent, external, coherent - external;
    if (bc == 0) { print "  ABSENT: no candidate in the window at all"; exit; }
    if (bcgap <= w)
        printf "  nearest coherent: %.0f at %s dBFS, %s dB, flags %s   %+.0f Hz\n",
               bc, clvl, cpr, (cfl == "" ? "-" : cfl), bc - coherent;
    else
        printf "  nearest coherent: none within %.0f kHz (closest %.0f, %+.1f kHz)\n",
               w / 1e3, bc, (bc - coherent) / 1e3;
    if (begap <= w)
        printf "  nearest external: %.0f at %s dBFS, %s dB, flags %s   %+.0f Hz\n",
               be, elvl, epr, (efl == "" ? "-" : efl), be - external;
    else
        printf "  nearest external: none within %.0f kHz (closest %.0f, %+.1f kHz)\n",
               w / 1e3, be, (be - external) / 1e3;
    if (coherent == external)
        print "  VERDICT: none. With no crystal error the two hypotheses are the same frequency -- this needs a measured, non-zero ppm.";
    else if (bcgap <= w && (begap > w || bcgap < begap / 4))
        print "  VERDICT: clocked by this receiver";
    else if (begap <= w && (bcgap > w || begap < bcgap / 4))
        print "  VERDICT: not clocked by this receiver";
    else if (bcgap > w && begap > w)
        print "  VERDICT: absent -- nothing near either hypothesis";
    else
        print "  VERDICT: neither hypothesis fits cleanly -- unexplained";
}'
