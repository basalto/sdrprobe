# 13 - Every capture carries a line at rate/256

Status: needs-triage
Found 2026-09-12, while testing a sideband hypothesis in `10-*`.

The envelope of every capture this receiver produces carries a line at exactly
**one per 256 sample pairs**, whatever the sample rate. Same channel
(470.3 MHz, nothing transmitting), same minute, three rates:

| sample rate | peak | strength | rate / peak |
| --- | --- | --- | --- |
| 2 000 000 | 7812.50 Hz | 132x the median | **256.000** |
| 2 048 000 | 8000.00 Hz | 154x | **256.000** |
| 1 920 000 | 7500.00 Hz | 148x | **256.000** |

Measured to 0.25 Hz (four seconds), so 256.000 is 256 and not something near
it. It is present at 479.7 MHz with a strong coherent tone in the window and
at 470.3 MHz with nothing in the window at all, and in
`testfiles/carrier_75000_bare.bin` recorded weeks earlier at 74.7 MHz -- so it
belongs to the receiver's data path and not to anything on air.

## 256 pairs is 512 bytes

Which is exactly **USB 2.0 high-speed's bulk maximum packet size**, and the
obvious reading is that every packet boundary imprints on the samples. Stated
as the hypothesis it is: 512 bytes is also an ordinary FIFO width, and nothing
measurable from outside this box separates those. What *is* established is the
count -- 256 pairs, at three rates, exactly.

**It is not the 125 us microframe**, which is what this was expected to be
when the question was opened. A microframe is 8000 Hz at every sample rate; a
packet is 8000 Hz only at 2.048 MS/s.

### The coincidence that would have confirmed the wrong thing

**At 2.048 MS/s the line sits on 8000.00 Hz exactly.** One of the three rates
this program uses lands the packet rate precisely on the USB frame rate, so a
measurement taken only at 2.048 MS/s would have "confirmed" the microframe
hypothesis to the last digit, with impeccable arithmetic and a false claim.
The rate sweep is what separates them and it cost two four-second recordings.

## Why it matters

**It manufactures sidebands on anything loud enough to show them.** At 2 MS/s
a survey bin is 976.6 Hz and 7812.5 is exactly 8 of them, so a strong carrier
appears to carry a symmetric family at "+/-7812 Hz" -- a number that is the
resolution and the packet rate agreeing by accident. `10-*` spent an
afternoon on a hypothesis built from that rounding.

**It is in every decode.** At 2.5% of the mean magnitude on the strong capture
it is unlikely to trouble a threshold, and **nothing here has looked** for an
effect on a decode. That is the first thing to measure if this is picked up.

## How it was found, and the error worth carrying

The first measurement of this line said **8018.00 Hz** and was wrong, in a way
that produced a confident wrong number rather than a failure.

`envspec.c` evaluates the DFT directly at each candidate frequency, scanning
7000-9000 Hz in **2 Hz steps**. Four seconds of record resolves **0.25 Hz**, so
the scan sampled the spectrum eight times too coarsely: half a hertz off the
true line the response has already fallen to 1.7% of its peak, and what the
scan returned was a sidelobe. The follow-up "fine scan" then searched
8000-8040 Hz at 0.1 Hz -- a window chosen from the wrong premise, which could
not contain the answer and confirmed the error to two decimal places. A
"harmonic at exactly 2 x 8018" was the same mistake in a window at twice the
frequency.

**A scan step must be finer than the resolution of the record it scans**, or
it is sampling a sinc pattern at arbitrary points. Evaluated afterwards at the
two candidates by the same C code, the record answers plainly: 1.9424 at
7812.5 against 0.2023 at 8018, and 0.033 half a hertz either side of 7812.5.

What caught it was **numpy** -- a transform of the whole record, sharing no
code with the C probe, landing on 7812.50 in all three captures. That is the
second-implementation argument in `AGENTS.md` paying for itself within minutes
of being written down, and neither a review of the C nor a second C probe
would have found it: the arithmetic was right and the sampling was wrong.

## What would settle what it is

- **Different host, port, hub, machine.** A packet boundary is the host's
  doing; an internal FIFO is not.
- **Does the count stay 256 at other rates?** 250 kS/s and 3.2 MS/s are far
  from the three tested and cost a recording each.
- **Does it survive the antenna coming off?** It should, and the passes in
  `10-*` were run at survey resolution rather than recorded, so it is untested.
