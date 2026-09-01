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

## Not done

`issues/01-burst-equaliser.md`: getting the 456 soft bits off the air. The
bursts are found and their training sequences come back perfectly; the data
bits are 9-17% wrong, which is inter-symbol interference and needs an MLSE
equaliser.

## When it lands

`check-pipelines` should assert, against `testfiles/gsm_arfcn_69.bin`:

- at least one BCCH block passes the Fire code;
- the block parses as a System Information message;
- `MCC 268` -- the captures were recorded in Portugal, and 40 parity bits
  behind a three-digit country code is about as close to ground truth as a
  recorded capture gets;
- the same capture gives the same answer twice.
