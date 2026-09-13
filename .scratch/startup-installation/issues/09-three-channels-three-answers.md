# 09 - Three GSM channels, three corrections, and they trend with frequency

Status: resolved, 2026-09-13

Found when the agreement gate from issue 08 fired on air for the first time.
It refused, correctly, and in doing so measured a third channel -- which turns
a two-cell puzzle into something with a shape.

## The measurement

R820T, site home-sala-estar, +32 ppm applied, `--calibrate` per channel,
25 s each, all within one hour. Every channel parity-verified: a
synchronisation burst decodes with a valid BSIC on each.

| ARFCN | downlink | BSIC | suggested correction |
| --- | --- | --- | --- |
| 17 | 938.4 MHz | 10 | **+71 ppm** |
| 63 | 947.6 MHz | 42 | **+51 ppm** |
| 113 | 957.6 MHz | 38 | **+35 ppm** |

A crystal error is **one number**, so at most one of these is right. What
makes it more than three-way noise is that they are ordered: the correction
falls monotonically as the channel frequency rises, by roughly **-1.8 ppm per
megahertz**.

Neither obvious model fits cleanly. As a fixed frequency offset the implied
error is 66.6, 48.3 and 33.5 kHz -- not constant. As a constant ppm it is 71,
51 and 35 -- not constant either. The residual after removing ARFCN 113's
answer is -1.6 kHz per MHz of channel separation.

## Why this is probably the receiver and not three transmitters

Three independent base stations, from at least two BSIC groups (NCC 5, NCC 4,
NCC 0/1), do not drift together in a line ordered by frequency. Something that
varies smoothly with tuning does, and the receiver is the only thing common to
all three measurements.

The candidate worth testing first is the **tuner's own frequency resolution**.
An R820T synthesises its local oscillator in steps, and librtlsdr rounds a
requested centre to what the part can make. If the residual rounding varies
with frequency -- which it does, since the step is not a constant fraction of
the tuning -- then every channel is measured against a slightly different true
LO, and each reports a different apparent ppm. `rtlsdr_get_center_freq()`
returns the driver's *computed* value rather than the achieved one, so the
existing readback check (1 kHz tolerance, `overlay_settings.c`) would not see
it.

`ARFCN 113 is believed` for the reasons in issue 08 -- it agrees with this
repository's own recorded history (-31.3 ppm at 0 applied, months ago) and it
is the cell `gsm_arfcn_113.bin` was recorded from, BSIC 38 / NCC 4 / BCC 6.
That is corroboration from outside the measurement, which is the only kind
available here.

## Test 1 was run, and it is suggestive but not conclusive

One transmitter -- ARFCN 113's FCCH -- recorded four times in ninety seconds,
each from a different tuning, and its **absolute** frequency reconstructed as
`tuning + measured baseband`. A transmitter cannot care where the receiver is
tuned, so a constant answer means the receiver is consistent and a moving one
means it is not.

| tuning | tone at baseband | absolute FCCH | from nominal |
| --- | --- | --- | --- |
| 956.800 MHz | +866608.4 | 957666608.4 | **-1100 Hz** |
| 957.000 MHz | +665565.4 | 957665565.4 | **-2143 Hz** |
| 957.200 MHz | +464738.6 | 957664738.6 | **-2970 Hz** |
| 957.400 MHz | +264214.0 | 957664214.0 | **-3494 Hz** |

It moves: **2394 Hz across 600 kHz of tuning**, monotonically, and ordered by
how far the tone sits from the receiver's own centre rather than by the tuning
itself. Four independent recordings, four separate tuning events. That is
consistent with a receiver-side effect and hard to explain with one
transmitter -- which is the direction issue 09 suspects.

**It is not conclusive, and the reason is the instrument.** The deficit is not
linear in the baseband offset (-3494, -2970, -2143, -1100 at 200, 400, 600 and
800 kHz), so it is not a simple rate error, and `gsm_fcch_detect()`'s own
frequency resolution over its short window is not clearly finer than the
2.4 kHz being measured. Four points ordered by chance is a one-in-twelve
event, which is suggestive and not evidence.

**A methodological fault in the first attempt is worth recording**, because it
is the one this repository keeps warning about. `probe-fcch` originally used
one knob for both the sweep step and each probe's search width, so resolving a
few-kHz effect meant stepping in a few-kHz stride -- the answer was quantised
to the size of the thing being measured, and a first run read a 3.6 kHz spread
with a 2.5 kHz step, which is a measurement of nothing. The tool now takes
`HALF_FCCH` separately from `STEP_FCCH`. The numbers above are from the fixed
version; the discarded ones differed by up to 800 Hz per point.

## What would settle it, and why this is the wrong signal for it

A transmitter is a poor probe for a receiver: its true frequency is exactly
what is in question. **`probe-tone` is the right instrument** -- a
clock-coherent tone is generated from the receiver's own reference, so its true
frequency is known by construction, and `reading_origin.h` already sets out the
arithmetic. Measure one comb member at several tunings: if its apparent
absolute frequency moves, the receiver is what moves, with no transmitter in
the argument at all.

## Other tests still to run
1. **`probe-tone`** at a comb member near 950 MHz, at several tunings. This is
   now the first test rather than the second, for the reason above.
2. **`make probe-fcch`** across all three channels, comparing where each tone
   sits against its own nominal, which is what established the shape of the
   ARFCN 63 case.
3. The same four-tuning experiment on ARFCN 17 and 63. If all three channels
   show the same 2.4 kHz-per-600 kHz slope, it is the receiver; if only one
   does, it is not.

## What is already safe

Nothing files a correction while this is unresolved: the agreement gate
refuses two references more than `STARTUP_AGREE_PPM` apart, reports both
numbers, and applies nothing. The first on-air run of it did exactly that --
`arfcn 63 +51 ppm, arfcn 17 +71 ppm`, `reason references-disagree`.

That is the right outcome and it is worth saying plainly: **the program is now
correct about not knowing**, which is a better position than the confident
`locked +57` it printed before the gate existed.


## Resolved: the receiver is consistent, and the three channels are three different things

### The receiver is not what moves

One transmitter, ARFCN 113's FCCH, recorded from five tunings spanning 400 kHz
with one tuning repeated, and measured with **the shipping detector's own
single wide-search call** rather than a sweep -- that is the number the
program acts on, and it has no sweep artefacts.

| tuning | absolute FCCH | from nominal |
| --- | --- | --- |
| 957.0 MHz | 957664522.6 | -3185.7 Hz |
| 957.1 MHz | 957664608.0 | -3100.3 Hz |
| 957.2 MHz | 957664564.6 | -3143.7 Hz |
| 957.2 MHz (repeat) | 957664636.0 | -3072.3 Hz |
| 957.3 MHz | 957664725.6 | -2982.7 Hz |
| 957.4 MHz | 957664453.2 | -3255.1 Hz |

**272 Hz of spread across 400 kHz of tuning, with 71 Hz between two
recordings at the same tuning, and no monotonic order** -- 957.4 gives the
most negative and 957.3 the least. ARFCN 63 repeated to **71 Hz** across two
tunings.

So the tuner is consistent to about 0.3 ppm and the -1.8 ppm-per-MHz trend
this ticket was opened about **was my own instrument**: the earlier sweep
picked a peak from probes whose step was comparable to the effect. The
hypothesis in the section above -- the R820T's LO resolution -- is refuted,
and so is the "fixed frequency offset" reading.

### The three channels are three different things

| channel | two tunings agree to | from nominal | what it is |
| --- | --- | --- | --- |
| ARFCN 113 | 272 Hz (6 runs) | -3.1 kHz (-3.2 ppm) | the receiver's own residual: **a good reference** |
| ARFCN 63 | 71 Hz | -14.8 kHz (-15.6 ppm) | **a transmitter genuinely off frequency** |
| ARFCN 17 | **3277 Hz** | -38 to -42 kHz | **not an FCCH at all** |

**ARFCN 63** has exactly one FCCH-like line, stable to 71 Hz, 14.77 kHz below
where nominal puts it -- and a fine sweep finds **nothing whatever** at the
nominal position. Taking the receiver's -3.2 ppm out leaves the transmitter
12.3 ppm low. A macro base station is held to about 0.05 ppm, so this is not
one: a repeater or a small cell on a poor reference are the candidates, and
neither is ours to fix.

**ARFCN 17**'s line sits at the very edge of the detector's +/-50 kHz search
and moves 3277 Hz when the receiver moves 200 kHz. A line on the air cannot do
that. It is whatever was strongest inside a search window, and the window
moved.

## What was built

**A tone-repeat check** (`STARTUP_CONFIRM_TONE`). The SCH gate proves a base
station is transmitting on the channel; it does not prove the line the
detector locked onto is that station's FCCH. So after verification the
receiver is moved `STARTUP_TONE_SHIFT_HZ` and the line asked again: a real one
has an absolute frequency and cannot follow the receiver.

`STARTUP_TONE_REPEAT_PPM` is 1.0 and was **measured where it breaks**: two
real lines repeated to 0.07 and 0.28 ppm, the artefact moved 3.5 ppm, and one
ppm is the geometric middle of that gap rather than a number perched on either
edge.

**And a fall-through that threw away a correct answer.** On the first live run
of the cross-check, ARFCN 113 measured +34 ppm, the remaining candidates
failed verification, and the machine went on to the LTE fall-back and reported
`no-cell` with 123 measurements behind it. A band running out of *second
opinions* is not the same as having nothing to calibrate against: with a
reference in hand the answer now stands, uncorroborated and saying so.

**The rejection reason is captured before the search moves on.** Reading
`status` after a rejection gives the *next* candidate's line, which is what
the first trace printed: `rejected arfcn 63: Checking ARFCN 17 is a GSM
broadcast carrier`.

## On air, after

```
calibrate rejected arfcn 17: ARFCN 17 carries a tone but no GSM synchronisation burst
calibrate rejected arfcn 63: ARFCN 63 carries a tone but no GSM synchronisation burst
calibrate verified arfcn 113 bsic 38 tone moved -366 Hz
calibrate-result locked 1 measurements 123 centre_ppm -2.28 suggested_ppm 34 reason locked
```

**+34 ppm**, which is ARFCN 113's answer and agrees with this repository's own
recorded history. Before this ticket the same command locked on +57, +51 or
+46 from a channel that should never have been chosen, or refused outright.

## What is still not known, and does not block anything

Why ARFCN 63's transmitter is 12 ppm low. It is a fact about somebody else's
equipment, it is now measured rather than guessed, and nothing in this program
depends on the answer -- the channel is refused before it can contribute.
