# 05 — The broadcast channel decodes synthetically and not on air

Status: resolved
Blocked by: (none)

**Resolved.** `testfiles/lte_b20_pci28.bin` reads cell 28, a 50-block 10 MHz
carrier on two antenna ports, PHICH normal 1/6, in 28 of its 30 blocks, with
the frame number advancing by six or seven per block exactly as its length
implies. A live receiver does the same. `check-lte-dsp` and `check-pipelines`
assert all of it.

There were two faults and the second was mine.

## The fault: a phase can only say so much

The primary sequence measures the frequency offset from the phase turned
between its two halves. A phase wraps. One full turn across 64 samples is
15 kHz, which is one subcarrier -- so what that measurement returns is **the
offset modulo a subcarrier**, and nothing about the whole subcarriers.

An uncalibrated dongle is tens of parts per million out. At 796 MHz this one
is about -36 ppm, which is -28 kHz: **two whole subcarriers**, plus the -1.8 kHz
the primary sequence could see. Two subcarriers leaves the primary correlation
standing -- 0.796, well over any threshold -- while putting every subcarrier
the secondary sequence and the reference signals need two places from where
they are read. So the search returned an identity, confidently, and it was
wrong; and everything downstream of it was reading noise.

The fix is to find the whole part by trying it: sweep integer subcarrier
offsets and let the secondary sequence say which one is right. That is what
`LTE_INTEGER_OFFSETS` is. It is standard practice in an LTE receiver and it
was simply missing here.

A second, smaller thing fell out of it. A frequency offset does not merely
weaken a Zadoff-Chu correlation, it **moves** it -- the sequence is a chirp,
so time and frequency errors trade against each other. The peak was landing
about seven samples off, which is inside the cyclic prefix and so invisible to
the secondary sequence, and fatal to the broadcast channel: seven samples is a
third of a turn per subcarrier, which no channel estimate follows. Once the
offset is known the peak is found again with it removed
(`pss_refine_timing`), and the correlation improves from 0.796 to 0.874 into
the bargain.

## The other fault: the sign, and the fix that was not one

Recorded in `issues/04`. The exponent of the primary sequence is negative --
36.211, and srsRAN's `pss.c` verbatim. It was flipped earlier in the belief
that the secondary sequence disagreeing with it meant the primary was wrong.
The disagreement was real; the conclusion was backwards. Flipping it made a
broken secondary detector agree with a newly broken primary one, and two
wrongs agreeing is indistinguishable from two rights agreeing.

What broke the deadlock was measuring the reference signals, which share
nothing with either synchronisation signal, and finding that they named a cell
whose N_ID_2 was what the **negative** sign reported.

## And a check that was asserting the wrong answer

`check-lte-dsp` had been written to assert the identity the broken pair agreed
on, so it passed throughout and lent the wrong answer real-signal authority.
That is the part worth remembering: a real-capture check is only as good as
the reasoning that chose what to assert, and this one had been chosen by the
same reasoning it was meant to check.

It now asserts the things a plausible fault fails: the identity, the number of
whole subcarriers of tuning error, the message, and the frame number
advancing at the rate the block length implies. The last is the one chance
cannot fake.

## What was checked against external references

All of it, in the end, and all but one matched:

| | reference | verdict |
| --- | --- | --- |
| primary-sequence exponent | `sign = -1`, roots {25,29,34} by N_ID_2 (`pss.c`) | **did not match** |
| reference-signal seed | `1024*(7*(ns+1)+lp+1)*(2*id+1) + 2*id + N_cp` | matched |
| sequence index | `mp = i + SRSRAN_MAX_PRB - nof_prb`, 104..115 centrally | matched |
| pilot subcarrier | `fidx = 6*m + ((v + id%6) % 6)` | matched |
| secondary-sequence construction | s/c/z recurrences, m0/m1, subframe 0 and 5 forms (`gen_sss.c`) | matched |
| length-31 Gold sequence | rewritten from the standard's index notation | matched |

## What is still open

- **Weak cells misread the cyclic prefix.** `captures/lte_earfcn6400` reports
  the extended prefix in some blocks and the normal one in others, and no
  message comes out of the blocks that get it wrong. The prefix is decided by
  which of two symbol positions the secondary sequence scores better at, and
  at low signal that is a coin toss. Deciding it once per cell rather than per
  block would fix it.
- **Sixteen kilohertz of the sweep is wasted.** The tuning error is a property
  of the receiver, not the carrier, so once it is known the sweep could start
  from it rather than from zero.
