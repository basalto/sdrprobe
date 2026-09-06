# 02 - Signal to noise, from the reference residual

Status: resolved

The chain has no noise measurement. `mean |soft|` was used as a proxy while
diagnosing band 8 and it is not one -- it rises with gain and says nothing
about how much of the magnitude is signal.

## Where to start

`estimate_port()` reads the references, divides each by its expected value and
hands the result to `interpolate_channel()`. The smoothed estimate is what the
channel is believed to be; the difference between it and each raw reference is
what could not be explained, which is noise plus whatever the interpolation
cannot follow.

So: `sum |raw - smoothed|^2` over the references, against `sum |smoothed|^2`,
is an SINR over the six central blocks. It needs no new samples and no new
transcription -- only the two arrays, both of which already exist inside that
function and neither of which escapes it.

Watch the bias: with references every sixth subcarrier and a linear
interpolation between them, a fast-fading channel puts real signal into the
residual and the figure reads low. That is a floor on what it can claim, and
the ticket should say so on screen rather than quietly report a number.

## What must be checkable

A synthetic buffer is built with a known noise sigma (`build_buffer` takes it),
so the measurement can be asserted against the noise that was added -- and
asserted to *move the right way* when sigma doubles, which is the property
that matters and the one a single value cannot show.

## Answer

Status: resolved -- but **not the way this ticket proposed**, and the two
estimators it did propose are recorded here because they are the obvious ones
and they do not work at this spacing.

### What the ticket asked for, and what it read

The residual between each raw reference and the smoothed estimate, which is
srsRAN's `estimate_noise_pilots`. With equal weights that residual is the
second difference `t[m-1] - 2t[m] + t[m+1]`, whose attraction is that it is
identically zero for a channel *linear in the complex plane*.

A channel is not linear in the complex plane. A delay makes it
`A*exp(-j*theta*m)`, a rotation, and the second difference of that is
`-4*A*sin^2(theta/2)`. References sit 90 kHz apart, so a microsecond of delay
leaves about ten decibels below the signal -- the floor becomes the channel.

Measured on air, against cells whose message rate is known:

```
                                     PCI 28 (decodes 158/175)   PCI 330 (264/292)
second difference                          -17.8 dB                  -0.2 dB
de-rotate the mean delay, then difference   +3.2 dB                  +0.6 dB
noise on the reserved elements             +24.0 dB                 +29.9 dB
```

De-rotating removes the slope and leaves the curvature, which is why the
second attempt improved the figure without fixing it. Both versions tracked a
known noise perfectly on synthetic buffers, because those have a flat channel
-- so the synthetic check passed throughout and said nothing.

### What works

36.211 clause 6.11.1.2 reserves the five resource elements either side of the
primary and secondary sequences and transmits nothing on them. Their power is
noise and interference with no channel in the way, which is srsRAN's
`SRSRAN_NOISE_ALG_EMPTY_SC`. Ten elements per frame, averaged over the frames
a block holds.

The signal is the reference power with that noise subtracted, since a
reference element carries both; srsRAN reports `rsrp/noise`, which is (S+N)/N
and cannot read below 0 dB.

On air the four cells here read, in the order their reference powers predict:

```
EARFCN 6200  PCI 28   RSRP -40.3 dBFS  RS-SINR 24.0 dB
EARFCN 3475  PCI 330  RSRP -34.2        RS-SINR 29.9
EARFCN 6300  PCI 59   RSRP -32.3        RS-SINR 31.0
EARFCN 3625  PCI 402  RSRP -32.8        RS-SINR 32.7
EARFCN 3625  PCI 190  RSRP -31.6        RS-SINR 33.8
```

### One change to the synthetic buffer

`place_sync()` wrote the 62 sync subcarriers and left `fill_other_traffic`'s
data on the ten either side, so the test carried signal where the air carries
nothing. It now reserves them, which is what the standard says and what makes
the noise measurement meaningful in a check at all.

`check-lte-dsp` asserts the property rather than a value: four times the noise
amplitude must raise the noise term twelve decibels and cost the ratio the
same, while the reference power does not move -- which is what separates a
noise estimate from a gain measurement.
