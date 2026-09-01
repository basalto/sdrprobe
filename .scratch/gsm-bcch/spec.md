# Past the SCH: what the cell is saying

The GSM side stops at the SCH: it reports a cell's identity code and its
clock, and nothing about what the cell carries. One layer further is the
BCCH, whose System Information messages name the network (MCC and MNC), the
location area, the cell, and the neighbours' frequencies.

## Done

`src/gsm_bcch.{c,h}`, `make check-gsm-bcch`, 491 checks. Four bursts to a
message:

| Layer | What |
| --- | --- |
| interleaving | GSM 05.03 4.1.4 block-rectangular, over four bursts |
| Fire code | (224,184), g(D) = (D^23 + 1)(D^17 + D^3 + 1) |
| convolutional | rate 1/2, K = 5 -- the same code the SCH uses |
| soft Viterbi | max-metric, both trellis ends known from the tail bits |
| LAPDm and RR | System Information 1, 2, 3, 4 |
| what it says | MCC, MNC (two- or three-digit), LAC, Cell Identity, ARFCN lists |

The checks push on both directions of every decision: the interleaver is
proved to be a permutation bit by bit, the Fire code must catch every single
error and two thousand eight-bit ones, a whole burst may be lost and the block
still decode, and soft decisions must beat hard ones on the same damage.

Getting the bits off the air is `gsm_normal_bursts()` in `src/gsm_dsp.c`:
derotate a quarter turn per symbol for coherent detection, fit a five-tap
channel to the training sequence at the alignment with the smallest residual,
take out the residual frequency offset from the two halves of that sequence,
then equalise forwards and backwards for soft bits. See
`issues/01-burst-equaliser.md` for what was wrong before and how each fault was
found.

## What it says

`./sdrprobe --file testfiles/gsm_arfcn_69.bin --headless --arfcn 69 --decode
--once` prints, beside its SCH lines:

    BCCH System Information 3  MCC 268 MNC 03  LAC 4010  CI 5131
    BCCH System Information 2  ARFCN 50 51 ... 69 71 73
    BCCH System Information 1  ARFCN 58 62 64 68 70 74

`check-pipelines` asserts the network, the location area, the cell identity,
and that the neighbour list names ARFCN 73 -- which is the other capture in
`testfiles/`, recorded from that neighbour. Two independent recordings agreeing
about the shape of the network is worth more than either alone.

`testfiles/gsm_arfcn_73.bin` yields nothing: it is the weaker cell, and its
bursts do not survive. That is an honest limit rather than a fault.
