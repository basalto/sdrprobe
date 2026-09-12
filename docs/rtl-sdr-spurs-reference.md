# RTL-SDR spurs: what the literature establishes, and what it does not

A literature search against `.scratch/device-model/issues/10-a-second-clock-nothing-models.md`,
run 2026-09-12. The question it was opened for: this repository has measured
three families of clock-coherent tones on one R820T dongle -- a comb every
14.4 MHz, a finer one every 1.6 MHz, and fifteen frequencies on neither --
and none of the three has ever been checked against a published source.

**The short answer is a null with one exception and one partial fit.**

- **Established, from the chips' own documentation**: the 28.8 MHz reference,
  the ADC clock derived from it, the R820T's PLL and its power-of-two mixer
  divider. Section 2.
- **Documented in the literature**: exactly one internal spur frequency for
  this hardware, **1.44 GHz**, measured by two independent authors and
  attributed to the 28.8 MHz oscillator. This repository measures it too.
  Section 4.
- **A partial fit, marked _likely_ and not established**: ten of the fifteen
  unexplained frequencies are the USB 2.0 high-speed clock family and its
  third harmonics. Section 5.2. It is a hypothesis with a cheap falsifying
  test, given in section 6.
- **A clean null**: nothing published describes a 14.4 MHz comb, a 1.6 MHz
  comb, or the 75/150/300 and 135/540 ladders. `28.8/18` appears in no source.
  Sections 3 and 5.3.

Confidence is marked on every substantive claim: **established** (a primary
source says it), **likely** (a mechanism class is documented and the numbers
fit, but no source says this), **speculation** (arithmetic that fits and
nothing more). The house rule is that a gap beats a guess, and section 5.3 is
the gap.

## 1. The measurements being correlated against

One R820T dongle, one indoor site, telescopic whip, crystal error measured
independently at **+31.84 ppm** (GSM FCCH, 874 measurements, standard error
0.22 ppm). A separate measurement the same week read -35.96 ppm with a
standard error of 0.04 over 498 measurements; both are quoted in ticket 10,
and the gap between them is temperature and not a disagreement about method.

The discriminator throughout is displacement. An uncalibrated receiver
displaces an **external** signal's reported frequency by `f*d`; a tone
generated from the receiver's **own** reference reads its exact nominal
whatever the crystal does, because the same error is in the tuning, the
sample rate and the tone. At these frequencies the separation is 2.4 to
17 kHz against a 977 Hz bin, so it is not marginal.

**Present and clock-coherent, fifteen frequencies** (ticket 10, "The
campaign", 2026-09-12), each reading within a bin of exact:

    30   60   75   120  135  150  180  240
    300  360  480  540  720  960  1440      MHz

**Absent**, at an 8 dB prominence bar, the last of them re-checked with narrow
sweeps rather than inferred from a wide one:

    37.5   45   175   225   270   600        MHz

**Untested**: 15 MHz and below (under the R820T's reach), 90 MHz (masked by FM
broadcast), 1920 MHz (over the tuner's reach).

Two of the three modelled families overlap this set: 1440 is on the 14.4 MHz
comb (x100) and the 1.6 MHz comb (x900); 120, 240, 360, 480, 720 and 960 are
on the 1.6 MHz comb. **30, 60, 75, 135, 150, 180, 300 and 540 are on neither.**

As multiples of 15 MHz the present set is 2, 4, 5, 8, 9, 10, 12, 16, 20, 24,
32, 36, 48, 64, 96 and the absent set is 3, 15, 18, 40. Sorted by odd part it
is four binary ladders:

| odd part | ladder | range | the hole |
| --- | --- | --- | --- |
| 1 | 30, 60, 120, 240, 480, 960 | complete | -- |
| 3 | 180, 360, 720, 1440 | complete | 45 absent, 90 untested |
| 5 | 75, 150, 300 | | 600 absent |
| 9 | 135, 540 | | **270 absent** |

**The tones are fixed rather than tuning-dependent, and that was measured
rather than assumed.** A tone found by a sweep could be a fixed emitter or an
artifact that moves with the tuning, and one sweep cannot tell those apart
because each frequency is covered by exactly one step. The 60 MHz tone was
swept four times with deliberately different step boundaries -- 59-61,
59.3-61.3, 58.6-60.6, 59.75-60.75 -- and read 60.002441, 60.002637, 60.001855
and 60.002441: a spread of 782 Hz, under one bin. That control has been run at
one frequency out of fifteen, which is one more than most published spur
lists, and it is the control the rest of this document assumes.

**The brief this search was given listed only five of the fifteen.** It named
75, 135, 150, 300 and 540 as the unexplained family. The campaign section of
ticket 10 had already added 30, 60, 120, 180, 240, 360, 480, 720, 960 and
1440, and 45 to the absences. The larger set is the one that matters, because
the ten added frequencies are the ten a published mechanism turns out to
account for.

## 2. The clock architecture

### 2.1 RTL2832U

**Established.** One crystal, 28.8 MHz, fundamental mode, and every clock in
the part is derived from it. The datasheet's feature list says "Single
low-cost crystal for clock generation" and Table 38 gives the crystal
conditions: 28.8 MHz typical, **+/-30 ppm tolerance and +/-30 ppm stability**,
40-60% duty cycle. The block diagram labels the sampling clock 28.8 MHz. Pins
10 and 11 are XI and XO; pins 7, 8 and 9 are VDDPLL, GNDPLL and a test pin for
an external ADC clock. [R1, secs. 4, 12.4, Table 1]

**The ADC clock is PLL-generated, not the bare crystal**, and the datasheet is
explicit and unhelpful about it in the same sentence: "Using a sampling clock
generated by the internal PLL with a 28.8MHz clock source, the RTL2832U
demodulates the received TV signal" [R1, sec. 7.1]. **The PLL's own VCO
frequency, its reference divider and its feedback divider are not in the
public datasheet.** That absence is load-bearing for section 5: an internal
VCO running at some hundreds of megahertz is the most obvious candidate for a
fixed clock-coherent tone, and there is no published number to test against.

**The ADC then runs at 28.8 MS/s and a resampler decimates.** This is stated
three ways. The datasheet: "As the ADC sampling clock is larger than the
symbol ratio, there is a re-sampler to convert sampling data to symbol ratio",
with `rsamp_ratio` a function of `f_crystal` [R1, sec. 9.4]. librtlsdr:
`rsamp_ratio = (dev->rtl_xtal * 2^22) / samp_rate` with
`DEF_RTL_XTAL_FREQ 28800000` [R2, `librtlsdr.c`]. And the RTL-SDR Blog V3
datasheet, describing direct sampling: "The RTL2832U samples at 28.8 MHz, so
0 - 14.4 MHz, and 14.4 MHz - 28.8 MHz can be listened to" [R6]. PA3FWM
states the same rate and the same folding independently [R7].

So the only **documented** divisor of 28.8 MHz anywhere in this chip's
signal chain is the Nyquist folding at 14.4 MHz. That is not the same as
saying the 14.4 MHz comb comes from there -- see section 3.

**The part has an internal switching regulator**, 3.3 V to 1.2 V, enabled by
default via the ENSWREG pin [R1, sec. 8.4]. A switching converter is a
textbook broadband comb source. **Its switching frequency is not published**,
and whether it is derived from the 28.8 MHz clock -- which is what would make
its comb coherent -- is not published either. This is a real candidate
mechanism with no number attached to it, and it is the second thing section 5
cannot test.

The USB 2.0 interface is on the same die and the same crystal. Its PHY clock
tree is not described in the datasheet at all; section 5.2 argues from the USB
specification instead.

### 2.2 R820T

**Established, from the driver rather than from the datasheet.** The R820T is
a low-IF tuner: LNA, tracking filter, mixer, **fractional-N PLL**, VGA and
LDO on one QFN-24 [R3]. In an RTL-SDR dongle it is clocked from the
RTL2832U's crystal -- librtlsdr sets `dev->tun_xtal = dev->rtl_xtal`, i.e.
28.8 MHz [R2, `librtlsdr.c`]. **This is not the R820T's design-centre
reference**: its own datasheet's crystal table gives 16 MHz typical with a
16 pF load, and the reference schematic shows a 16 MHz part [R3, sec. 6.1].
28.8/16 is 1.8, which is the factor by which every LO mistunes if a driver
assumes the wrong one [R12] -- and, noted here only because it will come up in
section 5.3, it is also 135/75.

The PLL, all of it from `tuner_r82xx.c` [R2]:

| quantity | value | where |
| --- | --- | --- |
| reference | `pll_ref = priv->cfg->xtal` = 28.8 MHz | `r82xx_set_pll` |
| VCO range | **1770 to 3540 MHz** (`vco_min = 1770000` kHz, `vco_max = vco_min * 2`) | `r82xx_set_pll` |
| mixer divider | **2, 4, 8, 16, 32, 64** -- `mix_div <<= 1` until the VCO lands in range | `r82xx_set_pll` |
| feedback | `vco_div = (pll_ref + 65536 * vco_freq) / (2 * pll_ref)`, `nint = vco_div / 65536`, `sdm = vco_div % 65536` | `r82xx_set_pll` |
| SDM step | 2 x 28.8 MHz / 65536 = **878.9 Hz** at the VCO, /mix_div at the LO | arithmetic on the above |
| IF | **3.57 MHz** (`int_freq`), so `lo_freq = freq + 3.57 MHz` | `r82xx_set_tv_standard`, `r82xx_set_freq` |
| tuning reach | 24 MHz to 1766 MHz measured; the datasheet specifies **42 to 1002 MHz** | [R5], [R3, Table 1-2] |

**The brief said the VCO runs 1770-3900 MHz. It is 1770-3540** -- `vco_max` is
literally `vco_min * 2` in the source. The difference matters for any argument
about which mixer divider is in use at a given tuning.

Two things follow that section 5 leans on. **The mixer divider is a binary
tree** -- a genuine power-of-two divider chain inside this receiver, which is
the structure that produces octave ladders and never a third harmonic. And
**the PLL is fractional-N with a 16-bit sigma-delta**, which is the structure
that produces fractional spurs at offsets from the carrier. Neither is
evidence that either produced what was measured; they are the documented
mechanism classes available.

### 2.3 What is not public, and where that bites

**The R820T datasheet is a preliminary, watermarked, NDA-marked document and
it is not complete.** The version that circulates -- rev 1.2, 2011/11/30,
"CONFIDENTIAL", "PRELIMINARY VERSION", hosted by rtl-sdr.com [R3] -- was read
for this document. It contains the pinout, AC/DC parameters, the crystal
requirements, an IF frequency table and a reference schematic. Its register
section documents **registers 0 to 4 only**: "Register 0 to Register 4 are
reserved for internal use only and can be written by I2C write command"
[R3, sec. 3]. The PLL registers librtlsdr programs -- 0x10 through 0x16, which
carry `refdiv2`, `div_num`, `ni`, `si` and the 16-bit SDM -- **are not in it**.

So every number in the table above except the IF comes from driver source
that Realtek's reference code was reverse-engineered into, not from a
datasheet. It is primary in the sense that it is the code that actually
programs the part, and it is not a manufacturer's specification. There is no
published VCO phase-noise figure, no published spurious specification with a
number against it -- the R820T datasheet's AC table has a row labelled
"Spurious" and **the cell is blank in the public version** -- and no
description of the internal clock distribution of either chip.

Anything below that is marked _likely_ or _speculation_ is marked that way
because of this paragraph.

## 3. Is a 14.4 MHz or a 1.6 MHz comb documented anywhere?

**No. Clean null on both, and the 1.6 MHz one comes with a warning.**

Searches across rtl-sdr.com, the osmocom project wiki, the two chips'
datasheets, librtlsdr's source and the measurement reports in the references
turned up **no published description of a spur comb at 14.4 MHz spacing and
none at 1.6 MHz**. The literature's account of RTL-SDR clock spurs is
harmonics of 28.8 MHz and nothing finer (section 4).

**28.8/2 = 14.4 is at least a divisor the architecture uses**: it is the
Nyquist frequency of the 28.8 MS/s ADC and is named as such in the V3
datasheet [R6]. That makes a 14.4 MHz family unsurprising without making it
documented. **Confidence: the comb is measured here; its attribution to any
particular divider is speculation.**

**28.8/18 = 1.6 MHz has no support at all.** The divisor 18 appears in
neither datasheet, in no register description, in librtlsdr, and in no
measurement report found. There is no standard clock at 1.6 MHz in either
chip's documented function.

One thing worth recording about that search, because it is the exact failure
mode this repository keeps writing down. A web search summary asserted, in
confident prose, that "the RTL2832U generates a 1.6 MHz internal clock by
dividing the 28.8 MHz crystal by 18" and that spurs therefore appear at
1.6 MHz intervals. **None of the sources it returned say any such thing.** It
is a generated restatement of the question. It is recorded here so that nobody
later finds it, recognises the number, and treats it as corroboration:
**the 1.6 MHz comb's only evidence is this repository's own measurements.**

## 4. Documented spur mechanisms, and which produce a fixed frequency

The useful published distinction is HB9AJG's, and it is the one that matters
for everything above: "most of them (except the harmonics of the clock, of
course) vary their frequency when moving the spectrum window in frequency"
[R5, sec. 4]. That is the same test as this repository's four-step-boundary
control at 60 MHz, run by hand in 2013 on a spectrum display.

Sorted by whether the artifact sits at a fixed absolute frequency:

| mechanism | fixed or moves | documented for this hardware? |
| --- | --- | --- |
| reference clock harmonics (n x 28.8 MHz) | **fixed** | **yes** -- [R5], [R4] |
| any other on-die clock and its harmonics | **fixed** | no published inventory |
| switching-regulator comb | **fixed** | the regulator is documented [R1, 8.4]; its frequency is not |
| USB PHY clock tree and harmonics | **fixed** | not for this part; the clocks are in the USB spec [R8] |
| LO harmonics / harmonic mixing | moves with tuning | general, and visible in [R5] |
| LO leakage to the RF input, DC offset | at the tuning centre | general; librtlsdr enables DC cancellation, `en_dc_est` [R2] |
| PLL fractional spurs (sigma-delta) | moves with tuning -- they are offsets from the LO | mechanism established [R2]; not measured for this part |
| ADC clock aliases (images at k x 28.8 +/- f) | moves with tuning | mechanism established [R1, 9.1]; folding at 14.4 MHz [R6] |
| VCO instability / oscillation above ~1.45 GHz | neither; it is a failure | **yes** -- [R4] |

**Only the first four rows can produce what was measured.** Everything that
moves with the tuning was excluded by the 60 MHz control, and everything at
the tuning centre was excluded by construction -- `testfiles/carrier_75000_bare.bin`
was deliberately recorded 300 kHz below its target so the carrier lands clear
of DC, and it still reads 74 999 978.2 Hz.

The two published measurements of internal spurs on this hardware:

**Kalberla 2015 [R4]**, a radio astronomer's stability report on an
RTL2838U/R820T2, whip antenna, repeated runs: "A very strong birdie is
observed for both dongles at 1.44 GHz, caused by the 28.8 Mhz oscillator", and
"some more nasty features are found, spreading out up to 20 MHz on both sides
of this peak". He also records the old R820T oscillating above about 1.45 GHz
and the R820T2 above about 1.7 GHz. **Established**: 1.44 GHz is an internal
spur on this hardware, seen by someone else, on different dongles, with the
antenna present and the features verified as not RFI.

**HB9AJG 2013 [R5]**, bench measurements with two signal generators and the
dongles wrapped in grounded aluminium foil: "Harmonics of the clock frequency
are quite strong and clearly visible up to at least 1GHz", and for the R820T
specifically, "7 harmonic of the clock frequency 28.8MHz and a few, weak
birdies" -- 7 x 28.8 = 201.6 MHz. Elsewhere he identifies "the 5 harmonic of
the clock" -- 144 MHz -- in a plot centred on 145 MHz, "and is always
present". **Established**: harmonics of 28.8 MHz are present and strong to at
least 1 GHz on an R820T dongle other than this one.

Note what that second one does for this repository's 14.4 MHz comb: 144 MHz is
5 x 28.8 **and** 10 x 14.4, and 201.6 is 7 x 28.8 **and** 14 x 14.4. Every
28.8 harmonic is a 14.4 harmonic. **The published literature is consistent
with the 14.4 MHz comb and does not establish it**, because nobody published
an odd multiple of 14.4 -- and the odd multiples are the whole content of the
claim. This repository has three: 489.6 (34 x 14.4), 547.2 (38) and 604.8
(42) are even, but 129.600159 MHz is **9 x 14.4** and is not a multiple of
28.8. That single frequency is the only evidence anywhere that the comb is
14.4 and not 28.8, and it would be worth more if it had company: 43.2, 72.0,
100.8, 158.4, 187.2 MHz are the other low odd multiples.

## 5. Does anything account for the coherent family?

### 5.1 The one degeneracy to know before theorising

**1440 = 50 x 28.8 = 3 x 480.** The strongest documented RTL-SDR birdie is
exactly the third harmonic of the USB 2.0 high-speed bit rate, and exactly the
fiftieth harmonic of the reference clock. **Kalberla's attribution to the
28.8 MHz oscillator is not distinguishable from a USB PHY harmonic by any
measurement in his report or in this one.** Both are coherent, because on this
board the USB PHY has no clock but the one crystal.

The same collision sits at 480 MHz, which is both USB high speed's bit rate
and exactly 1.6 x 300 on this repository's fine comb; and 480/28.8 is exactly
50/3. A receiver with one clock cannot separate sources that share it.

### 5.2 Ten of the fifteen: the USB 2.0 clock family -- _likely_

**The mechanism class is established; the attribution is not.** A USB 2.0
high-speed PHY has a documented clock family, and it is not a matter of
vendor choice: the UTMI specification defines a 480 Mbit/s serial rate with a
**60 MHz** clock for the 8-bit parallel interface and a **30 MHz** clock for
the 16-bit one, and ULPI runs at 60 MHz [R8]. A shipping PHY datasheet says
the same thing as an implementation rather than as a specification: the
Microchip USB3290 "generates a 480MHz multi-phase clock ... and is divided
down to 60MHz (CLKOUT) which acts as the system byte clock" [R9, sec. 7.2].
So a USB 2.0 device PHY ordinarily has 480 and 60 MHz live on the die
simultaneously, 30 MHz too in a 16-bit design, and a divider between them.

Set that against the measured ladders:

| measured | as a USB clock | present? |
| --- | --- | --- |
| 30 | UTMI 16-bit clock | yes |
| 60 | UTMI 8-bit / ULPI clock | yes |
| 120 | 2 x 60 -- an inferred divider stage, not a named clock | yes |
| 240 | 4 x 60 = 480/2 -- likewise inferred | yes |
| 480 | **high-speed bit rate** | yes |
| 960 | 2 x 480 | yes |
| 180 | **3 x 60** | yes |
| 360 | **3 x 120** | yes |
| 720 | **3 x 240** | yes |
| 1440 | **3 x 480** | yes |
| 90 | 3 x 30 | **untested** -- masked by FM broadcast |
| 45 | 1.5 x 30 -- not a clock, and not an odd harmonic of one | **absent** -- a prediction that held |
| 15 | 30/2 -- not a UTMI clock | untested, under the tuner's reach |

Two of those rows are weaker than the rest and should not be read as
datasheet facts. **240 and 120 MHz are named in no USB document found.** The
USB3290 divides 480 straight to 60 [R9] and says nothing about how; /8 built
as three cascaded /2 stages is the ordinary way and would put 240 and 120 on
the die, but that is an inference about logic design, not a quotation.
**960 MHz is 2 x 480 and nothing more.** A PLL whose VCO runs at 960 and
halves to 480 is a common USB 2.0 design, but the one datasheet read here
generates 480 directly, so "960 is the VCO" is speculation; the second
harmonic of any 480 MHz clock with an imperfect duty cycle would do as well.

**Ten of the fifteen present frequencies, and both complete ladders, are the
USB clock family and its third harmonics.** A square-wave clock is rich in odd
harmonics and poor in even ones, which is why the third-harmonic row is
expected rather than convenient. The whole family is coherent with 28.8 MHz
because it must be: one crystal.

It also **explains the shape** -- octave ladders with a stop at each end --
without needing a rule invented to fit. A clock tree has a top (480 MHz) and a
bottom (30 MHz), and divides by two in between. That is exactly what
`src/clock_chain.h` models, and this is the first external reason to think the
shape is right rather than merely fitted.

**And it earns one absence rather than merely surviving it.** The ladder's
bottom under this reading is 30 MHz -- the 16-bit UTMI clock -- and nothing
runs at 15. So **45 MHz, the third harmonic of a 15 MHz clock, should not be
there, and it is not.** Every reading that treats 15 MHz as the fundamental
has to explain why the third harmonic of the fundamental is missing while the
fifth (75) and ninth (135) are present, which is backwards for any square
wave. The USB reading does not have that problem, because under it 15 MHz
does not exist. That is the only absence in the whole set that a hypothesis
here predicts rather than accommodates.

**Why it is _likely_ and not established.** No source says the RTL2832U's USB
PHY is a UTMI or ULPI design, or names its internal clocks; the datasheet's
USB section says nothing about clocking. No source reports USB clock spurs
from an RTL-SDR. And the RTL2832U cannot make 480 MHz the easy way: the
USB3290 multiplies a 24 MHz reference by 20 [R9, secs. 4.4, 7.7], where this
part has 28.8 MHz and 480/28.8 is 50/3, so its USB PLL must divide the
reference by three (or be fractional) before multiplying. That is ordinary
and it is undocumented. The fit is ten points and one predicted absence,
which is a lot, and it is still a fit.

**And it makes one prediction that fails.** Fifth harmonics of the same
clocks would be 150 (5 x 30) and 300 (5 x 60) -- both present, which looks
like support -- but also **600 (5 x 120), which is absent and was checked
narrowly**, a 599.5-600.5 MHz sweep finding nothing at all. A mechanism that
puts a fifth harmonic of 120 below the noise while a fifth harmonic of 60 and
of 30 stand up is not impossible, but it is not the simple story, and
600 MHz's absence is the single strongest argument against reading the whole
set as harmonics of one clock tree.

### 5.3 The other five: nothing accounts for them

**75, 150, 300 and 135, 540. Clean null. Nothing published accounts for these,
and this document does not propose anything.**

What has been ruled out, each by a measured absence:

| hypothesis | predicts | refuted by |
| --- | --- | --- |
| 25 MHz x n (the original reading of 75.0005) | 175, 225 | both absent |
| 27 MHz x n (a common video clock; 27 x 5 = 135) | 270 = 27 x 10 | absent, and a confirmation pass called the nearest candidate `no-carrier` |
| harmonics of 15 MHz | 45, 225, 270 | all three absent -- and 45 is the *third* harmonic, which cannot be weaker than the ninth |
| harmonic distortion of a 75 MHz source | 225 | absent at a 12 dB bar |
| an octave chain from 135 | 270 | absent |
| fifth harmonics of the USB ladder | 600 | absent |

The arithmetic against the reference, for whoever picks this up: 75 is
28.8 x 125/48, 135 is 28.8 x 75/16, 150 is 28.8 x 125/24, 300 is 28.8 x 125/12
and 540 is 28.8 x 75/4. **All five are exact rationals of 28.8 MHz**, which
they must be to be coherent, and all five have denominators that no published
divider in either chip uses. A fractional-N synthesiser reaches any of them
trivially; so does an integer-N PLL with a reference divider. **There is no
published number to test either against**, because the RTL2832U's internal PLL
parameters are not in its datasheet (section 2.1) and the R820T's PLL
registers are not in its (section 2.3).

**270 MHz is the finding, not the gap.** Every other absence sits at the end
of a ladder, where a divider chain would naturally stop. 270 sits in the
*middle* of one: 135 and 540 are present and 270 is not. A doubler whose
intermediate stage is simply not radiated is physically ordinary and
unfalsifiable from outside the package, which is another way of saying the
measurement has reached the limit of what this method can resolve. Anything
past that is speculation, and it is left unwritten.

The honest summary is the one ticket 10 already reached: `unexplained` is the
correct mark for 135 and 540, and this literature search does not change it.

### 5.4 The four questions, answered directly

**Is there a documented 15 MHz or 30 MHz clock anywhere in this hardware?**
15 MHz: **no**, and the null is clean. It appears in neither datasheet, in no
register description, in no librtlsdr constant, and in no board description;
it is not 28.8/n (28.8/15 is 1.92) and not 28.8*n. 30 MHz: **yes, but not in
the RTL2832U's documentation** -- it is the UTMI 16-bit interface clock [R8],
a fact about USB PHYs rather than about this part. As for a second oscillator
on a typical dongle: the community teardown that enumerates the board lists
the RTL2832U, the tuner, **one 28.8 MHz crystal**, a 256-byte EEPROM, an
AMS1117 LDO and an IR receiver [R11]. No second can. Every coherent tone
measured here must therefore trace to that crystal, which is what the
measurements already said.

**What produces a 15 MHz x 2^n ladder up to 960 MHz? The question is posed
upside down, and that is the most useful thing in this section.** A divider
tree is generated **downward from its source**. What was measured is a ladder
whose top is 960 and whose bottom is 30; 15 MHz is an extrapolation past the
tuner's floor and **has never been observed**. So the fundamental to look for
is 480 or 960, not 15, and section 5.2 is what happens when you look there.
This is falsifiable rather than merely tidier: **the USB reading says 15 MHz
is absent**, where a 15 MHz-fundamental reading says it is the loudest member.
That cannot be tested with an R820T, but a dongle with the direct-sampling
Q-branch modification hears 14.4-28.8 MHz [R6] and one reaching below 24 MHz
would settle it outright.

**Does USB 480 MHz explain the chain better than the 28.8 MHz reference? Yes,
and the discriminator is arithmetic rather than preference.** Harmonics of the
reference are 28.8, 57.6, 86.4, 115.2, 144, 172.8, 201.6 MHz and so on. **Of
the six members of the complete ladder, exactly one -- none of them, in fact
-- is a multiple of 28.8**: 30/28.8, 60/28.8, 120/28.8, 240/28.8, 480/28.8
and 960/28.8 are 25/24, 25/12, 25/6, 25/3, 50/3 and 100/3. The reference-comb
reading accounts for **zero of six**; the USB reading accounts for six of six,
and for the four third harmonics as well. Within the evidence available that
is decisive. It is still marked _likely_, because what it decides is which of
two hypotheses fits, not that either is true. The documented intermediate
frequencies you asked for: **480 -> 60 is documented** [R9, sec. 7.2], **30 is
documented for a 16-bit interface** [R8], and **240 and 120 are not documented
anywhere** -- they are the inferred stages of a /8 (section 5.2).

**What explains 75, 135, 180 and their doublings?** The question splits, and
only half of it has an answer. **180, 360, 720 and 1440 are the third
harmonics of 60, 120, 240 and 480** -- a square-wave clock is rich in odd
harmonics, so this is expected rather than fitted, and it is _likely_ on the
same footing as the rest of 5.2. **75, 150, 300, 135 and 540 have no answer**,
and section 5.3 is the refusal. 150 and 300 are 5 x 30 and 5 x 60 and look
like fifth harmonics until 600 = 5 x 120 turns out to be absent; 75 and 135
are not harmonics of anything present, because the thing they would be
harmonics of is 15 MHz and its own third harmonic at 45 is absent.

**What explains the absences?** One of the four, and the score is worth stating
plainly.

| absent | status |
| --- | --- |
| **45** | **predicted** by section 5.2: the tree bottoms at 30 and nothing runs at 15 |
| 225 | consistent with everything here, and it refutes two hypotheses (15 MHz harmonics, harmonic distortion of 75) |
| **600** | **unexplained, and it is the best evidence against section 5.2** -- 5 x 120, where 5 x 30 and 5 x 60 are both present |
| **270** | **unexplained and the hardest of the four** -- a hole in the middle of a ladder whose ends are both present |

A hypothesis that predicts one absence, is neutral on one, and is contradicted
by one is not a theory. It is a lead.

## 6. What would settle it, cheaply

Four experiments, in order of how much they would decide per minute of sweep.
The first is the one to run.

**1. Sweep 48 MHz and 12 MHz.** These are the USB full-speed clocks, and they
are the sharpest available discriminator: **48 MHz is not a multiple of
15 MHz**, so it is predicted present by section 5.2 and predicted absent by
every "multiples of 15" reading of the data. 48 MHz is comfortably inside the
R820T's reach. 12 MHz is not, but the R820T's clock-output pin and the V3's
clock header are not the only way to see a 12 MHz line -- its fourth harmonic
is 48 and its fifth is 60, both already accounted for, so 48 carries the
whole test. **One 2 MHz window at `--ppm 0`.**

**2. Sweep 195 MHz and 255 MHz, to kill or feed one observation about the
residue.** Marked **speculation**, and offered only because it dies cheaply.
Every present frequency except 30, 75 and 135 sits on a 60 MHz grid --
60, 120, 180, 240, 300, 360, 480, 540 are all multiples of 60 -- and 75 and
135 sit on the *same* grid offset by 15: 15, 75, 135, **195**, **255**. That
offset grid contains none of the absences (45, 225, 270, 600 and 37.5 are all
off it), which is the only sense in which it currently fits. If the residue is
a 60 MHz-spaced family then 195 and 255 must be present; if they are absent
the family is two points and the spacing is coincidence. Both frequencies are
clear of anything that would mask them, and it is two 2 MHz windows. **Note
the grid is not clean either way**: 600 is a multiple of 60 and is absent,
which is the same objection section 5.4 raises.

**3. Run the four-step-boundary control at more than one frequency.** It has
been run at 60 MHz and nowhere else. The two that matter are **135 and 540**,
because those are the two with no explanation, and a fixed-frequency claim
about them currently rests on a control performed on a different tone.

**4. Sweep the low odd multiples of 14.4 MHz.** 43.2, 72.0, 100.8, 158.4 and
187.2 MHz are on the 14.4 comb and not on the 28.8 comb. The literature
establishes 28.8 harmonics and says nothing about 14.4; this repository's
whole 14.4 claim rests on one frequency, 129.600159. Five more would settle
whether the comb is 14.4 or whether it is 28.8 with one outlier.

**5. Unplug the antenna and repeat.** `docs/receiver-artifacts.md` already
records that the comb sorts into two kinds -- tones made and heard entirely
inside the receiver, and tones the dongle radiates and hears back. **Nothing
in the fifteen-frequency family has been sorted that way.** If 135 and 540
survive a terminated input, they are on the die or the board; if they vanish,
they are radiated and re-received, and the board layout and the USB cable come
into scope. HB9AJG's method -- wrap the dongle in grounded aluminium foil
[R5] -- is the cheap version.

A sixth, which is a different kind of answer: **a second dongle**. Everything
above is one R820T at one desk. `.scratch/calibrating-the-flags/` exists to
record that distinction, and ticket 10 has twice refused to put any of this in
`device_profile` for exactly that reason. A second receiver is what turns
"this desk does this" into "this part does this", and the USB hypothesis in
5.2 is a claim about a *part* -- so a second RTL2832U should show 30, 60, 120,
240, 480, 960, 180, 360, 720 and 1440, and might well not show 75, 135, 150,
300 and 540.

## 7. Published spur lists

**There are none worth the name.** This was searched for directly and the
result is a null: no maintained list of RTL-SDR birdie frequencies was found
on rtl-sdr.com, the osmocom project wiki, or in the comparison documents that
circulate. What exists is:

- **Kalberla [R4]** -- 1.44 GHz, and instability above 1.45 GHz (R820T) and
  1.7 GHz (R820T2). One frequency, well measured.
- **HB9AJG [R5]** -- "harmonics of the clock frequency ... clearly visible up
  to at least 1GHz", with 144 and 201.6 MHz named in passing from plot
  captions. A method, and no list.
- **RTL-SDR Blog V3 and V4 datasheets [R6], [R10]** -- marketing-adjacent but
  first-party about the hardware: the V3 "uses a modified 4-layer PCB design
  which helps to significantly reduce clock spurs and noise pickup", the V4
  claims improved phase noise from a better supply. Both confirm clock spurs
  are a known design problem on these boards and neither names a frequency.

**Nothing found contradicts the measurements in section 1**, and nothing found
predicts a frequency in the absent list. That is a weaker statement than it
sounds: the literature is thin enough that it predicts almost nothing either
way.

## 8. Things this document deliberately does not say

- It does not attribute the 14.4 MHz comb to any specific divider. 14.4 MHz is
  the ADC's Nyquist frequency and that is a coincidence of arithmetic until
  something measures the mechanism.
- It does not offer any origin for 1.6 MHz. `28.8/18` fits and explains
  nothing; 18 is not a divisor either chip is documented to use.
- It does not name an oscillator for 75, 135, 150, 300 or 540 MHz. Five points
  and two gaps can be fitted by more rational multiples of 28.8 than are worth
  enumerating, and fitting them is the guess ticket 10 has refused three times.
- It does not claim the USB hypothesis is established. It is ten points, a
  documented clock family, a mechanism that explains the ladder shape, and one
  prediction (600 MHz) that fails.

## 9. Measured after this document was written, 2026-09-12

Section 6 named the sharpest next experiment and two missing controls. All
three were run within the hour, and two of the three weaken the case above.

### 48 MHz: absent, and the test was weaker than billed

Nothing at 48.000000; the nearest candidate is 48.415527 at 8.6 dB, 415 kHz
away and inside VHF band I television.

**But this discriminates less than section 6 claimed.** The evidence in 5.2 is
UTMI's 480/60/30 [R8], and **48 MHz is not in that family** -- it is a
full-speed reference, and a high-speed PHY need not have one. So the absence is
consistent with the USB hypothesis rather than against it, and consistent with
"multiples of 15" as well. It rules out only the loose reading that any
USB-related clock radiates here.

### 90 MHz: absent, and this one does count against

3 x 30 MHz, the missing member of the third-harmonic row that carries 180, 360,
720 and 1440. A narrow 89.6-90.4 MHz sweep finds one candidate, 89.900366 at
22.9 dB, which is an FM broadcast station 100 kHz away. A tone at 90.000000
would have to sit under that station's local floor.

So the third-harmonic row is 3 x 60, 3 x 120, 3 x 240 and 3 x 480 present and
**3 x 30 absent**, beside the already-recorded failure at 5 x 120 = 600. Two
gaps in two harmonic rows is a worse fit than section 5.2 had.

### 135 and 540 are **intermittent**, and that is the biggest change

The two frequencies section 5.3 could account for by nothing are also the two
that come and go.

Both were present this morning -- 135 MHz at 10.3 dB and **confirmed 6 of 6
looks** by a confirmation pass, again at 10.8 and 12.4 dB in two later sweeps;
540 MHz at 10.9 dB. By afternoon both are **absent**: 135 in four sweeps with
different step grids, in a 1.0 s dwell sweep that found no candidate at all in
134-136 MHz, and at **gain max, 20 and 8** alike, so it is not a threshold
artifact. 540 likewise.

Over the same interval the ladder did not move at all:

| | this morning | this afternoon |
| --- | --- | --- |
| 30 MHz | 30 000 488 | 30 000 488 |
| 60 MHz | 60 002 441 | 60 002 441 |
| 180 MHz | 180 006 348 | 180 006 348 |
| 480 MHz | 480 015 137 | 480 015 137 |
| 960 MHz | 960 030 762 | 960 030 762 |

Identical to the hertz. **So the set divides into a steady ladder and two
intermittent tones, and the division falls exactly where the literature's
explanatory power does.**

It also means 135 and 540 have **never passed the fixed-frequency control**:
they vanished before the four-grid test section 6 asked for could be run on
them. Their status is weaker than the ladder's in two independent ways -- no
mechanism, and no control -- and section 5.3's clean null should be read with
that beside it.

### One hypothesis tested and discarded

That 135 and 540 are second-order intermodulation products of the steady
ladder: 135 = 60 + 75 and 540 = 60 + 480, both sums of tones that are present.
It fails immediately, because **225 = 75 + 150, 270 = 120 + 150 and
600 = 240 + 360 are also sums of steady tones and all three are absent**. Sums
do not discriminate, and the gain sweep that would have tested compression
directly could not be run while the tones were gone.

### What this changes

Nothing in sections 2, 3, 4 or 7. Section 5.2 keeps its ten points and loses
some margin: two harmonic rows now have a gap each. Section 5.3's null stands
and is now better characterised -- what nothing explains is also what does not
stay still.

The experiment still worth doing is the one that was never possible today:
**catch 135 or 540 while present and run the four-grid control and the antenna
unplug on them.** Until then they are two readings, not two frequencies.

## References

Ordered by how much of this document rests on each.

**R1. Realtek, _RTL2832U DVB-T COFDM Demodulator + USB 2.0 Datasheet_, Rev.
1.4** (marked CONFIDENTIAL, publicly mirrored).
<https://homepages.uni-regensburg.de/~erc24492/SDR/Data_rtl2832u.pdf>
*Primary.* Establishes the single 28.8 MHz crystal (sec. 4 block diagram,
Table 38: +/-30 ppm tolerance, +/-30 ppm stability, fundamental mode), the
sampling clock generated by an internal PLL from it (sec. 7.1), the fixed ADC
rate with a downstream resampler (secs. 7.4, 9.4), and the internal 3.3 V to
1.2 V switching regulator (sec. 8.4). **Does not** give the internal PLL's VCO
frequency or divider ratios, or anything about USB clocking or spurious
responses.

**R2. librtlsdr source, osmocom `rtl-sdr` master** -- `src/librtlsdr.c` and
`src/tuner_r82xx.c`.
<https://github.com/osmocom/rtl-sdr>
*Primary for behaviour, not a manufacturer specification.* `DEF_RTL_XTAL_FREQ
28800000`; `rsamp_ratio = (rtl_xtal * 2^22) / samp_rate`; `tun_xtal =
rtl_xtal`, so the tuner runs on 28.8 MHz. In `r82xx_set_pll`: `vco_min =
1770000` kHz and `vco_max = vco_min * 2`; `mix_div` doubling from 2 to 64;
`vco_div = (pll_ref + 65536 * vco_freq) / (2 * pll_ref)` with `nint` and a
16-bit `sdm`; `int_freq = 3570000` from `r82xx_set_tv_standard`; `lo_freq =
freq + int_freq` in `r82xx_set_freq`. **This is where the VCO range, the
binary mixer divider and the fractional-N structure come from** -- none of it
is in R3.

**R3. Rafael Micro, _R820T High Performance Low Power Advanced Digital TV
Silicon Tuner_, Preliminary rev 1.2, 2011/11/30** (marked CONFIDENTIAL;
mirrored by rtl-sdr.com).
<https://rtl-sdr.com/wp-content/uploads/2013/04/R820T_datasheet-Non_R-20111130_unlocked.pdf>
*Primary, and incomplete.* Establishes the fractional PLL in the feature list,
the IF frequency table (3.57 MHz for 6 MHz DVB-T), the crystal requirements
(16 MHz typical, 16 pF, +/-30 to +/-50 ppm) and an operating range of
42-1002 MHz. **Read for this document specifically to establish what it does
not contain**: its register section covers registers 0 to 4 only, the PLL
registers 0x10-0x16 are absent, and the "Spurious" row of its AC parameter
table has no value in the public version. The full datasheet is under NDA and
was not read.

**R4. Peter M. W. Kalberla, _Basic RTL-SDR Tests, Stability of a new
RTL2838U/R820T2 Dongle_, Argelander-Institut für Astronomie, Bonn, 6 March
2015.**
<https://www.rtl-sdr.com/wp-content/uploads/2015/03/rtl_R820T2.pdf>
*Secondary, measured, and load-bearing.* The only published attribution of a
specific internal spur frequency on this hardware: 1.44 GHz on both an R820T
and an R820T2, "caused by the 28.8 Mhz oscillator", with skirts to 20 MHz
either side, and repeated tests confirming the features are internal rather
than RFI. Also records R820T oscillation above ~1.45 GHz.

**R5. HB9AJG, _Some Measurements on DVB-T Dongles with E4000 and R820T
Tuners_, August 2013.**
<https://www.nooelec.com/store/downloads/dl/file/id/35/product/265/e4000_r820t_tuner_comparison.pdf>
*Secondary, measured, and load-bearing for method.* Bench measurements with
two HP signal generators, dongles wrapped in grounded foil. Establishes that
clock harmonics are "quite strong and clearly visible up to at least 1GHz" on
an R820T, names 144 MHz (5 x 28.8) and 201.6 MHz (7 x 28.8), and gives the
fixed-versus-moving test this repository independently arrived at: birdies
other than clock harmonics "vary their frequency when moving the spectrum
window in frequency".

**R6. RTL-SDR Blog, _RTL-SDR Blog V3 Datasheet_, 2018.**
<https://www.rtl-sdr.com/wp-content/uploads/2018/02/RTL-SDR-Blog-V3-Datasheet.pdf>
*Secondary, first-party about the board.* "The RTL2832U samples at 28.8 MHz,
so 0 - 14.4 MHz, and 14.4 MHz - 28.8 MHz can be listened to" -- the clearest
statement of the ADC rate and of where it folds. Also documents the clock
selector header (a V3 can output its clock or take an external one) and
credits the 4-layer PCB with reducing clock spurs.

**R7. PA3FWM (Pieter-Tjerk de Boer), _How RTL-SDR dongles work_, technote 20.**
<https://pa3fwm.nl/technotes/tn20.html>
*Secondary, technical.* Independent statement that the A/D converters run at
28.8 MHz and can process input up to 14.4 MHz, and a description of the
decimation chain (a 32-tap programmable FIR and a second filter whose cut-off
follows the downsampling factor) including its secondary passband. Used here
to corroborate R1 and R6 on the ADC rate.

**R8. _USB 2.0 Transceiver Macrocell Interface (UTMI) Specification_, version
1.05.**
<https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/usb2-transceiver-macrocell-interface-specification.pdf>
*Primary, for USB rather than for this chip.* The 480 Mbit/s high-speed rate
and the parallel-interface clocks it implies -- 60 MHz for 8-bit, 30 MHz for
16-bit; ULPI likewise 60 MHz. **This is the whole basis of section 5.2, and it
is a fact about USB PHYs in general, not about the RTL2832U**, whose USB
clocking is undocumented.

**R9. Microchip (SMSC), _USB3290 Small Footprint Hi-Speed USB 2.0 Device PHY
with UTMI Interface_ datasheet.**
<https://ww1.microchip.com/downloads/en/DeviceDoc/3290.pdf>
*Primary, for USB rather than for this chip.* A shipping USB 2.0 high-speed
PHY, read to turn the UTMI specification into an implementation. Section 7.2,
System Clocking: the PHY "generates a 480MHz multi-phase clock ... and is
divided down to 60MHz (CLKOUT) which acts as the system byte clock"; section
7.7 and Table 4.4 give the reference as a 24 MHz crystal on XI/XO. **What it
does not contain matters as much**: no intermediate divided clocks are named,
so 240 and 120 MHz are this document's inference and not a quotation, and the
PLL generates 480 MHz directly rather than halving a 960 MHz VCO, so "960 is
the VCO" is speculation.

**R10. RTL-SDR Blog, _RTL-SDR Blog V4 Datasheet_, December 2024.**
<https://www.rtl-sdr.com/wp-content/uploads/2024/12/RTLSDR_V4_Datasheet_V_1_0.pdf>
*Secondary.* Cited only for the negative: a current first-party hardware
document that discusses phase noise and supply design and names no spur
frequency.

**R11. _Realtek RTL2832U: the mystery chip at the heart of RTL-SDR_, version 1,
2015.**
<https://homepages.uni-regensburg.de/~erc24492/SDR/RTL2832U.pdf>
*Secondary, community, lightly load-bearing.* Cited for one thing only: it
enumerates a typical dongle's board -- RTL2832U, tuner, "28.800 MHz Crystal
Oscillator, pin 13", a 2 kbit EEPROM, an AMS1117 LDO and an IR port -- which
is the basis for saying there is **no second oscillator** to attribute a
coherent tone to. Its other numbers (an 18.284544 MHz sampling rate and a
4.571136 MHz low IF) are the DVB-T resampled rate, not the SDR one, and are
not used here.

**R12. gophertrunk.org, _R820T / R820T2 tuner_.**
<https://gophertrunk.org/reference/r820t-tuner/>
*Secondary, community, lightly load-bearing.* Corroborates the 3.57 MHz IF and
the 24 MHz - 1.766 GHz practical reach, and notes that R828D boards run a
16 MHz crystal instead, "an error in crystal selection causes every LO to
mistune by a factor of 1.8". Used for corroboration only; every claim taken
from it is also in R2 or R3.

### Consulted and established nothing

The osmocom `rtl-sdr` project wiki (access denied at the time of the search);
the Realtek product page for the RTL2832U; John Ackermann's RTL-SDR
dynamic-range
measurements at blog.febo.com (noise floor, MDS and clipping only -- no spur
data); and assorted forum threads on birdies which contain no frequencies.

**And one anti-reference.** A web search summary during this work asserted
that the RTL2832U divides its 28.8 MHz crystal by 18 to make a 1.6 MHz
internal clock, and that spurs therefore appear at 1.6 MHz intervals. **No
source it cited says this, and no source found anywhere says it.** It is a
restatement of the question in the voice of an answer. Recorded so that it is
never mistaken for corroboration of section 3.
