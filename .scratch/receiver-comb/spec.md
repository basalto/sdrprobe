# The reference comb is spaced 1.6 MHz, and the survey only knows about 14.4

`survey_suspect.h` flags a candidate as resembling the receiver when it sits on
a multiple of 14.4 MHz, half the RTL2832U's 28.8 MHz crystal. That comb is
real, and it is one ninth of the comb actually there.

Measured on 2026-09-05 while chasing `.scratch/phantom-candidates/`. Three
sweeps of 240-270 MHz with the step grid deliberately moved underneath them --
lower edges of 240.0, 239.2 and 239.5 MHz, so the boundaries fall in three
different places -- put candidates at the same absolute frequencies every time:

```
240.008  243.210  244.811  246.408  249.599  251.211
254.409  259.209  264.011  267.210  268.799
```

Every one is a multiple of 1.6 MHz. A sweep of the same band with `--ppm 0`
lands them on exact multiples -- offsets of +2.5, -0.8, -0.5, +0.6, +1.3, +2.3,
-0.4, +0.3 and +0.7 kHz against a 3.7 kHz bin -- while with this site's +35 ppm
correction applied they read about +35 ppm high. That is the signature of a
spur divided down from the same crystal that clocks the tuner and the ADC: its
ratio to the nominal grid is fixed, so the correction moves it and a real
transmitter would move the other way.

1.6 MHz is 28.8/18, so every ninth tone is also a multiple of 14.4 MHz. In the
`--ppm 0` sweep, 11 of 14 candidates were on the 1.6 MHz comb and the existing
detector flagged exactly the two that are also on the coarse one -- 244.8
(17 x 14.4) and 259.2 (18 x 14.4). Across the whole-tuner sweep of 2026-09-03,
43% of 289 candidates sit within half a bin of a 1.6 MHz multiple against 13%
by chance.

They are not weak and they are not fleeting. `--survey-confirm` over 240-270
confirms the eight strongest of them at 19-35 dB above their floors: a crystal
spur is there every time you look, which is why the confirmation pass cannot
substitute for the flag.

## Why this was not simply fixed

Flagging on a 1.6 MHz comb would mis-flag real broadcast. The FM raster is
100 kHz and 1.6 MHz is sixteen times it, so one FM channel in sixteen falls on
the comb exactly -- including **94.4 MHz, the loudest station at this site**,
confirmed at 46.2 dB. `survey_suspect_warns()` sets flagged candidates aside
from the report's per-allocation bests, so the flag would hide it. That is a
worse fault than the one being fixed.

The tolerance is the second obstacle. `survey_comb_tolerance()` grows to half a
survey bin, which is 106 kHz on a full-tuner sweep. Against a 14.4 MHz spacing
that is a 1.5% chance of a false hit; against 1.6 MHz it is 13%, or one
candidate in eight flagged by luck.
