# ADS-B Frame Analysis Charts

Status: ready-for-agent

## Problem

The ADS-B view shows successes and nothing else. A decoded message reaches the
log; everything upstream of it — the preamble that was accepted, the 112
pulse-position decisions, the CRC that failed — is invisible. So when the log
stays empty the user cannot tell apart the three reasons it might be empty:

- nothing is transmitting, or the antenna/tuning is wrong (no preambles at all);
- frames are arriving but the bit decisions are marginal (preambles accepted,
  CRC failing);
- the receiver is clipping, which mangles the pulse amplitudes the demodulator
  compares.

The GSM view already answers the equivalent question with its Burst Analysis
Charts. Mode S deserves the same x-ray, and the decode chain has exactly the
same three stages worth showing: where the frame was found, how confident each
bit was, and what the signal looked like.

## Decision

Add an analysis mode to the Decode tab's ADS-B view, mirroring the GSM view's
shape: a `View: Log / Analysis` toggle in the top right, three burst charts
across an upper panel, the message log below them, and a decision-margin
scatter in the lower right.

The three charts, all drawn with the existing `sdrgui_burst_chart`:

| Chart | Type | What it plots | GSM analogue |
| --- | --- | --- | --- |
| Preamble Score Landscape | line | preamble match score per sample offset, ±32 samples around the accepted frame | Timing Correlation Landscape |
| Pulse-Position Bit Confidence | bar | per bit, `abs(first - second) / (first + second)`, 112 bars in 0..1 | Soft Symbol Magnitudes |
| Frame Magnitude Envelope | line | the 240 magnitude samples of the frame (16 preamble + 224 data), normalised to the preamble's high level | Differential Phase Trajectory |

The lower-right panel reuses `sdrgui_constellation` with x = the signed decision
margin `(first - second) / (first + second)` and y = the bit's normalised
amplitude, coloured by the decided bit. Two tight clusters left and right is a
clean frame; a smear through the middle is a frame that decoded by luck. It is
**not** called a constellation in the UI — Mode S carries no modulated symbols
and no phase — see Vocabulary below.

Alongside the charts, a header line reports the decode funnel: preambles
accepted, extended-squitter-shaped attempts, CRC failures, messages decoded,
for the latest block and cumulatively. That single line answers "why is the log
empty" on its own, and is the cheapest part of this feature.

## What is latched, and when

The charts show one frame, not a running average. The plugin latches the most
recent **attempt** — a preamble that was accepted and produced a DF17/18-shaped
frame — whether or not its CRC passed, and the caption says which. Failures are
the interesting case, so they must not be the ones that get thrown away. A
`Hold last good` toggle pins the panel to the most recent CRC-valid frame for
comparison.

## Constraints this must respect

- **The DSP stays GUI-free.** The trace is produced in `adsb_dsp.c` and
  `check-adsb-dsp` keeps linking `-lm` only (ADR-0001, ADR-0009).
- **Decoding does not change.** This feature reports what the demodulator
  already does. No new acceptance path, no error correction: ADR-0009's "CRC
  failures are dropped" stays true — they are counted, not repaired.
- **No new GUI component is needed.** `sdrgui_burst_chart` (line/bar, own
  caption and gutter) and `sdrgui_constellation` (points + per-point bit colour)
  both take what these charts need. If one turns out not to, that is a finding
  worth a comment on the ticket, not a quiet fork of the component.
- **Layout is a pure function.** The ADS-B view derives its rectangles inline
  today; the analysis mode needs `adsb_layout.h` in the shape of
  `gsm_layout.h`, covered by `make check-layout`.

## Vocabulary

Three terms are new and belong in `docs/contexts/decoder/CONTEXT.md`, beside
the ones the GSM burst charts added:

- **Frame trace** — the intermediate demodulation data kept for one recovered
  Mode S frame so it can be visualised. _Avoid_: capture, recording, snapshot.
- **Preamble score** — how well the magnitude pattern at a sample offset
  matches the four-pulse Mode S preamble. _Avoid_: correlation (that is the
  GSM training-sequence term), coherence (a Probe term for tone-likeness).
- **Decision margin** — how far a pulse-position bit's two half-interval
  magnitudes stood apart, relative to their sum; the confidence behind one hard
  bit. _Avoid_: soft symbol, SNR, amplitude.

Two words must **not** appear in this view: "constellation" (no modulated
symbols exist here) and "symbol" for a pulse-position bit, which the decoder
glossary already rules out.

## Decisions taken at triage

1. **Panel arrangement: mirror the GSM view.** Three charts across the top, the
   message log lower-left at about half its current height, the decision-margin
   scatter lower-right. Seeing messages arrive while reading the charts is worth
   more than the height the log gives up.
2. **Landscape width: ±32 samples**, 65 points, 16 µs either side — a field
   about four preamble widths across, so a genuine lock stands alone in it.
   Re-check it against `testfiles/adsb_modes1.bin` in ticket 02 and adjust if
   the peak reads as a spike or a hump.
3. **Trace only the latched attempt.** One landscape per block, for the last
   DF17/18-shaped attempt, so a block full of false preambles costs nothing. A
   later per-block histogram over every attempt is what would have to revisit
   this.

## Tickets

See `issues/`. Implement in phase order: 01 → 02 → 03 → 04 → 05 → 06.
01–03 are DSP and hardware-free; 04–05 are the view; 06 is the writing.

## Comments
