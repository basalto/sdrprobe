# Is a shaped biphase filter worth it for RDS?

**Measured answer: no, and here is the table.** The rectangular filter stays.

## The claim

`fm_rds_correlate` uses a rectangular biphase filter -- eight samples of +1
then eight of -1, the shape of the symbol as the standard defines it. The
pulse actually transmitted is band-limited, so the true matched filter is that
shaped pulse, and the mismatch is worth about a decibel in theory.

## Why it took two attempts to answer

The first attempt compared the two on `testfiles/fm_rds_tsf.bin` and got **153
aligned syndrome hits against 153** -- no difference at all. That measurement
was not wrong, it was uninformative: a strong station decodes either way, and
a decibel cannot show where there is 20 dB of margin.

The second attempt needed a weak signal, and there is no weak RDS station
reachable from this site. Every carrier in band II here either decodes well or
carries no RDS at all -- 87.6, 95.3 and 96.6 were each listened to for twenty
seconds and produced nothing, while the seven that carry it all name
themselves within a couple of seconds. So the weak signal had to be made.

## How it was measured

`make probe-fm-filter` adds Gaussian noise to a real recording's I/Q and runs
both filters over the identical noisy samples, six independent noise draws per
point.

Noise goes on the **I/Q**, not the baseband. That matters: the FM
discriminator is nonlinear and has a threshold, so its output noise depends on
the carrier-to-noise ratio in a way that adding noise after it would not
reproduce. Both filters see the same draw, or the comparison would measure the
noise.

The shaped taps are scaled by root two so the two filters carry equal energy.
Without that the shaped one sums to half the rectangular one's and reads
uniformly quieter, which would have measured the scaling rather than the shape.

## The result

`testfiles/fm_rds_tsf.bin`, 3.01 s of TSF, six draws per row. "S/N" is the
recording against the noise added to it; the recording's own noise is already
in there, so these are relative steps rather than absolute figures.

```
   S/N    rectangular            shaped
          blocks groups named    blocks groups named
   as is    48     13   yes        48     13   yes
   +16 dB   48.0   13.0  6/6       48.0   13.0  6/6
   +14 dB   48.0   13.0  6/6       48.0   13.0  6/6
   +13 dB   47.5   13.0  6/6       47.5   13.0  6/6
   +12 dB   42.0   12.3  4/6       44.0   12.7  5/6
   +11 dB   35.8   12.2  3/6       36.5   12.0  4/6
   +10 dB   24.7   10.2  1/6       28.2   11.5  0/6
    +9 dB    1.3    0.7  0/6        1.8    1.2  0/6
    +8 dB    0.0    0.0  0/6        0.0    0.0  0/6
```

The shaped filter is **never worse and slightly better near the threshold** --
about 14% more blocks at +10 dB -- which is what theory predicts and is the
honest half of the result.

## Why it is not adopted anyway

Three reasons, in order of weight.

**The whole window is three decibels wide.** At +13 dB and above the two are
identical; at +9 dB both are dead. FM's own threshold effect is what closes
it: below about ten decibels the discriminator collapses and no amount of
filtering downstream matters. Half a decibel of sensitivity inside a
three-decibel window buys very little.

**The outcome a person sees did not improve.** Blocks and groups are the finer
instrument and they favour the shaped filter; whether the station gets
*named*, which is what the view reports, went 5/6 against 4/6, then 4/6
against 3/6, then 0/6 against 1/6. Those are one-trial differences on six
trials, in both directions.

**Nothing reachable lives in that window.** Every RDS station here sits far
above +13 dB. A change that cannot be observed from this site is a change
whose regressions cannot be observed from this site either.

## What was kept

`enum fm_rds_filter` and `fm_rds_soft_bits_with()`, so the shaped filter still
exists and the default is one argument away; and `make probe-fm-filter`, so
this question is re-answerable rather than re-arguable. If a marginal station
ever matters -- a distant transmitter, a worse antenna, an indoor site -- the
measurement takes one command and the answer may well come out differently.
